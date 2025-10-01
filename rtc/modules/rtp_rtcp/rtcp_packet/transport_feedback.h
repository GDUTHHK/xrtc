#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTCP_PACKET_TRANSPORT_FEEDBACK_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTCP_PACKET_TRANSPORT_FEEDBACK_H_

#include <sstream>
#include<vector>
#include <api/units/time_delta.h>
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/rtpfb.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/transport_feedback.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/common_header.h"

namespace xrtc {
namespace rtcp{

//RTCP的TransportFeedback包的数据存储
class TransportFeedback : public Rtpfb {
public:
    class ReceivePacket{
    public:
        //通过该方法构造说明收到了该RTP包
        ReceivePacket(uint16_t sequence_number,int16_t delta_ticks):
            sequence_number_(sequence_number),
            delta_ticks_(delta_ticks),
            received_(true){}

        //通过该方法构造说明没有收到该RTP包
        ReceivePacket(uint16_t sequence_number):
            sequence_number_(sequence_number),
            received_(false){}
        uint16_t sequence_number() const {  return sequence_number_;}
        int16_t delta_ticks() const {   return delta_ticks_;}
        int32_t delta_us() const { return delta_ticks_ * kDeltaScaleFactor; }
        bool received() const { return received_; }
        webrtc::TimeDelta delta() const { return webrtc::TimeDelta::Micros(delta_us()); }
        std::string ToString() const;
    private:
        uint16_t sequence_number_ ;
        int16_t delta_ticks_ =0;
        bool received_ ;
    };
    static const uint8_t kFeedbackMessageType = 15;
    static const int kDeltaScaleFactor = 250;//250us

    const std::vector<ReceivePacket>& AllPackets() const {
        return all_packets_;
    }

    const std::vector<ReceivePacket>& ReceivedPackets() const {
        return received_packets_;
    }

    uint16_t GetPacketStatusCount() const {return num_seq_no_;}

    webrtc::TimeDelta GetBaseTime() const;
    int64_t  GetBaseTimeUs() const;
    webrtc::TimeDelta GetBaseDelta(int64_t prev_timestamp_us) const;
    int64_t GetBaseDeltaUs(int64_t prev_timestamp_us) const;

    bool Parse(const rtcp::CommonHeader& packet);
    size_t BlockLength() const override;

    virtual bool Create(uint8_t* packet,
        size_t* index,
        size_t max_length,
        PacketReadyCallback callback) const override;
private:
    Class LastChunk{
    public:
        //解析出来的个数不应该超过总的大小
        void Decode(uint16_t chunk,size_t max_size);
        void AppendTo(std::vector<uint8_t>* deltas);
        void Clear();
    private:
        static const size_t kRunLengthCapacity = 0x1fff;
        static const size_t kOneBitCapacity = 14;
        static const size_t kTwoBitCapacity = 7;
        static const size_t kVectorCapacity = kOneBitCapacity;
        static const size_t kLarge = 2; 
        void DecodeRunLength(uint16_t chunk,size_t max_size);
        void DecodeOneBit(uint16_t chunk,size_t max_size);
        void DecodeTwoBit(uint16_t chunk,size_t max_size);
    private:
        uint8_t delta_size_ [kVectorCapacity];
        size_t size_ = 0;
        bool all_same_ = false;
        bool has_large_delta_ = false;
    };
    void Clear();//对变量进行一个重新的初始

private:
    uint16_t base_seq_no_ = 0;
    uint16_t num_seq_no_ = 0;//记录的是feedback包里面rtp包的个数
    uint32_t base_time_ticks_ = 0;
    uint8_t feedback_seq_ = 0;
    LastChunk last_chunk_;
    //存放所有的数据包，包含没有收到的数据包
    std::vector<ReceivePacket> all_packets_;
    //存放收到的数据包
    std::vector<ReceivePacket> received_packets_;
    bool include_lost_ = true;//是否存放没有收到的数据包
    bool include_timestamps_ = true;//是否存放时间戳
    size_t size_bytes_ = 0;//存放数据包总大小字节数
};

}//namespace rtcp
}//namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTCP_PACKET_TRANSPORT_FEEDBACK_H_