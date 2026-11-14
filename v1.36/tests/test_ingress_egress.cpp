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
//   Far    the far ends of the pins and the ports (tests/ie_far_end.h), which are not chip hardware: the serial line's
//          other end (uart_rx's level by cycle; uart_tx's traced and decoded) and the LVDS IO block (the beats offered
//          on lvds_in, each held until its handshake; lvds_out's TREADY by cycle); every output's level in every cycle
//          is traced. A test without one runs with the idle inputs (uart_rx 1, no beat offered, TREADY 0).
// Each script runs plain, with step-all, and shuffled.
//
// CYCLES are the simulator's (a machine's first cycle is 1). The UART's and the host interface's are written from the
// RTL unit's README (Timing) and its sources' headers, in cycles after the store's or the load's presentation p:
//   UART_TX into an empty FIFO, the transmitter idle: the start bit on uart_tx from p + 2; frames back to back 10 D
//   apart; a frame on uart_rx whose falling edge is on the pin in c: seen in e = c + 2, bit k sampled in e + k D +
//   floor(D / 2), the byte in the RX FIFO at the end of the stop bit's sample f = e + 9 D + floor(D / 2), read from
//   f + 1; a send: lvds_out_tvalid from p + 1 if the FIFO was empty; a beat on lvds_in: read from the cycle after its
//   handshake; a stalled store: taken in the first cycle at whose start its FIFO is not full.
//
// NOT CHECKED HERE: an error line's cycle through the chip's wiring (a cycle later than ERROR_LATENCY says:
// PENDING-v1.36.md's row "the nets") is the chip's, not this unit's: here a level is read in the cycle it is driven.
// The UART and the host interface through the master's core, on the chip, at the RTL chip's cycles: test_rtl_stream.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "harness.h"
#include "ie_far_end.h"
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
constexpr uint32_t EV_ALL_ARRIVED = 1u << 2, EV_WATCHDOG = 1u << 3, EV_UART_RX = 1u << 4, EV_UART_TX = 1u << 5, EV_STREAM_IN = 1u << 6,
                   EV_STREAM_OUT = 1u << 7;
