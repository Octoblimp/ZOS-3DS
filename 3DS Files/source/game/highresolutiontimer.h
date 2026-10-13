// HighResolutionTimer (decomp/classes/HighResolutionTimer.c): seconds since
// the last reset. The game read CLOCK_MONOTONIC; the port reads the ARM11's
// tick counter.
#pragma once

#include <3ds.h>

class HighResolutionTimer {
public:
    HighResolutionTimer() { reset(); }

    void reset() { _start = svcGetSystemTick(); }
    double getTime() const { return (double)(svcGetSystemTick() - _start) / (double)SYSCLOCK_ARM11; }

private:
    u64 _start;
};
