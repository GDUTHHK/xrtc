#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_TRENDLINE_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_TRENDLINE_ESTIMATOR_H_

#include<deque>
#include<fstream>
#include <absl/types/optional.h>
#include <api/units/time_delta.h>
#include <api/units/timestamp.h>
#include <api/network_state_prediction.h>
namespace xrtc {

class TrendlineEstimator {
public:
    TrendlineEstimator();
    ~TrendlineEstimator();
    //更新趋势线
    //recv_time_delta:接收端的时间差
    //send_time_delta:发送端的时间差
    //arrival_time:到达时间
    //packet_size:包大小
    //calculated_delta:是否计算了时间差
    void Update(webrtc::TimeDelta recv_time_delta,webrtc::TimeDelta send_time_delta,webrtc::TimeDelta arrival_time,
                    size_t packet_size,bool calculated_delta);
    webrtc::BandwidthUsage State() const;
struct PacketTiming {
    PacketTiming(
        double arrival_time_ms,//数据包到达的的时间
        double smoothed_delay_ms,//指数平滑后的延迟差
        double raw_delay_ms) ://原始的延迟差
        arrival_time_ms(arrival_time_ms), 
        smoothed_delay_ms(smoothed_delay_ms),
        raw_delay_ms(raw_delay_ms) { }
    // rtp包的到达时间
    double arrival_time_ms;
    // 指数平滑后的传输延迟差
    double smoothed_delay_ms;
    // 原始的传输延迟差
    double raw_delay_ms;
    };       
};
private:
    void UpdateTrendline(double recv_delta_ms,
                    double send_delta_ms,
                    int64_t send_time_ms,
                    int64_t arrival_time_ms,
                    size_t packet_size);
    absl::optional<double> LinearFitSlope(const std::deque<PacketTiming>& packets);
    void Detect(double trend,double ts_delta,int64_t now_ms);
    void UpdateThreshold(double modified_trend,int64_t now_ms);
private:
    int64_t first_arrival_time_ms_ = -1;
    double accumulated_delay_ms_ = 0;
    double smoothed_delay_ms_ = 0;
    //表示了历史数据的权重
    double moothing_coef_;
    //trend增益的阈值
    double threshold_gain_ ;
    std::deque<PacketTiming> delay_hist_;
    double prev_trend_ = 0.0f;
    webrtc::BandwidthUsage hypothesis_ = webrtc::BandwidthUsage::kBwNormal;
    double threshold_ = 12.5;//阈值，经验值，后续会动态自适应调整
    double time_over_using_ = -1;//超过阈值的时间
    int overuse_counter_ = 0;//超过阈值的次数
    int num_of_deltas_ = 0;//对样本的个数进行计数
    int64_t last_update_ms_ = -1;//最后一次更新阈值的时间
    double k_up_ = 0.0087;//调大的系数
    double k_down_ = 0.039;//调小的系数
    
} // namespace xrtc 

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_TRENDLINE_ESTIMATOR_H_