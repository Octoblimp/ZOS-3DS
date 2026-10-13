#ifndef ZOS_HEAPCFG_H
#define ZOS_HEAPCFG_H

#include <3ds.h>

#ifdef __cplusplus
extern "C" {
#endif

// Filled in by __system_allocateHeaps() (heapcfg.c) before main() runs, so
// main() can log what the console handed over rather than what was asked
// for. Plain integers: it is written before the malloc heap exists.
typedef struct {
    u32 commitMax;    // RESLIMIT_COMMIT's limit: the memory mode's grant
    u32 commitUsed;   // already committed (code, data, bss, stack)
    u32 avail;        // commitMax - commitUsed, 4K aligned
    u32 linearAsked;  // the first linear size tried
    u32 linearGot;
    u32 linearTries;  // 1: the first ask succeeded
    u32 linearFailRc; // the last refusal's Result
    u32 heapAsked;
    u32 heapGot;
    u32 heapTries;
    u32 heapFailRc;
    u32 queryFailed;  // 1: svcGetResourceLimit failed and avail is a guess
} ZosHeapInfo;

extern ZosHeapInfo g_heapInfo;

#ifdef __cplusplus
}
#endif

#endif