constexpr uint32_t STATUS_STREAM_LAST = 1u << 2;           // STATUS [2] (17.6)
constexpr uint32_t RX_VALID = 1u << 8, RX_OVERRUN = 1u << 16, FRAMING_ERROR = 1u << 17;   // UART_RX [8]; UART_STATUS [16], [17]
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
    // LOAD, WLOAD and EVENTS carry the value the specification says they return (b); the test compares after the run.
    //   LOAD    of a register of the window RegFile (a), with no effect
    //   WLOAD   a load of own window word a as the core makes it (tile_ctl.h, periph_own_load): the word's register,
    //           and the slot told of it if the word's load pops (the slot's static bit for the word, Win::R_POPS)
    //   SSTORE  a store that may stall, as register_bus presents it (17.6; register_bus.veryl): to window word a, in
    //           every cycle from t until its owner is ready (the slot's R_STALL bit for the word is 0), when it is
    //           taken; `taken` has the cycle of each. One at a time; no other access is made in the cycle one is taken.
    enum : uint8_t { STORE, LOAD, EVENTS, WLOAD, SSTORE };
    struct Ev { uint64_t t; uint8_t kind; uint32_t a, b; };
    sim::LinkOut core, region;
    sim::RegFileIn window;
    const Ev* ev = nullptr;
    unsigned n = 0, i = 0;
    static constexpr unsigned MAX = 256;
    uint32_t got[MAX] = {};
    unsigned n_got = 0;
    bool pending = false;
    Ev stalled{};
    uint64_t taken[16] = {};
    unsigned n_taken = 0;

    Core(sim::Ports& p, const Ev* e, unsigned count) : ev(e), n(count) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
    }
    bool own_bit(sim::Ctx& c, unsigned base, unsigned w) const { return c.read(window, Win::reg(1, base + (w - 32) / 32)) >> ((w - 32) % 32) & 1; }
    void present(sim::Ctx& c, const Ev& e) {
        if (own_bit(c, Win::R_STALL, e.a)) { pending = true; stalled = e; return; }
        c.put(core, CoreOp::store(1, e.a, e.b));
        if (n_taken < 16) taken[n_taken] = c.now();
        n_taken++;
        pending = false;
    }
    sim::Wake step(sim::Ctx& c) {
        if (pending) present(c, stalled);
        for (; i < n && ev[i].t == c.now(); i++) {
            const Ev& e = ev[i];
            switch (e.kind) {
                case STORE: c.put(core, CoreOp::store(1, e.a, e.b)); break;
                case SSTORE:
                    if (pending) FATAL("test_ingress_egress: two stalling stores at once (cycle %llu)", (unsigned long long)c.now());
                    present(c, e);
                    break;
                case WLOAD:
                    if (n_got < MAX) got[n_got] = c.read(window, Win::reg(1, Win::R_OWN + (e.a - 32)));
                    n_got++;
                    if (own_bit(c, Win::R_POPS, e.a)) c.put(core, CoreOp::load(1, e.a));
                    break;
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
        if (pending) return c.next();
        return i < n ? sim::Wake::at_cycle(ev[i].t) : sim::Wake::none();
    }
    uint64_t digest() const { return i * 131 + n_got + 7 * n_taken + pending; }
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

// The unit's configuration (19) and its far end, for a test that has them: UART_RESET_DIVISOR (0: the chip's, 8681),
// STREAM_BYTES (the chip's, 4).
struct Setup {
    uint32_t reset_divisor = 0;
    unsigned stream_bytes = 4;
    IeFarEnd* far = nullptr;
};

struct Rig {
    sim::Sim m;
    const Core::Ev* cev;
    unsigned cn;
    Rig(const Core::Ev* ce, unsigned n_core, const Nets::Ev* ne, unsigned n_nets, uint64_t last, const sim::Options& o, const Setup& su = Setup{})
        : m(build(ce, n_core, ne, n_nets, last, o)), cev(ce), cn(n_core) {
        IngressEgress& p = ie();
        if (su.reset_divisor) p.reset_divisor = su.reset_divisor;
        p.stream_bytes = su.stream_bytes;
        p.reset(0);                                    // the reset, at the configuration given (before the first cycle)
        if (su.far) su.far->attach(p);
    }
    Rig(const std::vector<Core::Ev>& ce, uint64_t last, const sim::Options& o, const Setup& su = Setup{})
        : Rig(ce.data(), unsigned(ce.size()), nullptr, 0, last, o, su) {}
    uint64_t taken(unsigned k) { return k < core().n_taken && k < 16 ? core().taken[k] : 0; }   // stalling store k's cycle
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
            if (cev[k].kind == Core::STORE || cev[k].kind == Core::SSTORE) continue;
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

constexpr uint8_t ST = Core::STORE, LD = Core::LOAD, EV = Core::EVENTS, WL = Core::WLOAD, SS = Core::SSTORE;

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
        // EVENTS_MASK has one bit per event, eight here: a store ignores the bits above them, and they read 0 (5.4;
        // peripheral_control.veryl, EVENT_BITS)
        {10, ST, W_EVENTS_MASK, 0xFFFFFFFF},
        {11, LD, reg(W_EVENTS_MASK), 0xFF},
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

// 17.6, the UART's TX FIFO and the transmitter's pops (uart.veryl's header, "The transmitter"): UART_DIVISOR is
// R/W, [15:0]; a store to UART_TX pushes its byte into the TX FIFO of UART_FIFO_DEPTH = 16 at the end of its cycle;
// UART_STATUS [4:0] is the FIFO's bytes, and the byte being sent has left it. A frame starts at an idle point with the
// FIFO not empty: the head is popped at the end of that cycle (the cycle after its push into an empty FIFO with the
// transmitter idle), and frames follow back to back, 10 D cycles apart, D the divisor as it stands at the frame's
// start (a store to UART_DIVISOR cuts no frame). With 16 bytes in the FIFO a store to UART_TX stalls (the slot's
// R_STALL bit for the word, the reg interface's ready) and EVENTS.uart_tx, live, is 0; both end with the next pop.
// The bytes leave in order (the model's log of the bytes the transmitter took; the line itself is the tests' below).
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

// ================================================================ the UART's line and the host interface's ports
namespace {

// uart_tx's levels over the cycles [from, to), a character each.
std::string tx_levels(const IeFarEnd& f, int64_t from, int64_t to) {
    std::string s;
    for (int64_t t = from; t < to; t++) s += f.at(t).uart_tx ? '1' : '0';
    return s;
}
// A frame's levels (17.6): a start bit, 0; 8 data bits, the least significant first; a stop bit, 1; each D cycles.
std::string frame_levels(uint8_t byte, unsigned d) {
    std::string s(d, '0');
    for (unsigned k = 0; k < 8; k++) s += std::string(d, (byte >> k & 1) ? '1' : '0');
    return s + std::string(d, '1');
}
// The frames decoded off uart_tx over [from, to) are `want` (each its start bit's first cycle and its byte), every
// bit held for the divisor its frame started at.
struct WantFrame { int64_t start; uint8_t byte; };
template <unsigned N>
void check_frames(const IeFarEnd& f, int64_t from, int64_t to, const std::function<unsigned(int64_t)>& divisor, const WantFrame (&want)[N],
                  const char* file, int line) {
    std::string error;
    const std::vector<IeFarEnd::TxFrame> got = f.decode_tx(from, to, divisor, error);
    if (!error.empty()) { test::fail(file, line, "uart_tx: %s", error.c_str()); return; }
    if (got.size() != N) { test::fail(file, line, "uart_tx carried %zu frames, 17.6 says %u", got.size(), N); return; }
    for (unsigned k = 0; k < N; k++)
        if (got[k].start != want[k].start || got[k].byte != want[k].byte) {
            test::fail(file, line, "uart_tx's frame %u: 0x%02x from cycle %lld, 17.6 and the unit's timing say 0x%02x from %lld", k, got[k].byte,
                       (long long)got[k].start, want[k].byte, (long long)want[k].start);
            return;
        }
}
#define CHECK_FRAMES(far, from, to, divisor, want) check_frames(far, from, to, divisor, want, __FILE__, __LINE__)

constexpr unsigned STALL_0 = Win::R_STALL, STALL_1 = Win::R_STALL + 1;       // words 32-63, 64-95
constexpr uint32_t STALL_TX = 1u << (W_UART_TX - 32);
constexpr uint32_t STALL_SENDS = 1u << (W_STREAM_SEND - 64) | 1u << (W_STREAM_SEND_LAST - 64);

}  // namespace

// 17.6, Reset; 18: after the reset every output pin and port is idle: uart_tx 1, toast and led 0, lvds_out_tvalid 0,
// and lvds_in_tready 0 in the first cycle, 1 from the next ("from the cycle after the reset"; the unit's README: a
// flip-flop, 0 in the first cycle after the reset). With nothing offered and nothing stored they stay so.
TEST(ie_pins_and_ports_after_the_reset) {
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(nullptr, 0, nullptr, 0, 40, opts(variant), Setup{0, 4, &far});
        r.m.run(40);
        CHECK_EQ(far.first, int64_t(1));                   // the machine's first cycle
        CHECK(!far.at(1).lvds_in_tready);
        for (int64_t t = 1; t <= 40; t++) {
            const IngressEgress::Pins::Out& o = far.at(t);
            CHECK(o.uart_tx);
            CHECK(!o.toast); CHECK(!o.led);
            CHECK(!o.lvds_out_tvalid);
            if (t >= 2) CHECK(o.lvds_in_tready);
        }
        CHECK(far.got.empty()); CHECK(far.taken_at.empty());
    }
}

// 5.3, 17.6: the loads that pop are UART_RX's and STREAM_RECEIVE's and no other word's (the slot's static bits for the
// core, Win::R_POPS, from the peripheral's map).
TEST(ie_the_loads_that_pop_are_uart_rx_and_stream_receive) {
    const Core::Ev ev[] = {
        {3, LD, Win::reg(1, Win::R_POPS), 1u << (W_UART_RX - 32)},
        {3, LD, Win::reg(1, Win::R_POPS + 1), 1u << (W_STREAM_RECEIVE - 64)},
        {3, LD, Win::reg(1, Win::R_POPS + 2), 0},
    };
    Rig r(ev, count(ev), nullptr, 0, 8, opts(0));
    r.m.run(8);
    CHECK_LOADS(r);
}

// 17.6, the transmitter's line, cycle by cycle (uart.veryl's header; the unit's README, Timing): a byte stored to
// UART_TX in p into an empty FIFO, the transmitter idle, is popped at the end of p + 1 and its start bit is on uart_tx
// from p + 2: a start bit, 0, the 8 data bits, the least significant first, a stop bit, 1, each D cycles, the line 1
// before and after. Frames follow back to back, 10 D apart, while the FIFO holds bytes, and UART_STATUS [4:0] counts
// the FIFO's bytes alone: the byte on the line has left it. A frame takes UART_DIVISOR as it stands at the start of
// the cycle that pops its byte: a store during a frame, or in the pop's own cycle, is the next frame's.
TEST(ie_uart_tx_frames_on_the_line_bit_for_bit) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_UART_DIVISOR, 3},
        {5, ST, W_UART_TX, 0x1A5},                     // [7:0]: 0xA5
        {6, LD, reg(W_UART_STATUS), 1},                // in the FIFO
        {7, LD, reg(W_UART_STATUS), 0},                // on the line
        // three bytes: the first starts at 42; the others wait in the FIFO, each popped in the last cycle of the stop
        // bit before it (71, 101)
        {40, ST, W_UART_TX, 0x00}, {41, ST, W_UART_TX, 0xFF}, {42, ST, W_UART_TX, 0x80},
        {41, LD, reg(W_UART_STATUS), 1}, {42, LD, reg(W_UART_STATUS), 1}, {43, LD, reg(W_UART_STATUS), 2},
        {71, LD, reg(W_UART_STATUS), 2}, {72, LD, reg(W_UART_STATUS), 1},
        {101, LD, reg(W_UART_STATUS), 1}, {102, LD, reg(W_UART_STATUS), 0},
        // the divisor: 0x55 from 142 at 3 (to 171); a store of 2 in its frame; 0x33 from 172 at 2 (to 191); 0x0F is
        // popped in 191, and a store of 4 presented in 191 is not its frame's: from 192 at 2 (to 211); 0xC3 from 212 at 4
        {140, ST, W_UART_TX, 0x55},
        {145, ST, W_UART_DIVISOR, 2},
        {146, LD, reg(W_UART_DIVISOR), 2},
        {147, ST, W_UART_TX, 0x33},
        {180, ST, W_UART_TX, 0x0F},
        {191, ST, W_UART_DIVISOR, 4},
        {200, ST, W_UART_TX, 0xC3},
    };
    std::vector<Core::Ev> sorted = ev;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Core::Ev& a, const Core::Ev& b) { return a.t < b.t; });
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(sorted, 270, opts(variant), Setup{0, 4, &far});
        r.m.run(270);
        CHECK_LOADS(r);
        // 0xA5's frame at D = 3, written out: the line 1 through 6, the start bit in 7-9, the bits 1 0 1 0 0 1 0 1,
        // the stop bit in 34-36, and 1 after it
        CHECK(tx_levels(far, 1, 7) == "111111");
        CHECK(tx_levels(far, 7, 37) == "000" "111" "000" "111" "000" "000" "111" "000" "111" "111");
        CHECK(tx_levels(far, 37, 42) == "11111");
        const WantFrame want[] = {{7, 0xA5}, {42, 0x00}, {72, 0xFF}, {102, 0x80}, {142, 0x55}, {172, 0x33}, {192, 0x0F}, {212, 0xC3}};
        CHECK_FRAMES(far, 1, 270, [](int64_t start) { return start < 172 ? 3u : start < 212 ? 2u : 4u; }, want);
        CHECK(tx_levels(far, 252, 270) == std::string(18, '1'));   // idle after the last stop bit (212 + 40)
        CHECK_EQ(r.ie().serial_n, 8u);
    }
}

