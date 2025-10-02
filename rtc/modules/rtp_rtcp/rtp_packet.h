#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_H_

#include <rtc_base/copy_on_write_buffer.h>
#include "xrtc/rtc/modules/rtp_rtcp/rtp_header_extension_map.h"
#include <vector>

namespace xrtc {

class RtpPacket {
public:
    RtpPacket();
    RtpPacket(const RtpHeaderExtensionMap* extensions);
    RtpPacket(const RtpHeaderExtensionMap* extensions,size_t capacity);

    uint16_t sequence_number() const { return sequence_number_; }
    bool marker() const { return marker_; }
    uint32_t timestamp() const { return timestamp_; }
    rtc::ArrayView<const uint8_t> payload() const {
        return rtc::MakeArrayView(data() + payload_offset_, payload_size_);
    }

    size_t header_size() const { return payload_offset_; }
    size_t payload_size() const { return payload_size_; }
    size_t padding_size() const { return padding_size_; }

    const uint8_t* data() const { return buffer_.cdata(); }
    size_t size() {
        return payload_offset_ + payload_size_ + padding_size_;
    }
    size_t capacity() { return buffer_.capacity(); }
    size_t FreeCapacity() { return capacity() - size(); }
    void Clear();

    void SetMarker(bool marker_bit);
    void SetPayloadType(uint8_t payload_type);
    void SetSequenceNumber(uint16_t seq_no);
    void SetTimestamp(uint32_t ts);
    void SetSsrc(uint32_t ssrc);
    uint8_t* SetPayloadSize(size_t bytes_size);

    bool SetPadding(size_t padding_bytes);
    
    uint8_t* AllocatePayload(size_t payload_size);

    uint8_t* WriteAt(size_t offset) {
        return buffer_.MutableData() + offset;
    }

    void WriteAt(size_t offset, uint8_t byte) {
        buffer_.MutableData()[offset] = byte;
    }

    rtc::ArrayView<const uint8_t> FindExtension(RTPExtensionType type)const ;
    template<typename Extension>
    absl::optional<typename Extension::Value_type> GetExtension() const;
    template<typename Extension,typename ... Values>
    bool SetExtension(const Value&... values);

    template<typename Extension>
    bool ReserveExtension();

    rtc::ArrayView<uint8_t> AllocateExtension(RTPExtensionType type,size_t length);
    const ExtensionInfo *FindExtensionInfo(uint8_t id)const ;
    void PromoteToTwoByteHeaderExtension();
    uint16_t SetExtensionLengthMaybeAddZeroPadding(size_t extensions_offset);
private:
    struct ExtensionInfo{
        ExtensionInfo(uint8_t id) : ExtensionInfo(id,0,0){}
        ExtensionInfo(uint8_t id,size_t length,size_t offset) : id(id), length(length), offset(offset) {}
        uint8_t id;//扩展的id
        size_t length;//扩展的长度
        size_t offset;//扩展在RTP头中的偏移量，不包含扩展的头部字节,指的是data的起始偏移地址
    };
    rtc::ArrayView<uint8_t> AllocateRawExtension(uint8_t id,size_t length);
private:
    bool marker_;
    uint8_t payload_type_;
    uint16_t sequence_number_;
    uint32_t timestamp_;
    uint32_t ssrc_;
    size_t payload_offset_;
    size_t payload_size_;
    size_t padding_size_;
    RtpHeaderExtensionMap extensions_;
    std::vector<ExtensionInfo> extension_entries_;  //存储扩展
    size_t extension_size_ = 0;//扩展的总长度
    rtc::CopyOnWriteBuffer buffer_;
};

template<typename Extension>
absl::optional<typename Extension::Value_type> RtpPacket::GetExtension() const{
    absl::optional<typename Extension::Value_type> result;
    auto raw= FindExtension(Extension::kId);
    if(raw.empty() || !Extension::Parse(raw,&result.emplace())){
        return absl::nullopt;
    }
    return result;
}
template<typename Extension,typename ... Values>
bool SetExtension(const Value&... values){
    size_t value_size = Extension::ValueSize(values...);
    auto buffer = AllocateExtension(Extension::kId,value_size);
    if(buffer.empty()){
        return false;
    }
    return Extension::Write(buffer,values...);
}

template<typename Extension>
bool RtpPacket::ReserveExtension(){
    auto buffer = AllocateExtension(Extension::kId,Extension::kValueSizeBytes);
    if(buffer.empty()){
        return false;
    }
    memset(buffer.data(),0,Extension::kValueSizeBytes);
    return true;
}
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_H_