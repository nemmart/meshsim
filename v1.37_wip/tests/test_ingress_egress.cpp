// test_ingress_egress: Ingress/Egress, the master tile's peripheral 1 (machine-spec 17.6), from the specification
// (17.1, 17.2, 17.6, 18, 5.3, 5.4) and, where it is silent, the RTL's rtl/blocks/ingress_egress_unit (README.md and
// src/ingress_egress_unit.veryl, src/uart.veryl) and register_bus.veryl, which v1.36's version of this test was
// written from. Every expectation is written here from those texts, never taken from the simulator. v1.37's stage 3
// (the INTEGRATOR): 17.6.1's words and bits (UART_TX and UART_RX in block 1, UART_CONTROL at 42, the queues' words at
// 64 to 67), the UART's control (17.6.9: off at the reset, its enables, its line errors in STATUS until its reset),
// Egress (17.6.8) and the parameters; v1.36's LVDS stream is gone. The link's side: test_ingress_link.
//
// THE BENCH. Ingress/Egress sits in the master tile's slot (PeriphSlot, tile 64, its DMA's packets kept by the slot),
// between two stands:
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
//   Far    the far end of the serial line (tests/ie_far_end.h), which is not chip hardware: uart_rx's level by cycle;
//          uart_tx's traced and decoded; every output's level in every cycle is traced. A test without one runs with
//          the line in idle (1).
// Each script runs plain, with step-all, and shuffled. A test whose UART runs from the start sets UART_CONTROL before
// the first cycle (Setup::uart), as a store before the reset ended would (17.6.9: it is 0 at the reset).
//
// CYCLES are the simulator's (a machine's first cycle is 1). The UART's are written from the RTL unit's README
// (Timing) and its sources' headers, in cycles after the store's or the load's presentation p:
//   UART_TX into an empty FIFO, the transmitter idle: the start bit on uart_tx from p + 2; frames back to back 10 D
//   apart; a frame on uart_rx whose falling edge is on the pin in c: seen in e = c + 2, bit k sampled in e + k D +
//   floor(D / 2), the character in the RX FIFO at the end of the stop bit's sample f = e + 9 D + floor(D / 2), read from
//   f + 1; a stalled store: taken in the first cycle at whose start its FIFO is not full.
//
// NOT CHECKED HERE: an error line's cycle through the chip's wiring (a cycle later than ERROR_LATENCY says:
// OPEN-v1.36.md, (a)) is the chip's, not this unit's: here a level is read in the cycle it is driven.
// The UART through the master's core, on the chip: test_rtl_stream.
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

// ---------------------------------------------------------------- 17.1's and 17.6.1's window words, from their tables
enum : unsigned {
    W_PARAMS = 0, W_STATUS = 1, W_ERRORS = 2, W_EVENTS = 3, W_EVENTS_PEEK = 4, W_EVENTS_MASK = 5, W_START = 6,
    W_DMA_HDR = 16, W_DMA_DEST_ADDR = 17, W_DMA_START = 18, W_UART_TX = 30, W_UART_RX = 31,
    W_ERROR_LINES_0 = 32, W_ERROR_LINES_1 = 33, W_ERROR_LINES_2 = 34, W_ARRIVALS_0 = 35, W_ARRIVALS_1 = 36,
    W_ARRIVE_EXPECT_0 = 37, W_ARRIVE_EXPECT_1 = 38, W_RELEASE = 39, W_WATCHDOG = 40, W_LED = 41, W_UART_CONTROL = 42,
    W_FOREGROUND_WORDS = 64, W_BACKGROUND_WORDS = 65, W_DMA_FORWARD_COUNT = 66, W_EGRESS_COUNT = 67,
};
// EVENTS (17.6.1): [2] all_arrived, [3] watchdog, [4] uart_rx, [5] uart_tx, [6] foreground_service, [7]
// background_service, [8] egress_credit
constexpr uint32_t EV_ALL_ARRIVED = 1u << 2, EV_WATCHDOG = 1u << 3, EV_UART_RX = 1u << 4, EV_UART_TX = 1u << 5,
                   EV_EGRESS_CREDIT = 1u << 8;   // (the service flags, [6] and [7]: test_ingress_link)
// STATUS (17.6.1): [2] foreground_ready, [3] background_ready, [12:8] uart_tx_bytes, [20:16] uart_rx_bytes, [24]
// rx_overrun, [25] framing_error
constexpr uint32_t ST_RX_OVERRUN = 1u << 24, ST_FRAMING = 1u << 25;
constexpr uint32_t st_uart(unsigned tx, unsigned rx) { return tx << 8 | rx << 16; }
// ERRORS (17.6.1): [0] tile_error, [1] link_error, [2] format_error, [3] egress_error
constexpr uint32_t ERR_EGRESS = 1u << 3;
constexpr uint32_t RX_VALID = 1u << 8;                     // UART_RX [8]
// UART_CONTROL (17.6.9): [15:0] divider, [16] enable_read, [17] enable_write
constexpr uint32_t UC_READ = 1u << 16, UC_WRITE = 1u << 17;
constexpr uint32_t on(uint32_t divider) { return (divider & 0xFFFFu) | UC_READ | UC_WRITE; }
// the events that read 1 with nothing happening: egress_credit, 2 at the reset (17.6.14); uart_tx too while the UART
// runs with room in its TX FIFO, 0 while it is in reset (17.6.1, 17.6.9)
constexpr uint32_t EV_IDLE = EV_EGRESS_CREDIT, EV_IDLE_UART = EV_IDLE | EV_UART_TX;
constexpr unsigned RELEASE_LATENCY = 1;                    // the bench's link (the chip's: net.h, release_latency)

// the window RegFile's register of a window word (periph_slot.h's Win), peripheral 1: words 30 and 31 are its own in
// block 1 (Win::low)
unsigned reg(unsigned w) {
    if (Win::low(w)) return Win::reg(1, Win::R_LOW + (w - Win::LOW0));
    return Win::reg(1, w < 32 ? Win::R_STD + Win::std(w) : Win::R_OWN + (w - 32));
}
// a static or dynamic bit of an own word (Win::R_DEFINED .. R_POPS), as the slot publishes it: words 32-127 one a
// register's bit, words 30 and 31 in R_LOWBITS, two bits a kind
struct OwnBit { unsigned r; uint32_t mask; };
OwnBit own_bit_of(unsigned base, unsigned w) {
    if (Win::low(w)) return {Win::reg(1, Win::R_LOWBITS), 1u << (2 * Win::kind_of(base) + (w - Win::LOW0))};
    return {Win::reg(1, base + (w - 32) / 32), 1u << ((w - 32) % 32)};
}
// the tile's live bits (17.2): [0] EVENTS.peripheral_1, [3] ERRORS.peripheral_1
constexpr unsigned LIVE = Win::R_LIVE;
constexpr uint32_t LIVE_EVENT = 1u << 0, LIVE_ERROR = 1u << 3;

