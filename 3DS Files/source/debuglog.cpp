#include "debuglog.h"

#include <cstdarg>
#include <cstdio>

static FILE* s_log;

void dbg_open()
{
    s_log = fopen("sdmc:/zos_debug.txt", "w");
}

void dbg_close()
{
    if (s_log) {
        fclose(s_log);
        s_log = nullptr;
    }
}

void dbg(const char* fmt, ...)
{
    if (!s_log)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);
    fputc('\n', s_log);
    // Flushed every line: the test script reads the file while the app runs,
    // and a crash must not take the last lines with it.
    fflush(s_log);
}
