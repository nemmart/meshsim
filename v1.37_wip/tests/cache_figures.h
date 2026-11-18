// cache_figures.h: the cache's figures as lines of text (updater_wip, WORKER5), for the harnesses that run the Tracer on
// the cache's controller (the chip's memory system, tests/proof_bench.cpp; the Tracer alone, tests/tracer_block.h): hits and misses by kind
// of line, evictions and write-backs, the walks' lengths, the cycles a fill and an update take, the AXI port's traffic,
// and the faults a run must not have (the cache's error flag; a read of LPDDR nothing wrote).
#pragma once

#include <cstdio>
#include <string>

#include "units/cache_system.h"

namespace cachefig {

inline std::string latency(const units::cache::CacheCtl::Latency& l) {
    char b[160];
    if (l.n == 0) return "none";
    std::snprintf(b, sizeof b, "%llu, mean %.1f, max %llu", (unsigned long long)l.n, double(l.sum) / double(l.n), (unsigned long long)l.max);
    return b;
}

inline void print(const char* name, const units::CacheSystem& s) {
    const units::cache::CacheCtl& c = s.ctl;
    using C = units::cache::CacheCtl;
    const char* kinds[C::CLASSES] = {"memory index", "memory data", "map index", "map data", "look-up index"};
    std::printf("%s: the cache:", name);
    for (unsigned k = 0; k < C::CLASSES; k++)
        if (c.hits[k] + c.misses[k])
            std::printf(" %s %llu hits, %llu misses (%.2f%%);", kinds[k], (unsigned long long)c.hits[k], (unsigned long long)c.misses[k],
                        100.0 * double(c.hits[k]) / double(c.hits[k] + c.misses[k]));
    std::printf(" evictions %llu, write-backs %llu, allocations %llu, replays %llu, served under a miss %llu\n", (unsigned long long)c.evictions,
                (unsigned long long)c.write_backs, (unsigned long long)c.allocations, (unsigned long long)c.replays,
                (unsigned long long)c.served_under_miss);
    std::printf("%s: the cache: walks of 0/1/2/3 index lines %llu/%llu/%llu/%llu; fill reads %s cycles; fill writes %s; look-ups %s; updates %s\n",
                name, (unsigned long long)c.walk_lines[0], (unsigned long long)c.walk_lines[1], (unsigned long long)c.walk_lines[2],
                (unsigned long long)c.walk_lines[3], latency(c.fill_read_latency).c_str(), latency(c.fill_write_latency).c_str(),
                latency(c.lookup_latency).c_str(), latency(c.update_latency).c_str());
    std::printf("%s: LPDDR: %llu read bursts, %llu write bursts, %llu beats read, %llu written; error bursts %llu, unwritten reads %llu; the cache's error flag %d\n",
                name, (unsigned long long)s.lpddr.read_bursts, (unsigned long long)s.lpddr.write_bursts, (unsigned long long)s.lpddr.beats_read,
                (unsigned long long)s.lpddr.beats_written, (unsigned long long)s.lpddr.error_bursts, (unsigned long long)s.lpddr.unwritten_reads,
                int(c.out().error));
    const units::AxiArbiter& a = s.arb;
    std::printf("%s: the clients: the Updater's look-ups %llu, updates %llu; cycles with its look-up and a fill request both outstanding %llu; "
                "the arbiter's bursts read/written by client 0 (the cache) %llu/%llu, 1 (the Updater) %llu/%llu, 2 (the RW engine) %llu/%llu, 3 %llu/%llu; "
                "pieces waiting for their turn, cycles: reads %llu/%llu/%llu/%llu, writes %llu/%llu/%llu/%llu; the joint's checks %llu\n",
                name, (unsigned long long)s.lookups, (unsigned long long)s.updates, (unsigned long long)s.both_outstanding_cycles,
                (unsigned long long)a.read_bursts[0], (unsigned long long)a.write_bursts[0], (unsigned long long)a.read_bursts[1],
                (unsigned long long)a.write_bursts[1], (unsigned long long)a.read_bursts[2], (unsigned long long)a.write_bursts[2],
                (unsigned long long)a.read_bursts[3], (unsigned long long)a.write_bursts[3], (unsigned long long)a.read_waits[0],
                (unsigned long long)a.read_waits[1], (unsigned long long)a.read_waits[2], (unsigned long long)a.read_waits[3],
                (unsigned long long)a.write_waits[0], (unsigned long long)a.write_waits[1], (unsigned long long)a.write_waits[2],
                (unsigned long long)a.write_waits[3], (unsigned long long)s.joint_violations());
}

// The faults a run must not have: each counted as a failed check.
inline int faults(const char* name, const units::CacheSystem& s) {
    int n = 0;
    if (s.ctl.out().error) { std::printf("%s: FAILED: the cache's error flag is set\n", name); n++; }
    if (s.lpddr.unwritten_reads) { std::printf("%s: FAILED: %llu beats read from LPDDR that nothing wrote\n", name, (unsigned long long)s.lpddr.unwritten_reads); n++; }
    if (s.lpddr.error_bursts) { std::printf("%s: FAILED: %llu LPDDR error responses\n", name, (unsigned long long)s.lpddr.error_bursts); n++; }
    for (unsigned k = 0; k < units::CacheSystem::J_CHECKS; k++)
        if (s.joint[k]) { std::printf("%s: FAILED: the joint's check: %llu times %s\n", name, (unsigned long long)s.joint[k], units::CacheSystem::joint_name(k)); n++; }
    // the clients' ports as 17.11 has them: the Updater on client 1, the RW engine on 2, nothing on 3
    if (s.arb.read_bursts[3] || s.arb.write_bursts[3]) { std::printf("%s: FAILED: bursts on the arbiter's client 3, which has no client\n", name); n++; }
    return n;
}

}  // namespace cachefig
