// checker.cpp (WORKER20, P2, rows B19/B20): the harness's side of the checker (src/check.h). Between cycles, the facts
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
    case Kind::FIFO_OVERFLOW: return "FIFO overflow " + fifo_names.at(size_t(a[0])) + " (invariant 1)";
    case Kind::DIR_UNEXPECTED:
        return fmt("unexpected arrival (a stray, at acceptance): WRITE_COUNTER_%" PRIu64 " has nothing armed for it (invariant 9; the error follows at its decrement, K = %u cycles later): ", a[0], sw_.K) + describe(a[1], a[2], a[3]);
    case Kind::DIR_UNCOUNTED_MODE:
        return fmt("uncounted mode-%u packet: modes 1-3 are always counted (3.1, invariant 22): ", unsigned(lw::mode(a[2]))) + describe(a[1], a[2], a[3]);
    case Kind::DIR_UNCOUNTED_REGION:
        return fmt("uncounted write to region 0x%" PRIx64 ": big-SRAM data and peripheral writes are always counted (3.1, 6.2; invariant 22): ", a[0]) + describe(a[1], a[2], a[3]);
    case Kind::DIR_UNCOUNTED_SRC:
        return fmt("uncounted write into controller memory from unit %" PRIu64 ": only commands and boot overlays, from C&C 1-3, are uncounted (3.1; invariant 22): ", a[0]) + describe(a[1], a[2], a[3]);
    case Kind::DIR_OVERLAY_AFTER_COMMAND:
        return "an overlay write landed after the unit's command and before its start (6.2: the overlays and the first command are of one kind, the command last): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_PAST_CMEM:
        return std::string("write past the end of ") + (a[0] == 0 ? "data" : "program") + " memory: the high offset bits are ignored and it aliases (2.2; invariant 19): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_RESERVED_BYTES: return "write into data memory's reserved first 16 bytes (2.2; invariant 22): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_COMMAND_BLOCK: return "write into the command block other than an uncounted one from C&C 1-3 inside it (4.12; invariant 11): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_REPLY_BLOCK:
        return "write into the reply block other than a 32- or 64-bit reply at the sender's own pair, REPLY + 8 x " + std::to_string(lw::src(a[2])) + " (4.12; invariant 11): " + describe(a[1], a[2], a[3]);
    case Kind::DIR_COMMAND_KIND:
        return std::string("a command (") + (a[0] == 2 ? "broadcast" : "unicast") + ") after overlays of the other kind: the mesh orders packets only within a kind (6.2; invariant 25): " + describe(a[1], a[2], a[3]);
    // ---- TileControl.violate (the register block; its store checks happen at the core's store in C++) ----
    case Kind::CTL_WFI_WOKEN:
        return "a WFI woken by an event other than GO with done set (wait_for_command()): only GO wakes the core between done and GO (the done gate, spec 4.12)";
    case Kind::CTL_RESERVED_BITS:
        return fmt("%s stored with reserved bits set (0x%" PRIx64 "): ignored, read 0 (8.1; invariant 21)", units::reg_info(unsigned(a[0])).name, a[1]);
    case Kind::CTL_RECORD_BASE_ODD: return fmt("DMA_RECORD_BASE_ADDRESS 0x%" PRIx64 " not aligned to a record (two chunks; 8.7; invariant 20)", a[0]);
    case Kind::CTL_RD_DEST_SIZE: return "SRAM_RD_DEST_ADDR with the reserved size 11 (8.8; invariant 21)";
    case Kind::CTL_RESERVED_ACCESS:
        return fmt("%s the reserved register-block word 0x%" PRIx64 ": reads 0, stores ignored (8.1)", a[0] ? "store to" : "load of", a[1]);
    case Kind::CTL_DONE_GO_MASKED:
        return "done pulsed (wait_for_command()) with GO clear in EVENTS_MASK: the GO it waits for would not be consumed (4.12)";
    case Kind::CTL_COUNTER_WRAP: return fmt("WRITE_COUNTER_%" PRIu64 " written past 2^20 - 1: the count wraps (4.12)", a[0]);
    case Kind::CTL_DONE_EARLY: {
        std::string b;
        for (unsigned c = 1; c <= 7; c++)
            if (a[0] >> c & 1) b += fmt(" WRITE_COUNTER_%u %u", c, unsigned(a[1 + (c - 1) / 3] >> (20 * ((c - 1) % 3)) & 0xFFFFF));
        return "done pulsed before every expected arrival landed (invariant 4; done early, ERRORS [8]):" + b;
    }
    // ---- the core's checks through the block (Controller.java: stopLoudly, mret, memWord, store; W26) ----
    case Kind::CTL_HALT: {
        static const char* const kHalt[9] = {"running", "illegal instruction", "ecall", "ebreak", "bus error", "SIM_EXIT",
                                             "fetch outside program memory and the boot ROM", "misaligned jump target", "a trap inside the handler"};
        const unsigned why = unsigned(a[0]);
        const std::string m = why == 6 ? fmt(", fetch at 0x%x", unsigned(a[1])) : why == 7 ? fmt(", to 0x%x", unsigned(a[1]))
                             : why == 8 ? fmt(", cause %d inside the handler", int(int32_t(a[1]))) : std::string();
        return fmt("tile %d: %s", f.tile, kHalt[why < 9 ? why : 0]) + m + ": a trap that sets no ERRORS bit; meshsim stops (4.13)";
    }
    case Kind::CTL_MRET: return fmt("tile %d: mret: every trap is terminal, the handler never returns (4.13)", f.tile);
    case Kind::CTL_ALIAS:
        return fmt("access at 0x%x, past the end of %s memory: the high offset bits are ignored and it aliases (2.2; invariant 19)",
                   unsigned(a[0]), lw::region(uint32_t(a[0])) == lw::R_DMEM ? "data" : "program");
    case Kind::CTL_REQ_FULL: return "SIMULATOR: a request pushed into a full request FIFO";
    case Kind::CTL_TABLE_BUSY: {
        static const char* const kTbl[4] = {"STREAM_SRC", "STREAM_DEST", "STREAM_COUNT", "GROUP"};
        if (a[0] == 5) return fmt("table window: HIST[%u] accessed while count-on-arrival is using HIST (4.10)", unsigned(a[1]));
        if (a[0] == 4) return fmt("table window: PTR[%u] accessed while the write engine is using PTR (4.10)", unsigned(a[1]));
        return fmt("table window: %s accessed while the address generator is running, its lead-in included (4.10)", kTbl[a[0] & 3]);
    }
    case Kind::CTL_DMEM_RESERVED: return fmt("core store into data memory's reserved first 16 bytes, at 0x%x (2.2; invariant 22; W4-02)", unsigned(a[0]));
    // ---- ControllerMemories (W26: invariant 8 strict, the command slot) ----
    case Kind::CMEM_REPLY_OVERWRITE:
        return fmt("a reply or read-port result overwrote an unread one at data word 0x%x (invariant 8, strict): ", unsigned(a[0])) + describe(a[1], a[2], 0);
    case Kind::CMEM_COMMAND_UNREAD: return "a command landed in command slot 0 before the core read the previous one (6.3): " + describe(a[1], a[2], a[3]);
    case Kind::WE_PAST_SRAM:
        return fmt("a big-SRAM write at chunk 0x%" PRIx64 ", at or past the end of big SRAM: undefined (4.1; invariant 19): ", a[0]) + describe(a[1], a[2], a[3]);
    case Kind::SLOT_VIOLATION: {                           // (W26) PeripheralSlot.violate's texts
        const unsigned n = unsigned(a[1]);
        switch (unsigned(a[0])) {
        case 1: return fmt("%s peripheral %u's reserved or undefined window word %u: reads 0, stores ignored (4.17.4)",
                           a[2] ? "store to" : "load of", n >> 8, n & 0xFF);
        case 2: return fmt("peripheral %u's DMA_HDR changed mid-transfer: undefined (4.17.9)", n);
        case 3: return fmt("peripheral %u's DMA_ADDR changed mid-transfer: undefined (4.17.9)", n);
        case 4: return fmt("peripheral %u: an event posted to bit %u at its full count %u (4.17.3) (invariant 16)", n, unsigned(a[2]), unsigned(a[3]));
        default: return a[2] == 1 ? std::string("a write into C&C 1's peripheral region: its map is Ingress/Egress's, deferred")
                                  : fmt("a write into a stub engine's region (type %u, WORKER14)", unsigned(a[2]));
        }
    }
    case Kind::TRACER_VIOLATION: {                         // (W26) the Tracer's text (Tracer.violate, W14's)
        char b[300];
        units::tracer::Tracer::violation_text(int(a[0]), a[1], a[2], b, sizeof b);
        return b;
    }
    case Kind::PERIPH_VIOLATION: {                         // (W20, F2) the replayer's text, without its list's "t<me> @<t>: "
        char b[300];
        units::Replayer::violation_text(unsigned(f.tile), int64_t(f.cycle), uint16_t(a[0]), a[1], a[2], a[3], b, sizeof b);
        const char* m = std::strstr(b, ": ");
        return m ? m + 2 : b;
    }
    case Kind::HIST_OVERFLOW: return "a counter exceeded 17 bits (invariant 6)";
    case Kind::ROTATION_LATE: return "address generator: a rotation started late (invariant 3; drift, see agReport)";
    case Kind::READ_RETURN_REFUSED: return "SIMULATOR: a read return was refused by the injection point";
    case Kind::CNC_RX_LOST: return "C&C's receive FIFO was full: a packet for C&C was lost (6.6)";
    default: break;
    }
    FATAL("checker: no text for fact kind %u yet (W20's step 2 adds them as the units record them)", unsigned(f.kind));
}