// the 128-bit word a test writes into the outgoing queue: its low half, the high half its complement
uint64_t word_lo(uint32_t k) { return 0xE6000000'00000000ull | uint64_t(k) << 8 | (k & 0xFF); }
constexpr uint32_t EGRESS_PART = 1u << 20;                 // offset bits [22:20]: part 1 (17.6.8)

// ---------------------------------------------------------------- the stands
struct Core {
    // LOAD, WLOAD and EVENTS carry the value the specification says they return (b); the test compares after the run.
    //   LOAD    of a register of the window RegFile (a), with no effect
    //   WLOAD   a load of own window word a as the core makes it (tile_ctl.h, periph_own_load): the word's register,
    //           and the slot told of it if the word's load pops (the slot's static bit for the word, Win::R_POPS)
    //   SSTORE  a store that may stall, as register_bus presents it (17.6; register_bus.veryl): to window word a, in
    //           every cycle from t until its owner is ready (the slot's R_STALL bit for the word is 0), when it is
    //           taken; `taken` has the cycle of each. One at a time; no other access is made in the cycle one is taken.
    //   WRITE   a 128-bit WRITE into the region, 0x04 (17.3), at offset a, carrying word(b): handed to the slot in t over
    //           the director's three cycles, so it lands at the end of t + 2 and the loads of t + 3 see it (periph_slot.h)
    enum : uint8_t { STORE, LOAD, EVENTS, WLOAD, SSTORE, WRITE };
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
    bool own_bit(sim::Ctx& c, unsigned base, unsigned w) const { const OwnBit b = own_bit_of(base, w); return (c.read(window, b.r) & b.mask) != 0; }
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
                case WRITE: c.put(region, word_lo(e.b), lw::hdr(7, 64, lw::M_WRITE, 0, 0x04000000u | e.a, lw::SIZE_128), ~word_lo(e.b)); break;
                case SSTORE:
                    if (pending) FATAL("test_ingress_egress: two stalling stores at once (cycle %llu)", (unsigned long long)c.now());
                    present(c, e);
                    break;
                case WLOAD:
                    if (n_got < MAX) got[n_got] = c.read(window, reg(e.a));
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

// The unit's configuration and its far end, for a test that has them: UART_CONTROL before the first cycle (0: the
// reset's, the UART off), INGRESS_QUEUE_WORDS (19), the serial line's far end, and Egress's link packets (until the
// link end is joined, ingress_egress.h, EGRESS: a harness's): kept, each taken at once unless `egress_hold`.
struct EgressPacket { int64_t t; std::vector<uint64_t> lo, hi; bool frame_ends; };
struct Setup {
    uint32_t uart = 0;
    unsigned queue_words = 1024;
    IeFarEnd* far = nullptr;
    std::vector<EgressPacket>* egress = nullptr;
    const bool* egress_hold = nullptr;
};
bool keep_egress(void* ctx, int64_t t, const uint64_t* lo, const uint64_t* hi, unsigned words, bool frame_ends) {
    const Setup* su = static_cast<const Setup*>(ctx);
    if (su->egress_hold && *su->egress_hold) return false;
    su->egress->push_back({t, std::vector<uint64_t>(lo, lo + words), std::vector<uint64_t>(hi, hi + words), frame_ends});
    return true;
}

struct Rig {
    sim::Sim m;
    const Core::Ev* cev;
    unsigned cn;
    Setup su;
    Rig(const Core::Ev* ce, unsigned n_core, const Nets::Ev* ne, unsigned n_nets, uint64_t last, const sim::Options& o, const Setup& s = Setup{})
        : m(build(ce, n_core, ne, n_nets, last, o, s)), cev(ce), cn(n_core), su(s) {
        IngressEgress& p = ie();
        if (su.far) su.far->attach(p);
        if (su.egress) { p.egress_out = &keep_egress; p.egress_ctx = &su; }
        p.uart_control = su.uart;                      // as a store before the reset ended (17.6.9)
    }
    Rig(const std::vector<Core::Ev>& ce, uint64_t last, const sim::Options& o, const Setup& s = Setup{})
        : Rig(ce.data(), unsigned(ce.size()), nullptr, 0, last, o, s) {}
    uint64_t taken(unsigned k) { return k < core().n_taken && k < 16 ? core().taken[k] : 0; }   // stalling store k's cycle
    static sim::Sim build(const Core::Ev* ce, unsigned n_core, const Nets::Ev* ne, unsigned n_nets, uint64_t last, const sim::Options& o,
                          const Setup& s) {
        sim::Config cfg(o);
        auto& nets = cfg.add<Nets>("nets", ne, n_nets, last);
        auto& core = cfg.add<Core>("core", ce, n_core);
        PeriphSlotConfig pc;
        pc.tile = 64; pc.type[1] = PT_INGRESS;       // the master tile: Ingress/Egress, its DMA's packets kept by the slot
        pc.ingress_queue_words = s.queue_words;
        auto& slot = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(core.core, slot.core, 1);
        cfg.link(core.region, slot.region, 3);
        cfg.connect(slot.window, core.window);
        const IngressEgress& ie = slot.s.ingress_egress(1);
        for (unsigned r = 0; r < 8; r++) cfg.connect(nets.row[r], slot.own.wire_in[ie.row0 + r]);
        for (unsigned k = 0; k < 3; k++) cfg.link(nets.line[k], slot.own.link_in[ie.line0 + k], 1);
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
            if (cev[k].kind == Core::STORE || cev[k].kind == Core::SSTORE || cev[k].kind == Core::WRITE) continue;
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

constexpr uint8_t ST = Core::STORE, LD = Core::LOAD, EV = Core::EVENTS, WL = Core::WLOAD, SS = Core::SSTORE, WR = Core::WRITE;

// R_LOWBITS as the slot publishes it with no store stalling: words 30 and 31 defined, UART_TX writable and the one
// that may stall, UART_RX the one whose load pops (17.6.1, 17.6.9)
uint32_t lowbits() {
    return own_bit_of(Win::R_DEFINED, W_UART_TX).mask | own_bit_of(Win::R_DEFINED, W_UART_RX).mask | own_bit_of(Win::R_WRITABLE, W_UART_TX).mask |
           own_bit_of(Win::R_MAYSTALL, W_UART_TX).mask | own_bit_of(Win::R_POPS, W_UART_RX).mask;
}
const unsigned LOWBITS = Win::reg(1, Win::R_LOWBITS);
const uint32_t STALL_TX = own_bit_of(Win::R_STALL, W_UART_TX).mask;

// 5.4's access of each word of Ingress/Egress's window, from 17.1's and 17.6.1's tables. Block 1: the DMA's words
// (17.4) and UART_TX and UART_RX; its other words, and every other word of blocks 2-7, unmapped (5.3).
enum WordAccess : uint8_t { NONE, R, W, RW };
WordAccess access(unsigned w) {
    if (w <= W_EVENTS_PEEK) return R;                        // PARAMS, STATUS, ERRORS, EVENTS, EVENTS_PEEK
    if (w == W_EVENTS_MASK) return RW;
    if (w == W_START) return W;
    if (w < 16) return NONE;
    if (w == W_DMA_HDR || w == W_DMA_DEST_ADDR) return RW;
    if (w == W_DMA_START) return W;
    if (w == W_UART_TX) return W;
    if (w == W_UART_RX) return R;
    if (w < 32) return NONE;
    if (w <= W_ARRIVALS_1) return R;                         // ERROR_LINES_0-2, ARRIVALS_0-1
    if (w <= W_ARRIVE_EXPECT_1) return RW;
    if (w == W_RELEASE) return W;
    if (w == W_WATCHDOG || w == W_LED || w == W_UART_CONTROL) return RW;
    if (w == W_FOREGROUND_WORDS || w == W_BACKGROUND_WORDS) return R;
    if (w == W_DMA_FORWARD_COUNT || w == W_EGRESS_COUNT) return RW;
    return NONE;                                             // v1.36's 43-45 and 68-72 among them
}

}  // namespace

// 17.6.1, 17.1, 17.5, 17.6.14: the control block as Ingress/Egress has it, and every word after the reset. PARAMS:
// type 1, version 1, no constants. It has no work, so STATUS.busy and EVENTS.done are 0; no queue holds a word and the
// UART is in its reset, so the rest of STATUS is 0; ERRORS is 0. Of the events only egress_credit reads 1, counted at 2
// (17.6.8: "There are 2 at the reset"); uart_tx and uart_rx read 0 while the UART is in reset (17.6.1). A consuming
// load under EVENTS_MASK takes one credit at a time. The reset zeroes ARRIVALS, ARRIVE_EXPECT, the watchdog, LED,
// UART_CONTROL, both counts and both queues' words. A W word reads 0.
TEST(ie_control_block_and_reset_values) {
    const Core::Ev ev[] = {
        {3, LD, reg(W_PARAMS), 1u | 1u << 8},
        {3, LD, reg(W_STATUS), 0},
        {3, LD, reg(W_ERRORS), 0},
        {3, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {3, LD, reg(W_EVENTS_MASK), 0},
        {3, LD, reg(W_START), 0},
        {3, LD, reg(W_DMA_START), 0},
        {3, LD, LIVE, 0},
        {3, LD, reg(W_UART_TX), 0},
        {3, LD, reg(W_UART_RX), 0},                    // [8] valid 0: the RX FIFO is empty
        {3, LD, reg(W_ERROR_LINES_0), 0}, {3, LD, reg(W_ERROR_LINES_1), 0}, {3, LD, reg(W_ERROR_LINES_2), 0},
        {3, LD, reg(W_ARRIVALS_0), 0}, {3, LD, reg(W_ARRIVALS_1), 0},
        {3, LD, reg(W_ARRIVE_EXPECT_0), 0}, {3, LD, reg(W_ARRIVE_EXPECT_1), 0},
        {3, LD, reg(W_RELEASE), 0},
        {3, LD, reg(W_WATCHDOG), 0},
        {3, LD, reg(W_LED), 0},
        {3, LD, reg(W_UART_CONTROL), 0},               // 0: the UART in its reset (17.6.9)
        {3, LD, reg(W_FOREGROUND_WORDS), 0}, {3, LD, reg(W_BACKGROUND_WORDS), 0},
        {3, LD, reg(W_DMA_FORWARD_COUNT), 0}, {3, LD, reg(W_EGRESS_COUNT), 0},
        {4, EV, 0, EV_IDLE},                           // a consuming load: the same value; EVENTS_MASK 0, nothing consumed
        {6, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        // EVENTS_MASK is R/W (17.1), and the tile's EVENTS.peripheral_1 is (EVENTS & EVENTS_MASK) != 0 (17.2)
        {6, ST, W_EVENTS_MASK, EV_ALL_ARRIVED},
        {7, LD, reg(W_EVENTS_MASK), EV_ALL_ARRIVED},
        {7, LD, LIVE, 0},
        {7, ST, W_EVENTS_MASK, EV_EGRESS_CREDIT | EV_WATCHDOG},
        {8, LD, reg(W_EVENTS_MASK), EV_EGRESS_CREDIT | EV_WATCHDOG},
        {8, LD, LIVE, LIVE_EVENT},
        {8, EV, 0, EV_IDLE},                           // consumes one credit of the two
        {10, LD, reg(W_EVENTS_PEEK), EV_IDLE},
        {10, LD, LIVE, LIVE_EVENT},
        {10, EV, 0, EV_IDLE},                          // and the second
        {12, LD, reg(W_EVENTS_PEEK), 0},
        {12, LD, LIVE, 0},
        // EVENTS_MASK has one bit per event, nine here: a store ignores the bits above them, and they read 0 (5.4)
        {12, ST, W_EVENTS_MASK, 0xFFFFFFFF},
        {13, LD, reg(W_EVENTS_MASK), 0x1FF},
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, count(ev), nullptr, 0, 20, opts(variant));
        r.m.run(20);
        CHECK_LOADS(r);
        CHECK(!r.ie().toast);                          // the pins: toast and led 0 (17.6.14)
        CHECK_EQ(r.ie().led, 0u);
        CHECK_EQ(r.m.unit<PeriphSlot>("slot").s.violation_total(), 0u);
    }
}

// 5.3, 5.4, 17.1, 17.6.1: every word of the window, loaded and stored. A word the tables do not define is unmapped, an
// access fault; a store to an R word is an access fault; a W word loads as 0. Block 1 holds the DMA's three words and,
// at 30 and 31, UART_TX and UART_RX; the rest of it is unmapped. v1.36's words 43 to 45 and 68 to 72 are unmapped. The
// bus decides it (register_bus.h's decode with the master tile's straps: peripheral 1 present, with a DMA) from the
// peripheral's map for its own words, which the slot publishes (Win::R_DEFINED, R_WRITABLE; for words 30 and 31,
// R_LOWBITS), as TileCore::Env::bus_kind composes them. The one store that may stall is UART_TX's (17.6.9).
TEST(ie_window_map_unmapped_words_fault) {
    uint32_t defined[3] = {}, writable[3] = {}, may_stall[3] = {};
    for (unsigned w = 32; w < 128; w++) {
        const unsigned k = w - 32;
        if (access(w) != NONE) defined[k / 32] |= 1u << (k % 32);
        if (access(w) == W || access(w) == RW) writable[k / 32] |= 1u << (k % 32);
    }
    Core::Ev ev[10];
    for (unsigned k = 0; k < 3; k++) {
        ev[k] = {3, LD, Win::reg(1, Win::R_DEFINED + k), defined[k]};
        ev[3 + k] = {3, LD, Win::reg(1, Win::R_WRITABLE + k), writable[k]};
        ev[6 + k] = {3, LD, Win::reg(1, Win::R_MAYSTALL + k), may_stall[k]};   // none from 32 up
    }
    ev[9] = {3, LD, LOWBITS, lowbits()};
    Rig r(ev, 10, nullptr, 0, 8, opts(0));
    r.m.run(8);
    CHECK_LOADS(r);
    const uint32_t* got = r.core().got;
    bus::Straps straps;
    straps.peripherals = 1;
    straps.peripheral_dmas = 1;
    auto faults = [&](unsigned w, bool store) {
        const bus::Decoded d = bus::decode(straps, 0xFFFFFA00u + 4 * w, store, true);
        if (d.target == bus::Target::FAULT) return true;
        if (d.target != bus::Target::PERIPH_OWN) return false;
        if (Win::low(w)) {
            if (!(got[9] & own_bit_of(Win::R_DEFINED, w).mask)) return true;
            return store && !(got[9] & own_bit_of(Win::R_WRITABLE, w).mask);
        }
        const unsigned k = w - 32;
        if (!(got[k / 32] >> (k % 32) & 1)) return true;
        return store && !(got[3 + k / 32] >> (k % 32) & 1);
    };
    for (unsigned w = 0; w < 128; w++) {
        const WordAccess a = access(w);
        const bool load_faults = a == NONE, store_faults = a == NONE || a == R;
        if (faults(w, false) != load_faults) { test::fail(__FILE__, __LINE__, "a load of window word %u: fault %d, 5.3/17.6.1 say %d", w, !load_faults, load_faults); return; }
        if (faults(w, true) != store_faults) { test::fail(__FILE__, __LINE__, "a store to window word %u: fault %d, 5.3/5.4/17.6.1 say %d", w, !store_faults, store_faults); return; }
    }
    CHECK(bus::decode(straps, 0xFFFFFA94u, false, false).target == bus::Target::FAULT);   // a part of a word (5.4)
    // 17.1: a peripheral without a DMA reads 0 in all of block 1, words 30 and 31 included
    bus::Straps no_dma;
    no_dma.peripherals = 2;
    for (unsigned w : {16u, 30u, 31u}) {
        CHECK(bus::decode(no_dma, 0xFFFFFC00u + 4 * w, false, true).target == bus::Target::ZERO);
        CHECK(bus::decode(no_dma, 0xFFFFFC00u + 4 * w, true, true).target == bus::Target::FAULT);
    }
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

// 17.6.9, the UART's TX FIFO and the transmitter's pops (uart.veryl's header, "The transmitter"): UART_CONTROL is R/W,
// [17:0], 0 at the reset; a store to UART_TX with the UART running pushes its character into the TX FIFO of
// UART_FIFO_DEPTH = 16 at the end of its cycle; STATUS [12:8] is the FIFO's characters, and the one being sent has left
// it. A character starts at an idle point with the FIFO not empty: the head is popped at the end of that cycle (the
// cycle after its push into an empty FIFO with the transmitter idle), and characters follow back to back, 10 D cycles
// apart, D the divider as it stands at the character's start (a new divider takes effect at the next idle point, so
// none is cut). With 16 characters in the FIFO a store to UART_TX stalls (the slot's R_STALL bit for the word, the reg
// interface's ready) and EVENTS.uart_tx, live, is 0; both end with the next pop. The characters leave in order (the
// model's log of the characters the transmitter took; the line itself is the tests' below).
TEST(ie_uart_transmit_fifo_stall_and_frames) {
    Core::Ev ev[64];
    unsigned n = 0;
    ev[n++] = {3, LD, reg(W_UART_CONTROL), 0};
    ev[n++] = {3, ST, W_UART_CONTROL, on(2)};                          // D = 2: a character is 20 cycles
    ev[n++] = {4, LD, reg(W_UART_CONTROL), on(2)};
    // 17 characters, a cycle apart, from 5: 'A' is popped at the end of 6 (pushed at the end of 5 into an empty FIFO);
    // the next pop is at the end of 26, so 'B'..'Q', pushed at the ends of 6..21, fill the FIFO
    for (unsigned k = 0; k < 17; k++) {
        if (k == 1) ev[n++] = {6, LD, reg(W_STATUS), st_uart(1, 0)};  // 'A' in the FIFO
        if (k == 2) ev[n++] = {7, LD, reg(W_STATUS), st_uart(1, 0)};  // 'A' on the line, 'B' in the FIFO
        if (k == 16) {
            ev[n++] = {21, LD, reg(W_STATUS), st_uart(15, 0)};
            ev[n++] = {21, LD, LOWBITS, lowbits()};
            ev[n++] = {21, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART};
        }
        ev[n++] = {5 + k, ST, W_UART_TX, 0xF00u | ('A' + k)};          // [7:0] the character
    }
    ev[n++] = {22, LD, reg(W_STATUS), st_uart(16, 0)};                 // full
    ev[n++] = {22, LD, LOWBITS, lowbits() | STALL_TX};
    ev[n++] = {22, LD, reg(W_EVENTS_PEEK), EV_IDLE};                   // no room: uart_tx 0
    ev[n++] = {26, LD, reg(W_STATUS), st_uart(16, 0)};
    ev[n++] = {27, LD, reg(W_STATUS), st_uart(15, 0)};                 // 'B' popped at the end of 26
    ev[n++] = {27, LD, LOWBITS, lowbits()};
    ev[n++] = {27, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART};
    ev[n++] = {27, LD, reg(W_UART_TX), 0};                             // W: a load reads 0
    ev[n++] = {27, ST, W_UART_TX, 'Z'};                                // the 18th character
    ev[n++] = {28, LD, reg(W_STATUS), st_uart(16, 0)};
    ev[n++] = {30, ST, W_UART_CONTROL, on(1)};                         // in 'B's frame: it keeps D = 2, to the end of 46
    ev[n++] = {31, LD, reg(W_UART_CONTROL), on(1)};
    ev[n++] = {47, LD, reg(W_STATUS), st_uart(15, 0)};                 // 'C' popped at the end of 46, with D = 1
    ev[n++] = {56, LD, reg(W_STATUS), st_uart(15, 0)};
    ev[n++] = {57, LD, reg(W_STATUS), st_uart(14, 0)};                 // 'D' at the end of 56: 10 cycles on
    ev[n++] = {57, LD, reg(W_ERRORS), 0};
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, n, nullptr, 0, 250, opts(variant));
        // the pops: the ends of 6, 26, 46, then every 10 cycles: the 18th character's at the end of 46 + 150 = 196
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

// 17.6.14, on the model itself (the simulator resets a unit only by building it; reset() is the model's): every
// register dirtied through its window words and the nets, then the reset: ARRIVALS, ARRIVE_EXPECT, the watchdog's
// limit and count, LED and the toast pin are 0, UART_CONTROL is 0 and the TX FIFO empty, EGRESS_COUNT is 0, and the
// events are the reset's: egress_credit at 2. The limit is 0 too: a RELEASE after the reset starts no watchdog.
TEST(ie_reset_zeroes_its_state) {
    const Nets::Ev nets[] = {{5, Nets::ARRIVE, 7, 0}, {5, Nets::ARRIVE, 50, 0}};
    const Core::Ev ev[] = {
        {3, ST, W_ARRIVE_EXPECT_0, 1u << 7},
        {4, ST, W_ARRIVE_EXPECT_1, 1u << 18},
        {5, ST, W_WATCHDOG, 2},                        // expires at the end of 7: the event and the toast pin
        {6, ST, W_LED, 1},
        {7, ST, W_UART_CONTROL, on(100)},
        {8, ST, W_UART_TX, 'x'},
        {9, ST, W_UART_TX, 'y'},
        {10, ST, W_WATCHDOG, 1000},
        {11, ST, W_EGRESS_COUNT, 5},
        {12, LD, reg(W_ARRIVALS_0), 1u << 7},
        {12, LD, reg(W_ARRIVALS_1), 1u << 18},
        {12, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART | EV_ALL_ARRIVED | EV_WATCHDOG},
        {12, LD, reg(W_STATUS), st_uart(1, 0)},
        {12, LD, reg(W_WATCHDOG), 999},
        {12, LD, reg(W_EGRESS_COUNT), 5},
    };
    Rig r(ev, count(ev), nets, count(nets), 12, opts(0));
    r.m.run_until(12);
    CHECK_LOADS(r);
    IngressEgress& ie = r.ie();
    CHECK(ie.toast);
    ie.reset(13);
    auto word = [&](unsigned w) { uint32_t v = ~0u; ie.word(14, int(w), v); return v; };   // (an undefined word leaves ~0)
    for (unsigned w : {unsigned(W_ARRIVALS_0), unsigned(W_ARRIVALS_1), unsigned(W_ARRIVE_EXPECT_0), unsigned(W_ARRIVE_EXPECT_1),
                       unsigned(W_WATCHDOG), unsigned(W_LED), unsigned(W_UART_CONTROL), unsigned(W_EGRESS_COUNT), unsigned(W_DMA_FORWARD_COUNT),
                       unsigned(W_FOREGROUND_WORDS), unsigned(W_BACKGROUND_WORDS)})
        CHECK_EQ(word(w), 0u);
    CHECK_EQ(ie.status_bits(14), 0u);
    CHECK(!ie.toast);
    CHECK_EQ(ie.led, 0u);
    ie.sync(14);
    CHECK_EQ(ie.events().value(14), EV_IDLE);
    CHECK_EQ(ie.write_word(14, W_RELEASE, 1), 1);      // the limit is 0: the count stays 0
    ie.step(15);
    CHECK_EQ(word(W_WATCHDOG), 0u);
}

// 17.6.15: "A START" is UB: it has no work. The simulator stops, naming it.
DEATH_TEST(ie_start_is_undefined_behaviour, "Ingress/Egress: undefined behavior at 5: a START") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_START, 1);
}

// ================================================================ the UART's line
namespace {

// uart_tx's levels over the cycles [from, to), a character each.
std::string tx_levels(const IeFarEnd& f, int64_t from, int64_t to) {
    std::string s;
    for (int64_t t = from; t < to; t++) s += f.at(t).uart_tx ? '1' : '0';
    return s;
}
// A character's levels (17.6.9): a start bit, 0; 8 data bits, the least significant first; a stop bit, 1; each D cycles.
std::string frame_levels(uint8_t byte, unsigned d) {
    std::string s(d, '0');
    for (unsigned k = 0; k < 8; k++) s += std::string(d, (byte >> k & 1) ? '1' : '0');
    return s + std::string(d, '1');
}
// The characters decoded off uart_tx over [from, to) are `want` (each its start bit's first cycle and its byte), every
// bit held for the divider its character started at.
struct WantFrame { int64_t start; uint8_t byte; };
template <unsigned N>
void check_frames(const IeFarEnd& f, int64_t from, int64_t to, const std::function<unsigned(int64_t)>& divisor, const WantFrame (&want)[N],
                  const char* file, int line) {
    std::string error;
    const std::vector<IeFarEnd::TxFrame> got = f.decode_tx(from, to, divisor, error);
    if (!error.empty()) { test::fail(file, line, "uart_tx: %s", error.c_str()); return; }
    if (got.size() != N) { test::fail(file, line, "uart_tx carried %zu characters, 17.6.9 says %u", got.size(), N); return; }
    for (unsigned k = 0; k < N; k++)
        if (got[k].start != want[k].start || got[k].byte != want[k].byte) {
            test::fail(file, line, "uart_tx's character %u: 0x%02x from cycle %lld, 17.6.9 and the unit's timing say 0x%02x from %lld", k,
                       got[k].byte, (long long)got[k].start, want[k].byte, (long long)want[k].start);
            return;
        }
}
#define CHECK_FRAMES(far, from, to, divisor, want) check_frames(far, from, to, divisor, want, __FILE__, __LINE__)

}  // namespace

// 17.6.14, 18: after the reset every output pin is idle: uart_tx 1, toast and led 0; with nothing stored they stay so.
TEST(ie_pins_after_the_reset) {
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(nullptr, 0, nullptr, 0, 40, opts(variant), Setup{.far = &far});
        r.m.run(40);
        CHECK_EQ(far.first, int64_t(1));                   // the machine's first cycle
        for (int64_t t = 1; t <= 40; t++) {
            const IngressEgress::Pins::Out& o = far.at(t);
            CHECK(o.uart_tx);
            CHECK(!o.toast); CHECK(!o.led);
        }
    }
}

// 5.3, 17.6.1: the one load that pops is UART_RX's ("The loads that act are below 32: EVENTS, which consumes, and
// UART_RX, which pops"): no word from 32 up pops (the slot's static bits for the core, Win::R_POPS and R_LOWBITS).
TEST(ie_the_load_that_pops_is_uart_rx) {
    const Core::Ev ev[] = {
        {3, LD, Win::reg(1, Win::R_POPS), 0},
        {3, LD, Win::reg(1, Win::R_POPS + 1), 0},
        {3, LD, Win::reg(1, Win::R_POPS + 2), 0},
        {3, LD, LOWBITS, lowbits()},
    };
    Rig r(ev, count(ev), nullptr, 0, 8, opts(0));
    r.m.run(8);
    CHECK_LOADS(r);
    CHECK(lowbits() & own_bit_of(Win::R_POPS, W_UART_RX).mask);
    CHECK(!(lowbits() & own_bit_of(Win::R_POPS, W_UART_TX).mask));
}

// 17.6.9, the transmitter's line, cycle by cycle (uart.veryl's header; the unit's README, Timing): a character stored
// to UART_TX in p into an empty FIFO, the transmitter idle, is popped at the end of p + 1 and its start bit is on
// uart_tx from p + 2: a start bit, 0, the 8 data bits, the least significant first, a stop bit, 1, each D cycles, the
// line 1 before and after. Characters follow back to back, 10 D apart, while the FIFO holds some, and STATUS [12:8]
// counts the FIFO's alone: the character on the line has left it. A character takes the divider as it stands at the
// start of the cycle that pops it: a store during a character, or in the pop's own cycle, is the next one's.
TEST(ie_uart_tx_frames_on_the_line_bit_for_bit) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_UART_CONTROL, on(3)},
        {5, ST, W_UART_TX, 0x1A5},                     // [7:0]: 0xA5
        {6, LD, reg(W_STATUS), st_uart(1, 0)},         // in the FIFO
        {7, LD, reg(W_STATUS), 0},                     // on the line
        // three characters: the first starts at 42; the others wait in the FIFO, each popped in the last cycle of the
        // stop bit before it (71, 101)
        {40, ST, W_UART_TX, 0x00}, {41, ST, W_UART_TX, 0xFF}, {42, ST, W_UART_TX, 0x80},
        {41, LD, reg(W_STATUS), st_uart(1, 0)}, {42, LD, reg(W_STATUS), st_uart(1, 0)}, {43, LD, reg(W_STATUS), st_uart(2, 0)},
        {71, LD, reg(W_STATUS), st_uart(2, 0)}, {72, LD, reg(W_STATUS), st_uart(1, 0)},
        {101, LD, reg(W_STATUS), st_uart(1, 0)}, {102, LD, reg(W_STATUS), 0},
        // the divider: 0x55 from 142 at 3 (to 171); a store of 2 in its character; 0x33 from 172 at 2 (to 191); 0x0F is
        // popped in 191, and a store of 4 presented in 191 is not its character's: from 192 at 2 (to 211); 0xC3 from 212 at 4
        {140, ST, W_UART_TX, 0x55},
        {145, ST, W_UART_CONTROL, on(2)},
        {146, LD, reg(W_UART_CONTROL), on(2)},
        {147, ST, W_UART_TX, 0x33},
        {180, ST, W_UART_TX, 0x0F},
        {191, ST, W_UART_CONTROL, on(4)},
        {200, ST, W_UART_TX, 0xC3},
    };
    std::vector<Core::Ev> sorted = ev;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Core::Ev& a, const Core::Ev& b) { return a.t < b.t; });
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(sorted, 270, opts(variant), Setup{.far = &far});
        r.m.run(270);
        CHECK_LOADS(r);
        // 0xA5's character at D = 3, written out: the line 1 through 6, the start bit in 7-9, the bits 1 0 1 0 0 1 0 1,
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

