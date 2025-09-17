#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ALR_DETECTOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ALR_DETECTOR_H_

#include<absl/types/optional.h>
#include"xtrc/rtc/modules/pacing/interval_budget.h"

namespace xrtc {
class AlrDetector {

struct AlrDetectorConfig {
    // Sent traffic ratio as a function of network capacity used to determine
    // application-limited region. ALR region start when bandwidth usage drops
    // below kAlrStartUsageRatio and ends when it raises above
    // kAlrEndUsageRatio. NOTE: This is intentionally conservative at the moment
    // until BW adjustments of application limited region is fine tuned.
    double bandwidth_usage_ratio = 0.65;//带宽使用比例
    double start_budget_level_ratio = 0.80;//开始使用ALR预算的阈值
    double stop_budget_level_ratio = 0.50;//停止使用ALR预算的阈值
};
       
public:
    AlrDetector();
    AlrDetector(const AlrDetectorConfig& config);
    ~AlrDetector();

    void OnByteSent(size_t bytes,int64_t send_time_ms);
    void SetEstimateBitrate(int64_t target_bitrate_kbps);
    absl::optional<int64_t> GetAlrStartTime() const{ return alr_start_time_ms_; }
private:
    AlrDetectorConfig conf_;
    IntervalBudget alr_budget_;
    absl::optional<int64_t> last_send_time_ms_;//最新发送时间
    absl::optional<int64_t> alr_start_time_ms_;//ALR开始时间
};
} // namespace xrtc


#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_ALR_DETECTOR_H_