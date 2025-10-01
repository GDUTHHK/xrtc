#include "xrtc/rtc/pc/transport_controller.h"

#include "xrtc/base/xrtc_global.h"
#include "xrtc/rtc/pc/session_description.h"
#include "xrtc/rtc/modules/rtp_rtcp/rtp_utils.h"

namespace xrtc {

TransportController::TransportController() :
    ice_agent_(new ice::IceAgent(XRTCGlobal::Instance()->network_thread(),
        XRTCGlobal::Instance()->port_allocator()))
{
    ice_agent_->SignalIceState.connect(this,
        &TransportController::OnIceState);
    ice_agent_->SignalReadPacket.connect(this,
        &TransportController::OnReadPacket);
}

TransportController::~TransportController() {
    if (ice_agent_) {
        ice_agent_->Destroy();
        ice_agent_ = nullptr;
    }
}

//设置对端的SDP
int TransportController::SetRemoteSDP(SessionDescription* desc) {
    if (!desc) {
        return -1;
    }

    for (auto content : desc->contents()) {
        std::string mid = content->mid();
        if (desc->IsBundle(mid) && mid != desc->GetFirstBundleId()) {
            continue;
        }

        // 创建ICE transport
        // RTCP, 默认开启a=rtcp:mux
        ice_agent_->CreateIceTransport(mid, 1); // 1: RTP

        // 设置ICE param
        auto td = desc->GetTransportInfo(mid);
        if (td) {
            ice_agent_->SetRemoteIceParams(mid, 1, ice::IceParameters(
                td->ice_ufrag, td->ice_pwd));
        }

        // 设置ICE candidate
        for (auto candidate : content->candidates()) {
            ice_agent_->AddRemoteCandidate(mid, 1, candidate);
        }
    }

    return 0;
}

//设置本地的SDP
int TransportController::SetLocalSDP(SessionDescription* desc) {
    if (!desc) {
        return -1;
    }
    /*
    没有BUNDLE的情况：
    ├── 为 "audio" 创建ICE传输通道
    └── 为 "video" 创建ICE传输通道  (两个独立的传输通道)

    有BUNDLE的情况：
    ├── 为 "audio" 创建ICE传输通道
    └── "video" 跳过，复用 "audio" 的传输通道  (只有一个传输通道)

    第一个BUNDLE成员，会设置ICE参数
    第二个BUNDLE成员，会跳过ICE设置,使用相同的ICE参数
    */
    for (auto content : desc->contents()) {
        std::string mid = content->mid();
        if (desc->IsBundle(mid) && mid != desc->GetFirstBundleId()) {
            continue;
        }

        auto td = desc->GetTransportInfo(mid);
        if (td) {
            ice_agent_->SetIceParams(mid, 1,
                ice::IceParameters(td->ice_ufrag, td->ice_pwd));
        }
    }

    //所有ICE参数设置完成后，开始收集候选者
    ice_agent_->GatheringCandidate();

    return 0;
}

//真正的数据发送
int TransportController::SendPacket(const std::string& transport_name, 
    const char* data, size_t len) 
{
    return ice_agent_->SendPacket(transport_name, 1, data, len);
}

//转发至上层接收ICE的状态
void TransportController::OnIceState(ice::IceAgent*, 
    ice::IceTransportState ice_state) 
{
    SignalIceState(this, ice_state);
}

//将从网络接收到的原始数据包进行类型识别和路由分发。
void TransportController::OnReadPacket(ice::IceAgent*, const std::string&, int, 
    const char* data, size_t len, int64_t ts) 
{
    auto array_view = rtc::MakeArrayView<const uint8_t>((const uint8_t*)data, len);
    RtpPacketType packet_type = InferRtpPacketType(array_view);
    if (RtpPacketType::kUnknown == packet_type) {
        return;
    }

    //RTCP包
    if (RtpPacketType::kRtcp == packet_type) {
        SignalRtcpPacketReceived(this, data, len, ts);
    }
    //RTP包
    else if (RtpPacketType::kRtp == packet_type) {
        SignalRtpPacketReceived(this, data, len, ts);
    }
}

} // namespace xrtc