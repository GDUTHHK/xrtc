#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ACKNOWLEDGED_BITRATE_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ACKNOWLEDGED_BITRATE_ESTIMATOR_H_

#include <api/transport/network_types.h>
#include "xrtc/rtc/modules/congestion_controller/google_gcc/bitrate_estimator.h"
//吞吐量估计
namespace xrtc {
class AcknowledgedBitrateEstimator {
public:
    AcknowledgedBitrateEstimator();
    ~AcknowledgedBitrateEstimator();

    void IncomingPacketFeedbackVector(const webrtc::PacketResult& packet_feedback_vector);
    absl::optional<webrtc::DataRate> bitrate() const;
    void SetAlrEndTime(webrtc::Timestamp alr_end_time);
private:
    std::unique_ptr<BitrateEstimator> bitrate_estimator_;
    absl::optional<webrtc::Timestamp> alr_end_time_;//ALR结束时间
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ACKNOWLEDGED_BITRATE_ESTIMATOR_H_