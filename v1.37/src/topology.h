// topology.h: which CPUs this machine has and where threads go (D21: nothing machine-specific is baked in).
//
// Detected at run time from /sys and this process's affinity: the online CPUs it may use; on a hybrid chip the
// performance and efficiency cores (/sys/devices/cpu_core/cpus, cpu_atom/cpus), elsewhere every core a "P" core; each
// core's hyperthread siblings. The **default placement** is: one CPU per P core (the first sibling each, in CPU order),
// then the P cores' other siblings, then the E cores, then anything left; thread k goes to placement[k % size]. The same
// rules as tools/machine.sh (`machine.sh cpus place`), and the same overrides, from the environment:
//   SIM_P_CPUS, SIM_E_CPUS   the P and E CPU lists ("0-11", "12-15")
//   SIM_ALLOWED_CPUS         the CPUs to use (else this process's affinity)
//   SIM_SYSFS                another /sys (tests: tools/fake_sysfs.sh)
// The configuration regime: STL, files, getenv.
#pragma once

#include <string>
#include <vector>

namespace sim {

struct Topology {
    std::vector<int> all;      // every CPU usable
    std::vector<int> p, p_one; // P-core CPUs; one per P core
    std::vector<int> e;        // E-core CPUs (empty on a non-hybrid chip)
    std::vector<int> place;    // the default placement order

    static Topology detect();
    // n CPUs for n threads, from the default placement (wrapping if n exceeds it)
    std::vector<int> placement(unsigned n) const;
    // the machine, for a benchmark's output (D21): CPU model, core types and counts with their clocks, memory, kernel,
    // compiler; one line
    std::string record() const;
};

// "0-3,8" -> {0, 1, 2, 3, 8}; and back
std::vector<int> parse_cpu_list(const std::string& s);
std::string cpu_list(const std::vector<int>& v);

}  // namespace sim