// 17.6.9, 19: every bit lasts `divider` cycles, at dividers 1 to 6 and at 8,681 (115,200 baud at 1 GHz, a character
// of 86,810 cycles): two characters stored in 5 and 6, from 7 and from 7 + 10 D.
TEST(ie_uart_tx_every_bit_lasts_the_divider) {
    for (unsigned d : {1u, 2u, 3u, 4u, 5u, 6u, 8681u}) {
        const std::vector<Core::Ev> ev = {
            {3, LD, reg(W_UART_CONTROL), on(d)},
            {5, ST, W_UART_TX, 0x96}, {6, ST, W_UART_TX, 0x69},
        };
        IeFarEnd far;
        const uint64_t last = 7 + 20 * uint64_t(d) + 5;
        Rig r(ev, last, opts(0), Setup{.uart = on(d), .far = &far});
        r.m.run(last);
        CHECK_LOADS(r);
        CHECK(tx_levels(far, 7, 7 + 10 * int64_t(d)) == frame_levels(0x96, d));
        const WantFrame want[] = {{7, 0x96}, {7 + 10 * int64_t(d), 0x69}};
        CHECK_FRAMES(far, 1, int64_t(last), [d](int64_t) { return d; }, want);
    }
}

// 17.6.9; register_bus.veryl and uart.veryl (lines_ready = !tx_full, from the FIFO's count at the cycle's start): a
// UART_TX store presented with the TX FIFO full stalls, a pop in that cycle notwithstanding, and is taken in the first
// cycle at whose start the FIFO has room: the cycle after the transmitter pops a character, which is the last cycle of
// a stop bit. At D = 20: 'A' (stored in 5) is on the line from 7 to 206, 'B'-'Q' (6-21) fill the FIFO, and 'R',
// presented from 22, is taken in 207, the cycle after 'B' is popped (206); the FIFO is full again from 208, and 'S',
// presented from 208, is taken in 407. EVENTS.uart_tx, live, is 0 exactly while the FIFO is full.
TEST(ie_uart_tx_a_stalled_store_is_taken_the_cycle_after_the_pop) {
    std::vector<Core::Ev> ev;
    for (unsigned k = 0; k < 17; k++) ev.push_back({5 + k, ST, W_UART_TX, uint32_t('A' + k)});
    ev.push_back({22, LD, LOWBITS, lowbits() | STALL_TX});
    ev.push_back({22, SS, W_UART_TX, 'R'});
    ev.push_back({206, LD, LOWBITS, lowbits() | STALL_TX});
    ev.push_back({206, LD, reg(W_STATUS), st_uart(16, 0)});
    ev.push_back({206, LD, reg(W_EVENTS_PEEK), EV_IDLE});
    ev.push_back({207, LD, LOWBITS, lowbits()});
    ev.push_back({207, LD, reg(W_STATUS), st_uart(15, 0)});
    ev.push_back({207, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART});
    ev.push_back({208, LD, LOWBITS, lowbits() | STALL_TX});
    ev.push_back({208, LD, reg(W_STATUS), st_uart(16, 0)});
    ev.push_back({208, LD, reg(W_EVENTS_PEEK), EV_IDLE});
    ev.push_back({208, SS, W_UART_TX, 'S'});
    ev.push_back({408, LD, reg(W_STATUS), st_uart(16, 0)});
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        Rig r(ev, 420, opts(variant), Setup{.uart = on(20), .far = &far});
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

// 17.6.9, the receiver (uart.veryl's header): two flip-flops synchronise uart_rx, so a falling edge on the pin in cycle
// c starts a character in e = c + 2; bit k (1-8 data, the least significant first; 9 the stop bit) is sampled at its
// middle, in e + k D + floor(D / 2); the character enters the RX FIFO at the end of the stop bit's sample, f = e + 9 D +
// floor(D / 2): a load in f reads none (and pops none), a load in f + 1 reads it with `valid`. STATUS [20:16] counts the
// RX FIFO; EVENTS.uart_rx is live, a character in the RX FIFO, and with EVENTS_MASK raises the tile's
// EVENTS.peripheral_1. UART_RX pops at the end of its load's cycle; with the FIFO empty it reads 0 and pops nothing.
// At D = 4:
//   - 0x1E on the pin from 20, 4 cycles a bit: f = 60;
//   - 0xB2 from 100 as a character only the specified sample cycles read (ie_far_end.h, needle_frame: a start bit of
//     one cycle, each data bit's level on the pin in the one cycle c + 4 k + 2 alone, the opposite level in the cycles
//     beside it; the falling edges inside it start nothing): f = 140;
//   - 0xC1 from 200 and 0x3C from 240, back to back: the first ends at its stop bit's middle, 240, so the second's
//     edge, seen in 242, starts a character: f = 240 and 280.
TEST(ie_uart_rx_frames_sampled_at_each_bits_middle) {
    const std::vector<Core::Ev> ev = {
        {3, ST, W_EVENTS_MASK, EV_UART_RX},
        {4, LD, reg(W_UART_CONTROL), on(4)},
        {59, LD, reg(W_STATUS), 0},
        {60, WL, W_UART_RX, 0},                        // the character enters at the end of 60: nothing read, nothing popped
        {60, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART},
        {60, LD, LIVE, 0},
        {61, LD, reg(W_UART_RX), RX_VALID | 0x1E},
        {61, LD, reg(W_STATUS), st_uart(0, 1)},
        {61, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART | EV_UART_RX},
        {61, LD, LIVE, LIVE_EVENT},
        {62, WL, W_UART_RX, RX_VALID | 0x1E},          // popped at the end of 62
        {63, LD, reg(W_UART_RX), 0},
        {63, LD, reg(W_STATUS), 0},
        {63, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART},
        {63, LD, LIVE, 0},
        {140, LD, reg(W_STATUS), 0},
        {141, WL, W_UART_RX, RX_VALID | 0xB2},
        {142, LD, reg(W_STATUS), 0},
        {240, LD, reg(W_STATUS), 0},
        {241, LD, reg(W_STATUS), st_uart(0, 1)},
        {280, LD, reg(W_STATUS), st_uart(0, 1)},
        {281, LD, reg(W_STATUS), st_uart(0, 2)},
        {281, WL, W_UART_RX, RX_VALID | 0xC1},         // in order
        {282, WL, W_UART_RX, RX_VALID | 0x3C},
        {283, LD, reg(W_STATUS), 0},
        {283, WL, W_UART_RX, 0},                       // empty: 0, and nothing to pop
        {284, LD, reg(W_ERRORS), 0},
        {284, LD, reg(W_STATUS), 0},                   // no line error
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x1E, 4);
        far.needle_frame(100, 0xB2, 4);
        far.frame(200, 0xC1, 4);
        far.frame(240, 0x3C, 4);
        Rig r(ev, 300, opts(variant), Setup{.uart = on(4), .far = &far});
        r.m.run(300);
        CHECK_LOADS(r);
        CHECK(tx_levels(far, 1, 300) == std::string(299, '1'));        // nothing sent
    }
}

