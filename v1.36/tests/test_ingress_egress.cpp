// test_ingress_egress: Ingress/Egress, the master tile's peripheral 1 (machine-spec 17.6), from the specification
// (the RTL's rtl/tile/base/spec.md: 17.1, 17.2, 17.6, 18, 5.3, 5.4) and, where it is silent, the RTL's
// rtl/blocks/ingress_egress_unit (README.md and src/ingress_egress_unit.veryl, src/uart.veryl) and register_bus.veryl.
// Every expectation is written here from those texts, never taken from the simulator.
//
// THE BENCH. Ingress/Egress sits in the master tile's slot (PeriphSlot, tile 64, no DMA port), between two stands:
//   Core   the core and register_bus: window stores and consuming EVENTS loads on the slot's `core` port, loads from
//          the window RegFile. In the RTL's terms (the unit's README, Timing): a STORE at cycle p is a store PRESENTED
//          in p, in effect at the end of p (17.6's "the end of t + 1 for a store issued in t", p = t + 1); a LOAD at
//          cycle u is a load presented in u, which reads the state at the end of u - 1.
//   Nets   the chip's nets (18), on the peripheral's own ports as ingress_egress.h defines them (THE NETS):
//          ARRIVE c, tile i   compute tile i's BARRIER_ARRIVE reaches Ingress/Egress in cycle c (barrier_arrive[i] is 1
//                             in c): the row's wire carries the bit in c + 1, the cycle whose loads first see it;
//          LINE c, tile i     tile i's error line is at `level` from cycle c on, as a load presented in c reads it
//                             (error_lines is read combinationally): a compute tile's on its row's wire in c, the
//                             memory and tracer tiles' (ids 65, 66) put on their links in c - 1;
//          OWN c              the master tile's own error line (error_line_own) is at `level` from cycle c on: put on
//                             its link in c;
//          release[k]         BARRIER_RELEASE to compute tile k, over a link of RELEASE_LATENCY (1 here, the chip's):
//                             the flip-flop's cycle is the first of its cycles, so a pulse of cycle f arrives in
//                             f + RELEASE_LATENCY.
// Each script runs plain, with step-all, and shuffled.
//
// THE STAND-IN (ingress_egress.h, WHAT IS NOT THE RTL'S YET; PENDING-v1.36.md): the UART's and the host interface's
// receive sides have no source, so they read empty, which is also what the hardware reads while nothing arrives; a
// store to the host interface's transmit words stops the simulator, which is the model's stand-in and NOT the
// hardware's (17.6: the beat goes into the transmit FIFO): the DEATH_TESTs at the end say so.
//
// NOT CHECKED HERE: the UART's line (uart_tx's waveform) and its receiver, and the host interface's FIFOs and ports:
// the model has none of them. An error line's cycle through the chip's wiring (a cycle later than ERROR_LATENCY says:
// PENDING-v1.36.md's row "the nets") is the chip's, not this unit's: here a level is read in the cycle it is driven.
#include <cstdint>
#include <cstdio>

#include "harness.h"
#include "config.h"
#include "sim.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/register_bus.h"

using namespace units;

namespace {

// ---------------------------------------------------------------- 17.1's and 17.6's window words, from their tables
enum : unsigned {
    W_PARAMS = 0, W_STATUS = 1, W_ERRORS = 2, W_EVENTS = 3, W_EVENTS_PEEK = 4, W_EVENTS_MASK = 5, W_START = 6,
    W_DMA_HDR = 16, W_DMA_DEST_ADDR = 17,
    W_ERROR_LINES_0 = 32, W_ERROR_LINES_1 = 33, W_ERROR_LINES_2 = 34, W_ARRIVALS_0 = 35, W_ARRIVALS_1 = 36,
    W_ARRIVE_EXPECT_0 = 37, W_ARRIVE_EXPECT_1 = 38, W_RELEASE = 39, W_WATCHDOG = 40, W_LED = 41, W_UART_TX = 42,
    W_UART_RX = 43, W_UART_DIVISOR = 44, W_UART_STATUS = 45,
    W_STREAM_OUT_W0 = 64, W_STREAM_SEND = 67, W_STREAM_SEND_LAST = 68, W_STREAM_IN_W0 = 69, W_STREAM_RECEIVE = 72,
};
// EVENTS (17.6): [2] all_arrived, [3] watchdog, [4] uart_rx, [5] uart_tx, [6] stream_in, [7] stream_out
constexpr uint32_t EV_ALL_ARRIVED = 1u << 2, EV_WATCHDOG = 1u << 3, EV_UART_TX = 1u << 5, EV_STREAM_OUT = 1u << 7;
constexpr uint32_t EV_IDLE = EV_UART_TX | EV_STREAM_OUT;   // "uart_tx and stream_out read 1 while it is idle" (17.6)
constexpr uint32_t RESET_DIVISOR = 8681;                   // UART_RESET_DIVISOR (19): ingress_egress_unit.veryl's default
constexpr unsigned RELEASE_LATENCY = 1;                    // the bench's link (the chip's: net.h, release_latency)

// the window RegFile's register of a window word (periph_slot.h's Win), peripheral 1
unsigned reg(unsigned w) { return Win::reg(1, w < 32 ? Win::R_STD + Win::std(w) : Win::R_OWN + (w - 32)); }
// the tile's live bits (17.2): [0] EVENTS.peripheral_1, [3] ERRORS.peripheral_1
constexpr unsigned LIVE = Win::R_LIVE;
constexpr uint32_t LIVE_EVENT = 1u << 0, LIVE_ERROR = 1u << 3;

// ---------------------------------------------------------------- the stands
struct Core {
    // LOAD and EVENTS carry the value the specification says they return (b); the test compares after the run.
    enum : uint8_t { STORE, LOAD, EVENTS };
    struct Ev { uint64_t t; uint8_t kind; uint32_t a, b; };
    sim::LinkOut core, region;
    sim::RegFileIn window;
    const Ev* ev = nullptr;
    unsigned n = 0, i = 0;
    static constexpr unsigned MAX = 128;
    uint32_t got[MAX] = {};
    unsigned n_got = 0;

