#include "xrtc/rtc/modules/congestion_controller/google_gcc/probe_controller.h"

#include <rtc_base/logging.h>

namespace xrtc {
namespace {
    const int64_t kDefaultMaxProbingBitrateBps = 5000000;//默认的最大探测码率
    const int kMinProbeDurationMs = 15;//最小探测持续时间
    const int kMinProbePacketsSent = 5;//最小探测数据包发送数
    const double kBitrateDropThreshold = 0.66;//码率降低的阈值
    const int kAlrEndedRecentlyMs = 3000;
    const double kProbeFractionAfterDrop = 0.85f;
    const double kProbeUncertainty = 0.05f;
    const int kBitrateDropTimeoutMs = 5000;
    const int kMinTimeBetweenAlrProbesMs = 5000;
    const int kMaxWaitingTimeForProbingResultMs = 1000;
}//namespace
ProbeController::ProbeController() {
}
ProbeController::ProbeController(const ProbeControllerConfig& config) : config_(config) {
}
ProbeController::~ProbeController() {
}


std::vector<webrtc::ProbeClusterConfig> ProbeController::SetBitrates(
    int64_t min_bitrate_bps,
    int64_t start_bitrate_bps,
    int64_t max_bitrate_bps,
    int64_t at_time_ms) 
{
    if(start_bitrate_bps > 0) {
        start_bitrate_bps_ = start_bitrate_bps;
    }
    else if(start_bitrate_bps == 0) {
        start_bitrate_bps_ = min_bitrate_bps;
    }

    max_bitrate_bps_ = max_bitrate_bps;

    switch(state_) {
        case State::kInit:
            state_ = State::kWaitingForProbingResult;
            break;
        case State::kWaitingForProbingResult:
            state_ = State::kProbingComplete;
            break;
    }
    return std::vector<webrtc::ProbeClusterConfig>();
}


std::vector<webrtc::ProbeClusterConfig> ProbeController::SetEstimateBitrates(
        int64_t estimate_bitrate_bps,
        int64_t at_time_ms) 
{
    std::vector<webrtc::ProbeClusterConfig> pending_probes;
    if(state_ == State::kWaitingForProbingResult) {
        RTC_LOG(LS_INFO)<<"Measured bitrate: "<<estimate_bitrate_bps
                        <<"Mininum probe bitrate: "<<min_bitrate_to_further_probe_bps_;
        if(min_bitrate_to_further_probe_bps_ != 0 && estimate_bitrate_bps >min_bitrate_to_further_probe_bps_)
        {
            pending_probes = InitProbing(at_time_ms,
                {static_cast<int64_t>(config_.further_exp_probe_scale * estimate_bitrate_bps)},
                true);
        }
    }
    /*
    如果当前的码率估计值较之前降低的非常大（比如 kBitrateDropThreshold=0.66），
    需要记录上一次的估计码率值，用于后续的快速恢复
    */
    //estimate_bitrate_bps_：上一次的码率估计值
    //estimate_bitrate_bps：当前的码率估计值
    if(estimate_bitrate_bps < estimate_bitrate_bps_ * kBitrateDropThreshold) 
    {
        time_of_last_drop_large_ms_ = at_time_ms;
        bitrate_before_last_drop_large_bps_ = estimate_bitrate_bps_;
    }
    return pending_probes;
}

void ProbeController::SetAlrStartTime(absl::optional<int64_t> alr_start_time) {
    alr_start_time_ms_ = alr_start_time;
}

void ProbeController::SetAlrEndTime(int64_t alr_end_time) {
    alr_end_time_ms_.emplace(alr_end_time);
}   


std::vector<webrtc::ProbeClusterConfig> ProbeController::InitExpProbing(int64_t at_time_ms) 
{
    std::vector<int64_t> probes ={ static_cast<int64_t>(config_.first_exp_probing_scale * start_bitrate_bps_) };
    if(config_.second_exp_probing_scale) {
        probes.push_back(static_cast<int64_t>(config_.second_exp_probing_scale * start_bitrate_bps_));
    }
    return InitProbing(at_time_ms,probes,true);
}

std::vector<webrtc::ProbeClusterConfig> ProbeController::Process(int64_t at_time_ms) {
    //处理探测超时状态
    if(at_time_ms - last_time_init_probing_ms_ > kMaxWaitingTimeForProbingResultMs) {
        if(state_ == State::kWaitingForProbingResult) {
            RTC_LOG(LS_INFO)<<"kMaxWaitingTimeForProbingResultMs timeout";
            state_ = State::kProbingComplete;
            min_bitrate_to_further_probe_bps_ = 0;
        }
    }

    //当处于ALR状态时，开启周期性探测的功能
    if(enable_periodic_alr_probing && state_ == State::kProbingComplete){
        if(alr_start_time_ms_&& estimate_bitrate_bps_ > 0){
            //计算下一次探测的时间
            int64_t next_probe_time =std::max(*alr_start_time_ms_,last_time_init_probing_ms_) + config.alr_probing_interval.ms();
            if(at_time_ms >= next_probe_time){
                return InitProbing(at_time_ms,
                {static_cast<int64_t>(estimate_bitrate_bps_ * config_.alr_probe_scale)},
                true);
            }
        }
    }
    return std::vector<webrtc::ProbeClusterConfig>();
}


//主动探测功能
std::vector<webrtc::ProbeClusterConfig> ProbeController::RequestProbe(int64_t at_time_ms) 
{
    bool in_alr = alr_start_time_ms_.has_value();
    bool alr_ended_recently = alr_end_time_ms_.has_value() && at_time_ms - alr_end_time_ms_.value() < kAlrEndedTimeoutMs;
    if(in_alr && alr_ended_recently) {
        if(state_ = State::kProbingComplete) {
            int64_t suggested_probe_bitrate = kProbeFractionAfterDrop * bitrate_before_last_drop_large_bps_;
            int64_t min_expetected_probe_result_bps =(1-kProbeUncertainty) * suggested_probe_bitrate;
            int64_t time_since_drop_time = at_time_ms - time_of_last_drop_large_ms_;
            int64_t time_since_probe_time = at_time_ms  - last_bwe_drop_probe_time_ms_;
            if(min_expetected_probe_result_bps > estimate_bitrate_bps_ &&
                time_since_drop_time < kBitrateDropTimeoutMs &&
                time_since_probe_time > kMinTimeBetweenAlrProbesMs) 
            {
                RTC_LOG(LS_INFO)<<"estimate bitrate drop large, request probe";
                last_bwe_drop_probe_time_ms_ = at_time_ms;
                return InitProbing(at_time_ms,{suggested_probe_bitrate}, false);
            }
        }
    }
    return InitProbing(at_time_ms,std::vector<int64_t>{start_bitrate_bps_},false);
}

std::vector<webrtc::ProbeClusterConfig> ProbeController::InitProbing(
    int64_t at_time_ms,
    const std::vector<int64_t>& probes,
    bool further_probe) 
{
    int64_t max_probe_bitrate_bps = max_bitrate_bps_> 0 ? max_bitrate_bps_ : kDefaultMaxProbingBitrateBps;

    //TODO:可以使用最大分配的码率来限制探测码率的上限
    //可以设置为最大分配码率的2倍
    //1：防止突发流量的开销
    //2：防止探测码率过高，导致网络拥塞，降低通话质量

    std::vector<webrtc::ProbeClusterConfig> pending_probes;
    for(int64_t bitrate : probes) {
        if(bitrate > max_probe_bitrate_bps) 
        {
            bitrate = max_probe_bitrate_bps;
            further_probe = false;
        }

        webrtc::ProbeClusterConfig cluster_config;
        cluster_config.at_time_ms = webrtc::Timestamp::Millis(at_time_ms);
        cluster_config.target_data_rate = webrtc::DataRate::BitsPerSec(bitrate);
        cluster_config.target_duration = webrtc::TimeDelta::Millis(kMinProbeDurationMs);
        cluster_config.target_probe_count = kMinProbePacketsSent;
        cluster_config.id = next_probe_cluster_id_++;

        pending_probes.push_back(cluster_config);
    }

    last_time_init_probing_ms_ = at_time_ms;

    //如果需要进一步探测，则返回探测任务配置
    if(further_probe) {
        state_ = State::kWaitingForProbingResult;
        min_bitrate_to_further_probe_bps_ = (*(probes.end()-1)) * config_.further_probe_threshold;
    }
    else {
        state_ = State::kProbingComplete;
        min_bitrate_to_further_probe_bps_ = 0;
    }
    return pending_probes;
}

} // namespace xrtc