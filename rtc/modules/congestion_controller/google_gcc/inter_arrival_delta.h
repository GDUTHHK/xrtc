#ifndef XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_INTER_ARRIVAL_DELTA_H_
#define XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_INTER_ARRIVAL_DELTA_H_

#include<api/units/time_delta.h>
#include<api/units/timestamp.h>
namespace xrtc {
//该类的作用:主要用于计算包组之间的时间差
class InterArrivalDelta {
public:
    InterArrivalDelta(webrtc::TimeDelta send_time_group_length);
    ~InterArrivalDelta();
    bool ComputeDeltas(webrtc::Timestamp send_time,
        webrtc::Timestamp arrival_time,
        webrtc::Timestamp system_time,
        size_t packet_size,
        webrtc::TimeDelta* send_time_delta,
        webrtc::TimeDelta* arrival_time_delta,
        int* packet_size_delta);
private:
    struct SendTimeGroup {
        SendTimeGroup() :
        size(0),
        first_send_time(webrtc::Timestamp::MinusInfinity()),
        send_time(webrtc::Timestamp::MinusInfinity()),
        first_arrival(webrtc::Timestamp::MinusInfinity()),
        complete_time(webrtc::Timestamp::MinusInfinity()),
        last_system_time(webrtc::Timestamp::MinusInfinity()) 
        { }
        bool IsFirstPacket() const { return complete_time.IsInfinite(); }
        // 包组中所有包的累计字节数
        size_t size;
        // 包组第一个包的发送时间
        webrtc::Timestamp first_send_time;
        // 包组最后一个包的发送时间
        webrtc::Timestamp send_time;
        // 包组中的包第一个到达对端时间
        webrtc::Timestamp first_arrival;
        // 包组中的包最后到达对端的时间
        webrtc::Timestamp complete_time;
        // 最新的系统时间,用于纠正接收端发生跳变的情况
        webrtc::Timestamp last_system_time;
    };
    bool NewTimestampGroup(webrtc::Timestamp arrival_time,webrtc::Timestamp send_time)const;
    //判断是否属于突发包
    bool BelongsToBurst(webrtc::Timestamp arrival_time,webrtc::Timestamp send_time)const;
    void Reset();
private:
    webrtc::TimeDelta send_time_group_length_;
    SendTimeGroup current_timestamp_group_;//当前分组
    SendTimeGroup prev_timestamp_group_;//上一个分组
};
} // namespace xrtc 


#endif // XRTCSDK_XRTC_RTC_MODULES_CONGESTION_CONTROLLER_GOOGLE_GCC_INTER_ARRIVAL_DELTA_H_