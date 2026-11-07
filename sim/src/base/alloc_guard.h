// The allocation hook: any allocation or deallocation between begin_run() and end_run() is fatal.
//
// The flag is per thread (each of W4's threads runs its own partition). Caught: every form of operator new and delete
// (replaced in alloc_guard.cpp), and malloc, calloc, realloc, free, aligned_alloc and posix_memalign called from our own
// object files (linked with -Wl,--wrap=..., see the Makefile's WRAP). Not caught: allocations made inside libc itself.
// Holds under ASan too: our operator new is preferred to ASan's, and calls the real (ASan-intercepted) malloc.
#pragma once

namespace sim {

void begin_run();  // DCHECK: not already in a run
void end_run();    // DCHECK: in a run
bool in_run();
void end_run_for_fatal();  // FATAL's own: clears the flag without checks

}  // namespace sim
