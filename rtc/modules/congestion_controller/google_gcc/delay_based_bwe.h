#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_DELAY_BASED_BWE_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_DELAY_BASED_BWE_H_

#include <api/transport/network_types.h>
#include <api/units/data_rate.h>
#include <absl/types/optional.h>

#include "xrtc/rtc/modules/congestion_controller/google_gcc/inter_arrival_delta.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/trendline_estimator.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/aimd_rate_control.h"
namespace xrtc {

    //基于延迟的带宽估计
class DelayBasedBwe {
public:
struct Result {
    bool updated = false;
    webrtc::DataRate target_rate = webrtc::DataRate::Zero();
    bool backoff_in_alr = false;//标记是否退避
    bool probe = false;//标记是否是探测
    bool recover_from_overusing = false;//标记是否是从过载恢复
}
    DelayBasedBwe();
    virtual~DelayBasedBwe();
    Result IncomingPacketFeedbackVector(const webrtc::TransportPacketsFeedback& msg,
        absl::optional<webrtc::DataRate> acked_bitrate,
        absl::optional<webrtc::DataRate> probe_bitrate,
        bool in_alr);
    void OnRttUpdate(int64_t rtt_ms);
    void SetStartBitrate(webrtc::DataRate start_bitrate);
    void SetMinBitrate(webrtc::DataRate min_bitrate);
private:

    void IncomingPacketFeedback(const webrtc::PacketResult& packet_feedback,
        const webrtc::Timestamp& at_time);
    Result MaybeUpdateEstimate(
        absl::optional<webrtc::DataRate> acked_bitrate,
        absl::optional<webrtc::DataRate> probe_bitrate,
        bool recover_from_overusing,
        webrtc::Timestamp& at_time,bool in_alr);
    //acked_bitrate当前的吞吐量
    //at_time:当前时间
    //target_rate:目标码率
    bool UpdateEstimate(absl::optional<webrtc::DataRate> acked_bitrate,webrtc::Timestamp& at_time,webrtc::DataRate* target_rate);
private:
    std::unique_ptr<InterArrivalDelta> video_inter_arrival_delta_;
    std::unique_ptr<TrendlineEstimator> video_delay_detector_;
    webrtc::Timestamp last_seen_timestamp_ = webrtc::Timestamp::MinusInfinity();
    AimdRateControl rate_control_;
    bool has_once_detect_overuse_ = false;//是否已经检测过一次过载
    webrtc::DataRate prev_bitrate_ = webrtc::DataRate::Zero();//上一次的吞吐量
    webrtc::BandwidthUsage prev_state_ = webrtc::BandwidthUsage::kBwNormal;//上一次的检测状态
    bool alr_limited_backoff_enabled_ = false;//ALR限制回退是否启用
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_DELAY_BASED_BWE_H_