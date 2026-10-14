#include "workers.h"

#include "debuglog.h"

namespace {

// The core the workers go on, once one has taken a thread (-3: not tried).
int s_core = -3;

Thread create(ThreadFunc fn, void* arg, size_t stackSize, s32 prio, int core)
{
    return threadCreate(fn, arg, stackSize, prio, core, false);
}

} // namespace

Thread createWorkerThread(ThreadFunc fn, void* arg, size_t stackSize, int prioBelow)
{
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio += prioBelow;
    if (prio > 0x3F)
        prio = 0x3F;

    if (s_core != -3) {
        Thread t = create(fn, arg, stackSize, prio, s_core);
        if (!t && s_core != -2)
            t = create(fn, arg, stackSize, prio, -2);
        return t;
    }

    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (n3ds) {
        if (Thread t = create(fn, arg, stackSize, prio, 2)) {
            s_core = 2;
            dbg("workers: on core 2");
            return t;
        }
    }
    // Core 1 is the system's, the app's threads there held to a share of its
    // time: as much as the exheader allows (the CIA's MaxCpu is 80), or the
    // share homebrew commonly gets.
    static const u32 kShares[] = {80, 30};
    for (u32 percent : kShares) {
        if (R_FAILED(APT_SetAppCpuTimeLimit(percent)))
            continue;
        if (Thread t = create(fn, arg, stackSize, prio, 1)) {
            s_core = 1;
            dbg("workers: on core 1, %lu%% of it", (unsigned long)percent);
            return t;
        }
        break;
    }
    s_core = -2;
    dbg("workers: on the main thread's core");
    return create(fn, arg, stackSize, prio, -2);
}

bool workersOffMainCore()
{
    return s_core == 1 || s_core == 2;
}
