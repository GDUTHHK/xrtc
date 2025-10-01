#include "xrtc/rtc/modules/congestion_controller/google_gcc/send_side_bandwidth_estimator.h"

namespace xrtc {
namespace {
    const int kLimitNumPackets = 20;
    const webrtc::TimeDelta kStartPhase = webrtc::TimeDelta::Seconds(2);//2秒
    const webrtc::DataRate kDefaultMaxBitrate = webrtc::DataRate::KilobitsPerSec(1000000);//1000Mbps
    const webrtc::DataRate kDefaultMinBitrate = webrtc::DataRate::KilobitsPerSec(5);//5kbps
    const webrtc::TimeDelta kBweIncreaseInterval = webrtc::TimeDelta::Seconds(1);//1秒
    const webrtc::TimeDelta kBweDecreaseInterval = webrtc::TimeDelta::Millis(300);//300ms
    const webrtc::TimeDelta kMaxRtcpFeedbackInterval = webrtc::TimeDelta::Seconds(5);//5秒
    const float kDefaultLowLossThreshold = 0.02;//2%
    const float kDefaultHighLossThreshold = 0.1;//10%
}
SendSideBandwidthEstimator::SendSideBandwidthEstimator():
    max_bitrate_config_(kDefaultMaxBitrate),
    min_bitrate_config_(kDefaultMinBitrate),
    low_loss_threshold_(kDefaultLowLossThreshold),
    high_loss_threshold_(kDefaultHighLossThreshold)
{
}

SendSideBandwidthEstimator::~SendSideBandwidthEstimator() {
}

webrtc::DataRate SendSideBandwidthEstimator::target_bitrate() const {
    return std::max(current_target_,min_bitrate_config_);
}
void SendSideBandwidthEstimator::UpdateDelayBasedBitrate(webrtc::Timestamp at_time,webrtc::DataRate bitrate) {
    delay_based_bitrate_ = bitrate.IsZero() ? webrtc::DataRate::PlusInfinity() : bitrate;
    UpdateTargetBitrate(current_target_);
}
void SendSideBandwidthEstimator::UpdatePacketsLost(int packets_lost,int num_of_packets,webrtc::Timestamp at_time) 
{
    last_loss_feedback_ = at_time;
    if(first_report_time_.IsInfinite()) {
        first_report_time_ = at_time;
    }

    //计算丢包指数，范围在[0-255]
    if(num_of_packets > 0) {
        //之前累计期待希望收到的丢包个数 + 这次要收到包的个数
        int expected = expected_packets_since_last_loss_update_ + num_of_packets;

        //如果样本数太小，误差会比较大
        if(expected <kLimitNumPackets) {
            expected_packets_since_last_loss_update_ = expected;
            loss_packet_since_last_loss_update_ += packets_lost;
            return;
        }

        //超过丢包个数，开始计算
        //计算丢包指数
        
        //丢包个数
        int64_t lost_q8 = static_cast<int64_t>(loss_packet_since_last_loss_update_ + packets_lost) << 8;

        last_fraction_lost_ = lost_q8 / expected;
        last_fraction_lost_ = std::min<uint8_t>(last_fraction_lost_, 255);

        expected_packets_since_last_loss_update_ = 0;
        loss_packet_since_last_loss_update_ = 0;
        last_loss_report_time_ = at_time;
        UpdateEstimate(at_time);
    }
}

void SendSideBandwidthEstimator::SetBitrates(absl::optional<webrtc::DataRate> send_bitrate,
    absl::optional<webrtc::DataRate> max_bitrate,
    absl::optional<webrtc::DataRate> min_bitrate) 
{
    SetMinMaxBitrate(min_bitrate,max_bitrate);
    if(send_bitrate) {
        SetSendBitrate(*send_bitrate);
    }
}

void SendSideBandwidthEstimator::SetMinMaxBitrate(webrtc::DataRate min_bitrate,webrtc::DataRate max_bitrate) {
    min_bitrate_config_ = std::max(min_bitrate,kDefaultMinBitrate);
    if(max_bitrate > webrtc::DataRate::Zero() && max_bitrate.IsFinite()) {
        max_bitrate_config_ = sdt::max(min_bitrate_config_,max_bitrate);
    }
    else{
        max_bitrate_config_ = kDefaultMaxBitrate;
    }
}

void SendSideBandwidthEstimator::SetSendBitrate(webrtc::DataRate start_bitrate) 
{
    delay_based_bitrate_ = webrtc::DataRate::PlusInfinity();//限制基于延迟的带宽码率估计值对起码码率的更新，一开始不参与计算
    UpdateTargetBitrate(start_bitrate);
    min_bitrate_history_.clear();
}
void SendSideBandwidthEstimator::UpdateRtt(webrtc::TimeDelta rtt) {
    if(rtt > webrtc::TimeDelta::Zero()) {
        last_rtt_ = rtt;
    }
}
void SendSideBandwidthEstimator::UpdateEstimate(webrtc::Timestamp at_time) 
{
    //启动前2秒，可能还没收到任何关于丢包的报告信息，此时我们信任基于延迟估计的码率或者REMB
    if(last_fraction_lost_ == 0 && IsInStartPhase(at_time)) {
        webrtc::DataRate new_bitrate = current_target_;
        if(delay_based_bitrate_.IsFinite()) {
            new_bitrate = std::max(new_bitrate, delay_based_bitrate_);
        }

        if(new_bitrate != current_target_) {
            min_bitrate_history_.clear();
            min_bitrate_history_.push_back(std::make_pair(at_time,new_bitrate));
            UpdateTargetBitrate(new_bitrate);
            return ;
        }
    }
    UpdateMinHistory(at_time);

    //根据丢包率来调整当前码率
    //确保包在有效的时间范围内
    if(at_time - last_loss_report_time_ <= 1.2 * kMaxRtcpFeedbackInterval) {
        float loss = last_fraction_lost_ / 256.0f;
        //如果丢包率小于低丢包率阈值，则需要增加码率
        //增加的是最近1秒钟内最小的码率的1.08倍，保守的增加
        if(loss < low_loss_threshold_) {//小于2%
            webrtc::DataRate new_bitrate = webrtc::DataRate::BitsPerSec(min_bitrate_history_.back().second.bps() * 1.08 +0.5);
            //为了防止增加过慢，额外增加1000bps
            new_bitrate += webrtc::DataRate::BitsPerSec(1000);
            UpdateTargetBitrate(new_bitrate);
            return ;
        }
        //如果丢包率大于高丢包率阈值，则需要降低码率
        else if(loss <= high_loss_threshold_ ) { //[2%,10%]
            //保持当前码率不变
        }
        else{//大于10%
            //需要控制一下频率,避免降低码率的频率过高，需要控制时间周期
            if((at_time - time_last_decrease_ )>= (kBweIncreaseInterval + last_rtt_)) {
                time_last_decrease_ = at_time;
                //码率降低的数值为当前码率 * 丢包率 * 0.5
                webrtc::DataRate new_bitrate = webrtc::DataRate::BitsPerSec(current_target_.bps() *static_cast<double>(512 - last_fraction_lost_) / 512.0f);
                UpdateTargetBitrate(new_bitrate);
                return ;
            }
        }
        UpdateTargetBitrate(current_target_);
    }
}

//更新目标码率
void SendSideBandwidthEstimator::UpdateTargetBitrate(webrtc::DataRate new_bitrate) {
    //限制码率上限
    new_bitrate = std::min(new_bitrate,GetUpperLimit());
    //限制码率下限
    new_bitrate = std::max(new_bitrate,min_bitrate_config_);
    current_target_ = new_bitrate;
}

bool SendSideBandwidthEstimator::IsInStartPhase(webrtc::Timestamp at_time) {
    if(first_report_time_.IsInfinite() || (at_time - first_report_time_)< kStartPhase) {
        return true;
    }
    return false;
}

webrtc::DataRate SendSideBandwidthEstimator::GetUpperLimit() const {
    webrtc::DataRate upper_limit = delay_based_bitrate_;

    return std::min(upper_limit,max_bitrate_config_);
}

//确保队列里面存放的数据为1秒内比当前码率小的码率
void SendSideBandwidthEstimator::UpdateMinHistory(webrtc::Timestamp at_time) 
{
    //超过1秒的数据，需要清楚
    while(!min_bitrate_history_.empty() && (at_time - min_bitrate_history_.front().first) > kBweIncreaseInterval) {
        min_bitrate_history_.pop_front();
    }

    //清除比当前码率大的数据
    while(!min_bitrate_history_.empty() && min_bitrate_history_.back().second >= current_target_) {
        min_bitrate_history_.pop_back();
    }
    min_bitrate_history_.push_back(std::make_pair(at_time,current_target_));
}

} // namespace xrtc