// 17.6, 19: every bit lasts UART_DIVISOR cycles, at divisors 1 to 6 and at the chip's UART_RESET_DIVISOR, 8681 (a
// frame of 86,810 cycles): two bytes stored in 5 and 6, their frames from 7 and from 7 + 10 D.
TEST(ie_uart_tx_every_bit_lasts_the_divisor) {
    for (unsigned d : {1u, 2u, 3u, 4u, 5u, 6u, 8681u}) {
        const std::vector<Core::Ev> ev = {
            {3, LD, reg(W_UART_DIVISOR), d},           // UART_RESET_DIVISOR after the reset
            {5, ST, W_UART_TX, 0x96}, {6, ST, W_UART_TX, 0x69},
        };
        IeFarEnd far;
        const uint64_t last = 7 + 20 * uint64_t(d) + 5;
        Rig r(ev, last, opts(0), Setup{d, 4, &far});
        r.m.run(last);
        CHECK_LOADS(r);
        CHECK(tx_levels(far, 7, 7 + 10 * int64_t(d)) == frame_levels(0x96, d));
        const WantFrame want[] = {{7, 0x96}, {7 + 10 * int64_t(d), 0x69}};
        CHECK_FRAMES(far, 1, int64_t(last), [d](int64_t) { return d; }, want);
    }
}

