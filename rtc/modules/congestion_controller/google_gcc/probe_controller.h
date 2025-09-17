#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_CONTROLLER_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_CONTROLLER_H_
#include <vector>
#include <api/transport/network_types.h>

namespace xrtc {
struct ProbeControllerConfig {
    double first_exp_probing_scale = 3.0f;
    double second_exp_probing_scale = 6.0f;
    double further_probe_threshold = 0.7f;
    double further_exp_probe_scale = 2.0f;
    webrtc::TimeDelta alr_probing_interval = webrtc::TimeDelta::Seconds(5);//周期为5秒钟
    double alr_probe_scale = 2.0f;
};
/*
本质上是构造一组探测任务的配置，包括探测的码率、探测的持续时间、探测包发送的个数，探测的 id 等，这组任务配置最终会发给 pacer 去执行
*/
class ProbeController {
public:
    ProbeController();
    ProbeController(const ProbeControllerConfig& config);
    ~ProbeController();
    
    std::vector<webrtc::ProbeClusterConfig> SetBitrates(int64_t min_bitrate_bps,
                                                        int64_t start_bitrate_bps,
                                                        int64_t max_bitrate_bps,
                                                        int64_t at_time_ms);
    std::vector<webrtc::ProbeClusterConfig> SetEstimateBitrates(int64_t estimate_bitrate_bps,
                                                                int64_t at_time_ms);
    std::vector<webrtc::ProbeClusterConfig> RequestProbe(int64_t at_time_ms);
    std::vector<webrtc::ProbeClusterConfig> Process(int64_t at_time_ms);
    void SetAlrStartTime(absl::optional<int64_t> alr_start_time);
    void SetAlrEndTime(int64_t alr_end_time);

private:
        std::vector<webrtc::ProbeClusterConfig> InitExpProbing(int64_t at_time_ms);//初始化探测,指数级探测
        std::vector<webrtc::ProbeClusterConfig> InitProbing(int64_t at_time_ms,const std::vector<int64_t>& probes,bool further_probe);
private:
enum class State {
    kInit,                      // 初始状态，探测没有触发
    kWaitingForProbingResult,   // 等得探测结果，才能继续进一步探测
    kProbingComplete,           // 探测完成
};

    ProbeControllerConfig config_;
    int64_t start_bitrate_bps_ = 0;
    int64_t max_bitrate_bps_ = 0;
    int64_t estimate_bitrate_bps_ = 0;
    int64_t time_of_last_drop_large_ms_ = 0;
    int64_t bitrate_before_last_drop_large_bps_ = 0;
    State state_ = State::kInit;
    bool network_avilable_ = true;
    int next_probe_cluster_id_ = 1;
    int64_t last_time_init_probing_ms_ = 0;
    //如果为0，表示不再进行探测
    int64_t min_bitrate_to_further_probe_bps_ = 0;

    //进入ALR的时间
    absl::optional<int64_t> alr_start_time_ms_;
    //ALR结束的时间
    absl::optional<int64_t> alr_end_time_ms_;
    int64_t last_bwe_drop_probe_time_ms_ = 0;
    bool enable_periodic_alr_probing = false;//是否开启周期性探测
};
} // namespace xrtc


#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_CONTROLLER_H_