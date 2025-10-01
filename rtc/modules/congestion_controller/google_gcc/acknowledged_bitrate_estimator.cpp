#include "xrtc/rtc/modules/congestion_controller/google_gcc/acknowledged_bitrate_estimator.h"

namespace xrtc {
AcknowledgedBitrateEstimator::AcknowledgedBitrateEstimator():
    bitrate_estimator_(std::make_unique<BitrateEstimator>()) {
}
AcknowledgedBitrateEstimator::~AcknowledgedBitrateEstimator() {
}
void AcknowledgedBitrateEstimator::IncomingPacketFeedbackVector(const webrtc::PacketResult& packet_feedback_vector) {
    for(const auto& packet : packet_feedback_vector) {
        //ALR
        if(alr_end_time_ && packet.sent_packet.send_time > *alr_end_time_) {
            bitrate_estimator_->ExpectedFastRateChange();
            alr_end_time_.reset();
        }

        webrtc::DataSize acknowledged_estimate = packet.send_packet.size;
        bitrate_estimator_->Update(packet.receive_time,acknowledged_estimate);//¸üĞÂÍÌÍÂÁ¿
    }
}

absl::optional<webrtc::DataRate> AcknowledgedBitrateEstimator::bitrate() const {
    return bitrate_estimator_->bitrate();
}
void AcknowledgedBitrateEstimator::SetAlrEndTime(webrtc::Timestamp alr_end_time) {
    alr_end_time_.emplace(alr_end_time);
}
} // namespace xrtc