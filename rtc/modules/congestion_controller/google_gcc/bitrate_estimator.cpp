#include "xrtc/rtc/modules/congestion_controller/google_gcc/bitrate_estimator.h"
#include <rtc_base/logging.h>

namespace xrtc {
namespace {
    const int kInitialRateWindowsMs = 500;//刚开始启动时，使用的码率窗口大小
    const int kRateWindowMs = 100;//稳定之后，使用的码率窗口大小
}
BitrateEstimator::BitrateEstimator() {
}
BitrateEstimator::~BitrateEstimator() {
}
void BitrateEstimator::Update(webrtc::Timestamp at_time,webrtc::DataSize amount) 
{

    int rate_window_ms = kRateWindowMs;
    if(bitrate_estimate_kbps_ < 0.0f) { //刚开始启动时，使用初始窗口大小
        rate_window_ms = kInitialRateWindowsMs;
    }
    //获得当前窗口的码率值(后验码率值)
    float bitrate_sample_kbps = UpdateWindow(at_time.ms(),amount.bytes(),rate_window_ms);
    //如果码率值小于0，表示没有收到数据包，则不更新码率值
    if(bitrate_sample_kbps < 0.0f) {
        return;
    }
    //如果码率值大于0，说明已经获得了样本的码率值，则更新码率值
    //先验的码率值
    if(bitrate_estimate_kbps_ < 0.0f) {//第一次获得样本数据
        //因为在这里可以形成一个先验链，用现在的后验分布作为下一个计算的先验分布
        bitrate_estimate_kbps_ = bitrate_sample_kbps;
        return;
    }
    //TOPO：根据样本的不同，可以给与不同的不确定性，比如小样本
    //给与更大的不确定性
    float uncertainty_scale = 10.0f;
    //计算后验分布方差 = 样本的方差
    float sample_uncertainty = uncertainty_scale * std::abs(bitrate_sample_kbps - bitrate_estimate_kbps_) / bitrate_estimate_kbps_;
    //计算后验分布方差
    float sample_var = sample_uncertainty * sample_uncertainty;
    //优化：增加先验分布的一个不确定性
    float pred_estimate_var = bitrate_estimate_var_  +5.0f;
    //计算估计值
    bitrate_estimate_kbps_ = (sample_var * bitrate_estimate_var_   +pred_estimate_var * bitrate_sample_kbps) / 
                                (sample_var + pred_estimate_var);
    //更新先验分布的方差
    bitrate_estimate_var_ = (sample_var * pred_estimate_var) / (sample_var + pred_estimate_var);
    RTC_LOG(LS_INFO) << "===========ack_bitrate_kbps_:" << bitrate_estimate_kbps_;
}


absl::optional<webrtc::DataRate> BitrateEstimator::bitrate() const {
    if(bitrate_estimate_kbps_ < 0.0f) {
        return absl::nullopt;
    }
    return webrtc::DataRate::KilobitsPerSec(bitrate_estimate_kbps_);
}

//让方差更大一点，估计的收敛速度会更快
void BitrateEstimator::ExpectedFastRateChange() {
    bitrate_estimate_var_ += 200.0f;
}

//根据窗口计算当前码率值
float BitrateEstimator::UpdateWindow(int64_t now_ms,int bytes,int rate_window_ms) {
    //如果时间发生向后移动，需要重置
    if(now_ms < prev_time_ms_) {
        prev_time_ms_ = -1;
        current_window_ms_ = 0;
        sum_ = 0;
    }
    if(prev_time_ms_ > 0){
        current_window_ms_ += (now_ms - prev_time_ms_);
        //如果超过一个完整的时间窗口，没有收到数据包，则重置
        if(now_ms - prev_time_ms_ > rate_window_ms) {
            current_window_ms_ %= rate_window_ms;
            sum_ = 0;
        }
    }
    prev_time_ms_ = now_ms;
    float bitrate_sample_kbps = -1.0f;
    //累计的时间已经达到了窗口时间，可以计算窗口码率了
    if(current_window_ms_ >= rate_window_ms) {
        bitrate_sample_kbps = sum_ * 8.0f / static_cast<float>(rate_window_ms);
        current_window_ms_ -= rate_window_ms;
        sum_ = 0;
    }
    sum_ += bytes;
    return bitrate_sample_kbps;
}
} // namespace xrtc
