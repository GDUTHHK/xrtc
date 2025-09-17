#include "xrtc/rtc/modules/congestion_controller/google_gcc/inter_arrival_delta.h"
#include <rtc_base/logging.h>

namespace xrtc {
namespace {
//判断是否属于突发包的阈值
constexpr webrtc::TimeDelta kBurstDeltaThreshould = webrtc::TimeDelta::Millis(5);//5ms
//两个包最大的差值的阈值，指的是最后一个包与包组的第一个包的时间差
constexpr webrtc::TimeDelta kMaxBurstDuration = webrtc::TimeDelta::Millis(100);//100ms
//系统时间偏移的阈值
constexpr webrtc::TimeDelta kArrivalTimeOffsetThreashold = webrtc::TimeDelta::Seconds(3);//3s
}
InterArrivalDelta::InterArrivalDelta(webrtc::TimeDelta send_time_group_length):
    send_time_group_length_(send_time_group_length)
{

}
InterArrivalDelta::~InterArrivalDelta() {
}
bool InterArrivalDelta::ComputeDeltas(webrtc::Timestamp send_time,
    webrtc::Timestamp arrival_time,
    webrtc::Timestamp system_time,
    size_t packet_size,
    webrtc::TimeDelta* send_time_delta,
    webrtc::TimeDelta* arrival_time_delta,
    int* packet_size_delta) 
{
    bool calculated_delta = false;//是否计算成功
    //如果是第一个包，需要初始化分组
    if(current_timestamp_group_.IsFirstPacket()) {
        current_timestamp_group_.send_time = send_time;//最后一个包的发送时间，每次都会更新
        current_timestamp_group_.first_send_time = send_time;//只会更新一次
        current_timestamp_group_.first_arrival = arrival_time;//只会更新一次
        // current_timestamp_group_.complete_time = arrival_time;
        // current_timestamp_group_.last_system_time = system_time;
    }else if( send_time <current_timestamp_group_.send_time) { //乱序包
        return false;//不处理乱序包
    }
    //是否需要创建新的分组
    else if(NewTimestampGroup(arrival_time,send_time)) {
        //判断是否需要计算两个包组之间的时间差
        if(prev_timestamp_group_.complete_time.IsInfinite()) {
            *send_time_delta = current_timestamp_group_.send_time - prev_timestamp_group_.send_time;
            *arrival_time_delta = current_timestamp_group_.complete_time - prev_timestamp_group_.complete_time;
            webrtc::TimeDelta system_time_delta = current_timestamp_group_.last_system_time - prev_timestamp_group_.last_system_time;
            if(*arrival_time_delta - system_time_delta >=kArrivalTimeOffsetThreashold){
                RTC_LOG(LS_WARNING) << "Arrival time clock offset has changed "<<"diff: "<<arrival_time_delta->ms()  - system_time_delta.ms();
                Reset();
                return false;
            }
        }
        *packet_size_delta = static_cast<int>(current_timestamp_group_.size - prev_timestamp_group_.size);
        calculated_delta = true;
        prev_timestamp_group_ = current_timestamp_group_;//更新上一个分组
        current_timestamp_group_.first_send_time = send_time;//更新当前分组的第一个包的发送时间
        current_timestamp_group_.send_time = send_time;//更新当前分组的最后一个包的发送时间
        current_timestamp_group_.first_arrival = arrival_time;//更新当前分组的第一个包的到达时间
        current_timestamp_group_.size = 0;
    }else{
        //当前分组的包
        current_timestamp_group_.send_time = std::max(current_timestamp_group_.send_time,send_time);//可能是乱序的。取最大值
    }
    //累计分组的包的大小
    current_timestamp_group_.size += packet_size;
    current_timestamp_group_.complete_time = arrival_time;
    current_timestamp_group_.last_system_time = system_time;

    return calculated_delta;
}
bool InterArrivalDelta::NewTimestampGroup(webrtc::Timestamp arrival_time,webrtc::Timestamp send_time)const {
    //当前分组还没有数据包，所以肯定不是一个新的分组
    if(current_timestamp_group_.complete_time.IsInfinite()) {
        return false;
    }
    //突发的包,也不需要一个新的分组
    else if(BelongsToBurst(arrival_time,send_time)) {
        return false;
    }
    else{
        //创建一个新的分组
        return send_time - current_timestamp_group_.first_send_time > send_time_group_length_;
    }
}
bool InterArrivalDelta::BelongsToBurst(webrtc::Timestamp arrival_time,webrtc::Timestamp send_time)const {
    webrtc::TimeDelta arrival_time_delta = arrival_time - current_timestamp_group_.complete_time;
    webrtc::TimeDelta send_time_delta = send_time - current_timestamp_group_.send_time;
    //计算传播延迟差
    webrtc::TimeDelta propagation_delay = arrival_time_delta - send_time_delta;
    if(send_time_delta.IsZero()) {//发送时间相同的包
        return true;
    }
    //在100ms以内发送的数据包，因为网络拥塞了，突然又缓解了，那就有可能突然全部收到了
    // 需要在100ms以内，并且传播延迟差小于5ms，则认为属于突发包
    else if(propagation_delay<webrtc::TimeDelta::Zero() && 
            arrival_time_delta < kBurstDeltaThreshould && 
            arrival_time - current_timestamp_group_.first_arrival < kMaxBurstDuration) 
    {
        return true;
    }
    return false;
}
void InterArrivalDelta::Reset() {
    current_timestamp_group_ = SendTimeGroup();
    prev_timestamp_group_ = SendTimeGroup();
}
} // namespace xrtc