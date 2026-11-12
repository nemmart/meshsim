// checker.cpp (WORKER20, P2, rows B19/B20): the harness's side of the checker (src/check.h). Between cycles, the facts
// the units recorded become ref's violation messages (Chip.violation's "cycle T[ tile I]: MSG"), in ref's report order,
// with ref's rules, counted as ref counts them (one per message, Chip.violations), the first max_violation_messages
// kept. Every text below is ref's, word for word (the Java it comes from is named at each).
#include "checker.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#include "base/fatal.h"
#include "units/linkword.h"

namespace mach {

using sim::check::Fact;
using sim::check::Kind;
using sim::check::Slot;
namespace lw = units::lw;

namespace {

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

const char* const kModeName[8] = {"write@addr", "write@PTR[src]", "bucket", "augmented", "gather", "count-only", "reserved6", "reserved7"};
const char* const kSizeName[4] = {"32", "64", "128", "11(undefined)"};

}  // namespace

// LinkWord.describe(payload, h, hi)
std::string describe(uint64_t d, uint64_t h, uint64_t hi) {
    const unsigned s = lw::size(h);
    const std::string p = s == lw::SIZE_32 ? fmt("0x%08" PRIx64, uint64_t(d & 0xFFFFFFFFull))
                        : s == lw::SIZE_64 ? fmt("0x%016" PRIx64, d)
                                           : fmt("0x%016" PRIx64 "_%016" PRIx64, hi, d);
    return fmt("src=%u dst=%u mode=%u(%s) ctr=%u addr=0x%08x size=%s payload=", unsigned(lw::src(h)), unsigned(lw::dst(h)),
               unsigned(lw::mode(h)), kModeName[lw::mode(h) & 7], unsigned(lw::counter(h)), unsigned(lw::addr(h)), kSizeName[s]) + p;
}

// LinkWord.whyBad (the reason a bad packet is bad; from the header alone, as ref's)
std::string why_bad(uint64_t h) {
    const unsigned m = lw::mode(h), s = lw::size(h);
    if (m == lw::M_AUG && s == lw::SIZE_128) return "mode 3 at 128 bits";
    return fmt("region 0x%x not reachable by a packet", unsigned(lw::region(lw::addr(h))));
}

// One fact's text (the unit's lastViolation, or Chip's own message).
std::string Checker::text(const Fact& f) const {
    const uint64_t* a = f.a;
    switch (f.kind) {
    // ---- the router (Chip.checkInvariants; CcCores.check says "unit") ----
    case Kind::EJECT_WRONG_TILE: return f.tile >= 64 ? "packet ejected at the wrong unit" : "packet ejected at the wrong tile";
    case Kind::ILLEGAL_FORWARD: return "illegal forward in a route block";
    // ---- InjectionPath.violate ----
    case Kind::INJ_BAD_PACKET: return "bad packet sent (" + why_bad(a[2]) + "; 3.1, invariant 5): " + describe(a[1], a[2], a[3]);
    case Kind::INJ_SIZE_UNDEF: return "packet with size 11, undefined, sent (3.1, invariant 5): " + describe(a[1], a[2], a[3]);
    case Kind::INJ_NO_ENDPOINT:
        return fmt("a packet sent to dst %u, which has no endpoint (2.1: 67-125 reserved; 127 is not a destination; invariant 17): ",
                   unsigned(lw::dst(a[2]))) + describe(a[1], a[2], a[3]);
    case Kind::INJ_GATHER_ELSEWHERE: return "mode-4 packet to another unit (invariant 10): " + describe(a[1], a[2], 0);
    case Kind::INJ_RESERVED_MODE:
        return fmt("packet in the reserved mode %u sent (3.1, invariant 21): ", unsigned(lw::mode(a[2]))) + describe(a[1], a[2], a[3]);
    case Kind::INJ_COUNT_CTR0: return "count-only packet on counter 0 sent: it does nothing (3.1, invariant 21): " + describe(a[1], a[2], a[3]);
    // ---- TrafficDirector.violate ----
    case Kind::DIR_READPORT_OVERLAP:
        return fmt("packet arrived with %" PRIu64 " read-port result(s) outstanding (spec 4.3's rule, invariant 14; read-port overlap): ", a[0]) +
               describe(a[1], a[2], a[3]);
    case Kind::DIR_BAD_PACKET:
        return "bad packet dropped, uncounted, ERRORS [1] (" + why_bad(a[2]) + "; 3.1, invariant 5): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_SIZE_UNDEF: return "packet with size 11 (undefined, 3.1; invariant 5) arrived and was dropped: " + describe(a[1], a[2], a[3]);
    case Kind::DIR_COUNT_CTR0: return "count-only packet on counter 0: it does nothing (3.1, invariant 21): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_PAST_SRAM:
        return fmt("mode-0 write at or past the end of big SRAM (chunk 0x%" PRIx64 "): undefined (2.2; invariant 19): ", a[0]) + describe(a[1], a[2], a[3]);
    default: break;
    }
    FATAL("checker: no text for fact kind %u yet (W20's step 2 adds them as the units record them)", unsigned(f.kind));
}

// ---- the report ------------------------------------------------------------------------------------------------------
Checker::Checker(uint32_t capacity, unsigned max_messages) : facts_(capacity), max_messages_(max_messages) {
    sink_.f = facts_.data();
    sink_.cap = capacity;
}

void Checker::install() { sim::check::sink = sim::check::kOn ? &sink_ : nullptr; }
void Checker::uninstall() { if (sim::check::sink == &sink_) sim::check::sink = nullptr; }

void Checker::report(int64_t t, int tile, const std::string& m) {   // Chip.violation
    violations_++;
    if (messages_.size() < max_messages_) messages_.push_back("cycle " + std::to_string(t) + (tile >= 0 ? " tile " + std::to_string(tile) : "") + ": " + m);
}

namespace {
// ref's order within a cycle: the slot; the cores' facts (tile 64-66) in CORES, by unit, then by their own check order
int rank(const Fact& f) { return f.tile >= 64 && sim::check::slot_of(f.kind) >= Slot::EJECT && sim::check::slot_of(f.kind) <= Slot::REFUSAL
                                     ? int(Slot::CORES) : int(sim::check::slot_of(f.kind)); }
}  // namespace

// The facts recorded since the last call, through the cycle just run (`now`): ref's checkInvariants of each cycle in
// turn, in its order. `scan` (ref's explicit scanOverflows, at the end of a run): the overflows not yet reported.
void Checker::process(int64_t now, bool scan) {
    const uint32_t n = std::min(sink_.n.load(std::memory_order_relaxed), sink_.cap);
    std::vector<Fact> fs;
    fs.swap(pending_);
    for (uint32_t i = done_; i < n; i++) fs.push_back(facts_[i]);
    done_ = n;
    // ref's report cycle of a fact: its own, except an overflow's: the 16-cycle scan that sees it, else the explicit
    // scan at the end of a run (`scan`), else a later call's
    auto at = [&](const Fact& f) -> int64_t {
        if (f.kind != Kind::FIFO_OVERFLOW) return int64_t(f.cycle);
        const int64_t s = (int64_t(f.cycle) + 15) & ~int64_t(15);
        return s <= now ? s : now + 1;
    };
    std::stable_sort(fs.begin(), fs.end(), [&](const Fact& a, const Fact& b) {
        const int64_t ta = at(a), tb = at(b);
        if (ta != tb) return ta < tb;
        const int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return a.tile < b.tile;                              // recording order kept within (cycle, slot, tile)
    });
    for (size_t i = 0; i < fs.size();) {
        size_t j = i + 1;                                    // a group: one cycle, one slot, one tile
        while (j < fs.size() && at(fs[j]) == at(fs[i]) && rank(fs[j]) == rank(fs[i]) && fs[j].tile == fs[i].tile) j++;
        const int64_t t = at(fs[i]);
        if (t > now && !scan) { pending_.insert(pending_.end(), fs.begin() + long(i), fs.begin() + long(j)); i = j; continue; }
        // ref: one message per changed counter per cycle, the unit's last text (the special slots come with their units)
        report(fs[i].kind == Kind::FIFO_OVERFLOW ? int64_t(fs[i].cycle) : t, fs[i].tile, text(fs[j - 1]));
        i = j;
    }
}

}  // namespace mach
