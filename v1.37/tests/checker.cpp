// checker.cpp: the harness's side of the checker (src/check.h). Between cycles, the facts
// the units recorded become ref's violation messages (Chip.violation's "cycle T[ tile I]: MSG"), in ref's report order,
// with ref's rules, counted as ref counts them (one per message, Chip.violations), the first max_violation_messages
// kept. Every text below is ref's, word for word (the Java it comes from is named at each).
#include "checker.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <cstdio>

#include "base/fatal.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/replayer.h"
#include "units/tracer.h"

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
    case Kind::FIFO_OVERFLOW: return "FIFO overflow " + fifo_names.at(size_t(a[0])) + " (invariant 1)";
    // ---- the tile control unit and the request unit ----
    case Kind::CTL_COUNTER_WRAP: return fmt("WRITE_COUNTER_%" PRIu64 " written past 2^20 - 1: the count wraps (4.12)", a[0]);
    case Kind::CTL_DONE_EARLY: {
        std::string b;
        for (unsigned c = 1; c <= 7; c++)
            if (a[0] >> c & 1) b += fmt(" WRITE_COUNTER_%u %u", c, unsigned(a[1 + (c - 1) / 3] >> (20 * ((c - 1) % 3)) & 0xFFFFF));
        return "done pulsed before every expected arrival landed (invariant 4; done early, ERRORS [8]):" + b;
    }
    case Kind::CTL_REQ_FULL: return "SIMULATOR: a request pushed into a full request FIFO";
    // ---- AddressGenerator.checkEntry ----
    case Kind::AG_SIZE_UNDEF: return "stream with read size 11, undefined (3.1, 4.2)";
    case Kind::AG_AUG128: return "mode-3 stream with read size 128: its packets are bad packets (3.1, 4.2)";
    case Kind::AG_DEST_ALIGN:
        return fmt("mode-0 stream whose destination address 0x%x is not aligned to its read size: its packets are bad packets (3.1, 4.2; invariant 20)", unsigned(a[0]));
    case Kind::AG_SRC_ALIGN:
        return fmt("stream whose source chunk 0x%x is not aligned to its read size: its reads are not performed (4.1, 4.2; invariant 20)", unsigned(a[0]));
    case Kind::AG_DEST_ENDPOINT: return fmt("stream with d = %u (4.2: 67-125 name no endpoint, 127 is invalid; invariant 17)", unsigned(a[0]));
    case Kind::AG_BROADCAST_ON_TILE:
        return fmt("stream with d = 126 (the broadcast) on mesh tile %u (4.2, D41 F14: only the controllers broadcast from streams; invariant 17)", unsigned(a[0]));
    case Kind::AG_GATHER_DEST: return fmt("mode-4 stream with d = %u (4.2: a gather stream goes to the tile itself, d = 0; invariant 10)", unsigned(a[0]));
    case Kind::AG_COUNT_CTR0: return "count-only stream on counter 0: it does nothing (3.1; invariant 21)";
    // ---- ReadPort.store ----
    case Kind::RP_SIZE_UNDEF: return "read-port read with the reserved size 11 (8.8): undefined";
    case Kind::RP_QUEUE_FULL:
        return fmt("SRAM_RD_ADDR_QUEUE store with no free entry: read of 0x%x dropped, ERRORS [7] (4.3; invariant 8)", unsigned(a[0]));
    case Kind::RP_DEST_ALIGN:
        return fmt("read-port destination 0x%x not aligned to its size: its result is a bad packet (4.3; invariant 20)", unsigned(a[0]));
    case Kind::WE_PAST_SRAM:
        return fmt("a big-SRAM write at chunk 0x%" PRIx64 ", at or past the end of big SRAM: undefined (4.1; invariant 19): ", a[0]) + describe(a[1], a[2], a[3]);
    case Kind::SLOT_VIOLATION: {                           // PeripheralSlot.violate's texts
        const unsigned n = unsigned(a[1]);
        switch (unsigned(a[0])) {
        case 1: return fmt("%s peripheral %u's reserved or undefined window word %u: reads 0, stores ignored (4.17.4)",
                           a[2] ? "store to" : "load of", n >> 8, n & 0xFF);
        case 2: return fmt("peripheral %u's DMA_HDR changed mid-transfer: undefined (4.17.9)", n);
        case 3: return fmt("peripheral %u's DMA_ADDR changed mid-transfer: undefined (4.17.9)", n);
        case 4: return fmt("peripheral %u: an event posted to bit %u at its full count %u (4.17.3) (invariant 16)", n, unsigned(a[2]), unsigned(a[3]));
        default: return a[2] == 1 ? std::string("a write into C&C 1's peripheral region: its map is Ingress/Egress's, deferred")
                                  : fmt("a write into a stub engine's region (type %u)", unsigned(a[2]));
        }
    }
    case Kind::TRACER_VIOLATION: {                         // the Tracer's text (Tracer.violate, the)
        char b[300];
        units::tracer::Tracer::violation_text(int(a[0]), a[1], a[2], b, sizeof b);
        return b;
    }
    case Kind::PERIPH_VIOLATION: {                         // the replayer's text, without its list's "t<me> @<t>: "
        char b[300];
        units::Replayer::violation_text(unsigned(f.tile), int64_t(f.cycle), uint16_t(a[0]), a[1], a[2], a[3], b, sizeof b);
        const char* m = std::strstr(b, ": ");
        return m ? m + 2 : b;
    }
    case Kind::DISJOINT:                                   // BigSRAM.mark's text, the phase's first
        return fmt("chunk 0x%x %s after being %s in the same phase (invariant 2, phase %u)", unsigned(a[0]), a[1] ? "written" : "read",
                   a[2] ? "written" : "read", unsigned(a[3]));
    case Kind::S1_LATE:                                    // S1.lastLate (BigSRAM.s1Ordered, TableStore's write-backs)
        if (a[0] == 0)
            return fmt("tile %u: big SRAM chunk 0x%x in its area's queue %" PRIu64 " cycles after acceptance, past K + 4 = %u (invariant 24)",
                       unsigned(a[1]), unsigned(a[2]), a[3], sw_.K + 4);
        return fmt("tile %u: %s[%u] complete %" PRIu64 " cycles after acceptance, past K = %u (invariant 24)", unsigned(a[1]),
                   a[0] == 1 ? "PTR" : "HIST", unsigned(a[2]), a[3], sw_.K);
    case Kind::HIST_OVERFLOW: return "a counter exceeded 17 bits (invariant 6)";
    // ---- the request bus (RequestBus.runCycle), the endpoints (Endpoint.send) ----
    case Kind::REQBUS_QUEUE_OVERFLOW: return fmt("SIMULATOR: target %u's receive queue overflowed (the root's credits should prevent it)", unsigned(a[0]));
    case Kind::REQBUS_NO_RECEIVER:
        return fmt("a request from unit %u for target %u%s dropped at the root (3.3, 6.7)", unsigned(a[0]), unsigned(a[1]),
                   a[1] >= 4 ? " (reserved)" : " (no receiver: deferred with the file system, item 119)");
    case Kind::EP_BAD_PACKET: return "bad packet (" + why_bad(a[0]) + ") sent by " + std::to_string(a[1]) + " (3.1, invariant 5)";
    case Kind::EP_SIZE_UNDEF: return fmt("packet with size 11, undefined, sent by %u (3.1, invariant 5)", unsigned(a[1]));
    case Kind::EP_RESERVED_MODE: return fmt("packet in the reserved mode %u sent by %u (3.1, invariant 21)", unsigned(a[0]), unsigned(a[1]));
    case Kind::CNC_RX_LOST: return "C&C's receive FIFO was full: a packet for C&C was lost (6.6)";
    default: break;
    }
    FATAL("checker: no text for fact kind %u yet", unsigned(f.kind));
}