// 17.6; register_bus.veryl and uart.veryl (lines_ready = !tx_full, from the FIFO's count at the cycle's start): a
// UART_TX store presented with the TX FIFO full stalls, a pop in that cycle notwithstanding, and is taken in the first
// cycle at whose start the FIFO has room: the cycle after the transmitter pops a byte, which is the last cycle of a
// stop bit. At D = 20: 'A' (stored in 5) is on the line from 7 to 206, 'B'-'Q' (6-21) fill the FIFO, and 'R',
// presented from 22, is taken in 207, the cycle after 'B' is popped (206); the FIFO is full again from 208, and 'S',
// presented from 208, is taken in 407. EVENTS.uart_tx, live, is 0 exactly while the FIFO is full.
TEST(ie_uart_tx_a_stalled_store_is_taken_the_cycle_after_the_pop) {
    std::vector<Core::Ev> ev;
    for (unsigned k = 0; k < 17; k++) ev.push_back({5 + k, ST, W_UART_TX, uint32_t('A' + k)});
    ev.push_back({22, LD, Win::reg(1, STALL_0), STALL_TX});
    ev.push_back({22, SS, W_UART_TX, 'R'});
    ev.push_back({206, LD, Win::reg(1, STALL_0), STALL_TX});
    ev.push_back({206, LD, reg(W_UART_STATUS), 16});
    ev.push_back({206, LD, reg(W_EVENTS_PEEK), EV_STREAM_OUT});
    ev.push_back({207, LD, Win::reg(1, STALL_0), 0});
    ev.push_back({207, LD, reg(W_UART_STATUS), 15});
    ev.push_back({207, LD, reg(W_EVENTS_PEEK), EV_IDLE});
    ev.push_back({208, LD, Win::reg(1, STALL_0), STALL_TX});
    ev.push_back({208, LD, reg(W_UART_STATUS), 16});
    ev.push_back({208, LD, reg(W_EVENTS_PEEK), EV_STREAM_OUT});
    ev.push_back({208, SS, W_UART_TX, 'S'});
    ev.push_back({408, LD, reg(W_UART_STATUS), 16});
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(ev, 420, opts(variant), Setup{20, 4, &far});
        r.m.run(420);
        CHECK_LOADS(r);
        CHECK_EQ(r.core().n_taken, 2u);
        CHECK_EQ(r.taken(0), uint64_t(207));
        CHECK_EQ(r.taken(1), uint64_t(407));
        CHECK(far.at(6).uart_tx); CHECK(!far.at(7).uart_tx);          // 'A's start bit
        CHECK(far.at(206).uart_tx); CHECK(!far.at(207).uart_tx);      // its stop bit's last cycle; 'B's start bit
        CHECK(far.at(406).uart_tx); CHECK(!far.at(407).uart_tx);
        CHECK_EQ(r.ie().serial_n, 3u);
        CHECK(std::string(r.ie().serial_out, 3) == "ABC");
    }
}

// 17.6, the receiver (uart.veryl's header): two flip-flops synchronise uart_rx, so a falling edge on the pin in cycle c
// starts a frame in e = c + 2; bit k (1-8 data, the least significant first; 9 the stop bit) is sampled at its middle,
// in e + k D + floor(D / 2); the byte enters the RX FIFO at the end of the stop bit's sample, f = e + 9 D + floor(D / 2):
// a load in f reads no byte (and pops none), a load in f + 1 reads it with `valid`. UART_STATUS [12:8] counts the RX
// FIFO; EVENTS.uart_rx is live, a byte in the RX FIFO, and with EVENTS_MASK raises the tile's EVENTS.peripheral_1.
// UART_RX pops at the end of its load's cycle; with the FIFO empty it reads 0 and pops nothing. At D = 4:
//   - 0x1E on the pin from 20, 4 cycles a bit: f = 60;
//   - 0xB2 from 100 as a frame only the specified sample cycles read (ie_far_end.h, needle_frame: a start bit of one
//     cycle, each data bit's level on the pin in the one cycle c + 4 k + 2 alone, the opposite level in the cycles
//     beside it; the falling edges inside it start nothing): f = 140;
//   - 0xC1 from 200 and 0x3C from 240, back to back: the first ends at its stop bit's middle, 240, so the second's
//     edge, seen in 242, starts a frame: f = 240 and 280.
TEST(ie_uart_rx_frames_sampled_at_each_bits_middle) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_EVENTS_MASK, EV_UART_RX},
        {4, LD, reg(W_UART_DIVISOR), 4},
        {59, LD, reg(W_UART_STATUS), 0},
        {60, WL, W_UART_RX, 0},                        // the byte enters at the end of 60: nothing read, nothing popped
        {60, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {60, LD, LIVE, 0},
        {61, LD, reg(W_UART_RX), RX_VALID | 0x1E},
        {61, LD, reg(W_UART_STATUS), 1u << 8},
        {61, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_UART_RX},
        {61, LD, LIVE, LIVE_EVENT},
        {62, WL, W_UART_RX, RX_VALID | 0x1E},          // popped at the end of 62
        {63, LD, reg(W_UART_RX), 0},
        {63, LD, reg(W_UART_STATUS), 0},
        {63, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {63, LD, LIVE, 0},
        {140, LD, reg(W_UART_STATUS), 0},
        {141, WL, W_UART_RX, RX_VALID | 0xB2},
        {142, LD, reg(W_UART_STATUS), 0},
        {240, LD, reg(W_UART_STATUS), 0},
        {241, LD, reg(W_UART_STATUS), 1u << 8},
        {280, LD, reg(W_UART_STATUS), 1u << 8},
        {281, LD, reg(W_UART_STATUS), 2u << 8},
        {281, WL, W_UART_RX, RX_VALID | 0xC1},         // in order
        {282, WL, W_UART_RX, RX_VALID | 0x3C},
        {283, LD, reg(W_UART_STATUS), 0},
        {283, WL, W_UART_RX, 0},                       // empty: 0, and nothing to pop
        {284, LD, reg(W_ERRORS), 0},
        {284, LD, reg(W_UART_STATUS), 0},              // no line error
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x1E, 4);
        far.needle_frame(100, 0xB2, 4);
        far.frame(200, 0xC1, 4);
        far.frame(240, 0x3C, 4);
        Rig r(ev, 300, opts(variant), Setup{4, 4, &far});
        r.m.run(300);
        CHECK_LOADS(r);
        CHECK(tx_levels(far, 1, 300) == std::string(299, '1'));        // nothing sent
    }
}

