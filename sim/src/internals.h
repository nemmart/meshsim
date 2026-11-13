// internals.h: the framework's internal access. config.h names `detail::Internals` a friend of Config and Sim,
// for the digest unit by unit (digest.h, E5) and the split of a configuration into domains (threads.h, E3).
#pragma once

#include <string>
#include <vector>

#include "config_impl.h"
#include "digest_view.h"

namespace sim::detail {

struct Internals {
    using CImpl = Config::Impl;    // for the code outside this struct (both are private to their classes)
    using SImpl = Sim::Impl;
    // digest.cpp: the view of a single-domain build (recorded at the end of build(), engine 0), and the digest over one
    // engine or several (sims[eng]); `now` is the machine's cycle.
    static void record_view(const Config::Impl& I, const std::vector<uint32_t>& inst_of, DigestView& v);
    static uint64_t digest(const DigestView& v, const Sim::Impl* const* sims, Cycle now);
    // threads.cpp: a configuration over several domains, split and built.
    static Sim::Impl* split(Config::Impl& G);
    // access
    static Config::Impl& impl(Config& c) { return *c.impl_; }
    static Sim::Impl* impl(const Sim& s) { return s.impl_; }
    // (regions.cpp): the machine's memory, structure by structure, with the domain (thread) that owns each: every
    // engine block's carved structures, every unit type's arena, the outboxes' storage (owned by their source), the
    // observer's and calibration's arrays. For footprint measurements (bench_chip --trace); unit-owned heap memory
    // (a unit's own allocations) is the unit's to report.
    struct MemRegion { std::string what; const void* base; size_t bytes; int domain; };
    static std::vector<MemRegion> regions(const Sim& s);
    static Sim make(Sim::Impl* i) { return Sim(i); }
    static void* unit_ptr(const Sim& s, const std::string& name, const TypeInfo* t) { return s.unit_ptr(name, t); }
};

}  // namespace sim::detail
