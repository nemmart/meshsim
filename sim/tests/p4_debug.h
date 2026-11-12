// p4_debug.h (WORKER22, P4): the framework's debug options for the P4 suites (sim.h DEBUG CHECKS; W20's MachConfig
// step_all and instance_major, W22-8), from two flags none of the Java suites have:
//   --step-all          every unit stepped every cycle (the missed-wake check's other half: a suite's output unchanged)
//   --instance-major    the instance-major stepping order (else type-major): both orders give the same output
// Each suite wraps every machine it builds as p4::dbg(config), and its main lets p4::debug_flag consume the flags.
#pragma once

#include <cstring>

#include "mach.h"

namespace p4 {
inline bool g_step_all = false, g_instance_major = false;
inline mach::MachConfig dbg(mach::MachConfig k) { k.step_all = k.step_all || g_step_all; k.instance_major = k.instance_major || g_instance_major; return k; }
inline bool debug_flag(const char* a) {
    if (std::strcmp(a, "--step-all") == 0) { g_step_all = true; return true; }
    if (std::strcmp(a, "--instance-major") == 0) { g_instance_major = true; return true; }
    return false;
}
}  // namespace p4
