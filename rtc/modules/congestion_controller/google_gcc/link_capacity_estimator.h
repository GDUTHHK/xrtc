#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_LINK_CAPACITY_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_LINK_CAPACITY_ESTIMATOR_H_

#include<api/units/data_rate.h>
#include <absl/types/optional.h>
namespace xrtc {
class LinkCapacityEstimator {
public:
    LinkCapacityEstimator();
    ~LinkCapacityEstimator();

    webrtc::DataRate UpperBound() const;//返回估计值上限
    webrtc::DataRate LowerBound() const;//返回估计值下限
    void OnoveruseDetected(webrtc::DataRate acked_bitrate);
    bool HasEstimate() const;
    webrtc::DataRate Estimate() const;
    void Reset();
private:
    void Update(webrtc::DataRate sample_capacity,double alpha);
    double deviation_estimate_kbps() const;//返回估计值的误差值
private:
    absl::optional<double> estimated_kbps_;//估计出来的容量
    double deviaction kbps_ = 0.4f;//估计值的误差,归一化后的方差（指数平滑）
}

} // namespace xrtc


#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_LINK_CAPACITY_ESTIMATOR_H_