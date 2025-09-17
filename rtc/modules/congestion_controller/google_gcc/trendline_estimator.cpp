#include "xrtc/rtc/modules/congestion_controller/google_gcc/trendline_estimator.h"
#include <rtc_base/numerics/safe_minmax.h>

namespace xrtc {
namespace {
    const double kDefaultTrendlineSmoothingCoef = 0.9;
    const size_t kDefaultTrendlineWindowSize = 20;
    const double kDefaultTrendlineThresholdGain = 4.0;//增益的阈值
    //随着样本数的增加，增益值逐渐增大，所以需要一个上限
    const int kMinNumDeltas = 60;//60个样本
    const double kOverUsingTimeThreshould = 10;//超过阈值的时间,时间为10ms
    const double kMaxAdaptOffset = 15.0;//最大自适应偏移
}
TrendlineEstimator::TrendlineEstimator(): 
    threshold_gain_(kDefaultTrendlineThresholdGain),
    moothing_coef_(kDefaultTrendlineSmoothingCoef) {
}
TrendlineEstimator::~TrendlineEstimator() {
}
void TrendlineEstimator::Update(webrtc::TimeDelta recv_time_delta,webrtc::TimeDelta send_time_delta,webrtc::TimeDelta arrival_time,
                    size_t packet_size,bool calculated_delta) 
{
    if(calculated_delta) {
        UpdateTrendline(recv_time_delta.ms<double>(),
                        send_time_delta.ms<double>(),
                        send_time.ms(),
                        arrival_time.ms(),
                        packet_size);
    }
}

webrtc::BandwidthUsage TrendlineEstimator::State() const {
    return hypothesis_;
}
void TrendlineEstimator::UpdateTrendline(double recv_delta_ms,
                    double send_delta_ms,
                    int64_t send_time_ms,
                    int64_t arrival_time_ms,
                    size_t packet_size) 
{
    //没有更新过。表示是第一个数据包
    if(first_arrival_time_ms_ == -1) {
        first_arrival_time_ms_ = arrival_time_ms;
    }
    //统计样本的个数
    ++num_of_deltas_;

    //计算传输的延迟差
    double delay_ms = recv_delta_ms - send_delta_ms;
    accumulated_delay_ms_ += delay_ms;
    //计算指数平滑后的延迟差
    smoothed_delay_ms_ = moothing_coef_ * smoothed_delay_ms_ + //历史数据占比
                        (1 - moothing_coef_) * accumulated_delay_ms_;//当前数据占比
    //将样本数据添加到队列
    delay_hist_.emplace_back(
                            static<double>(arrival_time_ms - first_arrival_time_ms_),
                            smoothed_delay_ms_,accumulated_delay_ms_);
    if(delay_hist_.size() > kDefaultTrendlineWindowSize) {
        delay_hist_.pop_front();
    }
    //当样本数据满足要求，计算trend值
    double trend = prev_trend_;
    if(delay_hist_.size() == kDefaultTrendlineWindowSize) {
        trend = LinearFitSlope(delay_hist_).value_or(trend);//如果有则用计算值，没有则用历史值
    }

    //根据trend值进行过载检测
    Detect(trend,send_time_ms,arrival_time_ms);

}

//线性回归最小二乘法
absl::optional<double> TrendlineEstimator::LinearFitSlope(const std::deque<PacketTiming>& packets) {
    double sum_x = 0.0f;
    double sum_y = 0.0f;
    for(const auto& packet : packets) {
        sum_x += packet.arrival_time_ms;
        sum_y += packet.smoothed_delay_ms;
    }
    //计算x，y的平均值
    double avg_x = sum_x / packets.size();
    double avg_y = sum_y / packets.size();
    //分别计算分子和分母
    double num = 0.0f;
    double den = 0.0f;
    for(const auto& packet : packets) {
        double x = packet.arrival_time_ms;
        double y = packet.smoothed_delay_ms;
        num += (x - avg_x) * (y - avg_y);
        den += (x - avg_x) * (x - avg_x);
    }
    //如果分母为0，则返回空
    if(den == 0.0f) {
        return absl::nullopt;
    }
    //如果不为0，返回斜率
    return num / den;
}

void TrendlineEstimator::Detect(double trend,double ts_delta,int64_t now_ms) {
    //样本个数小于2，无法进行检测
    if(num_of_deltas_ < 2) {
        hypothesis_ = webrtc::BandwidthUsage::kBwNormal;
        return;
    }
    //1.对原始的trend值进行增益处理，增加区分度。原始的trend值太小了，无法区分
    double modiied_trend = std::min(num_of_deltas_,kMinNumDeltas) * trend * threshold_gain_;
    //2.进行过载检测
    if(modiied_trend > threshold_) {//有可能过载了
        if(time_over_using_ == -1) {//第一次超过阈值
            time_over_using_ = ts_delta/2;
        }else{
            time_over_using_ += ts_delta;
        }
        ++overuse_counter_;

        if(time_over_using_ >kOverUsingTimeThreshould && overuse_counter_ > 1) {
            if(trend > prev_trend_) {
                //判定过载
                time_over_using_ = 0;
                overuse_counter_ = 0;
                hypothesis_ = webrtc::BandwidthUsage::kBwOverusing;
            }
        } 
    }else if(modiied_trend < -threshold_) {//负载过低的情况
        //判定负载过低
        time_over_using_ = -1;
        overuse_counter_ = 0;
        hypothesis_ = webrtc::BandwidthUsage::kBwUnderusing;
    }else{
        //判定正常网路状态
        time_over_using_ = -1;
        overuse_counter_ = 0;
        hypothesis_ = webrtc::BandwidthUsage::kBwNormal;
    }

    prev_trend_ = trend;

    
    //阈值的动态自适应调整
    UpdateThreshold(modified_trend,now_ms);
}

void TrendlineEstimator::UpdateThreshold(double modified_trend,int64_t now_ms) {
    if(last_update_ms_ == -1) {
        last_update_ms_ = now_ms;
    }

    //如果modified_trend异常大的时候，忽略本次更新
    if(modified_trend > threshold_ + kMaxAdaptOffset) {
        last_update_ms_ = now_ms;
        return;
    }

    //webrtc的经验阈值
    //调整阈值,当阈值降低调小的时候，使用的系数0.039
    //当阈值调大的时候，使用的系数是0.0087
    double k =fabs(modified_trend)< threshold_ ? k_down_ : k_up_;
    const int64_t kMaxTimeDelta = 100;//最小时间差100ms
    int64_t time_delta_ms = std::min(now_ms - last_update_ms_,kMaxTimeDelta);//当前跟上一次调整的时间,避免time_delta_ms过大
    threshold_  += k*(fabs(modified_trend) - threshold_)*time_delta_ms;
    threshold_ = SafeClamp(threshold_,6.0f,600.0f);//阈值的范围（6-600）经验值
    last_update_ms_ = now_ms;
}

} // namespace xrtc 