// 17.6.9: a character whose stop bit is 0 is dropped and sets framing_error, STATUS [25]. The line errors are status:
// ERRORS stays 0 and the toast pin low, and only the UART's reset clears them, a character received after one
// notwithstanding. The reset (a store of 0) empties the RX FIFO too; the UART starts again with a later store of its
// setting. At D = 4: a bad character from 20 (f = 60), a good one from 80 (f = 120), the UART's reset in 130 and its
// start in 132, a bad one from 140 (f = 180).
TEST(ie_uart_rx_a_stop_bit_of_0_drops_the_frame_until_the_uarts_reset) {
    const std::vector<Core::Ev> ev = {
        {60, LD, reg(W_STATUS), 0},
        {61, LD, reg(W_STATUS), ST_FRAMING},           // dropped: no character
        {61, LD, reg(W_UART_RX), 0},
        {61, LD, reg(W_ERRORS), 0},
        {61, LD, LIVE, 0},
        {121, LD, reg(W_STATUS), ST_FRAMING | st_uart(0, 1)},   // the good character; the error stays
        {121, LD, reg(W_UART_RX), RX_VALID | 0x42},
        {130, ST, W_UART_CONTROL, 0},                  // the UART's reset
        {131, LD, reg(W_STATUS), 0},                   // the error cleared, the RX FIFO empty
        {131, LD, reg(W_UART_RX), 0},
        {131, LD, reg(W_UART_CONTROL), 0},
        {131, LD, reg(W_EVENTS_PEEK), EV_IDLE},        // uart_rx and uart_tx 0 in the reset
        {132, ST, W_UART_CONTROL, on(4)},
        {133, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART},
        {180, LD, reg(W_STATUS), 0},
        {181, LD, reg(W_STATUS), ST_FRAMING},
        {182, LD, reg(W_ERRORS), 0},
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x77, 4, false);
        far.frame(80, 0x42, 4);
        far.frame(140, 0x11, 4, false);
        Rig r(ev, 200, opts(variant), Setup{.uart = on(4), .far = &far});
        r.m.run(200);
        CHECK_LOADS(r);
        CHECK(!r.ie().toast);                          // noise on the line never raises the toast pin
    }
}

