// parallel.h: independent chips on independent threads (WORKER4; SIMULATOR-NEXT 4, the first form of threads).
//
// A proof's segments are independent ASICs, so most runs are many chips at once with nothing shared: each chip is its
// own `Sim`, built and run by one thread, with no synchronisation until they are joined. Nothing in the framework is
// shared between Sims (the allocation hook is per thread; the type registry is per Config, W3's E1).
//
// Configuration regime (D6): these functions start and join threads; the cycle loop inside each `Sim::run` is the
// ordinary single-threaded one.
#pragma once

#include <functional>
#include <vector>

#include "config.h"

namespace sim {

// Pins the calling thread to one CPU (a no-op for cpu < 0); fatal if the kernel refuses.
void pin_to_cpu(int cpu);

// Runs each Sim for `cycles` cycles, Sim k on its own thread, pinned to cpus[k % cpus.size()] when cpus is not empty.
// Returns when every one has finished. The Sims must be distinct; the caller reads them afterwards.
void run_independent(const std::vector<Sim*>& sims, Cycle cycles, const std::vector<int>& cpus = {});

// N chips, each built, run and finished on its own thread (so its memory is first touched there): chip k is
// `build(k)`, then `run(k, sim)` (run it, check it, record what it needs), then destroyed on the same thread. Thread k is
// pinned to cpus[k % cpus.size()] when cpus is not empty. `build` and `run` are called concurrently and must only touch
// chip k's own data.
void run_independent(unsigned n, const std::function<Sim(unsigned)>& build, const std::function<void(unsigned, Sim&)>& run,
                     const std::vector<int>& cpus = {});

}  // namespace sim
