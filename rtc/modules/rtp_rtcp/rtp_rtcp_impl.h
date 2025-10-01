#ifndef XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_
#define XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_

#include <rtc_base/task_utils/pending_task_safety_flag.h>

#include "xrtc/rtc/modules/rtp_rtcp/rtp_rtcp_interface.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet_to_send.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtp_rtcp_defines.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_sender.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_receiver.h"
namespace xrtc {

//RTP / RTCP处理
class ModuleRtpRtcpImpl {
public:
    ModuleRtpRtcpImpl(const RtpRtcpInterface::Configuration& config);
    ~ModuleRtpRtcpImpl();

    void UpdateRtpStats(std::shared_ptr<RtpPacketToSend> packet,
        bool is_rtx, bool is_retransmit);
    void SetRTCPStatus(webrtc::RtcpMode mode);
    void SetSendingStatus(bool sending);
    void OnSendingRtpFrame(uint32_t rtp_timestamp,
        int64_t capture_time_ms,
        bool forced_report);
    void IncomingRtcpPacket(const uint8_t* packet, size_t length) {
        IncomingRtcpPacket(rtc::MakeArrayView(packet, length));
    }
    void IncomingRtcpPacket(rtc::ArrayView<const uint8_t> packet);

private:
    void ScheduleNextRtcpSend(webrtc::TimeDelta duration);
    void MaybeSendRTCP();
    void ScheduleMaybeSendRtcpAtOrAfterTimestamp(
        webrtc::Timestamp execute_time,
        webrtc::TimeDelta duration);
    RTCPSender::FeedbackState GetFeedbackState();

private:
    RtpRtcpInterface::Configuration config_;
    StreamDataCounter rtp_stats_; // 普通RTP包统计
    StreamDataCounter rtx_rtp_stats_;// 重传RTX包统计

    RTCPSender rtcp_sender_;//RTCP发送器
    RTCPReceiver rtcp_receiver_;//RTCP接收器
    
    webrtc::ScopedTaskSafety task_safety_;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_