// 17.6: a frame whose stop bit is 0 is dropped and sets framing_error, UART_STATUS [17], sticky; a UART_STATUS store
// clears each line error it writes 1 and ignores its other bits; an error raised in the store's cycle (at its end, the
// stop bit's sample) stays set. Line errors are status: ERRORS stays 0. At D = 4: a bad frame from 20 (f = 60), a good
// one from 80 (f = 120), a bad one from 140 (f = 180) with a clearing store presented in 180.
TEST(ie_uart_rx_a_stop_bit_of_0_drops_the_frame_and_a_clear_in_the_errors_cycle_loses) {
    const std::vector<Core::Ev> ev = {
        {60, LD, reg(W_UART_STATUS), 0},
        {61, LD, reg(W_UART_STATUS), FRAMING_ERROR},   // dropped: no byte
        {61, LD, reg(W_UART_RX), 0},
        {61, LD, reg(W_ERRORS), 0},
        {61, LD, LIVE, 0},
        {62, ST, W_UART_STATUS, RX_OVERRUN},           // the other error's bit: nothing
        {63, LD, reg(W_UART_STATUS), FRAMING_ERROR},
        {63, ST, W_UART_STATUS, FRAMING_ERROR | 0xFFFF},   // its bit, and the counts' bits, which a store ignores
        {64, LD, reg(W_UART_STATUS), 0},
        {64, LD, reg(W_UART_DIVISOR), 4},
        {121, LD, reg(W_UART_STATUS), 1u << 8},        // the good frame
        {121, LD, reg(W_UART_RX), RX_VALID | 0x42},
        {180, ST, W_UART_STATUS, FRAMING_ERROR},       // presented in the cycle the second bad frame ends
        {181, LD, reg(W_UART_STATUS), 1u << 8 | FRAMING_ERROR},   // raised at the end of 180: it stays
        {181, ST, W_UART_STATUS, FRAMING_ERROR},
        {182, LD, reg(W_UART_STATUS), 1u << 8},
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x77, 4, false);
        far.frame(80, 0x42, 4);
        far.frame(140, 0x11, 4, false);
        Rig r(ev, 200, opts(variant), Setup{4, 4, &far});
        r.m.run(200);
        CHECK_LOADS(r);
        CHECK(!r.ie().toast);                          // noise on the line never raises the toast pin
    }
}

// 17.6: a frame that ends with the RX FIFO full is dropped and sets rx_overrun, UART_STATUS [16], sticky; "full" is
// the FIFO's 16 bytes at the start of the cycle the frame ends in, a pop in that cycle notwithstanding (uart.veryl;
// the unit's README, Decisions). At D = 2 a frame from c ends in f = c + 21. Seventeen frames, 0x30 up, back to back
// from 20: the sixteenth fills the FIFO (f = 341), the seventeenth (f = 361) is dropped. A frame from 380 ends in 401,
// the cycle of a UART_RX load, which pops 0x30: dropped all the same. A frame from 420 (0x52) finds room.
TEST(ie_uart_rx_a_frame_ending_with_the_fifo_full_is_dropped_whatever_pops) {
    std::vector<Core::Ev> ev = {
        {341, LD, reg(W_UART_STATUS), 15u << 8},
        {342, LD, reg(W_UART_STATUS), 16u << 8},
        {361, LD, reg(W_UART_STATUS), 16u << 8},
        {362, LD, reg(W_UART_STATUS), 16u << 8 | RX_OVERRUN},
        {362, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_UART_RX},
        {362, LD, reg(W_ERRORS), 0},
        {363, ST, W_UART_STATUS, RX_OVERRUN},
        {364, LD, reg(W_UART_STATUS), 16u << 8},
        {401, WL, W_UART_RX, RX_VALID | 0x30},         // the oldest byte, popped at the end of 401
        {402, LD, reg(W_UART_STATUS), 15u << 8 | RX_OVERRUN},
        {442, LD, reg(W_UART_STATUS), 16u << 8 | RX_OVERRUN},
    };
    for (unsigned k = 1; k < 16; k++) ev.push_back({450 + k - 1, WL, W_UART_RX, RX_VALID | (0x30 + k)});   // in order
    ev.push_back({465, WL, W_UART_RX, RX_VALID | 0x52});
    ev.push_back({466, LD, reg(W_UART_STATUS), RX_OVERRUN});
    ev.push_back({466, WL, W_UART_RX, 0});
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        for (unsigned k = 0; k < 17; k++) far.frame(20 + 20 * k, uint8_t(0x30 + k), 2);
        far.frame(380, 0x51, 2);
        far.frame(420, 0x52, 2);
        Rig r(ev, 480, opts(variant), Setup{2, 4, &far});
        r.m.run(480);
        CHECK_LOADS(r);
    }
}

// 17.6: the receiver takes UART_DIVISOR at a frame's start (the cycle its falling edge is seen in), so a store during
// a frame, or in that very cycle, is the next frame's; and "the start bit is not checked, so a glitch starts a frame":
// one cycle of 0 on the pin is a frame of 1s. From D = 4: 0x9A from 20 at 4 a bit, UART_DIVISOR 2 stored in 30: f = 60
// still; 0x65 from 70 at 2 a bit: seen in 72, f = 91; 0x0F from 100 at 2 a bit, UART_DIVISOR 6 stored in 102, the
// cycle its edge is seen in: f = 121; 0xE1 from 130 at 6 a bit: seen in 132, f = 132 + 54 + 3 = 189; a glitch in 200:
// 0xFF, f = 202 + 57 = 259.
TEST(ie_uart_rx_the_divisor_at_a_frames_start_and_a_glitch_is_a_frame_of_ones) {
    const std::vector<Core::Ev> ev = {
        {30, ST, W_UART_DIVISOR, 2},
        {60, LD, reg(W_UART_STATUS), 0},
        {61, LD, reg(W_UART_RX), RX_VALID | 0x9A},
        {91, LD, reg(W_UART_STATUS), 1u << 8},
        {92, LD, reg(W_UART_STATUS), 2u << 8},
        {102, ST, W_UART_DIVISOR, 6},
        {121, LD, reg(W_UART_STATUS), 2u << 8},
        {122, LD, reg(W_UART_STATUS), 3u << 8},
        {189, LD, reg(W_UART_STATUS), 3u << 8},
        {190, LD, reg(W_UART_STATUS), 4u << 8},
        {259, LD, reg(W_UART_STATUS), 4u << 8},
        {260, LD, reg(W_UART_STATUS), 5u << 8},
        {261, WL, W_UART_RX, RX_VALID | 0x9A}, {262, WL, W_UART_RX, RX_VALID | 0x65}, {263, WL, W_UART_RX, RX_VALID | 0x0F},
        {264, WL, W_UART_RX, RX_VALID | 0xE1}, {265, WL, W_UART_RX, RX_VALID | 0xFF},
        {266, LD, reg(W_UART_STATUS), 0},
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x9A, 4);
        far.frame(70, 0x65, 2);
        far.frame(100, 0x0F, 2);
        far.frame(130, 0xE1, 6);
        far.rx_level(200, false);
        Rig r(ev, 280, opts(variant), Setup{4, 4, &far});
        r.m.run(280);
        CHECK_LOADS(r);
    }
}

