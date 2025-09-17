#ifndef XRTCSDK_XRTC_RTC_PC_RTP_TRANSPORT_CONTROLLER_SEND_H_
#define XRTCSDK_XRTC_RTC_PC_RTP_TRANSPORT_CONTROLLER_SEND_H_

#include <system_wrappers/include/clock.h>
#include <rtc_base/network/sent_packet.h>
#include <rtc_base/third_party/sigslot/sigslot.h>
#include <rtc_base/task_utils/repeating_task.h>

#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet_to_send.h"
#include "xrtc/rtc/modules/pacing/task_queue_paced_sender.h"
#include "xrtc/rtc/modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include "xrtc/rtc/pc/network_controller.h"

namespace xrtc {

class RtpTransportControllerSend: public TransportFeedbackObserver {
public:
    RtpTransportControllerSend(webrtc::Clock* clock,
        PacingController::PacketSender* packet_sender,
        webrtc::TaskQueueFactory* task_queue_factory);
    ~RtpTransportControllerSend();
    void EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet);
    void OnNetworkOk(bool network_ok);
    void OnSentPacket(const rtc::SentPacket& sent_packet);
    void OnNetworkUpdate(int64_t rtt_ms,
        int32_t packets_lost,
        uint32_t extended_highest_sequence_number,
        webrtc::Timestamp at_time);
    void OnAddPacket(const RtpPacketSendInfo& send_info) override;
    void OnTransportFeedback(const rtcp::TransportFeedback& feedback) override;
    sigslot::signal2<RtpTransportControllerSend*, const webrtc::TargetTransferRate&> SignalTargetTransferRate;

private:
    void MaybeCreateController();
    void PostUpdate(const webrtc::NetworkControlUpdate& update);
    void UpdateControllerWithTimeInterval();
    void StartProcessPeroidicTasks();
private:
    webrtc::Clock* clock_;
    std::unique_ptr<TaskQueuePacedSender> task_queue_pacer_;
    NetworkControllerConfig controller_config_;
    std::unique_ptr<NetworkControllerInterface> controller_;
    bool network_ok_ = false;

    TransportFeedbackAdapter transport_feedback_adapter_;
    int32_t last_packets_lost_ = 0;
    uint32_t last_extended_highest_sequence_number_ = 0;

    webrtc::RepeatingTaskHandle controller_task_;
    webrtc::TimeDelta process_interval_ = webrtc::TimeDelta::Millis(25);//定时器25ms触发一次
    rtc::TaskQueue task_queue_;//后面所有的拥塞控制的调用都放在这里面
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_PC_RTP_TRANSPORT_CONTROLLER_SEND_H_