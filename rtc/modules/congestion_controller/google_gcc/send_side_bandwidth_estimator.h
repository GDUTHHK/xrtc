#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_SEND_SIDE_BANDWIDTH_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_SEND_SIDE_BANDWIDTH_ESTIMATOR_H_

#include <deque>
#include <absl/types/optional.h>
#include <api/units/timestamp.h>
#include <api/units/data_rate.h>

namespace xrtc {

//基于丢包的带宽估计
class SendSideBandwidthEstimator {
public:
    SendSideBandwidthEstimator();
    ~SendSideBandwidthEstimator();
    webrtc::DataRate target_bitrate() const;

    uint8_t fraction_loss() const {return last_fraction_lost_;}
    webrtc::TimeDelta rtt() const {return last_rtt_;}

    void UpdateDelayBasedBitrate(webrtc::Timestamp at_time,webrtc::DataRate bitrate);
    void UpdatePacketsLost(int packets_lost,int num_of_packets,webrtc::Timestamp at_time);
    void UpdateRtt(webrtc::TimeDelta rtt);
    void UpdateEstimate(webrtc::Timestamp at_time);
    void SetBitrates(absl::optional<webrtc::DataRate> send_bitrate,
                                    absl::optional<webrtc::DataRate> max_bitrate,
                                    absl::optional<webrtc::DataRate> min_bitrate);
    void SetMinMaxBitrate(webrtc::DataRate min_bitrate,webrtc::DataRate max_bitrate);
    void SetSendBitrate(webrtc::DataRate start_bitrate);
private:
    void UpdateTargetBitrate(webrtc::DataRate new_bitrate);
    bool IsInStartPhase(webrtc::Timestamp at_time);
    webrtc::DataRate GetUpperLimit() const;
    void UpdateMinHistory(webrtc::Timestamp at_time);
private:
    webrtc::Timestamp last_loss_feedback_ = webrtc::Timestamp::MinusInfinity();
    webrtc::Timestamp first_report_time_ = webrtc::Timestamp::MinusInfinity();
    int expected_packets_since_last_loss_update_ = 0;
    int loss_packet_since_last_loss_update_ = 0;
    uint8_t last_fraction_lost_ = 0;
    webrtc::Timestamp last_loss_report_time_ = webrtc::Timestamp::MinusInfinity();
    webrtc::DataRate current_target_ = webrtc::DataRate::Zero();
    webrtc::DataRate delay_based_bitrate_ = webrtc::DataRate::PlusInfinity();//基于延迟的目标码率
    webrtc::DataRate max_bitrate_config_ ;//最大码率
    webrtc::DataRate min_bitrate_config_ ;//最小码率
    std::deque<std::pair<webrtc::Timestamp,webrtc::DataRate>> min_bitrate_history_;//最小码率历史队列
    float low_loss_threshold_ ;//低丢包率阈值
    float high_loss_threshold_ ;//高丢包率阈值
    webrtc::Timestamp time_last_decrease_ = webrtc::Timestamp::MinusInfinity();
    webrtc::TimeDelta last_rtt_ = webrtc::TimeDelta::Zero();
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_SEND_SIDE_BANDWIDTH_ESTIMATOR_H_