// 17.6, the host interface, out (ingress_egress_unit.veryl's header; the unit's README, Timing), at the chip's
// STREAM_BYTES, 4: a beat is one word, its top, so STREAM_OUT_W0-W2 hold nothing and a beat is one store. A send
// presented in p pushes the beat at the end of p: lvds_out_tvalid is 1 from p + 1 if the FIFO was empty, with the
// beat's TDATA and its TLAST (0 for STREAM_SEND, 1 for STREAM_SEND_LAST), held until the handshake, the cycle in which
// TREADY is 1 too; the next beat, or TVALID 0, follows in the next cycle. The FIFO holds STREAM_DEPTH = 4 beats: with
// 4 at a cycle's start a send stalls (stream_ready) and EVENTS.stream_out, live, is 0; a stalled send is taken in the
// cycle after a handshake. A handshake and a send in one cycle: the FIFO stays at one beat, TVALID stays 1.
TEST(ie_stream_out_beats_valid_held_until_the_handshake_and_a_full_fifo_stalls_the_send) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_STREAM_OUT_W0, 0xDEAD},              // word 0 is the top: it holds nothing
        {5, ST, W_STREAM_SEND, 0x11111111},            // handshake in 10
        {12, ST, W_STREAM_SEND_LAST, 0x22222222}, {13, ST, W_STREAM_SEND, 0x33333333}, {14, ST, W_STREAM_SEND, 0x44444444},
        {15, LD, Win::reg(1, STALL_1), 0},             // three beats at the start of 15
        {15, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {15, ST, W_STREAM_SEND_LAST, 0x55555555},
        {16, LD, Win::reg(1, STALL_1), STALL_SENDS},   // four: both sends stall
        {16, LD, reg(W_EVENTS_PEEK), EV_UART_TX},      // stream_out 0
        {16, SS, W_STREAM_SEND, 0x66666666},           // held until the handshake of 20 has made room: taken in 21
        {20, LD, Win::reg(1, STALL_1), STALL_SENDS},
        {21, LD, Win::reg(1, STALL_1), 0},
        {21, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {22, LD, Win::reg(1, STALL_1), STALL_SENDS},
        {22, LD, reg(W_STATUS), 0},                    // no beat in: stream_last 0
        {22, LD, reg(W_STREAM_SEND), 0},               // W: a load reads 0
        {40, ST, W_STREAM_SEND, 0x77777777},           // handshake in 42 ...
        {42, ST, W_STREAM_SEND_LAST, 0x88888888},      // ... and a send in 42: handshake in 43
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.ready = [](int64_t t) { return t == 10 || t == 20 || (t >= 30 && t <= 33) || t == 42 || t == 43; };
        Rig r(ev, 50, opts(variant), Setup{0, 4, &far});
        r.m.run(50);
        CHECK_LOADS(r);
        CHECK_EQ(r.taken(0), uint64_t(21));
        CHECK(!far.at(5).lvds_out_tvalid);             // the send's own cycle
        for (int64_t t = 6; t <= 10; t++) {            // held, unchanged, until its handshake
            CHECK(far.at(t).lvds_out_tvalid);
            CHECK_EQ(far.at(t).lvds_out_tdata[0], 0x11111111u);
            CHECK(!far.at(t).lvds_out_tlast);
            for (unsigned k = 1; k < 4; k++) CHECK_EQ(far.at(t).lvds_out_tdata[k], 0u);
        }
        CHECK(!far.at(11).lvds_out_tvalid); CHECK(!far.at(12).lvds_out_tvalid);
        CHECK(far.at(13).lvds_out_tvalid); CHECK(far.at(13).lvds_out_tlast);
        CHECK(far.at(21).lvds_out_tvalid); CHECK_EQ(far.at(21).lvds_out_tdata[0], 0x33333333u);   // the next, the cycle after
        CHECK(!far.at(34).lvds_out_tvalid);
        CHECK(!far.at(40).lvds_out_tvalid); CHECK(far.at(41).lvds_out_tvalid);
        CHECK(far.at(43).lvds_out_tvalid); CHECK_EQ(far.at(43).lvds_out_tdata[0], 0x88888888u); CHECK(far.at(43).lvds_out_tlast);
        CHECK(!far.at(44).lvds_out_tvalid);
        const struct { int64_t t; uint32_t data; bool last; } want[] = {
            {10, 0x11111111, false}, {20, 0x22222222, true}, {30, 0x33333333, false}, {31, 0x44444444, false},
            {32, 0x55555555, true}, {33, 0x66666666, false}, {42, 0x77777777, false}, {43, 0x88888888, true}};
        CHECK_EQ(far.got.size(), sizeof want / sizeof want[0]);
        for (size_t k = 0; k < far.got.size() && k < sizeof want / sizeof want[0]; k++) {
            CHECK_EQ(far.got[k].t, want[k].t);
            CHECK_EQ(far.got[k].w[0], want[k].data);
            CHECK_EQ(far.got[k].last, want[k].last);
        }
        CHECK_EQ(far.out_violations, uint64_t(0));
    }
}

