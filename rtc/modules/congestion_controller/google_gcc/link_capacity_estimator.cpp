#include "xrtc/rtc/modules/congestion_controller/google_gcc/link_capacity_estimator.h"
#include <rtc_base/numerics/safe_minmax.h>
namespace xrtc {
LinkCapacityEstimator::LinkCapacityEstimator() {
}
LinkCapacityEstimator::~LinkCapacityEstimator() {
}

void LinkCapacityEstimator::OnoveruseDetected(webrtc::DataRate acked_bitrate) {
    Update(acked_bitrate,0.05);//指数平滑处理0.05
}

bool LinkCapacityEstimator::HasEstimate() const {
    return estimated_kbps_.has_value();
}

webrtc::DataRate LinkCapacityEstimator::Estimate() const {
    return webrtc::DataRate::KilobitsPerSec(estimated_kbps_.value());
}

void LinkCapacityEstimator::Reset() {
    estimated_kbps_.reset();
}

//在上限和下限范围之间认为可信度为99.6%
//返回估计值的上限
webrtc::DataRate LinkCapacityEstimator::UpperBound() const {
    if(estimated_kbps_.has_value()) {
        return webrtc::DataRate::KilobitsPerSec(estimated_kbps_.value() + 3*deviation_estimate_kbps());
    }
    return webrtc::DataRate::Infinity();
}

//返回估计值的下限
webrtc::DataRate LinkCapacityEstimator::LowerBound() const {
    if(estimated_kbps_.has_value()) {
        return webrtc::DataRate::KilobitsPerSec(std::max(estimated_kbps_.value() - 3*deviation_estimate_kbps(),0.0f));//保证下限不为负
    }
    return webrtc::DataRate::Zero();
}

void LinkCapacityEstimator::Update(webrtc::DataRate sample_capacity,double alpha) {
    //获得指数平滑后的链路容量的估计值
    double sample_kbps = sample_capacity.kbps<double>();
    //第一次更新，使用样本值
    if(!estimated_kbps_.has_value()) {//第一次获得样本数据
        estimated_kbps_ = sample_kbps;
    }else{
        estimated_kbps_ =(1 - alpha) * estimated_kbps_.value() + alpha * sample_kbps;
    }

    //计算方差并且进行归一化
    double norm =std::max(estimated_kbps_.value(),1.0f);
    //计算误差
    double error_kbps = estimated_kbps_.value() - sample_kbps;
    //计算指数平滑后的归一化方差
    deviation_kbps_ = (1 - alpha) * deviation_kbps_ + alpha * error_kbps * error_kbps/ norm;
    deviation_kbps_ = rtc::SafeClamp(deviation_kbps_,0.4f,2.5f);
}

double LinkCapacityEstimator::deviation_estimate_kbps() const {
    return sqrt(deviation_kbps_ * estimated_kbps_.value());
}
} // namespace xrtc