// ---- the report ------------------------------------------------------------------------------------------------------
Checker::Checker(uint32_t capacity, unsigned max_messages, const CheckSwitches& sw) : facts_(capacity), max_messages_(max_messages), sw_(sw) {
    sink_.f = facts_.data();
    sink_.cap = capacity;
}

void Checker::install() {
    // drop the facts of kinds this checker's switches hold off (enabled() is false for them, so none is ever judged)
    for (uint64_t& w : sink_.off) w = 0;
    for (Kind k : {Kind::DIR_READPORT_OVERLAP, Kind::DISJOINT})
        if (!enabled(k)) sink_.off[unsigned(k) >> 6] |= uint64_t(1) << (unsigned(k) & 63);
    sim::check::sink = sim::check::kOn ? &sink_ : nullptr;
}
void Checker::uninstall() { if (sim::check::sink == &sink_) sim::check::sink = nullptr; }

void Checker::report(int64_t t, int tile, const std::string& m) {   // Chip.violation
    violations_++;
    if (messages_.size() < max_messages_) messages_.push_back("cycle " + std::to_string(t) + (tile >= 0 ? " tile " + std::to_string(tile) : "") + ": " + m);
}

namespace {
// ref's order within a cycle: the slot; the cores' facts (tile 64-66) in CORES, by unit, then by their own check order
int rank(const Fact& f) { return f.tile >= 64 && sim::check::slot_of(f.kind) >= Slot::INJ && sim::check::slot_of(f.kind) <= Slot::WRITER
                                     ? int(Slot::CORES) : int(sim::check::slot_of(f.kind)); }
}  // namespace

