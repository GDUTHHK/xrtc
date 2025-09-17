#include "xrtc/rtc/modules/pacing/bitrate_prober.h"
#include <rtc_base/logging.h>

namespace xrtc {
namespace {
    const webrtc::TimeDelta kProbeClusterTimeout = webrtc::TimeDelta::Seconds(5);
    const webrtc::DataSize kMinProbeSize = webrtc::DataSize::Bytes(200);
}//namespace
BitrateProber::BitrateProber() {
}

BitrateProber::BitrateProber(const BitrateProberConfig& config) : conf_(config) {
}
BitrateProber::~BitrateProber() {
}

void BitrateProber::CreateProbeCluster(webrtc::DataRate bitrate,webrtc::Timestamp now,int cluster_id) 
{
    ++total_probe_sent_count_;
    //超时则清除,清理排队超时的探测任务
    while(clusters_.empty() && now - clusters_.front().created_at > kProbeClusterTimeout) {
        clusters_.pop();
        ++total_probe_failed_sent_count_;
    }
    ProbeCluster cluster;
    cluster.pace_info.send_bitartr_bps = bitrate.bps();
    cluster.pace_info.probe_cluster_id = cluster_id;
    cluster.pace_info.probe_cluster_min_probes = conf_.min_probe_packets_sent;
    cluster.pace_info.probe_cluster_min_bytes = (bitrate*conf_.min_probe_duration).bytes();
    cluster.created_at = now;
    clusters_.push(cluster);

    RTC_LOG(LS_INFO) << "Probe Cluster(bitrate:min_bytes:min_probes): (" 
                    << cluster.pace_info.send_bitartr_bps << ":" 
                    << cluster.pace_info.probe_cluster_min_bytes << ":" 
                    << cluster.pace_info.probe_cluster_min_probes << ")" ;
    if(probing_state_ == ProbingState::kActive) {
        probing_state_ = ProbingState::kInactive;
    }

}

void BitrateProber::OnIncomingPacket(webrtc::DataSize packet_size,webrtc::Timestamp now) {
    if(probing_state_ == ProbingState::kInactive && !clusters_.empty()&&
        packet_size >std::min(RecommendedMinProbeSize(),kMinProbeSize))
    {
        probing_state_ = ProbingState::kActive;
        //表示立即启动探测
        next_probe_time_ = webrtc::Timestamp::MinusInfinity();
    }
}

webrtc::DataSize BitrateProber::RecommendedMinProbeSize() {
    if(clusters_.empty()) {
        return webrtc::DataSize::Zero();
    }
    webrtc::DataRate send_bitrate = webrtc::DataRate::BitsPerSec(clusters_.front().pace_info.send_bitartr_bps);
    return 2*send_bitrate*conf_.min_probe_delta;
}

webrtc::Timestamp BitrateProber::NextProbeTime() const {
    if(probing_state_ != ProbingState::kActive || clusters_.empty()) {
        return webrtc::Timestamp::PlusInfinity();
    }

    return next_probe_time_;
}

absl::optional<webrtc::PacedPacketInfo> BitrateProber::CurrentCluster() const {
    if(probing_state_ != ProbingState::kActive || clusters_.empty()) {
        return absl::nullopt;
    }
    webrtc::PacedPacketInfo paciing_info = clusters_.front().pace_info;
    pacing_info.probe_cluster_bytes_sent =clusters_.front().sent_bytes;
    return pacing_info;
}

void BitrateProber::ProbeSent(webrtc::Timestamp now,webrtc::DataSize data_sent) {
    if(!clusters_.empty()) {
        ProbeCluster* cluster = &clusters_.front();
        if(cluster->sent_probes == 0)
        {
            cluster->started_at = now;
        }

        cluster->sent_bytes += data_sent.bytes<int>();
        cluster->sent_probes +=1;
        next_probe_time_ = CalculateNextProbeTime(*cluster)

        if(cluster->sent_bytes >= cluster->pace_info.probe_cluster_min_bytes &&
            cluster->sent_probes >= cluster->pace_info.probe_cluster_min_probes)
        {
            clusters_.pop();
        }

        if(clusters_.empty())
        {
            probing_state_ = ProbingState::kSuspended;
        }
    }
}

webrtc::Timestamp BitrateProber::CalculateNextProbeTime(const ProbeCluster& cluster) {

    webrtc::DataRate sent_bytes = webrtc::DataRate::Bytes(cluster.sent_bytes);//发送字节数
    webrtc::DataRate send_bitrate = webrtc::DataRate::BitsPerSec(cluster.pace_info.send_bitartr_bps);//发送码率
    webrtc::TimeDelta delta = sent_bytes / send_bitrate;

    return cluster.started_at + delta;
}

} // namespace xrtc