    Core(sim::Ports& p, const Ev* e, unsigned count) : ev(e), n(count) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
    }
    sim::Wake step(sim::Ctx& c) {
        for (; i < n && ev[i].t == c.now(); i++) {
            const Ev& e = ev[i];
            switch (e.kind) {
                case STORE: c.put(core, CoreOp::store(1, e.a, e.b)); break;
                case LOAD: if (n_got < MAX) got[n_got] = c.read(window, e.a); n_got++; break;
                case EVENTS: {   // a consuming load of EVENTS: it consumes what it returned under EVENTS_MASK (7.2)
                    const uint32_t v = c.read(window, reg(W_EVENTS)), m = c.read(window, reg(W_EVENTS_MASK));
                    if (n_got < MAX) got[n_got] = v;
                    n_got++;
                    if (v & m) c.put(core, CoreOp::consume(1, v & m));
                    break;
                }
            }
        }
        return i < n ? sim::Wake::at_cycle(ev[i].t) : sim::Wake::none();
    }
    uint64_t digest() const { return i * 131 + n_got; }
};

struct Nets {
    enum : uint8_t { ARRIVE, LINE, OWN };
    struct Ev { uint64_t c; uint8_t kind; uint32_t tile, level; };
    sim::WireOut row[8];
    sim::LinkOut line[3];
    sim::LinkIn release[64];
    const Ev* ev = nullptr;
    unsigned n = 0;
    uint64_t last = 0;                 // the last cycle it steps
    uint8_t levels[8] = {};
    uint64_t release_at[64] = {}, releases[64] = {};

