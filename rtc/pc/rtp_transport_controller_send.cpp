#include "xrtc/rtc/pc/rtp_transport_controller_send.h"
#include <rtc_base/logging.h>
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/transport_feedback.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/google_cc_network_controller.h"
namespace xrtc {

RtpTransportControllerSend::RtpTransportControllerSend(webrtc::Clock* clock,
    PacingController::PacketSender* packet_sender,
    webrtc::TaskQueueFactory* task_queue_factory) :
    clock_(clock),
    task_queue_pacer_(std::make_unique<TaskQueuePacedSender>(
        clock, 
        packet_sender, 
        task_queue_factory,
        webrtc::TimeDelta::Millis(1)),
    task_queue_(task_queue_factory->CreateTaskQueue("rtp_send_task_queue",webrtc::TaskQueueFactory::Priority::NORMAL))
{
    task_queue_pacer_->EnsureStarted();//开启定时发送RTP数据包

    webrtc::TargetRateConstraints constraints;
    constraints.at_time = clock_->CurrentTime();
    constraints.start_bitrate = webrtc::DataRate::KilobitsPerSec(300);
    constraints.min_data_rate = constraints.start_bitrate;
    constraints.max_data_rate = 3 * constraints.start_bitrate.value();
    controller_config_.constraints = constraints;
}

RtpTransportControllerSend::~RtpTransportControllerSend() {
}

void RtpTransportControllerSend::EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet) {
    task_queue_pacer_->EnqueuePacket(std::move(packet));
}

void RtpTransportControllerSend::OnNetworkOk(bool network_ok) {
    RTC_LOG(LS_INFO) << "OnNetwork state, is network ok: " << network_ok;
    webrtc::NetworkAvailability msg;
    msg.at_time = webrtc::Timestamp::Millis(clock_->TimeInMilliseconds());
    msg.network_available = network_ok;

    //以下是一个独立的线程，用于处理网络状态的更新
    task_queue_.PostTask([this, msg]() {
        //网络状态没有变化，直接返回
        if(network_ok_ == msg.network_available) {
            return;
        }
        network_ok_ = msg.network_available;

        webrtc::TargetRateConstraints constraints = controller_config_.constraints;
        constraints.at_time = msg.at_time;
        if(controller_) {

        }else{
            MaybeCreateController();//在这里创建一个googlecc的网络控制器
            PostUpdate(controller_->OnNetworkOk(constraints));
        }
        controller_->OnNetworkOk(constraints);
    });
}

void RtpTransportControllerSend::OnSentPacket(const rtc::SentPacket& sent_packet) {
    task_queue_.PostTask([this, sent_packet]() {
        auto packet_msg = transport_feedback_adapter_.ProcessSentPacket(sent_packet);
        if(packet_msg && controller_) {
            PostUpdate(controller_->OnSentPacket(*packet_msg));
        }
    });
}

void RtpTransportControllerSend::OnAddPacket(const RtpPacketSendInfo& send_info) {
    webrtc::Timestamp creation_time = webrtc::Timestamp::Millis(clock_->TimeInMilliseconds());
    task_queue_.PostTask([this, creation_time, send_info]() {
        transport_feedback_adapter_.AddPacket(creation_time, 0, send_info);
    });
}

void RtpTransportControllerSend::OnNetworkUpdate(int64_t rtt_ms,
    int32_t packets_lost,//累计丢包数
    uint32_t extended_highest_sequence_number,//当前收到最大的序列号
    webrtc::Timestamp at_time) {

    //将丢包信息传入到拥塞控制模块
    task_queue_.PostTask([this, packets_lost,extended_highest_sequence_number,at_time]() {
        if(last_extended_high_seq_num_ == 0){
            last_extended_high_seq_num_ = extended_highest_sequence_number;
            last_packets_lost_ = packets_lost;
            return;
        }
        int32_t total_lost_packets = 0;
        int32_t total_packets = 0;

        //这段时间期待收到的总的报的个数
        total_packets = extended_highest_sequence_number - last_extended_high_seq_num_;
        total_lost_packets = packets_lost - last_packets_lost_;
        last_extended_high_seq_num_ = extended_highest_sequence_number;
        last_packets_lost_ = packets_lost;
        if(controller_) {
            controller_->OnTransportLoss(total_lost_packets,total_packets,at_time);
        }
    });

    //将RTT信息传入到拥塞控制模块
    task_queue_.PostTask([this, rtt_ms]() {
        if(controller_) {
            controller_->OnRttUpdate(rtt_ms);
        }
    });
}
void RtpTransportControllerSend::OnTransportFeedback(const rtcp::TransportFeedback& feedback) {
    webrtc::Timestamp feedback_time = webrtc::Timestamp::Millis(clock_->TimeInMilliseconds());
    task_queue_.PostTask([this, feedback, feedback_time]() {
        absl::optional<webrtc::TransportpacketsFeedback> feedback_msg =
        transport_feedback_adapter_.ProcessTransportFeedback(feedback, feedback_time);
        if(feedback_msg && controller_) {
            PostUpdate(controller_->OnTransportpacketsFeedback(*feedback_msg));
        }
    });
}

void RtpTransportControllerSend::MaybeCreateController() {
    if(!network_ok_) {
        return;
    }

    controller_ = std::make_unique<GoogleCCNetworkController>(controller_config_);
    UpdateControllerWithTimeInterval();
    StartProcessPeroidicTasks();//启动定时任务
}

void RtpTransportControllerSend::PostUpdate(const webrtc::NetworkControlUpdate& update) 
{
    //将估计的码率值作用到pacer中
    if(update.pacer_config) {
        task_queue_pacer_->SetPacingRates(update.pacer_config->data_rate());
    }

    //将探测的配置作用到pacer中
    for(const auto& probe : update.probe_clusters_configs) {
        task_queue_pacer_->CreateProbeCluster(probe.target_data_rate,probe.id);
    }
    //将估计的码率值以发送信号的方式发送到其他模块
    if(update.target_rate) {
        SignalTargetTransferRate(this, *update.target_rate);
    }
}

void RtpTransportControllerSend::UpdateControllerWithTimeInterval() {
    webrtc::ProcessInterval msg;
    msg.at_time = webrtc::Timestamp::Millis(clock_->TimeInMilliseconds());
    if(controller_) {
        PostUpdate(controller_->OnProcessInterval(msg));
    }
}

//定时器定时调整pacer和编码器的码率
void RtpTransportControllerSend::StartProcessPeroidicTasks() {
    controller_task_.Stop();
    if (process_interval_.IsFinite()) {
        webrtc::RepeatingTaskHandle::DelayedStart(
            task_queue_.Get(), process_interval_, [=]() {
                UpdateControllerWithTimeInterval();
                return process_interval_;
            });
    }
}

} // namespace xrtc