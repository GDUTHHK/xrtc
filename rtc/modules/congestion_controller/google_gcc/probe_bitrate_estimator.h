#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_BITRATE_ESTIMATOR_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_BITRATE_ESTIMATOR_H_

#include <map>
#include <absl/types/optional.h>
#include <api/units/data_rate.h>
#include <api/transport/network_types.h>

namespace xrtc {

class ProbeBitrateEstimator {
public:
    ProbeBitrateEstimator();
    ~ProbeBitrateEstimator();

    absl::optional<webrtc::DataRate> HandleProbeAndEstimateBitrate(const webrtc::PacketResult& feedback_packet);
    absl::optional<webrtc::DataRate> FetchAndRestLastEstimatedBitrate();//获取并重置最后一次估计的码率
private:
    struct AggregatedCluster {
        // 接收端确认收到探测包的总个数
        int num_probes = 0;
        // 第一次发送探测数据的时间
        webrtc::Timestamp first_send = webrtc::Timestamp::PlusInfinity();
        // 最后一次发送探测数据的时间
        webrtc::Timestamp last_send = webrtc::Timestamp::MinusInfinity();
        // 第一次收到探测数据的时间
        webrtc::Timestamp first_receive = webrtc::Timestamp::PlusInfinity();
        // 最后一次收到探测数据的时间
        webrtc::Timestamp last_receive = webrtc::Timestamp::MinusInfinity();
        // 最后一次发送包的大小
        webrtc::DataSize size_last_send = webrtc::DataSize::Zero();
        // 第一次收到包的大小
        webrtc::DataSize size_first_receive = webrtc::DataSize::Zero();
        // 累计探测反馈的包大小
        webrtc::DataSize size_total = webrtc::DataSize::Zero();
    };

    void EraseOldCluster(webrtc::Timestamp timestamp);
private:
    std::map<int,AggregatedCluster> clusters_;//探测包的聚合信息
    absl::optional<webrtc::DataRate> estimate_data_rate_;//估计的码率
}
}


#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_PROBE_BITRATE_ESTIMATOR_H_