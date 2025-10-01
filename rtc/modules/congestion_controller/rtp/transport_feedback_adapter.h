#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_
#include <map>
#include <api/units/time_delta.h>
#include <api/transport/network_types.h>
#include <rtc_base/network/sent_packet.h>
#include <modules/include/module_common_types_public.h>
#include "xrtc/rtc/modules/rtp_rtcp/rtp_rtcp_defines.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtcp_packet/transport_feedback.h"
namespace xrtc {
struct PacketFeedback {
    PacketFeedback() = default;
    
    webrtc::Timestamp creation_time = webrtc::Timestamp::MinusInfinity();//创建时间
    webrtc::SentPacket sent;//保存包的一些基本信息
    webrtc::Timestamp receive_time = webrtc::Timestamp::PlusInfinity();//包到达时间，已经转换为发送时间。但是间隔还是一样的
};

//用于记录发送和接收RTP的间隔、是否有丢包
class TransportFeedbackAdapter{// : public NetworkControllerInterface 
public:
    TransportFeedbackAdapter();
    ~TransportFeedbackAdapter();

    absl::optional<webrtc::SentPacket> ProcessSentPacket(const webrtc::SentPacket& sent_packet);
    void AddPacket(webrtc::Timestamp creation_time,size_t overhead_bytes,const RtpPacketSendInfo& send_info);
    absl::webrtc::optional<webrtc::TransportpacketsFeedback> ProcessTransportFeedback(
        const webrtc::TransportpacketsFeedback& feedback,
        webrtc::Timestamp feedback_time);

private:
        std::vector<webrtc::PacketResult> ProcessTransportFeedbackInner(
            const webrtc::TransportpacketsFeedback& feedback,
            webrtc::Timestamp feedback_time);
private:
    //当前的一个时间戳
    webrtc::Timestamp current_offset_ = webrtc::Timestamp::MinusInfinity();
    //上一次feedback数据包的一个基准的参考时间
    webrtc::TimeDelta last_timestamp_ = webrtc::TimeDelta::MinusInfinity();
    std::map<int64_t,PacketFeedback> history_;
    webrtc::SequenceNumberUnwrapper seq_num_unwrapper_;//解压缩,保证数字一直上升
    webrtc::Timestamp last_send_time_ = webrtc::Timestamp::MinusInfinity();
    int64_t last_ack_seq_num_ = -1;
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_RTP_TRANSPORT_FEEDBACK_ADAPTER_H_