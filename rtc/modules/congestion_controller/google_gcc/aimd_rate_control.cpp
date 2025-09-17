#include "xrtc/rtc/modules/congestion_controller/google_gcc/aimd_rate_control.h"

namespace xrtc {
namespace {
constexpr webrtc::TimeDelta kDefaultRtt = webrtc::TimeDelta::Millis(200);
constexpr double kDefaultBackoffFactor = 1.085;
}//namespace
AimdRateControl::AimdRateControl():
    min_config_bitrate_(webrtc::DataRate::KilobitsPerSec(5)),
    max_config_bitrate_(webrtc::DataRate::KilobitsPerSec(30000)),
    current_bitrate_(max_config_bitrate_),
    latest_estimated_throughput_(current_bitrate_),
    rtt_(kDefaultRtt),
    beta_(kDefaultBackoffFactor)
{
}
AimdRateControl::~AimdRateControl() {
}
void AimdRateControl::SetStartBitrate(webrtc::DataRate start_bitrate) {
    current_bitrate_ = start_bitrate;
    bitrate_is_init_ = true;
}

void AimdRateControl::SetMinBitrate(webrtc::DataRate min_bitrate) {
    min_config_bitrate_ = min_bitrate;
    current_bitrate_ = std::max(current_bitrate_,min_config_bitrate_);
}


bool AimdRateControl::ValidEstimate() const {
    return bitrate_is_init_;
}
webrtc::DataRate AimdRateControl::LatestEstimate() const {
    return current_bitrate_;
}

bool AimdRateControl::TimeToReduceFurther(webrtc::Timestamp& at_time,webrtc::DataRate estmated_throughput) const {
    //为了防止码率降低的过于频繁，需要控制码率降低的频率
    //两次码率降低的间隔，要大于一个RTT
    webrtc::TimeDelta bitrate_reduction_interval = rtt_Clamped(webrtc::TimeDelta::Millis(10),
                                                            webrtc::TimeDelta::Millis(200));
    if(at_time - time_last_bitrate_change >= rtt_) {
        return true;
    }

    //当前码率的一半必须要大于吞吐量,避免码率降得过低
    if(ValidEstimate()) {
        webrtc::DataRate threshold = LatestEstimate()*0.5;
        return estmated_throughput < threshold;
    }

    
    return false;
}

bool AimdRateControl::InitialTimeToReduceFurther(webrtc::Timestamp& at_time) const {

    return ValidEstimate() && TimeToReduceFurther(at_time,LatestEstimate()/2 - webrtc::DataRate::BitsPerSec(1));
}

void AimdRateControl::SetRtt(webrtc::TimeDelta rtt) {
    rtt_ = rtt;
}

void AimdRateControl::SetEstimate(webrtc::DataRate new_bitrate,webrtc::Timestamp at_time) {
    bitrate_is_init_ = true;
    webrtc::DataRate previous_bitrate = current_bitrate_;
    current_bitrate_ = ClampBitrate(new_bitrate);
    time_last_bitrate_change = at_time;
    if(current_bitrate_ < previous_bitrate) {
        time_last_bitrate_decrease = at_time;
    }
}

webrtc::DataRate AimdRateControl::Update(absl::optional<webrtc::DataRate> throughput_estimate,webrtc::BandwidthUsage state,webrtc::Timestamp at_time) {
    if(!bitrate_is_init_) {
        const webrtc::TimeDelta kInitTime =webrtc::TimeDelta::Seconds(5);
        if(time_first_throughput_estimate.IsInfinite()) {
            time_first_throughput_estimate = at_time;
        }
        else if(at_time - time_first_throughput_estimate > kInitTime && throughput_estimate) {
            current_bitrate_ = *throughput_estimate;
            bitrate_is_init_ = true;
        }
    }
    ChangeBitrate(throughput_estimate,state,at_time);
    return current_bitrate_;
}

webrtc::DataRate AimdRateControl::ClampBitrate(webrtc::DataRate new_bitrate) {
    new_bitrate = std::max(new_bitrate,min_config_bitrate_);
    return new_bitrate;
}

webrtc::DataRate AimdRateControl::AdditiveRateIncrease(webrtc::Timestamp at_time,webrtc::Timestamp last_time) {
    double time_delta_seconds = (at_time - last_time).seconds<double>();
    double increase_rate_bps_per_second = GetNearMaxIncreaseRateBpsPerSecond() * time_delta_seconds;
    return webrtc::DataRate::BitsPerSec(increase_rate_bps_per_second);
}

webrtc::DataRate AimdRateControl::MultiplicativeIncrease(webrtc::Timestamp at_time,webrtc::Timestamp last_time) {
    double alpha = 1.08;
    if(last_time.IsFinite()) {
        double time_since_last_update = (at_time - last_time).seconds<double>();
        alpha = std::pow(alpha,std::min(time_since_last_update,1.0));
    }
    webrtc::DataRate multiplicative_increase = std::max(current_bitrate_ * (alpha - 1),webrtc::DataRate::BitsPerSec(1000));
    return multiplicative_increase;
}

double AimdRateControl::GetNearMaxIncreaseRateBpsPerSecond() const {
    const webrtc::TimeDelta kFrameInterval = webrtc::TimeDelta::Seconds(1)/30;
    webrtc::DataSize frame_size = current_bitrate * kFrameInterval;
    const webrtc::DataSize packet_size = webrtc::DataSize::Bytes(1200);
    double packets_per_frame = std::ceil(frame_size /packet_size);
    webrtc::DataSize avg_packet_size = frame_size / packets_per_frame;

    //100ms表示网络过载时增加的延迟
    webrtc::TimeDelta response_time = rtt_ + webrtc::TimeDelta::Millis(100);
    double increase_rate_bps = (avg_packet_size/response_time).bps<double>();
    const double kMinIncreaseRateBps = 4000;
    return std::max(increase_rate_bps,kMinIncreaseRateBps);
}

void AimdRateControl::ChangeBitrate(absl::optional<webrtc::DataRate> acked_bitrate,//吞吐量,对方收到的码率
                                    webrtc::BandwidthUsage state,
                                    webrtc::Timestamp at_time) 
{
    absl::optional<webrtc::DataRate> new_bitrate;
    webrtc::DataRate estimated_throughput = acked_bitrate.value_or(latest_estimated_throughput_);
    //更新最新的吞吐量
    if(acked_bitrate) {
        latest_estimated_throughput_ = *acked_bitrate;
    }
    //当前网络状态是过载状态，即使没有初始化起始码率，仍需要降低码率
    if(!bitrate_is_init_ && state != webrtc::BandwidthUsage::kBwOverusing) {
        return ;
    }
    ChangeState(state,at_time);

    //将估计码率的上限设置为吞吐量的1.5倍加上10k，避免码率无上限的增加
    webrtc::DataRate throughput_base_limit = estimated_throughput*1.5 + webrtc::DataRate::KilobitsPerSec(10);

    switch(rate_control_state_) {
        case RateControlState::kReHold:{
            break;
        }
        case RateControlState::kReIncrease:{
            //吞吐量已经超出了估计值的上限，估计值已经不可靠了，重置
            if(estimated_throughput > link_capacity_estimator_.UpperBound()) {
                link_capacity_estimator_.Reset();
            }
            
            //如果当前的链路容量的估计值有效，表示我们的目标码率快接近最大容量了
            //此时，码率的增加应该谨慎，才去加性增加策略
            //当当前码率超过吞吐量的上限时就不用增加码率了
            //当处于ALR状态的时候发送码率很低，获得的feedback数据量比较少，估计出来的网络状态可能不是很准
            if((current_bitrate_ < throughput_base_limit) && !(in_alr_ && no_bitrate_increase_in_alr_)) {
                webrtc::DataRate increased_bitrate = webrtc::DataRate::MinusInfinity();

                if(link_capacity_estimator_.HasEstimate()) {
                    webrtc::DataRate additive_increase = AdditiveRateIncrease(at_time,time_last_bitrate_change);
                    increased_bitrate = current_bitrate_ + additive_increase;
                }else{
                    //当链路容量未知时，采用乘性增加码率的方式
                    //以快速发现容量
                    webrtc::DataRate multiplicative_increase = MultiplicativeIncrease(at_time,time_last_bitrate_change);
                    increased_bitrate = current_bitrate_ + multiplicative_increase;
                }
                new_bitrate = std::min(increased_bitrate,throughput_base_limit);
            }
            time_last_bitrate_change = at_time;
        }
            break;
        case RateControlState::kReDecrease:
        {
            webrtc::DataRate decreased_bitrate = webrtc::DataRate::PlusInfinity();
            decreased_bitrate = estimated_throughput * beta_;
            if(decreased_bitrate > current_bitrate_) {
                decreased_bitrate = linkC_capacity.Estimate() * beta_;
            }

            //避免码率下降逻辑，当前码率反而升高了
            if(decreased_bitrate < current_bitrate_) {
                new_bitrate = decreased_bitrate;
            }

            //吞吐量已经超出了估计值的下限，估计值已经不可靠了，重置
            if(estimated_throughput < link_capacity_estimator_.LowerBound()) {
                link_capacity_estimator_.Reset();
            }
            link_capacity_estimator_.OnoveruseDetected(estimated_throughput);
            bitrate_is_init_ = true;
            rate_control_state_ = RateControlState::kReHold;
            time_last_bitrate_change = at_time;
            time_last_bitrate_decrease = at_time;
        }
            break;
        default:
            break;
    }
    current_bitrate_ = ClampBitrate(new_bitrate.value_or(current_bitrate_));
}

void AimdRateControl::ChangeState(webrtc::BandwidthUsage state,webrtc::Timestamp at_time) {
    switch(state) {
        case webrtc::BandwidthUsage::kBwNormal:
            if(rate_control_state_ = RateControlState::kReHold){
                rate_control_state_ = RateControlState::kReDecrease;
            }
            break;
        case webrtc::BandwidthUsage::kBwOverusing:
           if(rate_control_state_ != RateControlState::kReDecrease){
            rate_control_state_ = RateControlState::kReDecrease;
           }
            break;
        case webrtc::BandwidthUsage::kBwUnderusing:
            rate_control_state_ = RateControlState::kReHold;
            break;
        default:
            break;
    }
}

} // namespace xrtc