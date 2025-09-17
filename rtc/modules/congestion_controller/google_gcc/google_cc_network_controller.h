#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_GOOGLE_CC_NETWORK_CONTROLLER_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_GOOGLE_CC_NETWORK_CONTROLLER_H_

#include "xrtc/rtc/modules/congestion_controller/google_gcc/delay_based_bwe.h"
#include "xrtc/rtc/pc/network_controller.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/acknowledged_bitrate_estimator.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/send_side_bandwidth_estimator.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/alr_detector.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/probe_controller.h"
#include "xrtc/rtc/modules/congestion_controller/google_gcc/probe_bitrate_estimator.h"
namespace xrtc {

    //作为带宽估计或者拥塞控制的模块
class GoogleCCNetworkController : public NetworkControllerInterface {
public:
    GoogleCCNetworkController(const NetworkControllerConfig& config);
    ~GoogleCCNetworkController() override;
    webrtc::NetworkControlUpdate OnNetworkOk(const webrtc::TargetRateConstraints &constraints) override;
    webrtc::NetworkControlUpdate OnTransportpacketsFeedback(
        const webrtc::TransportpacketsFeedback& report) override;
    webrtc::NetworkControlUpdate OnRttUpdate(int64_t rtt_ms) override;
    webrtc::NetworkControlUpdate OnTransportLoss(int32_t packets_lost,
        int32_t num_of_packets,webrtc::Timestamp at_time) override;
    webrtc::NetworkControlUpdate OnProcessInterval(const webrtc::ProcessInterval& msg) override;
    webrtc::NetworkControlUpdate OnSentPacket(const webrtc::SentPacket& sent_packet) override;
private:
    absl::optional<NetworkControllerConfig> init_config_;
    void MaybeTriggerOnNetworkChanged(webrtc::NetworkControlUpdate* update,webrtc::Timestamp at_time);
    webrtc::PacerConfig GetPacingRate(webrtc::Timestamp at_time);
    std::vector<webrtc::ProbeClusterConfig> ResetConstraints(const webrtc::TargetRateConstraints& constraints);
    std::unique_ptr<DelayBasedBwe> delay_based_bwe_;
    std::unique_ptr<AcknowledgedBitrateEstimator> acknowledged_bitrate_estimator_;
    std::unique_ptr<SendSideBandwidthEstimator> bandwidth_estimator_;
    std::unique_ptr<AlrDetector> alr_detector_;
    std::unique_ptr<ProbeController> probe_controller_;
    std::unique_ptr<ProbeBitrateEstimator> probe_bitrate_estimator_;
    webrtc::DataRate last_loss_based_bitrate_;
    uint8_t last_estimated_fraction_loss_ = 0;
    webrtc::TimeDelta last_estimated_rtt_ = webrtc::TimeDelta::PlusInfinity();
    double pace_factor_;
    bool previously_in_alr_ =false;//之前是否处于ALR状态

    webrtc::DataRate min_data_rate_ = webrtc::DataRate::Zero();
    webrtc::DataRate max_data_rate_ = webrtc::DataRate::PlusInfinity();
    absl::optional<webrtc::DataRate> starting_rate_;
};
} // namespace xrtc
#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_GOOGLE_CC_NETWORK_CONTROLLER_H_