// ---- the report ------------------------------------------------------------------------------------------------------
Checker::Checker(uint32_t capacity, unsigned max_messages, const CheckSwitches& sw) : facts_(capacity), max_messages_(max_messages), sw_(sw) {
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
// ref's Config switches (the unit counted it; ref reports only when its switch is on)
bool Checker::enabled(Kind k) const {
    switch (k) {
    case Kind::DIR_READPORT_OVERLAP: return sw_.read_port_overlap;
    case Kind::DIR_UNEXPECTED: return sw_.unexpected;
    case Kind::CMEM_COMMAND_UNREAD: return sw_.command_read;
    case Kind::CMEM_REPLY_OVERWRITE: return sw_.strict_replies;
    default: return true;
    }
}

namespace {
const char* const kCondName[15] = {"router overflow", "bad packet", "unexpected arrival", "drift", "write-engine input overflow",
                                   "table request overflow", "GATHER queue overflow", "read-port error", "done early", "bus error",
                                   "illegal instruction", "peripheral 1", "peripheral 2", "peripheral 3", "receive underflow"};
std::string cond_names(uint32_t bits) {                 // Conditions.names
    std::string sb;
    for (unsigned i = 0; i < 15; i++) if (bits >> i & 1) sb += (sb.empty() ? "" : ", ") + std::string(kCondName[i]);
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
        // ref's checkInvariants walks the tiles, each tile's checks in turn (W26: tile-major within the per-tile slots)
        auto major = [](int r) { return r >= int(Slot::EJECT) && r <= int(Slot::REFUSAL) ? int(Slot::EJECT) : r; };
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
        const Fact& last = fs[j - 1];
        Unit* u = fs[i].tile >= 0 && fs[i].tile < 67 ? &u_[fs[i].tile] : nullptr;
        bool say = true;                                     // ref's once-only rules (Chip.checkInvariants)
        switch (sim::check::slot_of(last.kind)) {
        case Slot::HIST17: if (u) { say = !u->hist; u->hist = true; } break;
        case Slot::ROTATION: if (u) { say = !u->rotation; u->rotation = true; } break;
        case Slot::REFUSAL: if (u) { say = !u->refusal; u->refusal = true; } break;
        case Slot::DISJOINT: if (u) { say = u->disjoint_phase != int64_t(last.a[3]); u->disjoint_phase = int64_t(last.a[3]); } break;
        default: break;
        }
        if (say) report(last.kind == Kind::FIFO_OVERFLOW ? int64_t(last.cycle) : (t > now ? now : t), fs[i].tile, text(last));
        i = j;
    }
}

}  // namespace mach
