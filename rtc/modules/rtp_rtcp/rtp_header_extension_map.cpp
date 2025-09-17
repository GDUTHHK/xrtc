#include "xrtc/rtc/modules/rtp_rtcp/rtp_header_extension_map.h"
#include <absl/strings/string_view.h>
#include<rtc_base/arraysize.h>
#include<rtc_base/logging.h>
#include"xrtc/rtc/modules/rtp_rtcp/rtp_header_extensions.h"
namespace xrtc {

namespace{
    struct ExtensionInfo{
        RTPExtensionType type;
        absl::string_view uri;
    };
    const ExtensionInfo kExtensionInfos[] = {
        {kRtpExtensionTransportSequenceNumber,2,0},
    };
    template<typename Extension>
    bool ExtensionInfo CreateExtensionInfo(){
        return {Extension::kId,Extension::Uri()};
    }
    const ExtensionInfo kExtensions [] = {
        CreateExtensionInfo<TransportSequenceNumber>(),
    };
    static_assert(arraysize(kExtensions) == static_cast<int>(kRtpExtensionNumberOfExtensions) - 1,"kExtensions expect to list all known extensions");
}
RtpHeaderExtensionMap::RtpHeaderExtensionMap() {
    for(auto& id : ids_){
        id = kInvalidId;
    }
}

bool RtpHeaderExtensionMap::RegisterUri(int id,absl::string_view uri){
    for(const auto& extension : kExtensions){
        if(extension.uri == uri){
            return Register(id,extension.type,extension.uri);
        }
    }

    RTC_LOG(LS_WARNING) << "unknown extension uri: " << uri<< ", id: " << id;
    return false;
}

uint8_t RtpHeaderExtensionMap::GetId(RTPExtensionType type) const {
    return ids_[type];
}

RTPExtensionType RtpHeaderExtensionMap::GetType(int id) const {
    for (int type = kRtpExtensionNone + 1; type < kRtpExtensionNumberOfExtensions;++type) 
    {
        if (ids_[type] == id) {
            return static_cast<RTPExtensionType>(type);
        }
    }
    return kRtpExtensionNone;
}

bool RtpHeaderExtensionMap::Register(int id,RTPExtensionType type,absl::string_view uri){
    //id 的大小必须是在[1, 255]的范围之内
    if(id < webrtc::RtpExtension::MinId || id > webrtc::RtpExtension::MaxId){
        RTC_LOG(LS_WARNING) << "invalid extension id: " << id;
        return false;
    }

    //检查ID是否已经注册了
    RTPExtensionType registered_type = GetType(id);
    if (registered_type == type) {
        RTC_LOG(LS_INFO) << "extension type already registered, uri: " << uri<< ", id: " << id;
        return true;
    }
    //type已经被其他的id占用了，不能再用了
    if (registered_type != kRtpExtensionNone) {
        RTC_LOG(LS_WARNING) << "failed to registered extension, uri: " << uri
            << ", id: " << id << ". Id already in use by extension type"
            << static_cast<int>(registered_type);
        return false;
    }
    //注册
    ids_[type] = static_cast<uint8_t>(id);
    return true;
}

}