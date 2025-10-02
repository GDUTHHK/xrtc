#ifndef XRTCSDK_XRTC_RTC_MODULES_PACING_PACING_CONTROLLER_H
#define XRTCSDK_XRTC_RTC_MODULES_PACING_PACING_CONTROLLER_H

#include <system_wrappers/include/clock.h>
#include <api/units/data_rate.h>

#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet_to_send.h"
#include "xrtc/rtc/modules/pacing/round_robin_packet_queue.h"
#include "xrtc/rtc/modules/pacing/interval_budget.h"
#include "xrtc/rtc/modules/pacing/bitrate_prober.h"

namespace xrtc {

//真正实现漏桶数据包平滑算法
class PacingController {
public:
    class PacketSender {
    public:
        virtual ~PacketSender() = default;
        virtual void SendPacket(std::unique_ptr<RtpPacketToSend> packet,const webrtc::PacedPacketInfo& pacing_info) = 0;
        virtual std::vector<std::unique_ptr<RtpPacketToSend>> GeneratePadding(
            webrtc::DataSize packet_size) = 0;
    };

    PacingController(webrtc::Clock* clock,
        PacketSender* packet_sender);
    ~PacingController();

    void EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet);
    void ProcessPackets();
    webrtc::Timestamp NextSendTime();
    void SetPacingBitrate(webrtc::DataRate bitrate);
    void SetQueueTimeLimit(webrtc::TimeDelta limit) {
        queue_time_limit_ = limit;
    }
    void CreateProbeCluster(webrtc::DataRate bitrate,int cluster_id);
private:
    void EnqueuePacketInternal(int priority,
        std::unique_ptr<RtpPacketToSend> packet);
    webrtc::TimeDelta UpdateTimeAndGetElapsed(webrtc::Timestamp now);
    void UpdateBudgetWithElapsedTime(webrtc::TimeDelta elapsed_time);
    void UpdateBudgetWithSendData(webrtc::DataSize size);
    std::unique_ptr<RtpPacketToSend> GetPendingPacket(const webrtc::PacedPacketInfo& pacing_info);
    void OnPacketSent(webrtc::DataSize packet_size, webrtc::Timestamp send_time);
    webrtc::DataSize PaddingToAdd(webrtc::DataSize recommended_probe_size,webrtc::DataSize data_sent);

private:
    webrtc::Clock* clock_;
    PacketSender* packet_sender_;//数据包发送
    uint64_t packet_counter_ = 0;
    webrtc::Timestamp last_process_time_;
    RoundRobinPacketQueue packet_queue_;//流队列，存储优先级包
    webrtc::TimeDelta min_packet_limit_;
    IntervalBudget media_budget_;//间隔预算
    webrtc::DataRate pacing_bitrate_;//目标码率，带宽估计会动态设置
    bool drain_large_queue_ = true;//数据比较大的时候是否启用排空的功能，控制在一定的延时发送
    webrtc::TimeDelta queue_time_limit_;  // 期望的最大延迟时间
    BitrateProber prober_;//比特探测
    bool probe_sent_failed_ = false;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_PACING_PACING_CONTROLLER_H