// The facts recorded since the last call, through the cycle just run (`now`): ref's checkInvariants of each cycle in
// turn, in its order. `scan` (ref's explicit scanOverflows, at the end of a run): the overflows not yet reported.
// ref's Config switches (the unit counted it; ref reports only when its switch is on)
bool Checker::enabled(Kind k) const {
    switch (k) {
    case Kind::DIR_READPORT_OVERLAP: return sw_.read_port_overlap;
    case Kind::DISJOINT: return sw_.disjoint;
    default: return true;
    }
}

namespace {
const char* const kCondName[13] = {"router overflow", "bad packet", "counter underflow", "drift", "gather overflow", "read overflow",
                                   "early arrival", "access fault", "illegal instruction", "receive underflow", "peripheral 1",
                                   "peripheral 2", "peripheral 3"};   // machine-spec 7.4, bit by bit
std::string cond_names(uint32_t bits) {                 // Conditions.names
    std::string sb;
    for (unsigned i = 0; i < 13; i++) if (bits >> i & 1) sb += (sb.empty() ? "" : ", ") + std::string(kCondName[i]);
    return sb.empty() ? "none" : sb;
}
std::string binary(uint32_t v) {                         // Integer.toBinaryString
    std::string s;
    do { s.insert(s.begin(), char('0' + (v & 1))); v >>= 1; } while (v);
    return s;
}
}  // namespace

// Chip.runCycle, invariant 15: while any tile's error line is high, each tile's ERRORS bits not yet seen are reported
// (and remembered); when every line is low, the remembered bits are forgotten.
void Checker::errors_at(int64_t t) {
    bool err = false;
    for (unsigned k = 0; k < 64; k++) err = err || u_[k].line;
    if (!err) { for (unsigned k = 0; k < 64; k++) u_[k].cond_seen = 0; return; }
    for (unsigned k = 0; k < 64; k++) {
        const uint32_t nw = u_[k].errors & ~u_[k].cond_seen;
        if (!nw) continue;
        u_[k].cond_seen |= nw;
        if (sw_.errors) report(t, int(k), "ERRORS [" + binary(nw) + "] set: " + cond_names(nw) + " (invariant 15)");
    }
}

