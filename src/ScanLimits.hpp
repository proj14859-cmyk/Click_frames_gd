#pragma once
#include <cstdint>
#include <cstddef>
namespace cf {
inline bool reachedHorizon(int now,int click,int offset,int limit,bool local,bool dataset,
    bool validating,bool auditing,bool recovery,bool fullRetry,bool disabled,bool dispatched,bool alive) {
    return limit>0 && local && !dataset && !validating && !auditing && !recovery &&
        !fullRetry && !disabled && dispatched && alive &&
        int64_t(now)>=int64_t(click)+offset+limit;
}
inline bool missingValidationTrace(int now,int stateTick,size_t compared) {
    return int64_t(now)-stateTick>=64 && compared==0;
}
}
