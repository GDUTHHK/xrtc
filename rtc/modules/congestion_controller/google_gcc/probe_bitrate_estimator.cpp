#include "xrtc/rtc/modules/congestion_controller/google_gcc/probe_bitrate_estimator.h"

#include <rtc_base/logging.h>

namespace xrtc {
namespace {
    const double kMinReceivedProbesRatio = 0.8f;
    const double kMinReceivedBytesRatio = 0.8f;
    const webrtc::TimeDelta kMaxProbeInterval = webrtc::TimeDelta::Seconds(1);
    const double kValidRatio = 2.0f;
    const double kMinRatioForUnsaturatedLink = 0.9f;
    const double kTargetUtilizationFraction = 0.95f;
    const webrtc::TimeDelta kClusterHistory = webrtc::TimeDelta::Seconds(1);
}
ProbeBitrateEstimator::ProbeBitrateEstimator() {
}

ProbeBitrateEstimator::~ProbeBitrateEstimator() {
}


absl::optional<webrtc::DataRate> ProbeBitrateEstimator::HandleProbeAndEstimateBitrate(const webrtc::PacketResult& feedback_packet) 
{
    int cluster_id = feedback_packet.sent_packet.pacing_info.probe_cluster_id;

    EraseOldCluster(feedback_packet.receive_time);
    AggregatedCluster* cluster =&clusters_[cluster_id];
    if(feedback_packet.sent_packet.send_time< cluster->first_send)
    {
        cluster->first_send = feedback_packet.sent_packet.send_time;
    }
    if(feedback_packet.sent_packet.send_time > cluster->last_send)
    {
        cluster->last_send = feedback_packet.sent_packet.send_time;
        cluster->size_last_send = feedback_packet.sent_packet.size;
    }

    if(feedback_packet.receive_time< cluster->first_receive)
    {
        cluster->first_receive = feedback_packet.receive_time;
        cluster->size_first_receive = feedback_packet.sent_packet.size;
    }

    if(feedback_packet.receive_time > cluster->last_receive)
    {
        cluster->last_receive = feedback_packet.receive_time;
    }

    cluster->num_probes++;
    cluster->size_total += feedback_packet.sent_packet.size;

    int min_probes = feedback_packet.sent_packet.pacing_info.probe_cluster_min_probes * kMinReceivedProbesRatio;
    webrtc::DataSize min_bytes = webrtc::DataSize::Bytes(feedback_packet.sent_packet.pacing_info.probe_cluster_min_bytes * 
                                                            kMinReceivedBytesRatio);

    if(cluster->num_probes < min_probes || cluster->size_total < min_bytes)
    {
        return absl::nullopt;
    }

    //计算发送间隔
    webrtc::TimeDelta send_interval = cluster->last_send - cluster->first_send;
    //接收间隔
    webrtc::TimeDelta receive_interval = cluster->last_receive - cluster->first_receive;

    //如果发送间隔或接收间隔为0，或者大于最大探测间隔，则返回nullopt.不成功的探测
    if(send_interval <= webrtc::TimeDelta::Zero() || send_interval > kMaxProbeInterval ||
        receive_interval <= webrtc::TimeDelta::Zero() || receive_interval > kMaxProbeInterval)
    {
        RTC_LOG(LS_INFO) << "Probing unsuccessful ,invalid send/receive interval  "
                        << ",cluster_id: " << cluster_id
                        << ",send_interval: " << webrtc::ToString(send_interval)
                        << ",receive_interval: " << webrtc::ToString(receive_interval);
        return absl::nullopt;
    }

    //计算该时间段累计发送的数据量和确认收到的数据量
    webrtc::DataSize total_send = cluster->size_total - cluster->size_last_send;
    webrtc::DataSize total_receive = cluster->size_total - cluster->size_first_receive;

    webrtc::DataRate send_rate = total_send / send_interval;
    webrtc::DataRate receive_rate = total_receive / receive_interval;

    double ratio = receive_rate / send_rate;
    if(ratio > kValidRatio){
        RTC_LOG(LS_INFO) << "Probing unsuccessful , receive/send ratio is too large  "
        << ",cluster_id: " << cluster_id
        <<",ratio: " << ratio
        <<", send_size(bytes): " << total_send.bytes()
        << ",send_interval: " << webrtc::ToString(send_interval)
        <<", send_rate(kbps): " << send_rate.kbps()
        <<", receive_size(bytes): " << total_receive.bytes()
        << ",receive_interval: " << webrtc::ToString(receive_interval)
        <<", receive_rate(kbps): " << receive_rate.kbps();
        return absl::nullopt;
    }

    /*
    如果我们接收的码率明显低于发送码率（kMinRatioForUnsaturatedLink = 0.9），这表
    明我们已经找到了链路的真实容量。在这种情况下，将目标码率设置的稍低
    （kTargetUtilizationFraction = 0.95），以避免立即出现网络过载。
    */
    webrtc::DataRate res = std::min(send_rate,receive_rate);
    if(receive_rate < send_rate * kMinRatioForUnsaturatedLink){
        //说明链路已经饱和，此时我们应当将探测到的码率，降低一定的倍数
        //以防立即出现网络过载
        res = res * kTargetUtilizationFraction;
    }
    estimate_data_rate_ = res;
    return estimate_data_rate_;
}

void ProbeBitrateEstimator::EraseOldCluster(webrtc::Timestamp timestamp) 
{
    for(auto it = clusters_.begin(); it != clusters_.end();) {
        if(it->second.last_receive + kClusterHistory < timestamp) {
            it = clusters_.erase(it);
        }
        else {
            ++it;
        }
    }
}

absl::optional<webrtc::DataRate> ProbeBitrateEstimator::FetchAndRestLastEstimatedBitrate() 
{
    absl::optional<webrtc::DataRate> rate = estimate_data_rate_;
    estimate_data_rate_.reset();
    return rate;
}

}
