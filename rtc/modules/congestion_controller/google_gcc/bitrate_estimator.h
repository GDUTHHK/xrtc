#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_BITRATE_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_BITRATE_ESTIMATOR_H_

#include <absl/types/optional.h>
#include <api/units/timestamp.h>
#include <api/units/data_size.h>
#include <api/units/data_rate.h>
namespace xrtc {

//吞吐量估计
class BitrateEstimator {
public:
    BitrateEstimator();
    ~BitrateEstimator();

    void Update(webrtc::Timestamp at_time,webrtc::DataSize amount);
    //返回估计出的码率值
    absl::optional<webrtc::DataRate> bitrate() const;
    void ExpectedFastRateChange();
private:
    float UpdateWindow(int64_t now_ms,int bytes,int rate_window_ms);
private:
    float bitrate_estimate_kbps_ = -1.0f;//先验的码率值
    float bitrate_estimate_var_ = 50.0f;//先验分布的方差
    int64_t prev_time_ms_ = -1;
    int current_window_ms_ = 0;
    int sum_ = 0;
};
} // namespace xrtc



#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_BITRATE_ESTIMATOR_H_