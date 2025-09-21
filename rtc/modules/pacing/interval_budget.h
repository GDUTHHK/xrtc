#ifndef XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_
#define XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_

#include <stdint.h>

namespace xrtc {

//间隔预算类
class IntervalBudget {
public:
    IntervalBudget(int initial_target_bitrate_kbps,
        bool can_build_up_underuse = false);
    ~IntervalBudget();

    void set_target_bitrate_kbps(int target_bitrate_kbps);
    void IncreaseBudget(int64_t elapsed_time);//随着时间而增加
    void UseBudget(size_t bytes);//消耗预算
    size_t bytes_remaining();//获得剩余多少预算
    double budget_ratio() const;//返回预算剩余率
private:
    int target_bitrate_kbps_;//目标码率
    int64_t max_bytes_in_budget_;//最大预算,防止时间片或者带宽过大，导致起不到平滑的作用
    int64_t bytes_remaining_;//剩余字节数
    bool can_build_up_underuse_;//定义一个上一个时间片剩余的码率下一个时间片是否能使用
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_