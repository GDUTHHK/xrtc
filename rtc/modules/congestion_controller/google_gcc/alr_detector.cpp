#include "xrtc/rtc/modules/congestion_controller/google_gcc/alr_detector.h"
#include <rtc_base/time_utils.h>
namespace xrtc {
AlrDetector::AlrDetector():alr_budget_(0,true) {
}
AlrDetector::AlrDetector(const AlrDetectorConfig& config):alr_budget_(0,true),conf_(config) {
}
AlrDetector::~AlrDetector() {
}

void AlrDetector::OnByteSent(size_t bytes,int64_t send_time_ms) {
    //第一次进来    
    if(!last_send_time_ms_.has_value()) {
        last_send_time_ms_ = send_time_ms;
        return ;
    }

    int time_delta_ms =send_time_ms - *last_send_time_ms_;
    last_send_time_ms_ = send_time_ms;

    alr_budget_.UseBudget(bytes);//减少预算
    alr_budget_.IncreaseBudget(time_delta_ms);//增加预算

    //当前的预算剩余率超过阈值，表明发送码率突降，此时应该进入ALR状态
    if(alr_budget_.budget_ratio() > conf_.start_budget_level_ratio && !alr_start_time_ms_) 
    {
        alr_start_time_ms_.emplace(rtc::TimeMillis());
    }
    //当前的预算剩余率低于阈值，表明发送码率恢复正常，此时应该退出ALR状态
    else if(alr_budget_.budget_ratio() < conf_.stop_budget_level_ratio && alr_start_time_ms_) {
        alr_start_time_ms_.reset();
    }
}

void AlrDetector::SetEstimateBitrate(int64_t target_bitrate_kbps) {
    target_bitrate_kbps_ = conf_.bandwidth_usage_ratio*target_bitrate_kbps;
    alr_budget_.set_target_bitrate_kbps(target_bitrate_kbps_);
}

} // namespace xrtc
