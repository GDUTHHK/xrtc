#include "xrtc/rtc/modules/pacing/pacing_controller.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const webrtc::TimeDelta kDefaultMinPacketLimit = webrtc::TimeDelta::Millis(5);
const webrtc::TimeDelta kMaxElapsedTime = webrtc::TimeDelta::Seconds(2);    
const webrtc::TimeDelta kMaxProcessingInterval = webrtc::TimeDelta::Millis(30);
const webrtc::TimeDelta kMaxExpectedQueueLength = webrtc::TimeDelta::Millis(2000);

// 值越小，优先级越高
const int kFirstPriority = 0;

int GetPriorityForType(RtpPacketMediaType type) {
    switch (type) {
    case RtpPacketMediaType::kAudio:
        return kFirstPriority + 1;
    case RtpPacketMediaType::kRetransmission:
        return kFirstPriority + 2;
    case RtpPacketMediaType::kVideo:
    case RtpPacketMediaType::kForwardErrorCorrection:
        return kFirstPriority + 3;
    case RtpPacketMediaType::kPadding:
        return kFirstPriority + 4;
    }
}

} // namespace

PacingController::PacingController(webrtc::Clock* clock,
    PacketSender* packet_sender) :
    clock_(clock),
    packet_sender_(packet_sender),
    last_process_time_(clock_->CurrentTime()),
    packet_queue_(last_process_time_),
    min_packet_limit_(kDefaultMinPacketLimit),
    media_budget_(0),
    pacing_bitrate_(webrtc::DataRate::Zero()),
    queue_time_limit_(kMaxExpectedQueueLength)
{
}

PacingController::~PacingController() {
}

void PacingController::EnqueuePacket(std::unique_ptr<RtpPacketToSend> packet) {
    // 1. 获得RTP packet的优先级
    int priority = GetPriorityForType(*packet->packet_type());
    // 2. 插入packet
    EnqueuePacketInternal(priority, std::move(packet));
}

void PacingController::ProcessPackets() {
    //RTC_LOG(LS_INFO) << "=========packet queue size: " << packet_queue_.SizePackets();

    webrtc::Timestamp now = clock_->CurrentTime();
    webrtc::Timestamp target_send_time = now;
    // 计算流逝的时间（当前时间距离上一次发送过去了多长时间）
    webrtc::TimeDelta elapsed_time = UpdateTimeAndGetElapsed(now);

    //大队列的情况
    if (elapsed_time > webrtc::TimeDelta::Zero()) {
        webrtc::DataRate target_rate = pacing_bitrate_;
        packet_queue_.UpdateQueueTime(now);
        // 队列当中正在排队的总字节数
        webrtc::DataSize queue_data_size = packet_queue_.Size();
        if (queue_data_size > webrtc::DataSize::Zero()) {
            //开启排空的处理
            if (drain_large_queue_) {
                // 当前队列的平均排队时间
                webrtc::TimeDelta avg_queue_time = packet_queue_.AverageQueueTime();
                webrtc::TimeDelta avg_queue_left = std::max(
                    webrtc::TimeDelta::Millis(1),
                    queue_time_limit_ - avg_queue_time);
                webrtc::DataRate min_rate_need = queue_data_size / avg_queue_left;
                if (min_rate_need > target_rate) {
                    //解决当队列中的排队延迟超过了最大延迟时，直接将码率设置为最低的码率，优先保证数据能在设置的延迟内发送出去
                    //通过调整码率设置，解决队列缓存累计的时间
                    target_rate = min_rate_need;
                    RTC_LOG(LS_INFO) << "large queue, pacing_rate: " << pacing_bitrate_.kbps()
                        << ", min_rate_need: " << min_rate_need.kbps()
                        << ", queue_data_size: " << queue_data_size.bytes()
                        << ", avg_queue_time: " << avg_queue_time.ms()
                        << ", avg_queue_left: " << avg_queue_left.ms();
                }
            }
        }

        // 更新预算
        media_budget_.set_target_bitrate_kbps(target_rate.kbps());
        UpdateBudgetWithElapsedTime(elapsed_time);
    }

    //如果需要探测，计算发送探测数据的大小
    bool is_first_packet_in_probe = false;
    webrtc::PacedPacketInfo pacing_info;
    webrtc::DataSize recommended_probe_size = webrtc::DataSize::Zero();
    bool is_probing = prober_.IsProbing();
    if(is_probing) {
        pacing_info = prober_.CurrentCluster().value_or(webrtc::PacedPacketInfo());
        if(pacing_info.probe_cluster_id !=webrtc::PacedPacketInfo::kNotAprobe) {
            is_first_packet_in_probe = pacing_info.probe_cluster_bytes_sent == 0;
            recommended_probe_size = prober_.RecommendedMinProbeSize();
        }else{
            is_probing = false;
        }
    }

    webrtc::DataSize data_sent = webrtc::DataSize::Zero();
    while (true) {
        //仅执行一次
        if(is_first_packet_in_probe) {
            //先发送一个1字节的填充包，可以获得一个可靠的估计码率的起始窗口
            auto padding = packet_sender_->GeneratePadding(webrtc::DataSize::Bytes(1));
            if(!padding.empty()) {
                EnqueuePacketInternal(0, std::move(padding[0]));
            }
            //发送完1字节的填充包后，继续发送探测包
            is_first_packet_in_probe = false;
        }

        // 从队列当中获取rtp数据包进行发送
        std::unique_ptr<RtpPacketToSend> rtp_packet = GetPendingPacket(pacing_info);

        if (!rtp_packet) {
            //判断是否需要发送填充包
            webrtc::DataSize padding_to_add = PaddingToAdd(recommended_probe_size,data_sent);
            if(padding_to_add > webrtc::DataSize::Zero()) {
                auto padding_packets = packet_sender_->GeneratePadding(padding_to_add);
                if(!padding_packets.empty()) {
                    EnqueuePacketInternal(0, std::move(padding[0]));
                }

                for(auto& padding : padding_packets) {
                    EnqueuePacket(std::move(padding));
                }

                continue;
            }
            // 队列为空或者预算耗尽了，停止发送循环
            break;
        }

        webrtc::DataSize packet_size = webrtc::DataSize::Bytes(
            rtp_packet->payload_size() + rtp_packet->padding_size());

        // 发送rtp_packet到网络
        packet_sender_->SendPacket(std::move(rtp_packet),pacing_info);

        data_sent += packet_size;

        // 更新预算
        OnPacketSent(packet_size, target_send_time);

        if(is_probing && data_sent >= recommended_probe_size) {
            break;
        }
    }

    if(is_probing) {
        //更新探测的相关的状态信息
        probe_sent_failed_ = (data_sent == webrtc::DataSize::Zero());
        if(!probe_sent_failed_) {
            prober_.ProbeSent(clock_->CurrentTime(),data_sent);
        }
    }
}

