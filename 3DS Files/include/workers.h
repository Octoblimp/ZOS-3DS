// The port's threads that work for the main thread (the imagery's downloads,
// the map's tiles off the card, place search, the player maps' builder) go
// on a core of their own where the console has one to give, so what they do
// costs the game's frames nothing:
//   - a New 3DS's core 2, the app's alone (the exheader's CanAccessCore2);
//   - else core 1, up to the share of it APT gives the app (MaxCpu);
//   - else core 0, below the main thread, as 1.0.0 had them all.
// A .3dsx gets whichever its host process allows.
#pragma once

#include <3ds.h>

// A thread prioBelow below the main thread's priority (on its own core, the
// order among the workers), not detached. From the main thread.
Thread createWorkerThread(ThreadFunc fn, void* arg, size_t stackSize, int prioBelow);
// Whether the workers run beside the main thread rather than in its gaps.
bool workersOffMainCore();
