#include "xrtc/rtc/modules/congestion_controller/google_gcc/google_cc_network_controller.h"
#include "xrtc/rtc/pc/logging.h"

namespace xrtc {
namespace {
const double kDefaultPaceMultiplier = 2.5f;
const webrtc::DataRate kGccMinBitrate = webrtc::DataRate::BitsPerSec(5000);//最小码率5kbps

int64_t GetBpsOrDefault(const absl::optional<webrtc::DataRate>& rate,int64_t fallback_bps) {
    if(rate && rate->IsFinite()) {
        return rate->bps();
    }
    return fallback_bps;
}

}

GoogleCCNetworkController::GoogleCCNetworkController(const NetworkControllerConfig& config):
    init_config_(config),
    delay_based_bwe_(std::make_unique<DelayBasedBwe>()),
    acknowledged_bitrate_estimator_(std::make_unique<AcknowledgedBitrateEstimator>()),
    bandwidth_estimator_(std::make_unique<SendSideBandwidthEstimator>()),
    last_loss_based_bitrate_(*config.constraints.start_bitrate),
    pace_factor_(kDefaultPaceMultiplier),
    alr_detector_(std::make_unique<AlrDetector>()),
    probe_controller_(std::make_unique<ProbeController>()),
    probe_bitrate_estimator_(std::make_unique<ProbeBitrateEstimator>()) 
{
    delay_based_bwe_->SetMinBitrate(kGccMinBitrate);
}
GoogleCCNetworkController::~GoogleCCNetworkController() 
{
}
webrtc::NetworkControlUpdate GoogleCCNetworkController::OnTransportpacketsFeedback(const webrtc::TransportpacketsFeedback& report) 
{
    if(report.packet_feedbacks.empty()) {
        return webrtc::NetworkControlUpdate();
    }
    absl::optional<int64_t> alr_start_time = alr_detector_->GetAlrStartTime();//判断是否进入ALR状态
    previously_in_alr_ = alr_start_time.has_value();//如果ALR开始时间不为空，则之前处于ALR状态

    //从ALR状态恢复
    if(previously_in_alr_ && !alr_start_time) {
        acknowledged_bitrate_estimator_->SetAlrEndTime(report.feedback_time);
        probe_controller_->SetAlrEndTime(report.feedback_time.ms());
        //如果之前处于ALR状态，现在退出ALR状态，则需要将带宽估计值设置为0
        bandwidth_estimator_->UpdateDelayBasedBitrate(report.feedback_time,webrtc::DataRate::Zero());
    }

    //将数据包反馈信息传递给吞吐量估计器
    //里面只包含对面确认接收到的数据包
    acknowledged_bitrate_estimator_->IncomingPacketFeedbackVector(report.SortedByReceiveTime());

    absl::optional<webrtc::DataRate> acked_bitrate = acknowledged_bitrate_estimator_->bitrate();

    for(const auto& feedback :report.SortedByReceiveTime())
    {
        if(feedback.sent_packet.pacing_info.probe_cluster_id != webrtc::PacedPacketInfo::kNotAProbe)
        {
            // RTC_LOG(LS_WARNING) << "============feedback pack_id:"<<feedback.packet_id;
            probe_bitrate_estimator_->HandleProbeAndEstimateBitrate(feedback);
        }
    }

    //获得探测的码率值
    absl::optional<webrtc::DataRate> probe_bitrate = probe_bitrate_estimator_->FetchAndRestLastEstimatedBitrate();

    webrtc::NetworkControlUpdate update;

    DelayBasedBwe::Result result;
    result = delay_based_bwe_->IncomingPacketFeedbackVector(report,acked_bitrate,
                                                                 probe_bitrate,alr_start_time.has_value());
    
    //基于延迟的带宽估计值发生更新，需要设置到基于丢包的带宽估计模块
    if(result.updated) {
        bandwidth_estimator_->UpdateDelayBasedBitrate(result.feedback_time,result.target_bitrate);

        MaybeRiggerOnNetworkChanged(&update,report.feedback_time);
    }

    //从过载状态恢复过来
    if(result.recover_from_overusing) {
        //主动请求探测
       auto probes =  probe_controller_->RequestProbe(report.feedback_time.ms());
       update.probe_clusters_configs.insert(update.probe_clusters_configs.end(),probes.begin(),probes.end());
    }


    return update;
}


webrtc::NetworkControlUpdate GoogleCCNetworkController::OnRttUpdate(int64_t rtt_ms) {
    bandwidth_estimator_->UpdateRtt(webrtc::TimeDelta::Millis(rtt_ms));
    delay_based_bwe_->OnRttUpdate(rtt_ms);
    return webrtc::NetworkControlUpdate();
}

webrtc::NetworkControlUpdate GoogleCCNetworkController::OnNetworkOk(const webrtc::TargetRateConstraints &constraints)
{
    webrtc::NetworkControlUpdate update;
    update.probe_clusters_configs = ResetConstraints(constraints);
    MaybeTriggerOnNetworkChanged(&update,constraints.at_time);
    return update;
}

webrtc::NetworkControlUpdate GoogleCCNetworkController::OnTransportLoss(int32_t packets_lost,
    int32_t num_of_packets,webrtc::Timestamp at_time) {
    bandwidth_estimator_->UpdatePacketsLost(packets_lost,num_of_packets,at_time);
    return webrtc::NetworkControlUpdate();
}

webrtc::NetworkControlUpdate GoogleCCNetworkController::OnProcessInterval(const webrtc::ProcessInterval& msg) {
    webrtc::NetworkControlUpdate update;
    if(init_config_) {
        update.probe_clusters_configs = ResetConstraints(init_config_->constraints);
        update.pacer_config = GetPacingRate(msg.at_time);
        init_config_.reset();
    }
    bandwidth_estimator_->UpdateEstimate(msg.at_time);
    absl::optional<int64_t> alr_start_time = alr_detector_->GetAlrStartTime();
    probe_controller_->SetAlrStartTime(alr_start_time);
    auto probes = probe_controller_->Process(msg.at_time.ms());
    update.probe_clusters_configs.insert(update.probe_clusters_configs.end(),probes.begin(),probes.end());
    MaybeTriggerOnNetworkChanged(&update,msg.at_time);
    return update;
}

webrtc::NetworkControlUpdate GoogleCCNetworkController::OnSentPacket(const webrtc::SentPacket& sent_packet) {

    alr_detector_->OnByteSent(sent_packet.size.bytes(),sent_packet.send_time.ms());
    return webrtc::NetworkControlUpdate();
}

void GoogleCCNetworkController::MaybeTriggerOnNetworkChanged(webrtc::NetworkControlUpdate* update,webrtc::Timestamp at_time) 
{
    uint8_t fraction_loss = bandwidth_estimator_->fraction_loss();
    webrtc::TimeDelta rtt = bandwidth_estimator_->rtt();
    webrtc::DataRate loss_based_bitrate = bandwidth_estimator_->target_bitrate();

    if(loss_based_bitrate != last_loss_based_bitrate_ ||
        fraction_loss != last_estimated_fraction_loss_ ||
        rtt != last_estimated_rtt_) 
    {
        last_estimated_fraction_loss_ = fraction_loss;
        last_estimated_rtt_ = rtt;
        last_loss_based_bitrate_ = loss_based_bitrate;

        alr_detector_->SetEstimateBitrate(loss_based_bitrate.kbps());

        webrtc::TargetTransferRate target_rate_msg;
        target_rate_msg.at_time = at_time;
        target_rate_msg.target_rate = loss_based_bitrate;

        update->target_rate = target_rate_msg;
        auto probings = probe_controller_->SetEstimateBitrates(loss_based_bitrate.bps(),at_time.ms());
        update->probe_clusters_configs.insert(update->probe_clusters_configs.end(),probings.begin(),probings.end());
        update->pacer_config = GetPacingRate(at_time);

        RTC_LOG(LS_INFO) << "***************bwe "<<at_time.ms()
                        <<" fraction_loss: "<<(int)fraction_loss
                        <<" rtt: "<<rtt.ms()
                        <<" target_bitrate: "<<loss_based_bitrate.kbps();
    }

}

webrtc::PacerConfig GoogleCCNetworkController::GetPacingRate(webrtc::Timestamp at_time) 
{
    //尽量将发送的码率大一点，因为编码器输出的码率不是固定的,并且头部信息也占用一定码率，所以不能直接等于计算出来的码率
    webrtc::DataRate pacing_rate =  last_loss_based_bitrate_ *  pacing_factor_;
    webrtc::PacerConfig msg;
    msg.at_time = at_time;
    msg.time_window = webrtc::TimeDelta::Seconds(1);
    msg.data_window = pacing_rate * msg.time_window;
    return msg;
}


std::vector<webrtc::ProbeClusterConfig> GoogleCCNetworkController::ResetConstraints(const webrtc::TargetRateConstraints& constraints) 
{
    min_data_rate_ = constraints.min_data_rate.value_or(webrtc::DataRate::Zero());
    max_data_rate_ = constraints.max_data_rate.value_or(webrtc::DataRate::PlusInfinity());
    starting_rate_ = constraints.start_bitrate;

    min_data_rate_ = std::max(min_data_rate_,kGccMinBitrate);
    if(max_data_rate_ < min_data_rate_) {
        max_data_rate_ = min_data_rate_;
    }

    if(starting_rate_ && *starting_rate_< min_data_rate_) {
        starting_rate_ = min_data_rate_;
    }

    //设置基于丢包的带宽码率
    bandwidth_estimator_->SetBitrates(starting_rate_,
        min_data_rate_,
        max_data_rate_);

    //设置基于延迟的带宽码率
    if(starting_rate_) {
        delay_based_bwe_->SetSendBitrate(*starting_rate_);
    }

    delay_based_bwe_->SetMinBitrate(min_data_rate_);

    return  probe_controller_->SetBitrates(
        min_data_rate_.bps_or(-1),
        GetBpsOrDefault(starting_rate_,-1),
        max_data_rate_.bps_or(-1),
        constraints.at_time.ms());

}