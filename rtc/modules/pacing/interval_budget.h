#ifndef XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_
#define XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_

#include <stdint.h>

namespace xrtc {

class IntervalBudget {
public:
    IntervalBudget(int initial_target_bitrate_kbps,
        bool can_build_up_underuse = false);
    ~IntervalBudget();

    void set_target_bitrate_kbps(int target_bitrate_kbps);
    void IncreaseBudget(int64_t elapsed_time);
    void UseBudget(size_t bytes);
    size_t bytes_remaining();
    double budget_ratio() const;//返回预算剩余率
private:
    int target_bitrate_kbps_;
    int64_t max_bytes_in_budget_;
    int64_t bytes_remaining_;
    bool can_build_up_underuse_;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_PACING_INTERVAL_BUDGET_H_