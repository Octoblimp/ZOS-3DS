#include "debuglog.h"

#include <3ds.h>

#include <cstdarg>
#include <cstdio>
#include <string>

// The lines are written by a thread of their own; dbg only formats one into
// memory. Written by the caller, a line waited for the card behind whatever
// else it was doing (1.0.0's imagery made that seconds), and on the main
// thread the frame waited with it.

namespace {

FILE* s_log = nullptr;
LightLock s_lock;
CondVar s_cond;
std::string s_pending; // lines not yet written
Thread s_thread = nullptr;
bool s_quit = false;
int s_dropped = 0;
// Beyond this unwritten, lines are dropped (the card is gone, or far behind)
// and how many said.
const size_t kMaxPending = 256 * 1024;
// After the first line, more are gathered this long (ms) for one write.
const int kGatherMs = 50;

void writer(void*)
{
    std::string chunk;
    for (;;) {
        LightLock_Lock(&s_lock);
        while (s_pending.empty() && !s_quit)
            CondVar_Wait(&s_cond, &s_lock);
        if (s_pending.empty()) {
            LightLock_Unlock(&s_lock);
            return;
        }
        const bool quit = s_quit;
        LightLock_Unlock(&s_lock);
        if (!quit)
            svcSleepThread((s64)kGatherMs * 1000 * 1000);
        LightLock_Lock(&s_lock);
        chunk.swap(s_pending);
        if (s_dropped) {
            char note[64];
            snprintf(note, sizeof(note), "(%d log lines dropped)\n", s_dropped);
            chunk += note;
            s_dropped = 0;
        }
        LightLock_Unlock(&s_lock);
        fwrite(chunk.data(), 1, chunk.size(), s_log);
        // Flushed each time: the test script reads the file while the app
        // runs, and a crash must not take more than the last moment's lines.
        fflush(s_log);
        chunk.clear();
    }
}

} // namespace

void dbg_open()
{
    LightLock_Init(&s_lock);
    CondVar_Init(&s_cond);
    s_log = fopen("sdmc:/zos_debug.txt", "w");
    if (!s_log)
        return;
    // Below the main thread: it writes while the main thread waits for the
    // screen.
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s_thread = threadCreate(writer, nullptr, 16 * 1024, prio + 1, -2, false);
    if (!s_thread) {
        fclose(s_log);
        s_log = nullptr;
    }
}

void dbg_close()
{
    if (!s_log)
        return;
    LightLock_Lock(&s_lock);
    s_quit = true;
    CondVar_Signal(&s_cond);
    LightLock_Unlock(&s_lock);
    threadJoin(s_thread, U64_MAX);
    threadFree(s_thread);
    s_thread = nullptr;
    LightLock_Lock(&s_lock);
    fclose(s_log);
    s_log = nullptr;
    LightLock_Unlock(&s_lock);
}

void dbg(const char* fmt, ...)
{
    // (Before dbg_open, the lock is not made yet.)
    if (!s_log)
        return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    LightLock_Lock(&s_lock);
    if (s_log && !s_quit) {
        if (s_pending.size() + (size_t)n > kMaxPending) {
            s_dropped++;
        } else {
            s_pending.append(line, (size_t)n);
            CondVar_Signal(&s_cond);
        }
    }
    LightLock_Unlock(&s_lock);
}
