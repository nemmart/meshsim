// A stand-in's loop: the example of how a simulator uses the wire (see README.md), with the test options.
#pragma once

#include <cstdint>
#include <cstdio>

#include "standin.h"
#include "wire.h"

namespace wire {

struct NodeCfg {
    StandInCfg s;
    bool null_traffic = false;  // sends zeros and ignores what comes (for the cost)
    uint64_t work_ns = 0;       // busy time a cycle (a chip's work)
    uint32_t pause_ppm = 0;     // random real-time pauses between cycles: per million cycles
    uint32_t pause_max_us = 0;  //   and their longest
    uint64_t hang_at = kNever;  // at this cycle, spin for ever without exchanging (a stuck process)
    const char* log_dir = nullptr;  // every cycle's bytes, to log_dir/p<process>.bin
};

// Runs the stand-in on the wire w, or with w null on a loop-back of `window` cycles (the cost with no wire).
// Writes the digest lines to out, and a timing line starting with '#'. Returns 0, or 1 on an error.
int run_node(Wire* w, const NodeCfg& c, uint32_t window, FILE* out);

}  // namespace wire
