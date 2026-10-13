// The split of the app's memory between the linear heap and the malloc heap,
// in place of libctru's weak __system_allocateHeaps().
//
// app.rsf asks for 96 MB on an Old 3DS and 178 MB on a New 3DS. The first
// build died before main() on a New 3DS XL, in Luma3DS as a "debug event":
//
//     prefetch abort, IFSR 2 (svcBreak), pc = svcBreak,
//     lr = __system_allocateHeaps after the linear heap's svcControlMemory
//     r0 = 0 (USERBREAK_PANIC), [sp] = 0x00010003 (MEMOP_ALLOC_LINEAR)
//     r4 = 0x0982a000: the 152 MB it asked for
//     the limit 0x0b200000 (178 MB), 0x001d6000 already committed
//
// main.cpp then set only __ctru_heap_size (24 MB), and libctru gives the
// linear heap "everything else" with no cap: 178 - 1.8 - 24 = 152 MB. The
// linear heap is one physically contiguous block (the GPU reads it), and
// FCRAM is 128 MB banks, the New 3DS's second above the first, so 152 MB can
// never be had in one piece. libctru answers any refusal with svcBreak.
// Azahar serves the linear heap out of one host buffer and never refuses.
//
// So: the linear heap is asked for first, while the region is least
// fragmented, never more than a bank, and on refusal 4 MB less until the
// kernel agrees. The malloc heap, which need not be contiguous, takes the
// rest. Neither panics while a smaller ask could still work. On a New 3DS
// XL that comes to: grant 178 MB, linear 80 MB, heap 96 MB.
//
// The game's linear memory is its textures, the vertex buffer and the
// sounds: about 46 MB with a map up (the person sheets 14 MB, the GUI's
// atlases 8 MB, the effects 13.6 MB), plus 10 MB of online tiles. The malloc
// heap held 2.8 MB with a map up on Azahar; building a player map adds
// Overpass's JSON. HEAP_WANT keeps 24 MB for it whatever the grant.

#include <3ds.h>
#include <3ds/allocator/mappable.h>

#include "heapcfg.h"

// libctru's globals (allocateHeaps.o): linear.o builds its pool from the
// linear pair on the first linearAlloc, and __libctru_exit frees both pairs.
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

// newlib's sbrk window.
extern char* fake_heap_start;
extern char* fake_heap_end;

ZosHeapInfo g_heapInfo;

#define HEAP_WANT   (24u * 1024 * 1024)
#define HEAP_MIN    ( 4u * 1024 * 1024)
#define HEAP_STEP   ( 4u * 1024 * 1024)
// The malloc heap's window in the address space (96 MB).
#define HEAP_AREA   ((u32)(OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN))

// A contiguous block cannot cross the boundary between FCRAM's 128 MB banks.
#define FCRAM_BANK  (128u * 1024 * 1024)
#define LINEAR_MIN  ( 4u * 1024 * 1024)
#define LINEAR_STEP ( 4u * 1024 * 1024)

#define ALIGN4K(x)  ((u32)(x) & ~0xFFFu)

// Called from __libctru_init() before main(), after .bss is cleared. No
// malloc, printf or files here: none of them exist yet.
void __system_allocateHeaps(void)
{
    s64 commitMax = 0, commitUsed = 0;
    ResourceLimitType kind = RESLIMIT_COMMIT;
    Handle reslimit = 0;
    u32 avail, heapWant, want;
    Result rc;

    // What this process may commit, less what loading it took (the query
    // libctru makes).
    if (R_SUCCEEDED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) {
        svcGetResourceLimitLimitValues(&commitMax, reslimit, &kind, 1);
        svcGetResourceLimitCurrentValues(&commitUsed, reslimit, &kind, 1);
        svcCloseHandle(reslimit);
    }
    if (commitUsed >= 0 && commitMax > commitUsed) {
        avail = ALIGN4K((u32)(commitMax - commitUsed));
    } else {
        // The smallest grant: 64 MB, ~2 MB of it the program.
        avail = 60u * 1024 * 1024;
        g_heapInfo.queryFailed = 1;
    }
    g_heapInfo.commitMax = (u32)commitMax;
    g_heapInfo.commitUsed = (u32)commitUsed;
    g_heapInfo.avail = avail;

    // The malloc heap's share is kept back from the linear heap's ask, never
    // more than half of a small grant.
    heapWant = HEAP_WANT;
    if (heapWant > avail / 2)
        heapWant = ALIGN4K(avail / 2);

    want = avail - heapWant;
    if (want > FCRAM_BANK)
        want = FCRAM_BANK;
    want = ALIGN4K(want);
    g_heapInfo.linearAsked = want;
    for (;;) {
        __ctru_linear_heap = 0;
        g_heapInfo.linearTries++;
        rc = svcControlMemory(&__ctru_linear_heap, 0, 0, want, MEMOP_ALLOC_LINEAR, MEMPERM_READWRITE);
        if (R_SUCCEEDED(rc)) {
            __ctru_linear_heap_size = want;
            g_heapInfo.linearGot = want;
            break;
        }
        g_heapInfo.linearFailRc = (u32)rc;
        if (want <= LINEAR_MIN) {
            __ctru_linear_heap = 0;
            __ctru_linear_heap_size = 0;
            break;
        }
        want = want > LINEAR_MIN + LINEAR_STEP ? want - LINEAR_STEP : LINEAR_MIN;
    }

    // The malloc heap at libctru's fixed base: what the linear heap left, up
    // to the window.
    want = avail - __ctru_linear_heap_size;
    if (want < heapWant)
        want = heapWant;
    if (want > HEAP_AREA)
        want = HEAP_AREA;
    want = ALIGN4K(want);
    g_heapInfo.heapAsked = want;
    for (;;) {
        __ctru_heap = 0;
        g_heapInfo.heapTries++;
        rc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, want, MEMOP_ALLOC, MEMPERM_READWRITE);
        if (R_SUCCEEDED(rc)) {
            __ctru_heap_size = want;
            g_heapInfo.heapGot = want;
            break;
        }
        g_heapInfo.heapFailRc = (u32)rc;
        // Without 4 MB of malloc heap nothing can run, and there is nothing
        // to report with: panic as libctru would.
        if (want <= HEAP_MIN)
            svcBreak(USERBREAK_PANIC);
        want = want > HEAP_MIN + HEAP_STEP ? want - HEAP_STEP : HEAP_MIN;
    }

    // What libctru's own does on the way out.
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
