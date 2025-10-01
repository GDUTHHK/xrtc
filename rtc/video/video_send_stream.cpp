#include "xrtc/rtc/video/video_send_stream.h"

#include <modules/rtp_rtcp/source/byte_io.h>

namespace xrtc {

const int16_t kRtxHeaderSize = 2;

std::unique_ptr<ModuleRtpRtcpImpl> CreateRtpRtcpModule(webrtc::Clock* clock,
    const VideoSendStreamConfig& vsconfig) 
{
    RtpRtcpInterface::Configuration config;
    config.audio = false;// 视频流
    config.receiver_only = false;// 发送模式
    config.clock = clock;// 时钟
    config.local_media_ssrc = vsconfig.rtp.ssrc;// SSRC
    config.payload_type = vsconfig.rtp.payload_type;// 负载类型
    config.rtcp_report_interval_ms = vsconfig.rtcp_report_interval_ms;// RTCP间隔

    config.clock_rate = vsconfig.rtp.clock_rate;
    config.rtp_rtcp_module_observer = vsconfig.rtp_rtcp_module_observer;
    config.transport_feedback_observer = vsconfig.transport_feedback_observer;
    
    auto rtp_rtcp = std::make_unique<ModuleRtpRtcpImpl>(config);
    return std::move(rtp_rtcp);
}

VideoSendStream::VideoSendStream(webrtc::Clock* clock, 
    const VideoSendStreamConfig& config) :
    config_(config),
    rtp_rtcp_(CreateRtpRtcpModule(clock, config))
{
    rtp_rtcp_->SetRTCPStatus(webrtc::RtcpMode::kCompound);// 复合RTCP模式
    rtp_rtcp_->SetSendingStatus(true);// 启用发送
}

VideoSendStream::~VideoSendStream() {
}

//发送RTP包时更新统计
void VideoSendStream::UpdateRtpStats(std::shared_ptr<RtpPacketToSend> packet, 
    bool is_rtx, bool is_retransmit) 
{
    rtp_rtcp_->UpdateRtpStats(packet, is_rtx, is_retransmit);
}

//发送视频帧时触发RTCP
void VideoSendStream::OnSendingRtpFrame(uint32_t rtp_timestamp, 
    int64_t capture_time_ms,
    bool forced_report) 
{
    rtp_rtcp_->OnSendingRtpFrame(rtp_timestamp, capture_time_ms,
        forced_report);
}

//接收RTCP反馈
void VideoSendStream::DeliverRtcp(const uint8_t* packet, size_t length) {
    rtp_rtcp_->IncomingRtcpPacket(packet, length);
}

void VideoSendStream::CopyHeaderAndExtensionToRtxPacket(
    RtpPacketToSend* packet,
    RtpPacketToSend* rtx_packet) 
{
    auto extension =RTPExtensionType::kRtpExtensionTransportSequenceNumber;
    rtc::ArrayView<const uint8_t> source = packet->FindExtension(extension);
    if(source.empty()){
        return;
    }
    rtc::ArrayView<uint8_t> dest = rtx_packet->AllocateExtension(extension,source.size());
    if(dest.size() != source.size()){
        return;
    }

    memcpy(dest.begin(),source.begin(),dest.size());
}

std::unique_ptr<RtpPacketToSend> VideoSendStream::BuildRtxPacket(
    RtpPacketToSend* packet,
    RtpHeaderExtensionMap* rtp_header_extension_map) 
{
    auto rtx_packet = std::make_unique<RtpPacketToSend>(rtp_header_extension_map);
    rtx_packet->SetPayloadType(config_.rtp.rtx.payload_type);
    rtx_packet->SetSsrc(config_.rtp.rtx.ssrc);
    rtx_packet->SetSequenceNumber(rtx_seq_++);
    rtx_packet->SetMarker(packet->marker());
    rtx_packet->SetTimestamp(packet->timestamp());
    rtx_packet->set_packet_type(*packet->packet_type());

    CopyHeaderAndExtensionToRtxPacket(packet,rtx_packet.get());
    // 分配负载的内存
    auto rtx_payload = rtx_packet->AllocatePayload(packet->payload_size()
        + kRtxHeaderSize);
    if (!rtx_payload) {
        return nullptr;
    }

    // 写入原始的sequence_number
    webrtc::ByteWriter<uint16_t>::WriteBigEndian(rtx_payload, packet->sequence_number());
    // 写入原始的负载数据
    auto payload = packet->payload();
    memcpy(rtx_payload + kRtxHeaderSize, payload.data(), payload.size());

    if(packet->padding_size() > 0){
        rtx_packet->SetPadding(packet->padding_size());
    }

    return rtx_packet;
}

} // namespace xrtc