// 17.6.9: a character that ends with the RX FIFO full is dropped and sets rx_overrun, STATUS [24], until the UART's
// reset; "full" is the FIFO's 16 characters at the start of the cycle the character ends in, a pop in that cycle
// notwithstanding (uart.veryl; the unit's README, Decisions). At D = 2 a character from c ends in f = c + 21. Seventeen,
// 0x30 up, back to back from 20: the sixteenth fills the FIFO (f = 341), the seventeenth (f = 361) is dropped. One from
// 380 ends in 401, the cycle of a UART_RX load, which pops 0x30: dropped all the same. One from 420 (0x52) finds room.
TEST(ie_uart_rx_a_frame_ending_with_the_fifo_full_is_dropped_whatever_pops) {
    std::vector<Core::Ev> ev = {
        {341, LD, reg(W_STATUS), st_uart(0, 15)},
        {342, LD, reg(W_STATUS), st_uart(0, 16)},
        {361, LD, reg(W_STATUS), st_uart(0, 16)},
        {362, LD, reg(W_STATUS), st_uart(0, 16) | ST_RX_OVERRUN},
        {362, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART | EV_UART_RX},
        {362, LD, reg(W_ERRORS), 0},
        {401, WL, W_UART_RX, RX_VALID | 0x30},         // the oldest character, popped at the end of 401
        {402, LD, reg(W_STATUS), st_uart(0, 15) | ST_RX_OVERRUN},
        {442, LD, reg(W_STATUS), st_uart(0, 16) | ST_RX_OVERRUN},
    };
    for (unsigned k = 1; k < 16; k++) ev.push_back({450 + k - 1, WL, W_UART_RX, RX_VALID | (0x30 + k)});   // in order
    ev.push_back({465, WL, W_UART_RX, RX_VALID | 0x52});
    ev.push_back({466, LD, reg(W_STATUS), ST_RX_OVERRUN});
    ev.push_back({466, WL, W_UART_RX, 0});
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        for (unsigned k = 0; k < 17; k++) far.frame(20 + 20 * k, uint8_t(0x30 + k), 2);
        far.frame(380, 0x51, 2);
        far.frame(420, 0x52, 2);
        Rig r(ev, 480, opts(variant), Setup{.uart = on(2), .far = &far});
        r.m.run(480);
        CHECK_LOADS(r);
    }
}

