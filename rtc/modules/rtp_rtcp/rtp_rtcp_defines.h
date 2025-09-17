#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_DEFINES_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_DEFINES_H_

#include <stdint.h>
#include <absl/types/optional.h>
#include<api/transport/network_types.h>

namespace xrtc {
namespace rtcp{
    class TransportFeedback;
}

class RtpPacket;

#define IP_PACKET_SIZE 1500
enum RTPExtensionType : int {
    kRtpExtensionNone,
    kRtpExtensionTransportSequenceNumber,
    kRtpExtensionNumberOfExtensions,
}
    enum RTCPPacketType : uint32_t {
    kRtcpReport = 0x0001,
    kRtcpSr = 0x0002,
    kRtcpRr = 0x0004,
};

class RtpPacketCounter {
public:
    RtpPacketCounter() = default;
    explicit RtpPacketCounter(const RtpPacket& packet);

    void Add(const RtpPacketCounter& other);
    void Subtract(const RtpPacketCounter& other);
    void AddPacket(const RtpPacket& packet);

    size_t header_bytes = 0;
    size_t payload_bytes = 0;
    size_t padding_bytes = 0;
    uint32_t packets = 0;
};

class StreamDataCounter {
public:
    RtpPacketCounter transmmited;
    RtpPacketCounter retransmitted;
};

enum class RtpPacketMediatype:size_t {
    kAudio,
    kVideo,
    kRetransmission,
    kForwardErrorCorrection,
    kPadding,
};

struct RtpPacketSendInfo {
public:
    RtpPacketSendInfo() = default;
    uint16_t transport_sequence_number = 0;
    absl::optional<uint32_t> media_ssrc;
    uint16_t rtp_sequence_number = 0;
    uint32_t rtp_timestamp = 0;
    size_t length = 0;
    absl::optional<RtpPacketMediaType> packet_type;
    webrtc::PacedPacketInfo pacing_info;
};

class TransportFeedbackObserver {
public:
    TransportFeedbackObserver() {}
    virtual ~TransportFeedbackObserver() {}
    virtual void OnAddPacket(const RtpPacketSendInfo& packet_info) = 0;
    virtual void OnTransportFeedback(const rtcp::TransportFeedback& feedback) = 0;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_DEFINES_H_