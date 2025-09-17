#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_AIMD_RATE_CONTROL_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_AIMD_RATE_CONTROL_H_

#include <api/units/data_rate.h>
#include <api/units/timestamp.h>
#include <api/network_state_prediction.h>
#include <absl/types/optional.h>
#include "xrtc/rtc/modules/congestion_controller/google_gcc/link_capacity_estimator.h"
namespace xrtc {

class AimdRateControl {
public:
    AimdRateControl();
    ~AimdRateControl();

    void SetStartBitrate(webrtc::DataRate start_bitrate);
    void SetMinBitrate(webrtc::DataRate min_bitrate);
    bool ValidEstimate() const;
    //不是初始状态下，是否需要进一步降低码率
    bool TimeToReduceFurther(webrtc::Timestamp& at_time,webrtc::DataRate estmated_throughput) const;
    //初始状态下，是否需要进一步降低码率
    bool InitialTimeToReduceFurther(webrtc::Timestamp& at_time) const;
    //最新的估计值
    webrtc::DataRate LatestEstimate() const;

    void SetRtt(webrtc::TimeDelta rtt);
    void SetEstimate(webrtc::DataRate new_bitrate,webrtc::Timestamp at_time);
    //更新码率
    //throughput_estimate:吞吐量
    //state:网络状态
    //at_time:当前时间
    webrtc::DataRate Update(absl::optional<webrtc::DataRate> throughput_estimate,webrtc::BandwidthUsage state,webrtc::Timestamp at_time);
    void SetInAlr(bool in_alr){in_alr_ = in_alr;};
private:
    webrtc::DataRate ClampBitrate(webrtc::DataRate new_bitrate);
    webrtc::DataRate AdditiveRateIncrease(webrtc::Timestamp at_time,webrtc::Timestamp last_time);
    webrtc::DataRate MultiplicativeIncrease(webrtc::Timestamp at_time,webrtc::Timestamp last_time);
    double GetNearMaxIncreaseRateBpsPerSecond() const;
    void ChangeBitrate(absl::optional<webrtc::DataRate> throughput_estimate,webrtc::BandwidthUsage state,webrtc::Timestamp at_time);
    void ChangeState(webrtc::BandwidthUsage state,webrtc::Timestamp at_time);
private:
    enum class RateControlState {
        kReHold,//保持码率不变
        kReIncrease,//增加码率
        kReDecrease,//降低码率
    };
    webrtc::DataRate min_config_bitrate_;
    webrtc::DataRate max_config_bitrate_;
    webrtc::DataRate current_bitrate_;
    webrtc::DataRate latest_estimated_throughput_;
    bool bitrate_is_init_ = false;
    webrtc::TimeDelta rtt_;
    double beta_;
    webrtc::Timestamp time_last_bitrate_change = webrtc::Timestamp::MinusInfinity();
    webrtc::Timestamp time_last_bitrate_decrease = webrtc::Timestamp::MinusInfinity();
    webrtc::Timestamp time_first_throughput_estimate = webrtc::Timestamp::MinusInfinity();
    RateControlState rate_control_state_ = RateControlState::kReHold;
    LinkCapacityEstimator link_capacity_;
    bool in_alr_ = false;
    bool no_bitrate_increase_in_alr_ = false;//增加开关是否启动ALR
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_AIMD_RATE_CONTROL_H_