    Nets(sim::Ports& p, const Ev* e, unsigned count, uint64_t last_cycle) : ev(e), n(count), last(last_cycle) {
        for (unsigned r = 0; r < 8; r++) p.out(row[r], "row");
        for (unsigned s = 0; s < 3; s++) p.out(line[s], "line", 1);
        for (unsigned k = 0; k < 64; k++) p.in(release[k], "release", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        for (unsigned k = 0; k < 64; k++)
            if (c.ready(release[k])) { c.take(release[k]); release_at[k] = t; releases[k]++; }
        uint8_t pulses[8] = {};
        for (unsigned k = 0; k < n; k++) {
            const Ev& e = ev[k];
            if (e.kind == ARRIVE && e.c + 1 == t) pulses[e.tile / 8] |= uint8_t(1u << (e.tile % 8));
            if (e.kind == LINE && e.tile < 64 && e.c == t)
                levels[e.tile / 8] = uint8_t((levels[e.tile / 8] & ~(1u << (e.tile % 8))) | e.level << (e.tile % 8));
            if (e.kind == LINE && e.tile > 64 && e.c == t + 1) c.put(line[e.tile - 64], sim::Word(e.level));
            if (e.kind == OWN && e.c == t) c.put(line[0], sim::Word(e.level));
        }
        for (unsigned r = 0; r < 8; r++) c.drive(row[r], sim::Word(pulses[r]) | sim::Word(levels[r]) << 8);
        return t < last ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { uint64_t h = 0; for (unsigned r = 0; r < 8; r++) h = h * 257 + levels[r]; return h; }
};

struct Rig {
    sim::Sim m;
    const Core::Ev* cev;
    unsigned cn;
    Rig(const Core::Ev* ce, unsigned n_core, const Nets::Ev* ne, unsigned n_nets, uint64_t last, const sim::Options& o)
        : m(build(ce, n_core, ne, n_nets, last, o)), cev(ce), cn(n_core) {}
    static sim::Sim build(const Core::Ev* ce, unsigned n_core, const Nets::Ev* ne, unsigned n_nets, uint64_t last, const sim::Options& o) {
        sim::Config cfg(o);
        auto& nets = cfg.add<Nets>("nets", ne, n_nets, last);
        auto& core = cfg.add<Core>("core", ce, n_core);
        PeriphSlotConfig pc;
        pc.tile = 64; pc.type[1] = PT_INGRESS;       // the master tile: Ingress/Egress, no DMA (17.6)
        auto& slot = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(core.core, slot.core, 1);
        cfg.link(core.region, slot.region, 3);
        cfg.connect(slot.window, core.window);
        const IngressEgress& ie = slot.s.ingress_egress(1);
        for (unsigned r = 0; r < 8; r++) cfg.connect(nets.row[r], slot.own.wire_in[ie.row0 + r]);
        for (unsigned s = 0; s < 3; s++) cfg.link(nets.line[s], slot.own.link_in[ie.line0 + s], 1);
        for (unsigned k = 0; k < 64; k++) cfg.link(slot.own.link_out[ie.release0 + k], nets.release[k], RELEASE_LATENCY);
        return cfg.build();
    }
    Core& core() { return m.unit<Core>("core"); }
    Nets& nets() { return m.unit<Nets>("nets"); }
    IngressEgress& ie() { return m.unit<PeriphSlot>("slot").s.ingress_egress(1); }
    // Every load of the script returned what the specification says (its b). Reports the first that did not.
    bool loads_as_specified(const char* file, int line) {
        Core& c = core();
        unsigned j = 0;
        for (unsigned k = 0; k < cn; k++) {
            if (cev[k].kind == Core::STORE) continue;
            if (j >= c.n_got) { test::fail(file, line, "the load of cycle %llu was not made", (unsigned long long)cev[k].t); return false; }
            if (c.got[j] != cev[k].b) {
                test::fail(file, line, "script entry %u: the load of cycle %llu (%s) returned 0x%x, the specification says 0x%x", k,
                           (unsigned long long)cev[k].t, cev[k].kind == Core::EVENTS ? "EVENTS, consuming" : "a window register", c.got[j], cev[k].b);
                return false;
            }
            j++;
        }
        return true;
    }
};
#define CHECK_LOADS(rig) do { if (!(rig).loads_as_specified(__FILE__, __LINE__)) return; } while (0)

sim::Options opts(int variant) {
    sim::Options o;
    if (variant == 1) o.step_all = true;
    if (variant == 2) o.shuffle_seed = 7;
    return o;
}
template <unsigned N> constexpr unsigned count(const Core::Ev (&)[N]) { return N; }
template <unsigned N> constexpr unsigned count(const Nets::Ev (&)[N]) { return N; }

constexpr uint8_t ST = Core::STORE, LD = Core::LOAD, EV = Core::EVENTS;

// 5.4's access of each word of Ingress/Egress's window, from 17.1's and 17.6's tables. Block 1 without a DMA: every
// word reads 0 (17.1) and a store there, UB in the specification, is faulted by the RTL's register_bus (its
// peripheral_reads_zero; rtl/blocks/peripheral/README.md: "reads every word of its block 1 as 0 and faults a store").
enum WordAccess : uint8_t { NONE, R, W, RW, ZERO };
WordAccess access(unsigned w) {
    if (w <= W_EVENTS_PEEK) return R;                        // PARAMS, STATUS, ERRORS, EVENTS, EVENTS_PEEK
    if (w == W_EVENTS_MASK) return RW;
    if (w == W_START) return W;
    if (w < 16) return NONE;
    if (w < 32) return ZERO;
    if (w <= W_ARRIVALS_1) return R;                         // ERROR_LINES_0-2, ARRIVALS_0-1
    if (w <= W_ARRIVE_EXPECT_1) return RW;
    if (w == W_RELEASE) return W;
    if (w == W_WATCHDOG || w == W_LED) return RW;
    if (w == W_UART_TX) return W;
    if (w == W_UART_RX) return R;
    if (w == W_UART_DIVISOR || w == W_UART_STATUS) return RW;
    if (w < 64) return NONE;                                 // "Every other word of blocks 2-7 is unmapped (5.3)"
    if (w <= W_STREAM_SEND_LAST) return W;                   // STREAM_OUT_W0-W2, STREAM_SEND, STREAM_SEND_LAST
    if (w <= W_STREAM_RECEIVE) return R;                     // STREAM_IN_W0-W2, STREAM_RECEIVE
    return NONE;
}

}  // namespace

// 17.6, 17.1, 17.5: the control block as Ingress/Egress has it, and every word after the reset. PARAMS: type 1,
// version 0, no constants. It has no work and no DMA, so STATUS.busy, STATUS.dma_busy, EVENTS.done and EVENTS.dma_done
// are 0 and its block 1 reads 0; STATUS.stream_last is 0 with no beat; ERRORS is 0; of the events only uart_tx and
// stream_out read 1 (room in both transmit FIFOs), and they wake nothing while EVENTS_MASK keeps its reset value, 0.
// The reset zeroes ARRIVALS, ARRIVE_EXPECT, the watchdog, LED and the line errors, empties the FIFOs (UART_RX and the
// host interface's receive words read 0 and pop nothing), and sets UART_DIVISOR to UART_RESET_DIVISOR. A W word reads 0.
TEST(ie_control_block_and_reset_values) {
    const Core::Ev ev[] = {
        {3, LD, reg(W_PARAMS), 1},
        {3, LD, reg(W_STATUS), 0},
        {3, LD, reg(W_ERRORS), 0},
        {3, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {3, LD, reg(W_EVENTS_MASK), 0},
        {3, LD, reg(W_START), 0},
        {3, LD, reg(W_DMA_HDR), 0},
        {3, LD, reg(W_DMA_DEST_ADDR), 0},
        {3, LD, LIVE, 0},
        {3, LD, reg(W_ERROR_LINES_0), 0}, {3, LD, reg(W_ERROR_LINES_1), 0}, {3, LD, reg(W_ERROR_LINES_2), 0},
        {3, LD, reg(W_ARRIVALS_0), 0}, {3, LD, reg(W_ARRIVALS_1), 0},
        {3, LD, reg(W_ARRIVE_EXPECT_0), 0}, {3, LD, reg(W_ARRIVE_EXPECT_1), 0},
        {3, LD, reg(W_RELEASE), 0},
        {3, LD, reg(W_WATCHDOG), 0},
        {3, LD, reg(W_LED), 0},
        {3, LD, reg(W_UART_TX), 0},
        {3, LD, reg(W_UART_RX), 0},                    // [8] valid 0: the RX FIFO is empty
        {3, LD, reg(W_UART_DIVISOR), RESET_DIVISOR},
        {3, LD, reg(W_UART_STATUS), 0},
        {3, LD, reg(W_STREAM_OUT_W0), 0}, {3, LD, reg(W_STREAM_SEND), 0}, {3, LD, reg(W_STREAM_SEND_LAST), 0},
        {3, LD, reg(W_STREAM_IN_W0), 0}, {3, LD, reg(W_STREAM_IN_W0 + 1), 0}, {3, LD, reg(W_STREAM_IN_W0 + 2), 0},
        {3, LD, reg(W_STREAM_RECEIVE), 0},
        {4, EV, 0, EV_IDLE},                           // a consuming load: the same value; nothing to consume
        {6, LD, reg(W_EVENTS_PEEK), EV_IDLE},          // live bits: no load changes them (7.2)
        // EVENTS_MASK is R/W (17.1), and the tile's EVENTS.peripheral_1 is (EVENTS & EVENTS_MASK) != 0 (17.2)
        {6, ST, W_EVENTS_MASK, EV_ALL_ARRIVED},
        {7, LD, reg(W_EVENTS_MASK), EV_ALL_ARRIVED},
        {7, LD, LIVE, 0},
        {7, ST, W_EVENTS_MASK, EV_UART_TX | EV_WATCHDOG},
        {8, LD, reg(W_EVENTS_MASK), EV_UART_TX | EV_WATCHDOG},
        {8, LD, LIVE, LIVE_EVENT},
        {8, EV, 0, EV_IDLE},                           // a consuming load under the mask: uart_tx is live, and stays
        {10, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {10, LD, LIVE, LIVE_EVENT},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nullptr, 0, 20, opts(variant));
        r.m.run(20);
        CHECK_LOADS(r);
        CHECK(!r.ie().toast);                          // the pins: toast and led 0 (17.6, Reset)
        CHECK_EQ(r.ie().led, 0u);
        CHECK_EQ(r.m.unit<PeriphSlot>("slot").s.violation_total(), 0u);
    }
}

// 5.3, 5.4, 17.1, 17.6: every word of the window, loaded and stored. A word the tables do not define is unmapped, an
// access fault; a store to an R word is an access fault; a W word loads as 0. The bus decides it (register_bus.h's
// decode with the master tile's straps: peripheral 1 present, without a DMA) from the peripheral's map for its own
// words, which the slot publishes (Win::R_DEFINED, R_WRITABLE), as TileCore::Env::bus_kind composes them. The stores
// that may stall are UART_TX, STREAM_SEND and STREAM_SEND_LAST and no other (17.6; register_bus.veryl, own_stalling).
TEST(ie_window_map_unmapped_words_fault) {
    uint32_t defined[3] = {}, writable[3] = {}, may_stall[3] = {};
    for (unsigned w = 32; w < 128; w++) {
        const unsigned k = w - 32;
        if (access(w) != NONE) defined[k / 32] |= 1u << (k % 32);
        if (access(w) == W || access(w) == RW) writable[k / 32] |= 1u << (k % 32);
        if (w == W_UART_TX || w == W_STREAM_SEND || w == W_STREAM_SEND_LAST) may_stall[k / 32] |= 1u << (k % 32);
    }
    Core::Ev ev[9];
    for (unsigned k = 0; k < 3; k++) {
        ev[k] = {3, LD, Win::reg(1, Win::R_DEFINED + k), defined[k]};
        ev[3 + k] = {3, LD, Win::reg(1, Win::R_WRITABLE + k), writable[k]};
        ev[6 + k] = {3, LD, Win::reg(1, Win::R_MAYSTALL + k), may_stall[k]};
    }
    Rig r(ev, 9, nullptr, 0, 8, opts(0));
    r.m.run(8);
    CHECK_LOADS(r);
    const uint32_t* got = r.core().got;
    bus::Straps straps;
    straps.peripherals = 1;
    auto faults = [&](unsigned w, bool store) {
        const bus::Decoded d = bus::decode(straps, 0xFFFFFA00u + 4 * w, store, true);
        if (d.target == bus::Target::FAULT) return true;
        if (d.target != bus::Target::PERIPH_OWN) return false;
        const unsigned k = w - 32;
        if (!(got[k / 32] >> (k % 32) & 1)) return true;
        return store && !(got[3 + k / 32] >> (k % 32) & 1);
    };
    for (unsigned w = 0; w < 128; w++) {
        const WordAccess a = access(w);
        const bool load_faults = a == NONE, store_faults = a == NONE || a == R || a == ZERO;
        if (faults(w, false) != load_faults) { test::fail(__FILE__, __LINE__, "a load of window word %u: fault %d, 5.3/17.6 say %d", w, !load_faults, load_faults); return; }
        if (faults(w, true) != store_faults) { test::fail(__FILE__, __LINE__, "a store to window word %u: fault %d, 5.3/5.4/17.6 say %d", w, !store_faults, store_faults); return; }
    }
    CHECK(bus::decode(straps, 0xFFFFFA94u, false, false).target == bus::Target::FAULT);   // a part of a word (5.4)
}

// 17.6, the barrier's master side: bit i of ARRIVALS is set at the end of the cycle in which compute tile i's
// BARRIER_ARRIVE reaches Ingress/Egress (a load in that cycle does not see it, the next does); bit b of word w is tile
// 32w + b. ARRIVE_EXPECT is R/W, laid out as ARRIVALS. all_arrived is live: (ARRIVALS & ARRIVE_EXPECT) = ARRIVE_EXPECT
// != 0: only the expected tiles count (an unexpected arrival changes nothing, a missing expected one holds it at 0),
// nothing expected is not "all arrived", it is current whatever order the two words are written in, and no load
// consumes it. The tile's EVENTS.peripheral_1 follows it under EVENTS_MASK (17.2).
TEST(ie_arrivals_and_all_arrived) {
    const Nets::Ev nets[] = {
        {5, Nets::ARRIVE, 0, 0},
        {7, Nets::ARRIVE, 63, 0},
        {9, Nets::ARRIVE, 31, 0},                      // not expected
        {14, Nets::ARRIVE, 32, 0},
    };
    const Core::Ev ev[] = {
        {4, ST, W_ARRIVE_EXPECT_1, 0x80000001},        // the upper word first: tiles 63 and 32
        {5, LD, reg(W_ARRIVALS_0), 0},                 // tile 0's pulse is in 5: set at its end
        {6, LD, reg(W_ARRIVALS_0), 1},
        {6, LD, reg(W_EVENTS_PEEK), EV_IDLE},          // 0 arrived, 32 and 63 expected
        {7, LD, reg(W_ARRIVALS_1), 0},
        {8, LD, reg(W_ARRIVALS_1), 0x80000000},        // tile 63: bit 31 of word 1
        {8, ST, W_ARRIVE_EXPECT_0, 1},                 // then the lower: tile 0 too
        {10, LD, reg(W_ARRIVALS_0), 0x80000001},       // tiles 0 and 31
        {10, LD, reg(W_ARRIVE_EXPECT_0), 1},
        {10, LD, reg(W_ARRIVE_EXPECT_1), 0x80000001},
        {14, LD, reg(W_EVENTS_PEEK), EV_IDLE},         // tile 32 is missing (31's arrival does not count)
        {15, LD, reg(W_ARRIVALS_1), 0x80000001},       // tile 32: bit 0 of word 1
        {15, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_ALL_ARRIVED},
        {15, LD, LIVE, 0},                             // EVENTS_MASK is 0
        {15, ST, W_EVENTS_MASK, EV_ALL_ARRIVED},
        {16, LD, LIVE, LIVE_EVENT},
        {16, EV, 0, EV_IDLE | EV_ALL_ARRIVED},         // a consuming load under the mask
        {18, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_ALL_ARRIVED},   // live: it stays
        {18, ST, W_ARRIVE_EXPECT_0, 3},                // tile 1 expected too: it has not arrived
        {19, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {19, LD, LIVE, 0},
        {19, ST, W_ARRIVE_EXPECT_0, 0},
        {20, ST, W_ARRIVE_EXPECT_1, 0},                // nothing expected
        {21, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {21, ST, W_ARRIVE_EXPECT_1, 0x80000000},       // tile 63 alone
        {22, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_ALL_ARRIVED},
        {22, LD, reg(W_ERRORS), 0},                    // arrivals raise no error
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nets, count(nets), 30, opts(variant));
        r.m.run(30);
        CHECK_LOADS(r);
        for (unsigned k = 0; k < 64; k++) CHECK_EQ(r.nets().releases[k], 0u);   // no RELEASE, no pulse
    }
}

// 17.6, RELEASE (the unit's README, Timing): a store with bit 0 set, presented in p, at the end of p clears ARRIVALS
// but keeps an arrival that reaches Ingress/Egress in p; BARRIER_RELEASE's flip-flop is 1 in p + 1 alone, the first of
// RELEASE_LATENCY's cycles, and the pulse is on every compute tile's line in the same cycle. A store with bit 0 clear
// does nothing. A load of RELEASE reads 0 (W). all_arrived, live, falls with ARRIVALS.
TEST(ie_release_clears_arrivals_keeps_its_cycles_arrival_and_pulses_every_tile) {
    const uint64_t p = 10;
    const Nets::Ev nets[] = {
        {5, Nets::ARRIVE, 3, 0},
        {5, Nets::ARRIVE, 40, 0},
        {p - 1, Nets::ARRIVE, 6, 0},                   // the cycle before the release's: cleared
        {p, Nets::ARRIVE, 5, 0},                       // in the release's cycle: kept
        {p + 1, Nets::ARRIVE, 9, 0},                   // after it
    };
    const Core::Ev ev[] = {
        {3, ST, W_ARRIVE_EXPECT_0, 1u << 3},
        {4, ST, W_ARRIVE_EXPECT_1, 1u << 8},           // tiles 3 and 40
        {8, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_ALL_ARRIVED},
        {8, ST, W_RELEASE, 2},                         // bit 0 clear: no release
        {p, LD, reg(W_ARRIVALS_0), 1u << 3 | 1u << 6},
        {p, LD, reg(W_ARRIVALS_1), 1u << 8},
        {p, ST, W_RELEASE, 1},
        {p + 1, LD, reg(W_ARRIVALS_0), 1u << 5},       // tiles 3 and 6 cleared, tile 5 kept
        {p + 1, LD, reg(W_ARRIVALS_1), 0},
        {p + 1, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {p + 1, LD, reg(W_RELEASE), 0},
        {p + 2, LD, reg(W_ARRIVALS_0), 1u << 5 | 1u << 9},
        {p + 2, LD, reg(W_ARRIVE_EXPECT_0), 1u << 3},  // a release leaves ARRIVE_EXPECT
        {p + 2, LD, reg(W_ARRIVE_EXPECT_1), 1u << 8},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nets, count(nets), 30, opts(variant));
        r.m.run(30);
        CHECK_LOADS(r);
        for (unsigned k = 0; k < 64; k++) {
            CHECK_EQ(r.nets().releases[k], 1u);                           // one pulse, of one cycle
            CHECK_EQ(r.nets().release_at[k], p + 1 + RELEASE_LATENCY);    // the flip-flop is 1 in p + 1
        }
    }
}

// 17.6, the watchdog: the limit and the count are 0 at the reset; a store of N sets both; a load returns the count; it
// decreases by 1 at the end of every cycle in which it is non-zero and nothing sets it; a store of N > 0 issued in t
// (presented in p = t + 1), with nothing after it, expires at the end of t + 1 + N = p + N: it posts EVENTS.watchdog
// (counted, depth 1: consumed by a load under EVENTS_MASK alone) and sets the toast pin; the count then stays at 0. A
// RELEASE sets the count to the limit. The expiry is an event and the pin, not an error: ERRORS stays 0. The toast
// pin is a flip-flop cleared only by the reset: consuming the event and restarting the watchdog leave it set.
TEST(ie_watchdog_expires_at_the_end_of_p_plus_n_and_restarts_at_a_release) {
    const uint64_t p = 4, n = 5, q = 16;
    const Core::Ev ev[] = {
        {3, LD, reg(W_WATCHDOG), 0},
        {p, ST, W_WATCHDOG, uint32_t(n)},
        {p + 1, LD, reg(W_WATCHDOG), 5},
        {p + 2, LD, reg(W_WATCHDOG), 4},
        {p + n, LD, reg(W_WATCHDOG), 1},
        {p + n, LD, reg(W_EVENTS_PEEK), EV_IDLE},                  // not before the end of p + N
        {p + n + 1, LD, reg(W_WATCHDOG), 0},
        {p + n + 1, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_WATCHDOG},
        {p + n + 1, LD, reg(W_ERRORS), 0},
        {p + n + 1, LD, LIVE, 0},
        {12, LD, reg(W_WATCHDOG), 0},                              // it stays at 0
        {12, EV, 0, EV_IDLE | EV_WATCHDOG},                        // EVENTS_MASK is 0: the load consumes nothing
        {13, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_WATCHDOG},
        {13, ST, W_EVENTS_MASK, EV_WATCHDOG},
        {14, LD, LIVE, LIVE_EVENT},
        {14, EV, 0, EV_IDLE | EV_WATCHDOG},                        // consumed at the end of 14
        {15, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {15, LD, LIVE, 0},
        {q, ST, W_RELEASE, 1},                                     // the count is the limit again at the end of q
        {q + 1, LD, reg(W_WATCHDOG), 5},
        {q + n, LD, reg(W_WATCHDOG), 1},
        {q + n, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {q + n + 1, LD, reg(W_WATCHDOG), 0},
        {q + n + 1, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_WATCHDOG},   // the second expiry, at the end of q + N
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nullptr, 0, 40, opts(variant));
        r.m.run_until(p + n - 1);
        CHECK(!r.ie().toast);
        r.m.run_until(p + n);                          // the pin is set at the end of p + N
        CHECK(r.ie().toast);
        CHECK_EQ(r.ie().toast_at, int64_t(p + n));
        r.m.run_until(40);
        CHECK_LOADS(r);
        CHECK(r.ie().toast);                           // latched: the event's consumption and the restart leave it
        CHECK_EQ(r.ie().toast_at, int64_t(p + n));
    }
}

// 17.6: "in which the count is non-zero and nothing sets it": a WATCHDOG store or a RELEASE presented in the cycle
// the watchdog would expire in wins, and it does not expire (ingress_egress_unit.veryl: watchdog_expires =
// !watchdog_store && !release_store && watchdog_count == 1; its README, Decisions, "The watchdog's ties"). A store of
// 0 stops it, and a RELEASE with a limit of 0 leaves it off. No expiry: no event, and the toast pin stays 0.
TEST(ie_watchdog_a_store_or_a_release_in_the_expirys_cycle_wins_and_a_store_of_0_stops_it) {
    const Core::Ev ev[] = {
        {3, ST, W_WATCHDOG, 3},                        // would expire at the end of 6
        {6, LD, reg(W_WATCHDOG), 1},
        {6, ST, W_WATCHDOG, 4},                        // presented in 6: it wins; now the end of 10
        {7, LD, reg(W_WATCHDOG), 4},
        {7, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {10, LD, reg(W_WATCHDOG), 1},
        {10, ST, W_RELEASE, 1},                        // presented in 10: it wins; the count is the limit, 4
        {11, LD, reg(W_WATCHDOG), 4},
        {11, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {13, LD, reg(W_WATCHDOG), 2},
        {13, ST, W_WATCHDOG, 0},                       // stops it
        {14, LD, reg(W_WATCHDOG), 0},
        {16, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {17, ST, W_RELEASE, 1},                        // the limit is 0: it stays off
        {18, LD, reg(W_WATCHDOG), 0},
        {24, LD, reg(W_EVENTS_PEEK), EV_IDLE},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nullptr, 0, 30, opts(variant));
        r.m.run(30);
        CHECK_LOADS(r);
        CHECK(!r.ie().toast);
    }
}

// 17.6: the toast pin is set at the end of a cycle in which the master tile's own error line is high OR the watchdog
// expires. A store that takes the watchdog's expiry of that cycle away (above) leaves the line's: the pin is set at
// the end of that cycle all the same, with no watchdog event (ingress_egress_unit.veryl: toast_pin = 1 if
// error_line_own || watchdog_expires).
TEST(ie_toast_pin_from_the_own_line_in_the_cycle_a_store_wins_over_the_expiry) {
    const Nets::Ev nets[] = {{6, Nets::OWN, 64, 1}, {7, Nets::OWN, 64, 0}};
    const Core::Ev ev[] = {
        {3, ST, W_WATCHDOG, 3},                        // would expire at the end of 6
        {6, ST, W_WATCHDOG, 0},                        // presented in 6: it wins, and stops the watchdog
        {7, LD, reg(W_WATCHDOG), 0},
        {7, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {12, LD, reg(W_EVENTS_PEEK), EV_IDLE},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nets, count(nets), 20, opts(variant));
        r.m.run(20);
        CHECK_LOADS(r);
        CHECK(r.ie().toast);
        CHECK_EQ(r.ie().toast_at, 6);
    }
}

// 17.6, 18: bit b of ERROR_LINES_w is the error line of tile 32w + b, live; at 8 x 8, ERROR_LINES_2's [1] is the
// memory tile's (id 65) and [2] the tracer tile's (66). ERRORS [0] tile_error is live, an error and not an event: 1
// while any bit of ERROR_LINES is 1, and with it the tile's ERRORS.peripheral_1 (17.2); it falls with the last line.
// The master tile's own bit, TILE_COUNT (ERROR_LINES_2 [0]), reads 0 and is no tile_error: its own line sets the
// toast pin instead, at the end of a cycle in which it is high, and the pin stays set when the line falls. Another
// tile's line reaches the pin only through the master tile's own ERRORS (17.6), which is the tile's, not this unit's.
TEST(ie_error_lines_tile_error_and_the_toast_pin) {
    const Nets::Ev nets[] = {
        {4, Nets::LINE, 0, 1}, {5, Nets::LINE, 31, 1}, {6, Nets::LINE, 32, 1}, {7, Nets::LINE, 63, 1},
        {8, Nets::LINE, 65, 1}, {9, Nets::LINE, 66, 1},
        {12, Nets::LINE, 0, 0}, {12, Nets::LINE, 31, 0}, {12, Nets::LINE, 32, 0}, {12, Nets::LINE, 63, 0},
        {13, Nets::LINE, 65, 0}, {14, Nets::LINE, 66, 0},
        {18, Nets::OWN, 64, 1}, {19, Nets::OWN, 64, 0},
    };
    const Core::Ev ev[] = {
        {3, LD, reg(W_ERROR_LINES_0), 0},
        {3, LD, reg(W_ERRORS), 0},
        {4, LD, reg(W_ERROR_LINES_0), 1},                          // tile 0
        {4, LD, reg(W_ERRORS), 1},
        {4, LD, LIVE, LIVE_ERROR},
        {4, LD, reg(W_EVENTS_PEEK), EV_IDLE},                      // no event
        {5, LD, reg(W_ERROR_LINES_0), 0x80000001},                 // tile 31
        {6, LD, reg(W_ERROR_LINES_1), 1},                          // tile 32
        {7, LD, reg(W_ERROR_LINES_1), 0x80000001},                 // tile 63
        {7, LD, reg(W_ERROR_LINES_2), 0},
        {8, LD, reg(W_ERROR_LINES_2), 2},                          // the memory tile
        {9, LD, reg(W_ERROR_LINES_2), 6},                          // and the tracer tile
        {12, LD, reg(W_ERROR_LINES_0), 0},
        {12, LD, reg(W_ERROR_LINES_1), 0},
        {12, LD, reg(W_ERRORS), 1},                                // 65 and 66 still
        {13, LD, reg(W_ERROR_LINES_2), 4},
        {13, LD, reg(W_ERRORS), 1},                                // from one line
        {14, LD, reg(W_ERROR_LINES_2), 0},
        {14, LD, reg(W_ERRORS), 0},                                // live: not sticky
        {14, LD, LIVE, 0},
        {18, LD, reg(W_ERROR_LINES_2), 0},                         // the master tile's own line is high in 18
        {18, LD, reg(W_ERRORS), 0},
        {19, LD, reg(W_ERROR_LINES_2), 0},
        {19, LD, reg(W_ERRORS), 0},
        {19, LD, reg(W_EVENTS_PEEK), EV_IDLE},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nets, count(nets), 30, opts(variant));
        r.m.run_until(18);
        CHECK(!r.ie().toast);                          // the other tiles' lines did not set it
        r.m.run_until(30);
        CHECK_LOADS(r);
        CHECK(r.ie().toast);                           // set at the end of 18, and kept after the line fell
        CHECK_EQ(r.ie().toast_at, 18);
    }
}

// 17.6: LED drives the led pin from a flip-flop, its bit 0; R/W.
TEST(ie_led_is_bit_0) {
    const Core::Ev ev[] = {
        {3, ST, W_LED, 1},
        {4, LD, reg(W_LED), 1},
        {4, ST, W_LED, 2},
        {5, LD, reg(W_LED), 0},
        {5, ST, W_LED, 0xFFFFFFFF},
        {6, LD, reg(W_LED), 1},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nullptr, 0, 10, opts(variant));
        r.m.run_until(4);
        CHECK_EQ(r.ie().led, 1u);                      // the pin, from the end of 3
        r.m.run_until(5);
        CHECK_EQ(r.ie().led, 0u);
        r.m.run(5);
        CHECK_LOADS(r);
        CHECK_EQ(r.ie().led, 1u);
    }
}

// 17.6, the UART's transmit side as far as the model has it (uart.veryl's header, "The transmitter"): UART_DIVISOR is
// R/W, [15:0]; a store to UART_TX pushes its byte into the TX FIFO of UART_FIFO_DEPTH = 16 at the end of its cycle;
// UART_STATUS [4:0] is the FIFO's bytes, and the byte being sent has left it. A frame starts at an idle point with the
// FIFO not empty: the head is popped at the end of that cycle (the cycle after its push into an empty FIFO with the
// transmitter idle), and frames follow back to back, 10 D cycles apart, D the divisor as it stands at the frame's
// start (a store to UART_DIVISOR cuts no frame). With 16 bytes in the FIFO a store to UART_TX stalls (the slot's
// R_STALL bit for the word, the reg interface's ready) and EVENTS.uart_tx, live, is 0; both end with the next pop.
// The bytes leave in order (the model's log of the bytes sent; the line itself is not modelled).
TEST(ie_uart_transmit_fifo_stall_and_frames) {
    const unsigned stall = Win::reg(1, Win::R_STALL);                  // words 32-63: bit w - 32
    const uint32_t stall_tx = 1u << (W_UART_TX - 32);
    Core::Ev ev[64];
    unsigned n = 0;
    ev[n++] = {3, LD, reg(W_UART_DIVISOR), RESET_DIVISOR};
    ev[n++] = {3, ST, W_UART_DIVISOR, 0x10002};                        // D = 2: a frame is 20 cycles
    ev[n++] = {4, LD, reg(W_UART_DIVISOR), 2};
    // 17 bytes, a cycle apart, from 5: 'A' is popped at the end of 6 (pushed at the end of 5 into an empty FIFO); the
    // next pop is at the end of 26, so 'B'..'Q', pushed at the ends of 6..21, fill the FIFO
    for (unsigned k = 0; k < 17; k++) {
        if (k == 1) ev[n++] = {6, LD, reg(W_UART_STATUS), 1};          // 'A' in the FIFO
        if (k == 2) ev[n++] = {7, LD, reg(W_UART_STATUS), 1};          // 'A' on the line, 'B' in the FIFO
        if (k == 16) {
            ev[n++] = {21, LD, reg(W_UART_STATUS), 15};
            ev[n++] = {21, LD, stall, 0};
            ev[n++] = {21, LD, reg(W_EVENTS_PEEK), EV_IDLE};
        }
        ev[n++] = {5 + k, ST, W_UART_TX, 0xF00u | ('A' + k)};          // [7:0] the byte
    }
    ev[n++] = {22, LD, reg(W_UART_STATUS), 16};                        // full
    ev[n++] = {22, LD, stall, stall_tx};
    ev[n++] = {22, LD, reg(W_EVENTS_PEEK), EV_STREAM_OUT};             // no room: uart_tx 0
    ev[n++] = {26, LD, reg(W_UART_STATUS), 16};
    ev[n++] = {27, LD, reg(W_UART_STATUS), 15};                        // 'B' popped at the end of 26
    ev[n++] = {27, LD, stall, 0};
    ev[n++] = {27, LD, reg(W_EVENTS_PEEK), EV_IDLE};
    ev[n++] = {27, LD, reg(W_UART_TX), 0};                             // W: a load reads 0
    ev[n++] = {27, ST, W_UART_TX, 'Z'};                                // the 18th byte
    ev[n++] = {28, LD, reg(W_UART_STATUS), 16};
    ev[n++] = {30, ST, W_UART_DIVISOR, 1};                             // in 'B's frame: it keeps D = 2, to the end of 46
    ev[n++] = {31, LD, reg(W_UART_DIVISOR), 1};
    ev[n++] = {47, LD, reg(W_UART_STATUS), 15};                        // 'C' popped at the end of 46, with D = 1
    ev[n++] = {56, LD, reg(W_UART_STATUS), 15};
    ev[n++] = {57, LD, reg(W_UART_STATUS), 14};                        // 'D' at the end of 56: 10 cycles on
    ev[n++] = {57, LD, reg(W_ERRORS), 0};
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, n, nullptr, 0, 250, opts(variant));
        // the pops: the ends of 6, 26, 46, then every 10 cycles: the 18th byte's at the end of 46 + 150 = 196
        const struct { uint64_t through; unsigned sent; } at[] = {{5, 0}, {6, 1}, {25, 1}, {26, 2}, {45, 2}, {46, 3}, {55, 3}, {56, 4},
                                                                 {195, 17}, {196, 18}, {250, 18}};
        for (const auto& a : at) {
            r.m.run_until(a.through);
            CHECK_EQ(r.ie().serial_n, a.sent);
        }
        CHECK_LOADS(r);
        for (unsigned k = 0; k < 17; k++) CHECK_EQ(unsigned(r.ie().serial_out[k]), unsigned('A' + k));
        CHECK_EQ(unsigned(r.ie().serial_out[17]), unsigned('Z'));
        CHECK_EQ(r.m.unit<PeriphSlot>("slot").s.violation_total(), 0u);
    }
}

// 17.6, Reset, on the model itself (the simulator resets a unit only by building it; reset() is the model's): every
// register dirtied through its window words and the nets, then the reset: ARRIVALS, ARRIVE_EXPECT, the watchdog's
// limit and count, LED and the toast pin are 0, the TX FIFO is empty, UART_DIVISOR is UART_RESET_DIVISOR again, and
// the events are the idle ones. The limit is 0 too: a RELEASE after the reset starts no watchdog.
TEST(ie_reset_zeroes_its_state) {
    const Nets::Ev nets[] = {{5, Nets::ARRIVE, 7, 0}, {5, Nets::ARRIVE, 50, 0}};
    const Core::Ev ev[] = {
        {3, ST, W_ARRIVE_EXPECT_0, 1u << 7},
        {4, ST, W_ARRIVE_EXPECT_1, 1u << 18},
        {5, ST, W_WATCHDOG, 2},                        // expires at the end of 7: the event and the toast pin
        {6, ST, W_LED, 1},
        {7, ST, W_UART_DIVISOR, 100},
        {8, ST, W_UART_TX, 'x'},
        {9, ST, W_UART_TX, 'y'},
        {10, ST, W_WATCHDOG, 1000},
        {12, LD, reg(W_ARRIVALS_0), 1u << 7},
        {12, LD, reg(W_ARRIVALS_1), 1u << 18},
        {12, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_ALL_ARRIVED | EV_WATCHDOG},
        {12, LD, reg(W_UART_STATUS), 1},
        {12, LD, reg(W_WATCHDOG), 999},
    };
    Rig r(ev, count(ev), nets, count(nets), 12, opts(0));
    r.m.run_until(12);
    CHECK_LOADS(r);
    IngressEgress& ie = r.ie();
    CHECK(ie.toast);
    ie.reset(13);
    auto word = [&](unsigned w) { uint32_t v = ~0u; ie.word(14, int(w), v); return v; };   // (an undefined word leaves ~0)
    for (unsigned w : {unsigned(W_ARRIVALS_0), unsigned(W_ARRIVALS_1), unsigned(W_ARRIVE_EXPECT_0), unsigned(W_ARRIVE_EXPECT_1),
                       unsigned(W_WATCHDOG), unsigned(W_LED), unsigned(W_UART_STATUS)})
        CHECK_EQ(word(w), 0u);
    CHECK_EQ(word(W_UART_DIVISOR), RESET_DIVISOR);
    CHECK(!ie.toast);
    CHECK_EQ(ie.led, 0u);
    ie.sync(14);
    CHECK_EQ(ie.events().value(14), EV_IDLE);
    CHECK_EQ(ie.write_word(14, W_RELEASE, 1), 1);      // the limit is 0: the count stays 0
    ie.step(15);
    CHECK_EQ(word(W_WATCHDOG), 0u);
}

// 17.6: "A START is UB (1.3)": it has no work. The simulator stops, naming it.
DEATH_TEST(ie_start_is_undefined_behaviour, "Ingress/Egress: undefined behavior at 5: a START") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_START, 1);
}
// 17.6: "No writes. Its region takes none: a WRITE to the master tile's region 0x04 is UB (17.3)."
DEATH_TEST(ie_a_write_into_its_region_is_undefined_behaviour, "a WRITE into the master tile's region 0x04") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.accept(5, 1, 0x1234, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04000040, lw::SIZE_128), 0x5678);
}
// THE STAND-IN, not the hardware (17.6: STREAM_OUT_W0-W2 hold the words below the beat's top, and STREAM_SEND and
// STREAM_SEND_LAST push the beat into the transmit FIFO): the model has no transmit FIFO and no lvds ports, so a store
// to any of the host interface's transmit words stops the simulator (ingress_egress.h; PENDING-v1.36.md, section 5).
DEATH_TEST(ie_stand_in_a_store_to_stream_out_w0_stops_the_simulator, "the host interface's transmit side is not modelled") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_STREAM_OUT_W0, 0x11);
}
DEATH_TEST(ie_stand_in_a_store_to_stream_send_stops_the_simulator, "window word 67 at 5: the host interface's transmit side is not modelled") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_STREAM_SEND, 0x22);
}
DEATH_TEST(ie_stand_in_a_store_to_stream_send_last_stops_the_simulator, "window word 68 at 5: the host interface's transmit side is not modelled") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_STREAM_SEND_LAST, 0x33);
}
