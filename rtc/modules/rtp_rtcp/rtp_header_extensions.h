#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_HEADER_EXTENSIONS_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_HEADER_EXTENSIONS_H_
#include "xrtc/rtc/modules/rtp_rtcp/rtp_rtcp_defines.h"
#include <api/rtp_parameters.h>
#include<api/array_view.h>

namespace xrtc {
//存放的是RTP的序列号
class TransportSequenceNumber{
    public:
    using value_type = uint16_t;
    static const  RTPExtensionType kId = kRtpExtensionTransportSequenceNumber;
    static const size_t kValueSizeBytes = 2;
    static const absl::string_view Uri() {
        return webrtc::RtpExtension::kTransportSequenceNumberUri;
    }
    static size_t ValueSize(uint16_t){
        return kValueSizeBytes;
    }
    static bool Parse(rtc::ArrayView<const uint8_t> data,uint16_t* transport_sequence_number);
    static bool Write(rtc::ArrayView<uint8_t> data,uint16_t transport_sequence_number);
}
}
#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_HEADER_EXTENSIONS_H_