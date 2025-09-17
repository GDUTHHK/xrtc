#include "xrtc/rtc/modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include<rtc_base/logging.h>

namespace xrtc {

//最多存储60秒的包
constexpr webrtc::TimeDelta kSendTimeHistoryWindow =webrtc::TimeDelta::Seconds(60);
TransportFeedbackAdapter::TransportFeedbackAdapter() {
}
TransportFeedbackAdapter::~TransportFeedbackAdapter() {
}

absl::optional<webrtc::SentPacket> TransportFeedbackAdapter::ProcessSentPacket(const webrtc::SentPacket& sent_packet) {
    auto send_time = webrtc::Timestamp::Millis(sent_packet.send_time_ms);
    int64_t unwrapped_sequence_number = seq_num_unwrapper_.Unwrap(sent_packet.packet_id);

    auto it = history_.find(unwrapped_sequence_number);
    if(it != history_.end()) {// 找到了发送记录
        //如果是重传包则时间会被更新则不是负无穷，是一个有限值
        bool packet_retransmit = it->second.sent.send_time.IsFinite();//是否是重传的包
        //更新包的发送时间
        it->second.sent.send_time = send_time;
        last_send_time_ = std::max(last_send_time_, send_time);

        //如果不是重传包
        if(!packet_retransmit) {
            return it->second.sent;
        }
    }
    return absl::nullopt;
}

void TransportFeedbackAdapter::AddPacket(webrtc::Timestamp creation_time,size_t overhead_bytes,const RtpPacketSendInfo& send_info) {
    PacketFeedback packet;
    packet.creation_time = creation_time;
    //可以将会反转的序列变成一直递增的序列，这样这个序列值不会出现循环反转的情况
    packet.sent.sequence_number = seq_num_unwrapper_.Unwrap(send_info.transport_sequence_number);
    packet.sent.size = webrtc::DataSize::Bytes(send_info.length + overhead_bytes);
    packet.sent.audio =(send_info.packet_type == RtpPacketMediaType::kAudio);
    packet.sent.pacing_info = send_info.pacing_info;

    //我们需要清理窗口时间以外的老的数据包，防止history一直增加
    while(!history_.empty() && 
        packet.creation_time - history_.begin()->second.creation_time > kSendTimeHistoryWindow) {
        history_.erase(history_.begin());
    }
    history_.insert(std::make_pair(packet.sent.sequence_number, packet));
}

//将原始的TransportpacketsFeedback转换成拥塞控制内部需要的一个结构
absl::webrtc::optional<webrtc::TransportpacketsFeedback> TransportFeedbackAdapter::ProcessTransportFeedback(
    const webrtc::TransportpacketsFeedback& feedback,
    webrtc::Timestamp feedback_time) {
        if(feedback.GetPacketStatusCount() == 0) {
            RTC_LOG(LS_INFO) << "TransportFeedbackAdapter::ProcessTransportFeedback: feedback is empty";
            return absl::nullopt;
        }

        webrtc::TransportPacketsFeedback msg;
        msg.feedback_time = feedback_time;
        msg.packet_feedbacks = ProcessTransportFeedbackInner(feedback,feedback_time);

        if(msg.packet_feedbacks.empty()) {
            return absl::nullopt;
        }
        auto it = history_.find(last_ack_seq_num_);
        if(it != history_.end()) {
            msg.first_unacked_send_time = it->second.sent.send_time;
        }
        return msg;
}

//将原始的RTP的包转换成拥塞控制内部需要的一个结构
std::vector<webrtc::PacketResult> TransportFeedbackAdapter::ProcessTransportFeedbackInner(
    const webrtc::TransportpacketsFeedback& feedback,
    webrtc::Timestamp feedback_time) {
        if(last_timestamp_.isInfinite()){//第一次收到feedbac
            current_offset_ = feedback_time;//current_offset_为基准时间
        }else{
            //计算两次feedback之间的差值
            webrtc::TimeDelta delta = feedback.GetBaseDelta(last_timestamp_.us()).RoundDownTo(webrtc::TimeDelta::Millis(1));
            if(delta <webrtc::TimeDelta::Zero()-current_offset_) {
                RTC_LOG(LS_WARNING) << "TransportFeedbackAdapter::ProcessTransportFeedbackInner: current_offset_ is less than zero";
                current_offset_ = feedback_time;
            }else{
                current_offset_ += delta;
            }
        }
        last_timestamp_ = feedback.GetBaseTime();
        std::vector<webrtc::PacketResult> packet_results_vector;
        packet_results_vector.reserve(feedback.GetPacketStatusCount());

        size_t failed_lookups = 0;//查找失败的数量
        webrtc::TimeDelta packet_offset = webrtc::TimeDelta::Zero();
        for(const auto& packet : feedback.AllPackets()) {
            int64_t seq_num =seq_num_unwrapper_.Unwrap(packet.sequence_number());
            //因为可能会发生乱序，所以需要判断seq_num是否大于last_ack_seq_num_，这样才保证存储的是最新的
            if(seq_num > last_ack_seq_num_) {
                last_ack_seq_num_ = seq_num;
            }
            //查找seq_num是否在发送历史记录中存在
            auto it = history_.find(seq_num);
            if( it ==history_.end()) {
                ++failed_lookups;
                continue;
            }
            //包还没有发送就已经收到了feedback的信息(一般是不存在这个情况)
            if(it->second.sent.send_time.IsFinite()) {
                RTC_LOG(LS_WARNING) << "TransportFeedbackAdapter::ProcessTransportFeedbackInner: packet has already been sent";
                continue;
            }

            PacketFeedback packet_feedback = it->second;


            //计算每个RTP包的到达时间，转换成发送端的时间
            if(packet.received()) {
                packet_offset += packet.delta();
                packet_feedback.receive_time = current_offset_ + packet_offset.RoundDownTo(webrtc::TimeDelta::Millis(1));
                //一旦收到了RTP的feedback，就将历史记录删除
                history_.erase(it);
            }
            webrtc::PacketResult result;
            result.sent_packet = packet_feedback.sent;
            result.receive_time = packet_feedback.receive_time;
            packet_results_vector.push_back(result);
        }
        return packet_results_vector;
}



} // namespace xrtc