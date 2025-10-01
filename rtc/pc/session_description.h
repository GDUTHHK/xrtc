#ifndef XRTCSDK_XRTC_RTC_PC_SESSION_DESCRIPTION_H_
#define XRTCSDK_XRTC_RTC_PC_SESSION_DESCRIPTION_H_

#include <string>
#include <vector>
#include <memory>

#include <api/media_types.h>
#include <ice/candidate.h>
#include <ice/ice_credentials.h>

#include "xrtc/rtc/pc/codec_info.h"
#include "xrtc/rtc/pc/stream_params.h"

namespace xrtc {

//SDP类型枚举
enum class SdpType {
    kOffer,// Offer类型的SDP
    kAnswer,// Answer类型的SDP
};

//RTP方向枚举详解
enum class RtpDirection {
    kSendRecv,  // 双向：既发送又接收
    kSendOnly,  // 仅发送
    kRecvOnly,  // 仅接收  
    kInactive,  // 不活跃：既不发送也不接收
};

//内容组类
class ContentGroup {
public:
    ContentGroup(const std::string& semantics) : semantics_(semantics) {}
    ~ContentGroup() {}

    std::string semantics() const { return semantics_; }
    const std::vector<std::string>& content_names() const { return content_names_; }
    void AddContentName(const std::string& content_name);
    bool HasContentName(const std::string& content_name);

private:
    std::string semantics_; //组语义 (如"BUNDLE")
    std::vector<std::string> content_names_;// 内容名称列表 (如["audio", "video"])
};

//传输描述类
class TransportDescription {
public:
    std::string mid;
    std::string ice_ufrag;
    std::string ice_pwd;
};

class MediaContentDescription {
public:
    virtual ~MediaContentDescription() {}
    virtual webrtc::MediaType type() = 0;
    virtual std::string mid() = 0;

    const std::vector<std::shared_ptr<CodecInfo>>& codecs() {
        return codecs_;
    }

    const std::vector<ice::Candidate>& candidates() {
        return candidates_;
    }

    void AddCandidate(const ice::Candidate& c) {
        candidates_.push_back(c);
    }

    RtpDirection direction() { return direction_; }
    void set_direction(RtpDirection direction) { direction_ = direction; }

    bool rtcp_mux() { return rtcp_mux_; }
    void set_rtcp_mux(bool value) { rtcp_mux_ = value; }

    const std::vector<StreamParams>& streams() { return send_streams_; }
    void AddStream(const StreamParams& stream) {
        send_streams_.push_back(stream);
    }

protected:
    std::vector<ice::Candidate> candidates_;           // ICE候选者列表
    std::vector<std::shared_ptr<CodecInfo>> codecs_;   // 编解码器列表
    RtpDirection direction_ = RtpDirection::kInactive; // RTP方向
    bool rtcp_mux_ = true;                             // RTCP复用
    std::vector<StreamParams> send_streams_;           // 发送流参数
};

class AudioContentDescription : public MediaContentDescription {
public:
    AudioContentDescription();

    webrtc::MediaType type() override { return webrtc::MediaType::AUDIO; }
    std::string mid() override { return "audio"; }
};

class VideoContentDescription : public MediaContentDescription {
public:
    VideoContentDescription();

    webrtc::MediaType type() override { return webrtc::MediaType::VIDEO; }
    std::string mid() override { return "video"; }
};

class SessionDescription {
public:
    SessionDescription(SdpType type);
    ~SessionDescription();

    const std::vector<std::shared_ptr<MediaContentDescription>>& contents() {
        return contents_;
    }

    void AddContent(std::shared_ptr<MediaContentDescription> content) {
        contents_.push_back(content);
    }

    const ContentGroup* GetGroupByName(const std::string& name);
    void AddGroup(const ContentGroup& group) {
        content_groups_.push_back(group);
    }

    std::shared_ptr<TransportDescription> GetTransportInfo(const std::string transport_name);
    void AddTransportInfo(std::shared_ptr<TransportDescription> td);
    void AddTransportInfo(const std::string& mid, const ice::IceParameters& ice_param);

    bool IsBundle(const std::string& mid);
    std::string GetFirstBundleId();

    std::string ToString();

private:
    SdpType sdp_type_;
    std::vector<std::shared_ptr<MediaContentDescription>> contents_;//媒体内容描述
    std::vector<ContentGroup> content_groups_;//内容组类
    std::vector<std::shared_ptr<TransportDescription>> transport_info_;
};

} // namespace xrtc

#endif // XRTCSDK_XRTC_RTC_PC_SESSION_DESCRIPTION_H_