// 17.6, a beat's words: STREAM_BEAT_WORDS = ceil(STREAM_BYTES / 4), word w TDATA's bits [32w + 31 : 32w], the top word
// the last. STREAM_OUT_Ww holds word w below the top; the send's store is the top word and clears STREAM_OUT_W0-W2; a
// word's bits past TDATA and the words at or above the top are ignored on a store. At 13 bytes (4 words, 8 bits in the
// top): a beat of four stores, then one whose W0 and W2 the first send cleared. At 6 bytes (2 words, 16 bits in the
// top): W1 and W2 hold nothing.
TEST(ie_stream_out_a_beats_words_below_the_top_are_cleared_by_the_send) {
    {
        const std::vector<Core::Ev> ev = {
            {3, ST, W_STREAM_OUT_W0, 0xA0A0A0A0}, {4, ST, W_STREAM_OUT_W0 + 1, 0xB1B1B1B1}, {5, ST, W_STREAM_OUT_W0 + 2, 0xC2C2C2C2},
            {6, ST, W_STREAM_SEND_LAST, 0xFFFFFFAB},
            {7, LD, reg(W_STREAM_OUT_W0), 0},          // W: a load reads 0
            {8, ST, W_STREAM_OUT_W0 + 1, 0x0D0D0D0D},
            {9, ST, W_STREAM_SEND, 0x1CD},
        };
        for (int variant = 0; variant < 3; variant++) {
            IeFarEnd far;
            far.ready = [](int64_t t) { return t >= 20; };
            Rig r(ev, 30, opts(variant), Setup{0, 13, &far});
            r.m.run(30);
            CHECK_LOADS(r);
            CHECK_EQ(far.got.size(), size_t(2));
            if (far.got.size() != 2) return;
            const uint32_t first[4] = {0xA0A0A0A0, 0xB1B1B1B1, 0xC2C2C2C2, 0xAB}, second[4] = {0, 0x0D0D0D0D, 0, 0xCD};
            for (unsigned k = 0; k < 4; k++) { CHECK_EQ(far.got[0].w[k], first[k]); CHECK_EQ(far.got[1].w[k], second[k]); }
            CHECK(far.got[0].last); CHECK(!far.got[1].last);
            CHECK_EQ(far.got[0].t, int64_t(20)); CHECK_EQ(far.got[1].t, int64_t(21));
            CHECK(far.at(7).lvds_out_tvalid); CHECK_EQ(far.at(7).lvds_out_tdata[3], 0xABu);
        }
    }
    {
        const std::vector<Core::Ev> ev = {
            {3, ST, W_STREAM_OUT_W0, 0xA0A0A0A0}, {4, ST, W_STREAM_OUT_W0 + 1, 0xB1B1B1B1}, {5, ST, W_STREAM_OUT_W0 + 2, 0xC2C2C2C2},
            {6, ST, W_STREAM_SEND, 0x12345678},
            {7, ST, W_STREAM_SEND_LAST, 0x9ABCDEF0},
        };
        IeFarEnd far;
        far.ready = [](int64_t t) { return t >= 20; };
        Rig r(ev, 30, opts(0), Setup{0, 6, &far});
        r.m.run(30);
        CHECK_EQ(far.got.size(), size_t(2));
        if (far.got.size() != 2) return;
        const uint32_t first[4] = {0xA0A0A0A0, 0x5678, 0, 0}, second[4] = {0, 0xDEF0, 0, 0};
        for (unsigned k = 0; k < 4; k++) { CHECK_EQ(far.got[0].w[k], first[k]); CHECK_EQ(far.got[1].w[k], second[k]); }
        CHECK(!far.got[0].last); CHECK(far.got[1].last);
    }
}

// 17.6, the host interface, in (the unit's README, Timing), at 4 bytes: lvds_in_tready is 1 while the receive FIFO is
// not full (0 in the first cycle after the reset); a beat enters at the end of its handshake's cycle and is read from
// the next: STREAM_RECEIVE reads the oldest beat's top word and pops it at the end of the load's cycle; STATUS [2]
// stream_last is the oldest beat's TLAST; EVENTS.stream_in, live, a beat in the FIFO. With the FIFO empty at a cycle's
// start the words read 0 and STREAM_RECEIVE pops nothing, a beat entering at that cycle's end included. A load of
// STREAM_IN_Ww pops nothing. Four beats fill the FIFO: TREADY falls the cycle after the fourth handshake, the fifth
// beat is held, and TREADY rises the cycle after a pop.
TEST(ie_stream_in_beats_read_from_the_cycle_after_the_handshake_and_tready_follows_the_fifo) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_EVENTS_MASK, EV_STREAM_IN},
        {5, WL, W_STREAM_RECEIVE, 0},                  // the handshake's cycle: empty at its start, nothing popped
        {5, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {6, LD, reg(W_STREAM_RECEIVE), 0xAAAA0001},
        {6, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_STREAM_IN},
        {6, LD, LIVE, LIVE_EVENT},
        {6, LD, reg(W_STATUS), 0},
        {6, WL, W_STREAM_IN_W0, 0},                    // word 0 is the top: STREAM_IN_W0 reads 0, and pops nothing
        {7, LD, reg(W_STREAM_RECEIVE), 0xAAAA0001},
        {8, LD, reg(W_STATUS), 0},                     // the oldest beat's TLAST, not the second's
        {8, WL, W_STREAM_RECEIVE, 0xAAAA0001},         // popped at the end of 8
        {9, LD, reg(W_STREAM_RECEIVE), 0xBBBB0002},
        {9, LD, reg(W_STATUS), STATUS_STREAM_LAST},
        {15, WL, W_STREAM_RECEIVE, 0xBBBB0002},        // makes room: TREADY from 16
        {16, LD, reg(W_STATUS), 0},
        {20, WL, W_STREAM_RECEIVE, 0xC3}, {21, WL, W_STREAM_RECEIVE, 0xC4},
        {22, LD, reg(W_STATUS), STATUS_STREAM_LAST},
        {22, WL, W_STREAM_RECEIVE, 0xC5}, {23, WL, W_STREAM_RECEIVE, 0xC6},
        {24, LD, reg(W_STREAM_RECEIVE), 0},
        {24, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {24, LD, reg(W_STATUS), 0},
        {24, LD, LIVE, 0},
        {24, WL, W_STREAM_RECEIVE, 0},                 // empty: pops nothing
        {31, LD, reg(W_STREAM_RECEIVE), 0xD7},         // the next beat is whole
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.offers = {{5, {0xAAAA0001}, false}, {7, {0xBBBB0002}, true}, {10, {0xC3}, false}, {10, {0xC4}, false}, {10, {0xC5}, true},
                      {13, {0xC6}, false}, {30, {0xD7}, false}};
        Rig r(ev, 40, opts(variant), Setup{0, 4, &far});
        r.m.run(40);
        CHECK_LOADS(r);
        const int64_t want[] = {5, 7, 10, 11, 12, 16, 30};
        CHECK_EQ(far.taken_at.size(), sizeof want / sizeof want[0]);
        for (size_t k = 0; k < far.taken_at.size() && k < sizeof want / sizeof want[0]; k++) CHECK_EQ(far.taken_at[k], want[k]);
        CHECK(!far.at(1).lvds_in_tready);
        for (int64_t t = 2; t <= 12; t++) CHECK(far.at(t).lvds_in_tready);
        for (int64_t t = 13; t <= 15; t++) CHECK(!far.at(t).lvds_in_tready);   // four beats; the fifth held
        CHECK(far.at(16).lvds_in_tready);
        for (int64_t t = 17; t <= 20; t++) CHECK(!far.at(t).lvds_in_tready);   // four again, until the pop of 20
        CHECK(far.at(21).lvds_in_tready);
        CHECK_EQ(far.held_valid, int64_t(3));
    }
}