// 17.6.9: the receiver takes the divider at a character's start (the cycle its falling edge is seen in), so a store
// during a character, or in that very cycle, is the next one's ("at each direction's next idle point, so no character
// is cut"); and "the start bit is not checked, so a glitch starts a character": one cycle of 0 on the pin is a
// character of 1s. From D = 4: 0x9A from 20 at 4 a bit, the divider 2 stored in 30: f = 60 still; 0x65 from 70 at 2 a
// bit: seen in 72, f = 91; 0x0F from 100 at 2 a bit, the divider 6 stored in 102, the cycle its edge is seen in:
// f = 121; 0xE1 from 130 at 6 a bit: seen in 132, f = 132 + 54 + 3 = 189; a glitch in 200: 0xFF, f = 202 + 57 = 259.
TEST(ie_uart_rx_the_divider_at_a_frames_start_and_a_glitch_is_a_frame_of_ones) {
    const std::vector<Core::Ev> ev = {
        {30, ST, W_UART_CONTROL, on(2)},
        {60, LD, reg(W_STATUS), 0},
        {61, LD, reg(W_UART_RX), RX_VALID | 0x9A},
        {91, LD, reg(W_STATUS), st_uart(0, 1)},
        {92, LD, reg(W_STATUS), st_uart(0, 2)},
        {102, ST, W_UART_CONTROL, on(6)},
        {121, LD, reg(W_STATUS), st_uart(0, 2)},
        {122, LD, reg(W_STATUS), st_uart(0, 3)},
        {189, LD, reg(W_STATUS), st_uart(0, 3)},
        {190, LD, reg(W_STATUS), st_uart(0, 4)},
        {259, LD, reg(W_STATUS), st_uart(0, 4)},
        {260, LD, reg(W_STATUS), st_uart(0, 5)},
        {261, WL, W_UART_RX, RX_VALID | 0x9A}, {262, WL, W_UART_RX, RX_VALID | 0x65}, {263, WL, W_UART_RX, RX_VALID | 0x0F},
        {264, WL, W_UART_RX, RX_VALID | 0xE1}, {265, WL, W_UART_RX, RX_VALID | 0xFF},
        {266, LD, reg(W_STATUS), 0},
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x9A, 4);
        far.frame(70, 0x65, 2);
        far.frame(100, 0x0F, 2);
        far.frame(130, 0xE1, 6);
        far.rx_level(200, false);
        Rig r(ev, 280, opts(variant), Setup{.uart = on(4), .far = &far});
        r.m.run(280);
        CHECK_LOADS(r);
    }
}

// 17.6.9, 17.6.14: the UART is off at the reset, UART_CONTROL 0, until the core turns it on: both FIFOs empty, the
// line out idle at 1, the line in ignored, uart_rx and uart_tx 0, a store to UART_TX dropped (and never stalling, so
// twenty in a row are each taken in its cycle), UART_RX 0. A store of its setting starts it; a store of 0 resets it:
// the TX FIFO emptied and the line out back to idle in the next cycle, a character on the line cut.
//   - a character on uart_rx from 20 at D = 4, while it is off: nothing;
//   - on from the end of 100 at D = 4: a character from 110, seen in 112, f = 112 + 36 + 2 = 150;
//   - 'a' 'b' 'c' stored in 200-202: 'a' popped at the end of 201 and on the line from 202 (0x61 at D = 4: the start
//     bit in 202-205, bit 0, 1, in 206-209, bit 1, 0, in 210-213); the reset in 210: the line 1 from 211, 'b' and 'c'
//     gone; on again from 250: nothing more is sent.
TEST(ie_uart_off_at_the_reset_and_a_store_of_0_resets_it) {
    std::vector<Core::Ev> ev;
    for (unsigned k = 0; k < 20; k++) ev.push_back({5 + k, SS, W_UART_TX, 'A' + k});
    for (Core::Ev e : std::initializer_list<Core::Ev>{
             {30, LD, reg(W_STATUS), 0},
             {30, LD, reg(W_EVENTS_PEEK), EV_IDLE},
             {30, LD, reg(W_UART_RX), 0},
             {30, LD, LOWBITS, lowbits()},
             {100, ST, W_UART_CONTROL, on(4)},
             {101, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART},
             {101, LD, reg(W_STATUS), 0},
             {151, LD, reg(W_STATUS), st_uart(0, 1)},
             {151, WL, W_UART_RX, RX_VALID | 0x5A},
             {200, ST, W_UART_TX, 'a'}, {201, ST, W_UART_TX, 'b'}, {202, ST, W_UART_TX, 'c'},
             {203, LD, reg(W_STATUS), st_uart(2, 0)},
             {210, ST, W_UART_CONTROL, 0},
             {211, LD, reg(W_STATUS), 0},
             {211, LD, reg(W_EVENTS_PEEK), EV_IDLE},
             {250, ST, W_UART_CONTROL, on(4)},
             {251, LD, reg(W_STATUS), 0},
         })
        ev.push_back(e);
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x33, 4);
        far.frame(110, 0x5A, 4);
        Rig r(ev, 320, opts(variant), Setup{.far = &far});
        r.m.run(320);
        CHECK_LOADS(r);
        CHECK_EQ(r.core().n_taken, 20u);
        for (unsigned k = 0; k < 16; k++) CHECK_EQ(r.taken(k), uint64_t(5 + k));   // none stalls
        CHECK(tx_levels(far, 1, 202) == std::string(201, '1'));
        CHECK(tx_levels(far, 202, 211) == "0000" "1111" "0");
        CHECK(tx_levels(far, 211, 320) == std::string(109, '1'));
        CHECK_EQ(r.ie().serial_n, 1u);                 // 'a' alone
    }
}

