#include "xrtc/rtc/modules/congestion_controller/google_gcc/delay_based_bwe.h"
#include<rtc_base/logging.h>

namespace xrtc {
namespace {
constexpr webrtc::TimeDelta kStreamTimeout = webrtc::TimeDelta::Seconds(2);
constexpr webrtc::TimeDelta kSendTimeGroupLength = webrtc::TimeDelta::Millis(5);
}
    DelayBasedBwe::DelayBasedBwe():video_delay_detector_(std::make_unique<TrendlineEstimator>()) {
}
DelayBasedBwe::~DelayBasedBwe() {
}
DelayBasedBwe::Result DelayBasedBwe::IncomingPacketFeedbackVector(const webrtc::TransportPacketsFeedback& msg,
    absl::optional<webrtc::DataRate> acked_bitrate,
    absl::optional<webrtc::DataRate> probe_bitrate,
    bool in_alr) {
    //数据包按照到达时间进行排序,会将没收到的数据包剔除掉
    auto packet_feedback_vector = msg.SortedByReceiveTime();
    if(packet_feedback_vector.empty()) {
        return DelayBasedBwe::Result();
    }
    //判断是否是从过载恢复
    bool recover_from_overusing = false;

    webrtc::BandwidthUsage prev_detector_state = video_delay_detector_->State();//上一次的检测状态

    //计算数据包的到达时间
    for(const auto& packet_feedback : packet_feedback_vector) {
        IncomingPacketFeedback(packet_feedback,msg.feedback_time);
        if(prev_detector_state == webrtc::BandwidthUsage::kBwUnderusing && 
            video_delay_detector_->State() == webrtc::BandwidthUsage::kBwNormal) {
            recover_from_overusing = true;
        }
        prev_detector_state = video_delay_detector_->State();
    }
    //将ALR状态设置到码控模块中
    rate_control_.SetInAlr(in_alr);
    return MaybeUpdateEstimate(acked_bitrateprobe_bitrate,recover_from_overusing,msg.feedback_time,in_alr);
}

void DelayBasedBwe::OnRttUpdate(int64_t rtt_ms) {
    rate_control_.SetRtt(webrtc::TimeDelta::Millis(rtt_ms));
}

void DelayBasedBwe::SetMinBitrate(webrtc::DataRate min_bitrate) {
    rate_control_.SetMinBitrate(min_bitrate);
}

void DelayBasedBwe::SetStartBitrate(webrtc::DataRate start_bitrate) {
    rate_control_.SetStartBitrate(start_bitrate);
}

void DelayBasedBwe::IncomingPacketFeedback(const webrtc::PacketResult& packet_feedback,
    const webrtc::Timestamp& at_time) {
        //如果是第一次收到packet，需要创建计算包租时间差的对象
        //如果长时间没有收到packet超过一定的阈值，需要重新创建对象
    if(last_seen_timestamp_.IsPlusInfinity() ||
        at_time - last_seen_timestamp_ > kStreamTimeout) 
    {
        video_inter_arrival_delta_ = std::make_unique<InterArrivalDelta>(kSendTimeGroupLength);
        video_delay_detector_.reset(new TrendlineEstimator());
    }

    last_seen_timestamp_ = at_time;
    //数据包的大小
    size_t packet_size = packet_feedback.sent_packet.size.bytes();

    webrtc::TimeDelta send_time_delta = webrtc::TimeDelta::Zero();
    webrtc::TimeDelta recv_time_delta = webrtc::TimeDelta::Zero();
    int packet_size_delta = 0;
    bool calculated_delta = video_inter_arrival_delta_->ComputeDeltas(
        packet_feedback.sent_packet.send_time,
        packet_feedback.receive_time,
        at_time,
        packet_size,
        &send_time_delta,
        &recv_time_delta,
        &packet_size_delta);
    // if(calculated_delta) {
    //     RTC_LOG(LS_INFO)<< "******************** send_time_delta: " << send_time_delta.ms()
    //         << " recv_time_delta: " << recv_time_delta.ms()
    //         << " packet_size_delta: " << packet_size_delta;
    // }   

    video_delay_detector_->Update(recv_time_delta,send_time_delta,
                                packet_feedback.sent_packet.send_time,
                                packet_feedback.receive_time,
                                packet_size,
                                calculated_delta);
}

DelayBasedBwe::Result DelayBasedBwe::MaybeUpdateEstimate(
    absl::optional<webrtc::DataRate> acked_bitrate, 
    absl::optional<webrtc::DataRate> probe_bitrate,
    bool recover_from_overusing,
    webrtc::Timestamp& at_time,bool in_alr) 
{
    //根据网络检测状态来动态的调整发送码率
    Result result;
    //当网络出现过载的时候
    if(video_delay_detector_->State() == webrtc::BandwidthUsage::kBwOverusing) {
        if(has_once_detect_overuse_ && in_alr && alr_limited_backoff_enabled_) {
            if(rate_control_.TimeToReduceFurther(at_time,prev_bitrate_)) {
                result.updated = UpdateEstimate(prev_bitrate_,at_time,&result.target_bitrate);
                result.backoff_in_alr = true;
            }
        }
        //已知吞吐量时，也就是发送码率，对方确认接收的码率
        else if(acked_bitrate && rate_control_.TimeToReduceFurther(at_time,*acked_bitrate)) {
            result.updated = UpdateEstimate(acked_bitrate,at_time,&result.target_rate);
        }
        //当不知道吞吐量的时候
        else if(!acked_bitrate && rate_control_.ValidEstimate() && rate_control_.InitialTimeToReduceFurther(at_time)){
            //当我们不知道吞吐量的时候，直接将当前的码率下降一半
            rate_control_.SetEstimate(rate_control_.LatestEstimate()/2,at_time);
            result.updated = true;
            result.target_rate = rate_control_.LatestEstimate();
        }
        has_once_detect_overuse_ = true;
    }
    //网络没有出现过载的时候
    else{
        if(probe_bitrate) {
            result.probe = true;
            result.updated = true;
            result.target_rate = *probe_bitrate;//将探测的码率设置为目标码率
            rate_control_.SetEstimate(*probe_bitrate,at_time);
        }
        else {
            result.updated = UpdateEstimate(acked_bitrate,at_time,&result.target_bitrate);
            result.recover_from_overusing = recover_from_overusing;
        }
    }
    webrtc::BandwidthUsage detected_state = video_delay_detector_->State();
    if((result.updated && result.target_bitrate != prev_bitrate_) || (detected_state != prev_state_)) {
        prev_bitrate_ = result.target_bitrate;
        prev_state_ = detected_state;
    }
    return result;
}
bool DelayBasedBwe::UpdateEstimate(absl::optional<webrtc::DataRate> acked_bitrate,webrtc::Timestamp& at_time,webrtc::DataRate* target_rate) {
    *target_rate = rate_control_.Update(acked_bitrate,video_delay_detector_->State(),at_time);
    return rate_control_.ValidEstimate();
}
} // namespace xrtc
