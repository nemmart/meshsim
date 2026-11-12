// checker.h (WORKER20, P2): the harness's side of the checker (src/check.h; checker.cpp). Used by mach::Chip.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "check.h"

namespace mach {

std::string describe(uint64_t d, uint64_t h, uint64_t hi);   // ref's LinkWord.describe
std::string why_bad(uint64_t h);                              // ref's LinkWord.whyBad

class Checker {
public:
    Checker(uint32_t capacity, unsigned max_messages);
    void install();                     // this machine's sink is the units' (at every run: one machine runs at a time)
    void uninstall();
    void process(int64_t now, bool scan);   // the facts so far into messages (ref's checkInvariants, cycle by cycle)
    uint64_t violations() const { return violations_; }
    const std::vector<std::string>& messages() const { return messages_; }
    void clear_messages() { messages_.clear(); }
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
};

}  // namespace mach