// The facts recorded since the last call, through the cycle just run (`now`): ref's report of each cycle in turn, in
// its order. `scan` (ref's explicit scanOverflows, at the end of a run): the overflows not yet reported.
void Checker::process(int64_t now, bool scan, bool final) {
    const uint32_t n = std::min(sink_.n.load(std::memory_order_relaxed), sink_.cap);
    std::vector<Fact> fs;
    fs.swap(pending_);
    for (uint32_t i = done_; i < n; i++) if (enabled(facts_[i].kind)) fs.push_back(facts_[i]);
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
        // ref's checkInvariants walks the tiles, each tile's checks in turn (tile-major within the per-tile slots)
        auto major = [](int r) { return r >= int(Slot::INJ) && r <= int(Slot::WRITER) ? int(Slot::INJ) : r; };
        const int ra = rank(a), rb = rank(b);
        if (major(ra) != major(rb)) return major(ra) < major(rb);
        if (a.kind == Kind::FIFO_OVERFLOW && b.kind == Kind::FIFO_OVERFLOW) return a.a[0] < b.a[0];   // ref's scan order
        if (a.tile != b.tile) return a.tile < b.tile;          // (the tiles', then C&C's FIFOs)
        if (ra != rb) return ra < rb;
        return false;                                        // recording order kept within (cycle, slot, tile)
    });
    for (size_t i = 0; i < fs.size();) {
        const int64_t t = at(fs[i]);
        if ((t > now && !scan) || (t == now && !final && fs[i].kind != Kind::FIFO_OVERFLOW)) {   // a later call's
            pending_.insert(pending_.end(), fs.begin() + long(i), fs.end());
            break;
        }
        if (rank(fs[i]) == int(Slot::ERRORS)) {              // the cycle's ERRORS facts together, then ref's loop
            size_t j = i;
            for (; j < fs.size() && at(fs[j]) == t && rank(fs[j]) == int(Slot::ERRORS); j++) {
                const Fact& f = fs[j];
                if (f.tile < 0 || f.tile >= 64) continue;   // the cores' ERRORS: CcCores.check (with the cores' slots)
                if (f.kind == Kind::ERRORS_SET) u_[f.tile].errors = uint32_t(f.a[0]);
                else u_[f.tile].line = f.a[0] != 0;
            }
            errors_at(t);
            i = j;
            continue;
        }
        size_t j = i + 1;                                    // a group: one cycle, one slot, one tile (each overflow alone)
        while (j < fs.size() && fs[i].kind != Kind::FIFO_OVERFLOW && at(fs[j]) == t && rank(fs[j]) == rank(fs[i]) && fs[j].tile == fs[i].tile) j++;
        const Fact* lastp = &fs[j - 1];
        if (rank(*lastp) == int(Slot::S1_LATE)) {           // ref's s1.lastLate is the last in its tiles' child order
            for (size_t k = i; k < j; k++)                    // (tile by tile; in a tile big SRAM, then PTR, then HIST)
                if (fs[k].a[1] > lastp->a[1] || (fs[k].a[1] == lastp->a[1] && fs[k].a[0] >= lastp->a[0])) lastp = &fs[k];
        }
        if (rank(*lastp) == int(Slot::ENDPOINT)) {          // ref's text is the last of the highest endpoint that ever had one
            for (size_t k = i; k < j; k++) { const unsigned e = unsigned(fs[k].a[1]) - 64; if (e < 3) { ep_last_[e] = fs[k]; ep_has_[e] = true; } }
            for (unsigned e = 0; e < 3; e++) if (ep_has_[e]) lastp = &ep_last_[e];
        }
        const Fact& last = *lastp;
        Unit* u = fs[i].tile >= 0 && fs[i].tile < 67 ? &u_[fs[i].tile] : nullptr;
        bool say = true;                                     // ref's once-only rules (Chip.checkInvariants)
        switch (sim::check::slot_of(last.kind)) {
        case Slot::HIST17: if (u) { say = !u->hist; u->hist = true; } break;
        case Slot::DISJOINT: if (u) { say = u->disjoint_phase != int64_t(last.a[3]); u->disjoint_phase = int64_t(last.a[3]); } break;
        default: break;
        }
        if (say) report(last.kind == Kind::FIFO_OVERFLOW ? int64_t(last.cycle) : (t > now ? now : t), fs[i].tile, text(last));
        i = j;
    }
}

}  // namespace mach
