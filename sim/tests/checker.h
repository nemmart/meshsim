// checker.h (WORKER20, P2): the harness's side of the checker (src/check.h; checker.cpp). Used by mach::Chip.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "check.h"

namespace mach {

std::string describe(uint64_t d, uint64_t h, uint64_t hi);   // ref's LinkWord.describe
std::string why_bad(uint64_t h);                              // ref's LinkWord.whyBad

// ref's Config switches that decide whether a violation is reported (the units count either way)
struct CheckSwitches {
    bool errors = true, unexpected = true, read_port_overlap = true, command_read = true, strict_replies = false;
    bool disjoint = true;               // W26: Config.checkDisjoint (invariant 2; the harness sets it from MachConfig)
    unsigned K = 8;                     // the director's delay line (DIR_UNEXPECTED's text)
};

class Checker {
public:
    Checker(uint32_t capacity, unsigned max_messages, const CheckSwitches& sw = {});
    void install();                     // this machine's sink is the units' (at every run: one machine runs at a time)
    void uninstall();
    // The facts so far into messages (ref's report, cycle by cycle), through cycle `now` - 1: the cycle just run (`now`)
    // waits for the units whose step at now + 1 is ref's end of now, unless `final` (the harness adds those first).
    void process(int64_t now, bool scan, bool final = false);
    void add(const sim::check::Fact& f) { pending_.push_back(f); }   // a fact the harness made (`final`'s, overflows)
    std::vector<std::string> fifo_names;      // FIFO_OVERFLOW's a0 indexes ref's names (the harness fills it)
    uint64_t violations() const { return violations_; }
    const std::vector<std::string>& messages() const { return messages_; }
    void clear_messages() { messages_.clear(); }
    void clear_count() { violations_ = 0; }
    // ref's Chip.doReset: every tile's error line seen as low again (errSeen); the ERRORS seen (condSeen) are kept
    void on_reset() { for (Unit& u : u_) { u.line = false; u.errors = 0; } }
    CheckSwitches& switches() { sink_.off[0] = sink_.off[1] = sink_.off[2] = sink_.off[3] = 0; return sw_; }   // a switch may change: keep every kind until the next install
    void set_max_messages(unsigned n) { max_messages_ = n; }
    uint32_t facts() const { return sink_.n.load(); }

private:
    std::string text(const sim::check::Fact& f) const;
    void report(int64_t t, int tile, const std::string& m);
    std::vector<sim::check::Fact> facts_;
    std::vector<sim::check::Fact> pending_;   // overflows waiting for the scan that sees them
    sim::check::Sink sink_;
    uint32_t done_ = 0;
    unsigned max_messages_;
    uint64_t violations_ = 0;
    std::vector<std::string> messages_;
    CheckSwitches sw_;
    // ref's per-tile report state (Chip.seen, s2, condSeen, refusalReported; 67 units)
    struct Unit { uint32_t errors = 0, cond_seen = 0; bool line = false, hist = false, refusal = false; int64_t disjoint_phase = -1, rotation_run = -1; };
    Unit u_[67];
    sim::check::Fact ep_last_[3] = {};  // W26: each endpoint's last violation (Chip.checkInvariants' `el`)
    bool ep_has_[3] = {};
    void errors_at(int64_t t);          // invariant 15 at the end of cycle t (Chip.runCycle's loop)
    bool enabled(sim::check::Kind k) const;
};

}  // namespace mach