// 17.6.9: "The enables only gate the line." enable_read 0: the line in is ignored, no character starts and no line
// error is raised, and what is in the RX FIFO stays. enable_write 0: nothing is sent, and a store to UART_TX is dropped,
// so that it cannot stall for ever; what is in the TX FIFO stays, and goes when enable_write is 1 again. At D = 4:
//   - 0x21 from 20, f = 60; enable_read 0 from the end of 70; 0x22 from 80 and a bad character from 140: nothing;
//   - enable_read 1 again from the end of 190;
//   - 'm' 'n' 'o' stored in 200-202: 'm' popped at the end of 201, on the line 202-241; enable_write 0 from the end of
//     210: 'n' and 'o' stay; a store to UART_TX in 220 is dropped and taken at once; enable_write 1 from the end of
//     300: 'n' popped at the end of 301, on the line from 302, 'o' from 342.
TEST(ie_uart_the_enables_only_gate_the_line) {
    const std::vector<Core::Ev> ev = {
        {61, LD, reg(W_STATUS), st_uart(0, 1)},
        {70, ST, W_UART_CONTROL, 4 | UC_WRITE},
        {71, LD, reg(W_UART_CONTROL), 4 | UC_WRITE},
        {130, LD, reg(W_STATUS), st_uart(0, 1)},
        {130, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART | EV_UART_RX},   // the UART runs: its events are live
        {189, LD, reg(W_STATUS), st_uart(0, 1)},       // no framing error
        {189, WL, W_UART_RX, RX_VALID | 0x21},
        {190, ST, W_UART_CONTROL, on(4)},
        {200, ST, W_UART_TX, 'm'}, {201, ST, W_UART_TX, 'n'}, {202, ST, W_UART_TX, 'o'},
        {210, ST, W_UART_CONTROL, 4 | UC_READ},
        {220, SS, W_UART_TX, 'x'},
        {221, LD, reg(W_STATUS), st_uart(2, 0)},
        {221, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART},   // room in the TX FIFO: uart_tx 1
        {300, LD, reg(W_STATUS), st_uart(2, 0)},
        {300, ST, W_UART_CONTROL, on(4)},
        {302, LD, reg(W_STATUS), st_uart(1, 0)},
    };
    for (int variant = 0; variant < 3; variant++) {
        IeFarEnd far;
        far.frame(20, 0x21, 4);
        far.frame(80, 0x22, 4);
        far.frame(140, 0x23, 4, false);
        Rig r(ev, 400, opts(variant), Setup{.uart = on(4), .far = &far});
        r.m.run(400);
        CHECK_LOADS(r);
        CHECK_EQ(r.core().n_taken, 1u);
        CHECK_EQ(r.taken(0), uint64_t(220));
        CHECK(tx_levels(far, 242, 302) == std::string(60, '1'));    // nothing sent while enable_write is 0
        const WantFrame want[] = {{302, 'n'}, {342, 'o'}};
        CHECK_FRAMES(far, 242, 400, [](int64_t) { return 4u; }, want);
        CHECK_EQ(r.ie().serial_n, 3u);
        CHECK(std::string(r.ie().serial_out, 3) == "mno");
    }
}

// 17.6.15: "a non-zero UART_CONTROL with a divider of 0" is UB.
DEATH_TEST(ie_uart_control_with_a_divider_of_0_is_ub, "UART_CONTROL 0x30000, not 0 with a divider of 0") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_UART_CONTROL, UC_READ | UC_WRITE);
}

// ================================================================ Egress (17.6.8)
namespace {
// The words `first` to `first + n - 1` a test wrote, as the link packet carries them.
bool carries(const EgressPacket& p, uint32_t first, unsigned n, bool frame_ends) {
    if (p.lo.size() != n || p.frame_ends != frame_ends) return false;
    for (unsigned k = 0; k < n; k++) if (p.lo[k] != word_lo(first + k) || p.hi[k] != ~word_lo(first + k)) return false;
    return true;
}
// n WRITEs of the words first.. into part 1, one a cycle from cycle t, at offsets whose other bits vary: "the rest of
// the address is ignored"
void writes(std::vector<Core::Ev>& ev, uint64_t t, uint32_t first, unsigned n) {
    for (unsigned k = 0; k < n; k++) ev.push_back({t + k, WR, EGRESS_PART | ((first + k) * 0x3350u & 0xFFFF0u), first + k});
}
}  // namespace

// 17.6.8: the core stores a frame's length in EGRESS_COUNT and writes that many words into part 1 of the region; the
// count falls as each word lands (a WRITE handed over in p lands at the end of p + 2), the word that takes it to 0 is
// the frame's last, and the next length may be stored at once. Egress offers a link packet when the queue holds 32
// words of a frame, or the frame's last word: a frame of 40 is two link packets, 32 and 8, the second ending the frame,
// and a frame of 5 one, in the order written, each word as written. 45 words gone is no credit (256 are).
TEST(ie_egress_count_falls_as_words_land_and_frames_end_at_link_packets) {
    std::vector<Core::Ev> ev = {{3, ST, W_EGRESS_COUNT, 40}, {4, LD, reg(W_EGRESS_COUNT), 40}};
    writes(ev, 5, 0, 40);                              // 5 to 44: the first lands at the end of 7, the last at the end of 46
    ev.insert(ev.begin() + 3, {8, LD, reg(W_EGRESS_COUNT), 39});
    for (Core::Ev e : std::initializer_list<Core::Ev>{
             {46, LD, reg(W_EGRESS_COUNT), 1},
             {47, LD, reg(W_EGRESS_COUNT), 0},
             {47, ST, W_EGRESS_COUNT, 5}})
        ev.push_back(e);
    writes(ev, 48, 40, 5);
    ev.push_back({56, LD, reg(W_EGRESS_COUNT), 0});
    ev.push_back({60, LD, reg(W_ERRORS), 0});
    ev.push_back({60, LD, reg(W_EVENTS_PEEK), EV_IDLE});
    std::stable_sort(ev.begin(), ev.end(), [](const Core::Ev& a, const Core::Ev& b) { return a.t < b.t; });
    for (int variant = 0; variant < 3; variant++) {
        std::vector<EgressPacket> pk;
        Rig r(ev, 80, opts(variant), Setup{.egress = &pk});
        r.m.run(80);
        CHECK_LOADS(r);
        CHECK_EQ(pk.size(), size_t(3));
        if (pk.size() != 3) continue;
        CHECK(carries(pk[0], 0, 32, false));
        CHECK(carries(pk[1], 32, 8, true));
        CHECK(carries(pk[2], 40, 5, true));
        CHECK(pk[0].t >= 38 && pk[1].t >= 46 && pk[2].t >= 54);   // not before their words have landed
        CHECK(pk[0].t < pk[1].t && pk[1].t < pk[2].t);
        CHECK_EQ(r.ie().eg_n, 0u);
    }
}

// 17.6.8: "a frame ends at the end of a link packet": two frames waiting in the queue together, 40 words and 5, leave
// as 32, 8 and 5, never a link packet across the frames' boundary.
TEST(ie_egress_a_link_packet_never_crosses_a_frames_end) {
    std::vector<Core::Ev> ev = {{3, ST, W_EGRESS_COUNT, 40}};
    writes(ev, 5, 0, 40);
    ev.push_back({47, ST, W_EGRESS_COUNT, 5});
    writes(ev, 48, 40, 5);
    for (int variant = 0; variant < 3; variant++) {
        std::vector<EgressPacket> pk;
        bool hold = true;
        Rig r(ev, 200, opts(variant), Setup{.egress = &pk, .egress_hold = &hold});
        r.m.run_until(80);
        CHECK(pk.empty());
        CHECK_EQ(r.ie().eg_n, 45u);
        hold = false;
        r.m.run_until(120);
        CHECK_EQ(pk.size(), size_t(3));
        if (pk.size() != 3) continue;
        CHECK(carries(pk[0], 0, 32, false));
        CHECK(carries(pk[1], 32, 8, true));
        CHECK(carries(pk[2], 40, 5, true));
    }
}

// 17.6.8: the credit is the replayer's: egress_credit, counted, depth 2, 2 at the reset; one comes back each time 256
// words have left the queue. Both reset credits consumed, a frame of 300 words: none back after 224 have gone, one by
// the time all 300 have; then 212 more, 512 in all: the second.
TEST(ie_egress_a_credit_each_256_words_gone) {
    std::vector<Core::Ev> ev = {
        {3, ST, W_EVENTS_MASK, EV_EGRESS_CREDIT},
        {4, EV, 0, EV_IDLE},                           // consumes one
        {6, EV, 0, EV_IDLE},                           // and the other
        {8, LD, reg(W_EVENTS_PEEK), 0},
        {9, ST, W_EGRESS_COUNT, 300},
    };
    writes(ev, 10, 0, 300);                            // 10 to 309
    for (Core::Ev e : std::initializer_list<Core::Ev>{
             {250, LD, reg(W_EVENTS_PEEK), 0},         // word 240 written: 224 gone at most
             {320, LD, reg(W_EVENTS_PEEK), EV_EGRESS_CREDIT},
             {320, LD, LIVE, LIVE_EVENT},
             {321, EV, 0, EV_EGRESS_CREDIT},
             {323, LD, reg(W_EVENTS_PEEK), 0},
             {324, ST, W_EGRESS_COUNT, 212}})
        ev.push_back(e);
    writes(ev, 325, 300, 212);                         // 325 to 536
    ev.push_back({520, LD, reg(W_EVENTS_PEEK), 0});    // 480 gone at most
    ev.push_back({545, LD, reg(W_EVENTS_PEEK), EV_EGRESS_CREDIT});
    std::stable_sort(ev.begin(), ev.end(), [](const Core::Ev& a, const Core::Ev& b) { return a.t < b.t; });
    for (int variant = 0; variant < 3; variant++) {
        std::vector<EgressPacket> pk;
        Rig r(ev, 560, opts(variant), Setup{.egress = &pk});
        r.m.run(560);
        CHECK_LOADS(r);
        size_t words = 0;
        for (const EgressPacket& p : pk) words += p.lo.size();
        CHECK_EQ(words, size_t(512));
        CHECK_EQ(r.ie().egress_words_out, 512u);
    }
}

