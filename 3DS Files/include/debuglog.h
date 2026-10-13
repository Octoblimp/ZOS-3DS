// The port's log: sdmc:/zos_debug.txt, opened "w" at boot so each run starts
// it afresh. tools/run_azahar_test.ps1 waits on its lines ("entering main
// loop", "left main loop") to drive the emulator and give its verdict.
#pragma once

void dbg_open();
void dbg_close();
void dbg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
