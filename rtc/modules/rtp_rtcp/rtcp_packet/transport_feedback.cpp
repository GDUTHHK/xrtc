#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/transport_feedback.h"

#include <rtc_base/logging.h>
#include<absl/algorithm/container.h>
#include <modules/rtp_rtcp/source/byte_io.h>
namespace xrtc {

namespace rtcp{
namespace{
const size_t kRtcpTransportFeedbackHeaderSize = 4 + 8 + 8;
//Transport feedback 包的最小长度
// 从SSRC of packet sender开始
// Rtcp 通用部分：                 8 字节SSRC of packet sender和SSRC of media source
// Transport feedback 头部：       8 字节base sequence number、packet status count、 reference time 和 fb pkt. count
// 至少要包含一个 packet chunk：   2 字节
const size_t kMinPayloadSizeBytes = 8+8+2;
const size_t kChunkSizeBytes = 2;
constexpr int64_t kBaseScaleFactor = TransportFeedback::kDeltaScaleFactor*256;//64ms
const int64_t kTimeWrapPeriodUs =(1ll<<24)*kBaseScaleFactor;
// Message format
//
// 0 1 2 3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P| FMT=15 | PT=205 | length |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 | SSRC of packet sender |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 4 | SSRC of media source |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 8 | base sequence number | packet status count |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 12 | reference time | fb pkt. count |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 16 | packet chunk | packet chunk |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// . .
// . .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | packet chunk | recv delta | recv delta |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// . .
// . .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | recv delta | recv delta | zero padding |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
}
size_t TransportFeedback::BlockLength() const {
    return 0;
}
bool TransportFeedback::Create(uint8_t* packet,size_t* index,
    size_t max_length,PacketReadyCallback callback) const {
    return false;
}

webrtc::TimeDelta TransportFeedback::GetBaseTime() const {
    return webrtc::TimeDelta::Micros(GetBaseTimeUs());
}

int64_t TransportFeedback::GetBaseTimeUs() const {
    return static_cast<int64_t>(base_time_ticks_) * kBaseScaleFactor;
}
webrtc::TimeDelta TransportFeedback::GetBaseDelta(int64_t prev_timestamp_us) const {
    return webrtc::TimeDelta::Micros(GetBaseDeltaUs(prev_timestamp_us));
}

//计算前后两个feedback包中的基准参考时间的差值
int64_t TransportFeedback::GetBaseDeltaUs(int64_t prev_timestamp_us) const {
    int64_t delta = GetBaseTimeUs() - prev_timestamp_us; 
    if(std::abs(delta - kTimeWrapPeriodUs) < std::abs(delta)) {
        delta -= kTimeWrapPeriodUs;
    }
    else if(std::abs(delta + kTimeWrapPeriodUs) < std::abs(delta)) {
        delta += kTimeWrapPeriodUs;
    }
    return delta;
}


//对Feedback包数据进行解析
bool TransportFeedback::Parse(const rtcp::CommonHeader& packet) {
    //检查长度是否满足最低要求
    if (packet.payload_size() < kMinPayloadSizeBytes) {
        RTC_LOG(LS_WARNING) << "payload length: " << packet.payload_size()
            << " is too small for transport feedback"
            <<", min length: " << kMinPayloadSizeBytes;
        return false;
    }

    const uint8_t* payload = packet.payload();
    //解析rtcp通用的部分
    ParseCommonFeedback(payload);//SSRC of packet sender、SSRC of media source

    //解析transport feedback 的头部
    base_seq_no_ = webrtc::ByteReader<uint16_t>::ReadBigEndian(&payload[8]);
    uint16_t status_count = webrtc::ByteReader<uint16_t>::ReadBigEndian(&payload[10]);
    base_time_ticks_ = webrtc::ByteReader<uint32_t,3>::ReadBigEndian(&payload[12]);
    feedback_seq_ = payload[15];

    if(status_count == 0) {
        RTC_LOG(LS_WARNING) << "empty transport feedback message not allowed size is 0";
        return false;
    }
    Clear();

    //数据块的起始位置
    size_t index = 16;
    //数据块的结束位置
    size_t end_index = packet.payload_size();

    //定义一个vector来保存所有的rtp包状态
    std::vector<uint8_t> delta_sizes;
    delta_sizes.reserve(status_count);

    //读取完所有的Packet chunk包的状态信息，才会结束循环
    while(delta_sizes.size() < status_count) {
        if(index + kChunkSizeBytes > end_index) {
            RTC_LOG(LS_WARNING) << "Buffer overlow when parsing transport feedback";
            Clear();
            return false;
        }

        //读取一个chunk进行处理
        uint16_t chunk = webrtc::ByteReader<uint16_t>::ReadBigEndian(&payload[index]);
        //偏移指向下一个Chunk
        index += kChunkSizeBytes;

        //解码chunk
        last_chunk_.Decode(chunk,status_count - delta_sizes.size());
        last_chunk_.AppendTo(&delta_sizes);
    }
   
    num_seq_no_ = status_count;
    uint16_t seq_no = base_seq_no_;
    //0,RTP包没有收到，就没有对应的recv_delta
    //1,RTP包收到了，数据包间隔比较小，recv_delta使用1字节来表示时间
    //2，RTP收到了，数据包间隔比较大，recv_delta使用2字节来表示时间
    //所以全部相加刚好等于所有的总数
    size_t recv_delta_size = absl::c_accumulate(delta_sizes,0);
    //表示存在recv_delta数据块
    if(end_index >= index + recv_delta_size) {
        for(size_t delta_size : delta_sizes) {
            if(index + delta_size > end_index) {
                RTC_LOG(LS_WARNING) << "Buffer overlow when parsing transport feedback";
                Clear();
                return false;
            }
            switch(delta_size) {
                case 0:{
                    if(include_lost_) {
                        all_packets_.emplace_back(seq_no);
                    }
                    break;
                }
                case 1:{
                    int16_t delta =payload[index];
                    received_packets_.emplace_back(seq_no,delta);
                    if(include_lost_) {
                        all_packets_.emplace_back(seq_no);
                    }
                    index += delta_size;
                    break;
                }
                case 2:{ 
                    int16_t delta = webrtc::ByteReader<int16_t>::ReadBigEndian(&payload[index]);
                    received_packets_.emplace_back(seq_no,delta);
                    if(include_lost_) {
                        all_packets_.emplace_back(seq_no);
                    }
                    index += delta_size;
                    break;
                }
                case 3:{
                    RTC_LOG(LS_WARNING) << "invalid seq_no: " << seq_no;
                    Clear();
                    return false;
                }
                    break;
                default:
                    break;
            }
            seq_no++;
        }
    }else{ //不包含recv_deltas数据块
        include_timestamps_ = false;
        for(size_t delta_size : delta_sizes) {
            if(delta_size>0){
                received_packets_.emplace_back(seq_no,0);
            }
            if(include_lost_) {
                if(delta_size>0){
                    //数据包收到了，但是没有时间信息
                    all_packets_.emplace_back(seq_no,0);
                }else{
                    //数据包没有收到
                    all_packets_.emplace_back(seq_no);
                }
            }
            seq_no++;
        }
    }
    size_bytes_ = RtcpPacket::kHeaderSize + index;
    return true;
}

void TransportFeedback::Clear() {
    last_chunk_.Clear();
    all_packets_.clear();
    received_packets_.clear();
    size_bytes_ = kRtcpTransportFeedbackHeaderSize;
}

void TransportFeedback::LastChunk::Decode(uint16_t chunk,size_t max_size) {
    if( 0 == (chunk & 0x8000)) { //run length 编码块
        DecodeRunLength(chunk,max_size);
    }else if(0 == (chunk & 0x4000)) { //1bit状态矢量编码块
        DecodeOneBit(chunk,max_size);
    }else{//2bit状态矢量编码块
        DecodeTwoBit(chunk,max_size);
    }
    
}

void TransportFeedback::LastChunk::AppendTo(std::vector<uint8_t>* deltas) {
    //如果为true则为行程编码数据块
    if(all_same_) {
        deltas->insert(deltas->end(),size_,delta_size_[0]);
    }else{
        deltas->insert(deltas->end(),delta_size_,delta_size_ + size_);
    }
}

void TransportFeedback::LastChunk::Clear() {
    size_ = 0;
    all_same_ = true;
    has_large_delta_ = false;
}

//行程长度编码数据块解码
void TransportFeedback::LastChunk::DecodeRunLength(uint16_t chunk,size_t max_size) {
    size_ = std::min<size_t>(chunk& 0x1fff,max_size);
    all_same_ = true;
    //RTP包的状态值
    uint8_t delta_size = (chunk >> 13) & 0x3;
    has_large_delta_ = (delta_size > kLarge);
    //将delta_size写入delta_size_
    delta_size_[0] = delta_size;
}

//1bit状态矢量编码块解码
void TransportFeedback::LastChunk::DecodeOneBit(uint16_t chunk,size_t max_size) {
    size_ = std::min(kOneBitCapacity,max_size);
    all_same_ = false;
    has_large_delta_ =false;//一般收到了数据包间隔都比较小
    //RTP包的状态值
    for(size_t i = 0; i < size_; ++i) {
        //通过移动chunk来获得对应位置的RTP包状态
        delta_size_[i] = (chunk>>(kOneBitCapacity - i - 1)) & 0x1;
    }
}

//2bit状态矢量编码块解码
void TransportFeedback::LastChunk::DecodeTwoBit(uint16_t chunk,size_t max_size) {
    size_ = std::min(kTwoBitCapacity,max_size);
    all_same_ = false;
    has_large_delta_ =true;
    //RTP包的状态值
    for(size_t i = 0; i < size_; ++i) {
        //通过移动chunk来获得对应位置的RTP包状态
        delta_size_[i] = (chunk>>(2*(kTwoBitCapacity - i - 1))) & 0x3;
    }
}

std::string TransportFeedback::ReceivePacket::ToString() const {
    std::stringstream ss;
    ss << "sequence_number: " << sequence_number_
       << ", delta_us: " << delta_us()
       << ", received: " << received_;
    return ss.str();
}

}//namespace rtcp

}//namespace xrtc
