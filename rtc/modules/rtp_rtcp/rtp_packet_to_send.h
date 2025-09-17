#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_TO_SEND_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_TO_SEND_H_

#include <absl/types/optional.h>

#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtp_rtcp_defines.h"
namespace xrtc {

class RtpPacketToSend : public RtpPacket {
public:
    RtpPacketToSend();
    RtpPacketToSend(const RtpHeaderExtensionMap* extensions);
    RtpPacketToSend(const RtpHeaderExtensionMap* extensions,size_t capacity);
    
void set_packet_type(RtpPacketMediaType type) {
        packet_type_ = type;
    }

    absl::optional<RtpPacketMediaType> packet_type() const {
        return packet_type_;
    }

private:
    absl::optional<RtpPacketMediaType> packet_type_;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_PACKET_TO_SEND_H_