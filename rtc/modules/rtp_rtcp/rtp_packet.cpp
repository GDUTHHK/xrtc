#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet.h"

#include <rtc_base/logging.h>
#include <rtc_base/numerics/safe_conversions.h>
#include <modules/rtp_rtcp/source/byte_io.h>
#include<api/rtp_parameters.h>
namespace xrtc {

const size_t kDefaultCapacity = 1500;
const size_t kFixedHeaderSize = 12;
const uint8_t kRtpVersion = 2;
const uint16_t kOneByteHeaderExtensionProfileId = 0xBEDE;
const uint16_t kTwoByteHeaderExtensionProfileId = 0x1000;
const size_t kOneByteHeaderExtensionLength = 1;
const size_t kTwoByteHeaderExtensionLength = 2;
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |  header eXtension profile id  |       length in 32bits        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                           Payload                             |
// |             ....              :  padding...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
RtpPacket::RtpPacket() : RtpPacket(nullptr,kDefaultCapacity) {
}

RtpPacket::RtpPacket(const RtpHeaderExtensionMap* extensions) : 
    RtpPacket(extensions,kDefaultCapacity) {

}

RtpPacket::RtpPacket(const RtpHeaderExtensionMap* extensions,size_t capacity) :
    extensions_(extensions?*extensions:RtpHeaderExtensionMap()),
    buffer_(capacity)
{
    Clear();
}

void RtpPacket::Clear() {
    marker_ = false;
    payload_type_ = 0;
    sequence_number_ = 0;
    timestamp_ = 0;
    ssrc_ = 0;
    payload_offset_ = kFixedHeaderSize;
    payload_size_ = 0;
    padding_size_ = 0;

    buffer_.SetSize(kFixedHeaderSize);
    // 写入RTP版本信息
    WriteAt(0, kRtpVersion << 6);
}

void RtpPacket::SetMarker(bool marker_bit) {
    marker_ = marker_bit;
    if (marker_bit) {
        WriteAt(1, data()[1] | 0x80);
    }
    else {
        WriteAt(1, data()[1] & 0x7F);
    }
}

void RtpPacket::SetPayloadType(uint8_t payload_type) {
    payload_type_ = payload_type;
    WriteAt(1, (data()[1] & 0x80) | payload_type);
}

void RtpPacket::SetSequenceNumber(uint16_t seq_no) {
    sequence_number_ = seq_no;
    webrtc::ByteWriter<uint16_t>::WriteBigEndian(WriteAt(2), seq_no);
}

void RtpPacket::SetTimestamp(uint32_t ts) {
    timestamp_ = ts;
    webrtc::ByteWriter<uint32_t>::WriteBigEndian(WriteAt(4), ts);
}

void RtpPacket::SetSsrc(uint32_t ssrc) {
    ssrc_ = ssrc;
    webrtc::ByteWriter<uint32_t>::WriteBigEndian(WriteAt(8), ssrc);
}

uint8_t* RtpPacket::SetPayloadSize(size_t bytes_size) {
    if (payload_offset_ + bytes_size > capacity()) {
        RTC_LOG(LS_WARNING) << "set payload size failed, no enough space in buffer";
        return nullptr;
    }

    payload_size_ = bytes_size;
    buffer_.SetSize(payload_offset_ + payload_size_);
    return WriteAt(payload_offset_);
}

bool RtpPacket::SetPadding(size_t padding_bytes) {
    if(payload_offset_ +payload_size_ + padding_bytes>capacity())
    {
        RTC_LOG(LS_WARNING)<<set padding failed, capacity is not enough to hold padding bytes
        <<padding bytes;
        return false;
    }
    padding_size =rtc::dchecked_cast<uint8_t>(padding_bytes);
    buffer_.SetSize(payload_offset_ + payload_size_ + padding_size_);
    if(padding_size_ >0){
        // padding的起始偏移量
        size_t padding_offset = payload_offset_ + payload_size_;
        // padding结束的偏移量
        size_t padding_end = padding_offset + padding_size_;
        memset (WriteAt(padding_offset),0,padding_size_ -1);
        // 尾部1字节，写入padding的长度
        WriteAt(padding_end-1,padding_size_);
        //将RTP头部的padding位设置为1
        WriteAt(0, data()[0] | 0x20);
    }
    else {
        //将RTP头部的padding位设置为0
        WriteAt(0, data()[0] & ~0x20);
    }
    return true;
}

uint8_t* RtpPacket::AllocatePayload(size_t payload_size) {
    SetPayloadSize(0);
    return SetPayloadSize(payload_size);
}

rtc::ArrayView<const uint8_t> RtpPacket::FindExtension(RTPExtensionType type) const {
    uint8_t id = extensions_.GetId(type);
    if (id == RtpHeaderExtensionMap::kInvalidId) {//扩展没有注册
        return nullptr;
    }
    const ExtensionInfo* extension_info = FindExtensionInfo(id);
    if(!extension_info){
        return nullptr;
    }
    return rtc::MakeArrayView<const uint8_t>(data() + extension_info->offset, extension_info->length);
}

rtc::ArrayView<uint8_t> RtpPacket::AllocateExtension(RTPExtensionType type, size_t length) {
    //长度校验
    if (length == 0 || length > webrtc::RtpExtension::kMaxValueSize) {
        return nullptr;
    }
    //检查扩展是否已经注册
    uint8_t id = extensions_.GetId(type);
    if (id == RtpHeaderExtensionMap::kInvalidId) {
    // 扩展没有注册
        return nullptr;
    }

    return AllocateRawExtension(id, length);
}

//添加扩展头
rtc::ArrayView<uint8_t> RtpPacket::AllocateRawExtension(uint8_t id, size_t length) {
    //判断将要添加的扩展，是否已经添加过了
    ExtensionInfo* extension_entry = FindExtensionInfo(id);
    if(extension_entry){
        //扩展已经添加过
        if(extension_entry->length == length){
            return rtc::MakeArrayView<uint8_t>(WriteAt(extension_entry->offset), length);
        }
        RTC_LOG(LS_WARNING) << "length mismatch id:"<<id<<" ,expected length:"
                            <<static_cast<size_t>(extension_entry->length)
                            <<" ,actual length:"<<length;
        return nullptr;
    }
    //如果RTP的负载已经设置，不允许在添加新的头部扩展，涉及到内存的移动所以直接禁止掉
    if(paylad_size_>0){
        RTC_LOG(LS_WARNING) << "cannot add extension id:"<<id<<" after payload is set";
        return nullptr;
    }
    if(padding_size_>0){
        RTC_LOG(LS_WARNING) << "cannot add extension id:"<<id<<" after padding is set";
        return nullptr;
    }

    //获得扩展在RTP头中的偏移量
    size_t num_csrc = data()[0] & 0x0F;
    size_t extensions_offset = kFixedHeaderSize + (num_csrc * 4) + 4;//profile_id和length之后的地址

    //将要添加的扩展使用的是一字节头还是两字节头
    bool two_bytes_header_required = id>webrtc::RtpExtension::kOneByteHeaderExtensionMaxId
        || length>webrtc::RtpExtension::kOneByteHeaderExtensionMaxValueSize
        || length ==0;

        //之前已经添加过扩展
        uint16_t profile_id;
        if(extension_size_>0){
            profile_id = webrtc::ByteReader<uint16_t>::ReadBigEndian(WriteAt(extensions_offset - 4));
            //判断是否要将一字节需要提升为两字节头
            if(profile_id == kOneByteHeaderExtensionProfileId && two_bytes_header_required){
                //原始的已添加的扩展都是1字节头，但是新添加的扩展需要两字节头
                //需要将扩展头提升为两字节头
                //在提升之前需要判断容量是否足够
                //提升之后的扩展头长度  = 原来的扩展头长度 + 已经存在的扩展头个数*1 + 当前新扩展的头部+当前新扩展的数据长度
                size_t expected_extension_size = extension_size_ + extension_entries_.size()*1 + kTwoByteHeaderExtensionLength + length;
                if(extensions_offset + expected_extension_size > capacity()){
                    RTC_LOG(LS_WARNING) << "cannot promote to two byte header extension, no enough space in buffer";
                    return nullptr;
                }
                //将一字节扩展头提升为两字节头
                PromoteToTwoByteHeaderExtension();
                profile_id = kTwoByteHeaderExtensionProfileId;
            }
        }else{//第一次添加头部扩展
            profile_id = two_bytes_header_required ? kTwoByteHeaderExtensionProfileId : kOneByteHeaderExtensionProfileId;
        }
        //添加新的扩展
    const size_t extension_header_size = profile_id == kOneByteExtensionProfileId
                                                ? kOneByteExtensionHeaderProfileId
                                                : kTwoByteExtensionHeaderProfileId;
        // 计算添加扩展后的新的扩展总长度
    size_t new_extensions_size = extension_size_ + extension_header_size + length;

    if (extensions_offset + new_extensions_size > capacity()) {
        RTC_LOG(LS_ERROR)<< "Extension cannot be registered: Not enough space left in buffer.";
        return nullptr;
    }

    //如果是第一个扩展的话，还需要写入profile_id
    if (extension_size_ == 0) {
        // 如果之前没有添加扩展，需要设置RTP头部扩展位，设置X标记位置
        WriteAt(0, data()[0] | 0x10); // Set extension bit.
        // 写入扩展profile_id 
        webrtc::ByteWriter<uint16_t>::WriteBigEndian(WriteAt(extensions_offset - 4),profile_id);
    }

    if (profile_id == kOneByteExtensionProfileId) {
        uint8_t one_byte_header = ((uint8_t)(id)) << 4;
        one_byte_header |= (uint8_t)(length - 1);
        WriteAt(extensions_offset + extension_size_, one_byte_header);
    }
    else {
        // TwoByteHeaderExtension.
        uint8_t extension_id = (uint8_t)(id);
        WriteAt(extensions_offset + extension_size_, extension_id);
        uint8_t extension_length = (uint8_t)(length);
        WriteAt(extensions_offset + extension_size_ + 1, extension_length);
    }

    //将新添加的扩展保存到extension_entries_中
    const uint16_t extension_info_offset = (uint16_t)(extensions_offset + extension_size_ + extension_header_size);
    const uint8_t extension_info_length = (uint8_t)(length);
    extension_entries_.emplace_back(id, extension_info_length,extension_info_offset);
    extension_size_ = new_extensions_size;
    //设置扩展长度和填充值
    uint16_t extensions_size_padded = SetExtensionLengthMaybeAddZeroPadding(extensions_offset);
    //更新payload_offset_
    payload_offset_ = extensions_offset + extensions_size_padded;
    buffer_.SetSize(payload_offset_);
    return rtc::MakeArrayView(WriteAt(extension_info_offset),extension_info_length);
}

const RtpPacket::ExtensionInfo* RtpPacket::FindExtensionInfo(uint8_t id) const {
    for(const auto& extry : extension_entries_){
        if(extry.id == id){
            return &extry;
        }
    }
    return nullptr;
}

void RtpPacket::PromoteToTwoByteHeaderExtension() {
    //首先获得当前扩展的偏移量
    size_t num_csrc = data()[0] & 0x0F;
    //偏移量不包含自身的头部
    size_t extensions_offset = kFixedHeaderSize + (num_csrc * 4) + 4;
    //向后移动的个数
    size_t write_read_delta = extension_entries_.size();
    //从后向前遍历扩展
    for (auto extension_entry = extension_entries_.rbegin();extension_entry != extension_entries_.rend(); ++extension_entry) {
        size_t read_index = extension_entry->offset;
        //移动之后的扩展的偏移量
        size_t write_index = read_index + write_read_delta;
        extension_entry->offset = (uint16_t)write_index;
        // 整体后移write_read_delta个字节
        memmove(WriteAt(write_index), data() + read_index, extension_entry->length);
        //重新写入新的扩展头部数据
        // 写入扩展头部数据扩展长度
        WriteAt(--write_index, extension_entry->length);
        // 写入扩展头部数据扩展id
        WriteAt(--write_index, extension_entry->id);
        --write_read_delta;
    }
    // 更新profile header, 扩展长度和填充值
    webrtc::ByteWriter<uint16_t>::WriteBigEndian(WriteAt(extensions_offset - 4),kTwoByteExtensionProfileId);
    extension_size_ += extension_entries_.size();
    //判断是否要填充padding
    uint16_t extensions_size_padded =SetExtensionLengthMaybeAddZeroPadding(extensions_offset);
    payload_offset_ = extensions_offset + extensions_size_padded;
    buffer_.SetSize(payload_offset_);
}

//判断是否要填充padding
uint16_t RtpPacket::SetExtensionLengthMaybeAddZeroPadding(size_t extensions_offset) {
    // 确保4字节对齐
    uint16_t extensions_words = (uint16_t)((extension_size_ + 3) / 4); // Wrap up to 32bit.
    webrtc::ByteWriter<uint16_t>::WriteBigEndian(WriteAt(extensions_offset - 2),extensions_words);
    // 需要填充的字节数
    size_t extension_padding_size = 4 * extensions_words - extension_size_;
    memset(WriteAt(extensions_offset + extension_size_), 0,extension_padding_size);
    return 4 * extensions_words;//返回最终含padding的字节数
}



} // namespace xrtc