// 17.6.8: a word written with EGRESS_COUNT at 0 is dropped and sets ERRORS.egress_error, sticky; it is P, the
// peripheral's error, so the tile's ERRORS.peripheral_1 is 1 (17.2).
TEST(ie_egress_a_word_with_the_count_at_0_is_dropped_and_an_error) {
    std::vector<Core::Ev> ev = {
        {3, WR, EGRESS_PART, 7},                       // lands at the end of 5
        {5, LD, reg(W_ERRORS), 0},
        {6, LD, reg(W_ERRORS), ERR_EGRESS},
        {6, LD, LIVE, LIVE_ERROR},
        {6, LD, reg(W_EGRESS_COUNT), 0},
        {10, ST, W_EGRESS_COUNT, 1},
        {11, WR, EGRESS_PART, 8},
        {20, LD, reg(W_ERRORS), ERR_EGRESS},           // sticky
        {20, LD, reg(W_EGRESS_COUNT), 0},
    };
    for (int variant = 0; variant < 3; variant++) {
        std::vector<EgressPacket> pk;
        Rig r(ev, 30, opts(variant), Setup{.egress = &pk});
        r.m.run(30);
        CHECK_LOADS(r);
        CHECK_EQ(pk.size(), size_t(1));
        if (pk.size() == 1) CHECK(carries(pk[0], 8, 1, true));
    }
}

// 17.6.8: the outgoing queue holds 512 words; a word written into it full is dropped, sets egress_error and takes
// nothing off EGRESS_COUNT (it has not landed). With the link taking nothing, 513 words of a frame of 600: the 513th
// (written in 516, landing at the end of 518) is the error; the 512 leave in order, 16 link packets of 32.
TEST(ie_egress_a_word_into_a_full_queue_is_dropped_and_an_error) {
    std::vector<Core::Ev> ev = {{3, ST, W_EGRESS_COUNT, 600}};
    writes(ev, 4, 0, 513);
    ev.push_back({518, LD, reg(W_ERRORS), 0});
    ev.push_back({518, LD, reg(W_EGRESS_COUNT), 600 - 512});
    ev.push_back({519, LD, reg(W_ERRORS), ERR_EGRESS});
    ev.push_back({519, LD, reg(W_EGRESS_COUNT), 600 - 512});
    for (int variant = 0; variant < 3; variant++) {
        std::vector<EgressPacket> pk;
        bool hold = true;
        Rig r(ev, 700, opts(variant), Setup{.egress = &pk, .egress_hold = &hold});
        r.m.run_until(530);
        CHECK_LOADS(r);
        CHECK_EQ(r.ie().eg_n, 512u);
        hold = false;
        r.m.run_until(700);
        CHECK_EQ(pk.size(), size_t(16));
        for (unsigned k = 0; k < pk.size() && k < 16; k++) CHECK(carries(pk[k], 32 * k, 32, false));
    }
}

// 17.6.15's UB for Egress: a store to EGRESS_COUNT while it is not 0; a WRITE into the region of other than 128 bits
// or to a part other than 1.
DEATH_TEST(ie_egress_count_stored_while_not_0_is_ub, "EGRESS_COUNT stored .4. while it is 3, not 0") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_EGRESS_COUNT, 3);
    s.write(6, 1, W_EGRESS_COUNT, 4);
}
// invariant 35: a frame's words "from one source at a time" (17.6.13); the next frame may come from another.
DEATH_TEST(ie_egress_a_frames_words_from_two_sources_is_ub, "from 9 while the frame's words come from 7") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_EGRESS_COUNT, 1);
    s.accept(6, 1, 1, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100000, lw::SIZE_128), 0);
    s.write(7, 1, W_EGRESS_COUNT, 2);                  // the next frame, from 7 again and then 9
    s.accept(8, 1, 2, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100000, lw::SIZE_128), 0);
    s.accept(9, 1, 3, lw::hdr(9, 64, lw::M_WRITE, 0, 0x04100000, lw::SIZE_128), 0);
}
TEST(ie_egress_the_next_frame_may_come_from_another_source) {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_EGRESS_COUNT, 1);
    s.accept(6, 1, 1, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100000, lw::SIZE_128), 0);
    s.write(7, 1, W_EGRESS_COUNT, 1);
    s.accept(8, 1, 2, lw::hdr(9, 64, lw::M_WRITE, 0, 0x04100000, lw::SIZE_128), 0);
    CHECK_EQ(s.ingress_egress(1).eg_n, 2u);
    CHECK_EQ(s.ingress_egress(1).errors_sticky, 0);
}
DEATH_TEST(ie_egress_a_write_of_64_bits_is_ub, "a WRITE into its region of size 1 or to part 1") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_EGRESS_COUNT, 3);
    s.accept(6, 1, 0x1234, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100040, lw::SIZE_64), 0);
}
DEATH_TEST(ie_egress_a_write_to_part_0_is_ub, "a WRITE into its region of size 2 or to part 0") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.write(5, 1, W_EGRESS_COUNT, 3);
    s.accept(6, 1, 0x1234, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04000040, lw::SIZE_128), 0x5678);
}
DEATH_TEST(ie_egress_a_write_to_part_2_is_ub, "a WRITE into its region of size 2 or to part 2") {
    SlotCore s;
    s.tile = 64;
    s.install(1, PT_INGRESS);
    s.accept(6, 1, 0x1234, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04200000, lw::SIZE_128), 0x5678);
}

// 17.6.14, of the UART and Egress, on the model itself (as ie_reset_zeroes_its_state): a character and a framing
// error received, characters waiting in the TX FIFO behind one on the line, a divider stored, words in the outgoing
// queue with EGRESS_COUNT not 0; then the reset: UART_CONTROL 0, both FIFOs empty, the line errors 0, uart_tx 1; the
// outgoing queue empty, EGRESS_COUNT 0, egress_credit at 2.
TEST(ie_reset_empties_the_fifos_and_the_queue_and_idles_the_pins) {
    std::vector<Core::Ev> ev = {
        {7, ST, W_UART_TX, 'x'}, {8, ST, W_UART_TX, 'y'},
        {9, ST, W_UART_CONTROL, on(9)},
        {20, ST, W_EGRESS_COUNT, 3},
        {21, WR, EGRESS_PART, 0}, {22, WR, EGRESS_PART, 1},
        {190, ST, W_UART_TX, 'p'}, {191, ST, W_UART_TX, 'q'}, {192, ST, W_UART_TX, 'r'},   // 'p' on the line from 192
        {200, LD, reg(W_STATUS), st_uart(2, 1) | ST_FRAMING},
        {200, LD, reg(W_EVENTS_PEEK), EV_IDLE_UART | EV_UART_RX},
        {200, LD, reg(W_EGRESS_COUNT), 1},
    };
    IeFarEnd far;
    far.frame(10, 0x5C, 9);                            // seen in 12, at the divider stored in 9: f = 12 + 81 + 4 = 97
    far.frame(110, 0x00, 9, false);                    // f = 197: a framing error
    Rig r(ev, 200, opts(0), Setup{.uart = on(7), .far = &far});
    r.m.run_until(200);
    CHECK_LOADS(r);
    IngressEgress& ie = r.ie();
    CHECK(!ie.out().uart_tx);                          // 'p's start bit (192-200 at D = 9)
    CHECK_EQ(ie.eg_n, 2u);                             // nothing takes Egress's link packets here
    ie.reset(201);
    auto word = [&](unsigned w) { uint32_t v = ~0u; ie.word(202, int(w), v); return v; };
    for (unsigned w : {unsigned(W_UART_RX), unsigned(W_UART_CONTROL), unsigned(W_EGRESS_COUNT)}) CHECK_EQ(word(w), 0u);
    CHECK_EQ(ie.status_bits(202), 0u);
    CHECK_EQ(ie.eg_n, 0u);
    ie.sync(202);
    CHECK_EQ(ie.events().value(202), EV_IDLE);
    const IngressEgress::Pins::Out o = ie.out();
    CHECK(o.uart_tx); CHECK(!o.toast); CHECK(!o.led);
    ie.boundary = nullptr;
    ie.step(202);                                      // the line stays idle
    CHECK(ie.out().uart_tx);
    CHECK_EQ(ie.tx_n, 0u); CHECK_EQ(ie.rx_n, 0u);
    CHECK_EQ(ie.write_word(203, W_UART_TX, 'z'), 1);   // dropped: the UART is in its reset
    CHECK_EQ(ie.tx_n, 0u);
}