webrtc::Timestamp PacingController::NextSendTime() {
    //优先执行探测任务
    if(prober_.IsProbing()) {
        webrtc::Timestamp probing_time = prober_.NextProbeTime();
        //上一轮没有失败
        if(probing_time != webrtc::Timestamp::PlusInfinity() && !probe_sent_failed_) {
            return probing_time;
        }
    }
    return last_process_time_ + min_packet_limit_;
}

void PacingController::SetPacingBitrate(webrtc::DataRate bitrate) {
    pacing_bitrate_ = bitrate;
    RTC_LOG(LS_INFO) << "pacing bitrate update, pacing_bitrate_kbps: "
        << pacing_bitrate_.kbps();
}

void PacingController::CreateProbeCluster(webrtc::DataRate bitrate,int cluster_id) {
    prober_.CreateProbeCluster(bitrate, clock_->CurrentTime(), cluster_id);
}

void PacingController::EnqueuePacketInternal(int priority, 
    std::unique_ptr<RtpPacketToSend> packet) 
{
    webrtc::Timestamp now = clock_->CurrentTime();
    packet_queue_.Push(priority, now, packet_counter_++, std::move(packet));
}

webrtc::TimeDelta PacingController::UpdateTimeAndGetElapsed(webrtc::Timestamp now) {
    if (now < last_process_time_) {
        return webrtc::TimeDelta::Zero();
    }

    webrtc::TimeDelta elapsed_time = now - last_process_time_;
    last_process_time_ = now;
    if (elapsed_time > kMaxElapsedTime) {
        elapsed_time = kMaxElapsedTime;
        RTC_LOG(LS_WARNING) << "elapsed time " << elapsed_time.ms()
            << " is longer than expected, limitting to "
            << kMaxElapsedTime.ms();
    }
    return elapsed_time;
}

void PacingController::UpdateBudgetWithElapsedTime(
    webrtc::TimeDelta elapsed_time) 
{
    webrtc::TimeDelta delta = std::min(elapsed_time, kMaxProcessingInterval);
    media_budget_.IncreaseBudget(delta.ms());
}

void PacingController::UpdateBudgetWithSendData(webrtc::DataSize size) {
    media_budget_.UseBudget(size.bytes());
}

std::unique_ptr<RtpPacketToSend> PacingController::GetPendingPacket(const webrtc::PacedPacketInfo& pacing_info) {
    // 如果队列已经为空
    if (packet_queue_.Empty()) {
        return nullptr;
    }

    bool is_probing = pacing_info.probe_cluster_id != webrtc::PacedPacketInfo::kNotAprobe;
    //如果当前处于探测状态，此时需要发送探测需要的数据大小不进行预算的判断
    if(!is_probing){
        // 如果本轮预算已经耗尽
        if (media_budget_.bytes_remaining() <= 0) {
            return nullptr;
        }
    }

    return packet_queue_.Pop();
}

void PacingController::OnPacketSent(webrtc::DataSize packet_size, 
    webrtc::Timestamp send_time) 
{
    //消耗预算
    UpdateBudgetWithSendData(packet_size);
    last_process_time_ = send_time;
}

webrtc::DataSize PacingController::PaddingToAdd(webrtc::DataSize recommended_probe_size,webrtc::DataSize data_sent) {
    if(!packet_queue_.Empty()) {
        return webrtc::DataSize::Zero();
    }
   
    //当没有发送任何正常数据包之前，不能发送填充包
    if(packet_counter_ == 0) {
        return webrtc::DataSize::Zero();
    }

    //需要发送探测包
    if(!recommended_probe_size.IsZero()) {
        //如果期望发送的探测包大小大于已经发送的数据大小，则需要发送填充包
        if(recommended_probe_size > data_sent) {
            return recommended_probe_size - data_sent;
        }
        return webrtc::DataSize::Zero();
    }

    return webrtc::DataSize::Zero();

}

} // namespace xrtc