// 17.6, a beat's words in: STREAM_IN_Ww reads word w below the oldest beat's top, STREAM_RECEIVE its top word; the
// bits past TDATA and the words at or above the top read 0. At 13 bytes: W0-W2 and 8 bits of the top; at 6 bytes: W0
// and 16 bits of the top, W1 and W2 reading 0.
TEST(ie_stream_in_a_beats_words) {
    {
        const std::vector<Core::Ev> ev = {
            {6, WL, W_STREAM_IN_W0, 0x01010101}, {7, WL, W_STREAM_IN_W0 + 1, 0x02020202}, {8, WL, W_STREAM_IN_W0 + 2, 0x03030303},
            {9, LD, reg(W_STATUS), STATUS_STREAM_LAST},
            {9, WL, W_STREAM_RECEIVE, 0xEE},
            {10, LD, reg(W_STREAM_IN_W0), 0},
            {10, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        };
        for (int variant = 0; variant < 3; variant++) {
            IeFarEnd far;
            far.offers = {{5, {0x01010101, 0x02020202, 0x03030303, 0xFFFFFFEE}, true}};
            Rig r(ev, 20, opts(variant), Setup{0, 13, &far});
            r.m.run(20);
            CHECK_LOADS(r);
        }
    }
    {
        const std::vector<Core::Ev> ev = {
            {6, LD, reg(W_STREAM_IN_W0), 0x01010101}, {6, LD, reg(W_STREAM_IN_W0 + 1), 0}, {6, LD, reg(W_STREAM_IN_W0 + 2), 0},
            {6, LD, reg(W_STREAM_RECEIVE), 0x2222},
        };
        IeFarEnd far;
        far.offers = {{5, {0x01010101, 0xFFFF2222, 0x33333333, 0x44444444}, false}};
        Rig r(ev, 20, opts(0), Setup{0, 6, &far});
        r.m.run(20);
        CHECK_LOADS(r);
    }
}

// 17.6, Reset, of the UART and the host interface, on the model itself (as ie_reset_zeroes_its_state): a byte and a
// framing error received, bytes waiting in the TX FIFO behind a frame on the line, a beat in, a beat out, a low word
// stored, a divisor stored; then the reset: the line errors and STREAM_OUT_W0-W2 are 0, the four FIFOs are empty,
// UART_DIVISOR is UART_RESET_DIVISOR, uart_tx is 1, lvds_out_tvalid 0, and lvds_in_tready 0 until the first edge.
TEST(ie_reset_empties_the_fifos_and_idles_the_pins) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_STREAM_OUT_W0, 0x12345678},
        {4, ST, W_STREAM_OUT_W0 + 1, 0x9ABCDEF0},
        {5, ST, W_STREAM_SEND, 0x21},
        {6, ST, W_STREAM_OUT_W0, 0x0F0F0F0F},          // a low word left stored
        {7, ST, W_UART_TX, 'x'}, {8, ST, W_UART_TX, 'y'},
        {9, ST, W_UART_DIVISOR, 9},
        {190, ST, W_UART_TX, 'p'}, {191, ST, W_UART_TX, 'q'}, {192, ST, W_UART_TX, 'r'},   // 'p' on the line from 192
        {200, LD, reg(W_UART_STATUS), 2u | 1u << 8 | FRAMING_ERROR},
        {200, LD, reg(W_EVENTS_PEEK), EV_IDLE | EV_UART_RX | EV_STREAM_IN},
        {200, LD, reg(W_STATUS), STATUS_STREAM_LAST},
    };
    IeFarEnd far;
    far.frame(10, 0x5C, 9);                            // seen in 12, at the divisor stored in 9: f = 12 + 81 + 4 = 97
    far.frame(110, 0x00, 9, false);                    // f = 197: a framing error
    far.offers = {{20, {1, 2, 3, 4}, true}};
    Rig r(ev, 200, opts(0), Setup{7, 13, &far});
    r.m.run_until(200);
    CHECK_LOADS(r);
    IngressEgress& ie = r.ie();
    CHECK(ie.out().lvds_out_tvalid);
    CHECK(!ie.out().uart_tx);                          // 'p's start bit (192-200 at D = 9)
    ie.reset(201);
    auto word = [&](unsigned w) { uint32_t v = ~0u; ie.word(72, int(w), v); return v; };
    for (unsigned w : {unsigned(W_UART_RX), unsigned(W_UART_STATUS), unsigned(W_STREAM_IN_W0), unsigned(W_STREAM_IN_W0 + 1),
                       unsigned(W_STREAM_IN_W0 + 2), unsigned(W_STREAM_RECEIVE)})
        CHECK_EQ(word(w), 0u);
    CHECK_EQ(word(W_UART_DIVISOR), 7u);
    CHECK_EQ(ie.status_bits(72), 0u);
    ie.sync(72);
    CHECK_EQ(ie.events().value(72), EV_IDLE);
    IngressEgress::Pins::Out o = ie.out();
    CHECK(o.uart_tx); CHECK(!o.lvds_out_tvalid); CHECK(!o.lvds_in_tready); CHECK(!o.toast); CHECK(!o.led);
    // STREAM_OUT_W0-W2 are 0: a send after the reset carries no word stored before it
    ie.boundary = nullptr;
    CHECK_EQ(ie.write_word(72, W_STREAM_SEND_LAST, 0x7F), 1);
    o = ie.out();
    CHECK(o.lvds_out_tvalid);
    CHECK_EQ(o.lvds_out_tdata[0], 0u); CHECK_EQ(o.lvds_out_tdata[1], 0u); CHECK_EQ(o.lvds_out_tdata[2], 0u); CHECK_EQ(o.lvds_out_tdata[3], 0x7Fu);
    ie.step(73);                                       // the first edge after the reset: TREADY rises, the line stays idle
    o = ie.out();
    CHECK(o.lvds_in_tready); CHECK(o.uart_tx);
    CHECK_EQ(ie.tx_n, 0u); CHECK_EQ(ie.rx_n, 0u);
}
