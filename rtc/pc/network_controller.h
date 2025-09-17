#ifndef XRTCSDK_XRTC_RTC_PC_NETWORK_CONTROLLER_H_
#define XRTCSDK_XRTC_RTC_PC_NETWORK_CONTROLLER_H_
#include<api/transport/network_types.h>
namespace xrtc {

struct NetworkControllerConfig {
public:
    webrtc::TargetRateConstraints constraints;
};
class NetworkControllerInterface {
    public:
        virtual ~NetworkControllerInterface() {}
        //网络建立成功触发
        virtual webrtc::NetworkControlUpdate OnNetworkOk(const webrtc::TargetRateConstraints &constraints) = 0;
        //收到TCC数据包反馈触发
        virtual webrtc::NetworkControlUpdate OnTransportpacketsFeedback(
            const webrtc::TransportpacketsFeedback& ) = 0;
        virtual webrtc::NetworkControlUpdate OnRttUpdate(int64_t rtt_ms) = 0;
        //收到丢包触发
        virtual webrtc::NetworkControlUpdate OnTransportLoss(int32_t packets_lost,
            int32_t num_of_packets,webrtc::Timestamp at_time) = 0;
        //定时触发
        virtual webrtc::NetworkControlUpdate OnProcessInterval(const webrtc::ProcessInterval& msg) = 0;
        //触发使用ALR预算
        virtual webrtc::NetworkControlUpdate OnSentPacket(const webrtc::SentPacket& sent_packet) = 0;
};
} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_PC_NETWORK_CONTROLLER_H_
