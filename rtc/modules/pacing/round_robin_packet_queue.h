#ifndef XRTCSDK_XRTC_RTC_MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_
#define XRTCSDK_XRTC_RTC_MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_

#include <queue>
#include <map>
#include <unordered_map>

#include <api/units/timestamp.h>
#include <api/units/data_size.h>

#include "xrtc/rtc/modules/rtp_rtcp/rtp_packet_to_send.h"

namespace xrtc {

class RoundRobinPacketQueue {
public:
    RoundRobinPacketQueue(webrtc::Timestamp start_time);//起始时间
    ~RoundRobinPacketQueue();

    void Push(int priority,
        webrtc::Timestamp enqueue_time,
        uint64_t enqueue_order,
        std::unique_ptr<RtpPacketToSend> packet);
    std::unique_ptr<RtpPacketToSend> Pop();

    bool Empty() const;
    webrtc::DataSize Size() const { return size_; }//队列当中还存在的数据大小
    size_t SizePackets() const { return size_packets_; }
    void UpdateQueueTime(webrtc::Timestamp now);
    webrtc::TimeDelta AverageQueueTime() const;

private:
    struct QueuedPacket {
    public:
        QueuedPacket(int priority,
            webrtc::Timestamp enqueue_time,
            uint64_t enqueue_order,
            std::unique_ptr<RtpPacketToSend> packet);
        ~QueuedPacket();

        bool operator<(const QueuedPacket& other) const {
            if (priority_ != other.priority_) {
                return priority_ > other.priority_;
            }

            // 如果优先级相同，先到的包优先级高
            return enqueue_order_ > other.enqueue_order_;
        }

        int Priority() const { return priority_; }
        uint32_t Ssrc() const { return owned_packet_->ssrc(); }
        webrtc::Timestamp EnqueueTime() const { return enqueue_time_; }//入队列的时间
        RtpPacketToSend* rtp_packet() const {
            return owned_packet_;
        }

    private:
        int priority_; // RTP包的优先级
        webrtc::Timestamp enqueue_time_; // RTP包入队列时间
        uint64_t enqueue_order_; // RTP包入队列的顺序
        RtpPacketToSend* owned_packet_;
    };

    //Stream的优先级
    struct StreamPrioKey {
        StreamPrioKey(int priority, webrtc::DataSize size) :
            priority(priority), size(size) { }

        bool operator<(const StreamPrioKey& other) const {
            if (priority != other.priority) {
                return priority < other.priority;
            }

            return size < other.size;
        }

        int priority; // stream里面RTP数据包的最大优先级
        webrtc::DataSize size; // stream累计发送的字节数
    };

    struct Stream {
        Stream();
        ~Stream();

        webrtc::DataSize size;
        uint32_t ssrc;
        std::priority_queue<QueuedPacket> packet_queue;
        std::multimap<StreamPrioKey, uint32_t>::iterator priority_it;
    };

private:
    void Push(const QueuedPacket& packet);
    Stream* GetHighestPriorityStream();
    webrtc::DataSize PacketSize(const QueuedPacket& queued_packet);

private:
    size_t size_packets_ = 0;
    webrtc::DataSize max_size_;//累计发送的最大的流的字节数
    webrtc::DataSize size_ = webrtc::DataSize::Zero();//获得排队过程中所有数据包的大小
    std::unordered_map<uint32_t, Stream> streams_;
    // 按照StreamPrioKey从小到到进行排序
    std::multimap<StreamPrioKey, uint32_t> stream_priorities_;
    webrtc::Timestamp last_time_updated_;//上一次更新的时间
    webrtc::TimeDelta queue_time_sum_ = webrtc::TimeDelta::Zero();//排队总时间
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_PACING_ROUND_ROBIN_PACKET_QUEUE_H_