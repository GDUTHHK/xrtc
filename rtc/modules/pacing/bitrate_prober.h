#ifndef XRTCSDK_XRTC_RTC_MODULES_PACING_BITRATE_PROBER_H_
#define XRTCSDK_XRTC_RTC_MODULES_PACING_BITRATE_PROBER_H_

#include <queue>
#include <api/units/data_rate.h>
#include <api/units/timestamp.h>
#include <api/transport/network_types.h>

namespace xrtc {

struct BitrateProberConfig {
        int min_probe_packets_sent = 5;//最小探测次数
        webrtc::TimeDelta min_probe_duration = webrtc::TimeDelta::Millis(15);//最小探测持续时间
        webrtc::TimeDelta min_probe_delta = webrtc::TimeDelta::Millis(1);
};

class BitrateProber {
public:
    BitrateProber();
    BitrateProber(const BitrateProberConfig& config);
    ~BitrateProber();

    void CreateProbeCluster(webrtc::DataRate bitrate,webrtc::Timestamp now,int cluster_id);
    void OnIncomingPacket(webrtc::DataSize packet_size);
    webrtc::DataSize RecommendedMinProbeSize();
    webrtc::Timestamp NextProbeTime() const;//下一次探测时间
    bool IsProbing() const{
        return probing_state_ == ProbingState::kActive;
    }
    absl::optional<webrtc::PacedPacketInfo> CurrentCluster() const;
    void ProbeSent(webrtc::Timestamp now,webrtc::DataSize data_sent);
private:
enum class ProbingState {
    kDisabled, // 此状态下，不会触发探测
    kInactive, // 准备触发探测的状态
    kActive, // 正在探测的状态
    kSuspended, // 探测被挂起，需要被再次启动
};

struct ProbeCluster {
    webrtc::PacedPacketInfo pace_info;
    int sent_probes = 0;
    int sent_bytes = 0;
    webrtc::Timestamp created_at = webrtc::Timestamp::MinusInfinity();
    webrtc::Timestamp started_at = webrtc::Timestamp::MinusInfinity();//启动时间
    int retries = 0;//重试次数
};

    webrtc::Timestamp CalculateNextProbeTime(const ProbeCluster& cluster);
    
private:
    BitrateProberConfig conf_;
    ProbingState probing_state_ = ProbingState::kInactive;
    std::queue<ProbeCluster> clusters_;//探测任务队列
    int total_probe_sent_count_ = 0;//探测任务总个数
    int total_probe_failed_sent_count_ = 0;//发送任务失败的总个数
    webrtc::Timestamp next_probe_time_ = webrtc::Timestamp::PlusInfinity();
};
} // namespace xrtc


#endif // XRTCSDK_XRTC_RTC_MODULES_PACING_BITRATE_PROBER_H_