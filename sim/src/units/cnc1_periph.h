// cnc1_periph.h (WORKER16, G7): C&C 1's peripheral 1, Ingress/Egress with the UART and the chip lines (type 1;
// sim/ref/meshsim/src/Cnc1Peripheral.java), and the stub engine of C&C 2 (and C&C 3 without the Tracer) (type 2;
// CcCores.Stub). Both are peripherals of W15's slot (units/peripheral.h's method set; AUDIT-M3 3.1, 3.4, 6.9).
//
// TIMING. ref's Cnc1Peripheral is not lazily settled: a store's writeWord acts in the core's step at t, and runCycle(t)
// (C&C 1's slot, after the core) applies the error lines due, decrements the watchdog, shifts the UART and sets the
// live bits. Under the slot's rule a store at t reaches write_word(t, ...) at level 0 of t + 1 (D33: approved): every
// read and GO are exact (the earliest sample after a store at t is t + 2); the peripheral's own work in the store's
// cycle is one cycle later than ref's (a watchdog written at t counts from t + 1, a UART byte pushed at t starts at
// t + 1 at the earliest). Neither reference run reaches the watchdog or the UART.
//
// THE CHIP LINES, as events (D23, AUDIT-M3 section 4), through the peripheral's own ports (ports/begin/end):
//   rows[8]    LinkIn, width 1, latency 1, from M2's row collectors (cnc.h's row word: [7:0] READY edges, [15:8] which
//              tiles sent a line word, [23:16] their error levels): a word arriving at t carries what ref makes visible
//              at t (readyAt = edge + ready_latency; an error line's change at edge, visible error_latency later; M2
//              requires error_latency == ready_latency, one line per tile carries both).
//   cc_line[3] LinkIn, width 1: the controllers' register blocks' line words (tile_ctl.h: bit 0 READY rose, ignored for
//              controllers as in ref; bit 1 the error level after endCycle). [1], [2]: C&C 2's and C&C 3's, latency
//              ready_latency - 1 (put at level 0 of t + 1 for the end of t: visible at t + error_latency, ERROR_LINES_2
//              [1], [2]). [0]: C&C 1's own, latency 1: its ERRORS non-zero latches the toast pin (ref: at the end of t,
//              CcCores.endCycle; here two cycles later, with toast_at = t; T2 reaches no error, so no reference sees it).
//   go[64]     LinkOut, width 0, to every tile's register block: PULSE_GO stored at t is applied at level 0 of t + 1 and
//              put in end(t + 1) with latency go_latency + skew[k], so the block's GO input arrives at
//              t + go_latency + skew[k] + 1, as M2's GO net (cnc.h: the tile sees GO at t + go_latency + skew[k]).
// READY arrivals are applied in begin(t) (READY_LINES at a sample t reads arrivals up to t, ref's readyBits(t)); error
// line arrivals at t are applied in step(t) (ref applies errChanges visible at t in runCycle(t), after the core's reads
// of t). EVENTS [2] (all READY) is live, set by step(t) from allReady(t), as ref's runCycle(t): a load at t + 1 sees it.
//
// THE UART's receive side has no source in M3 (no harness drives ref's rxByte): RX stays empty, so UART_RX reads 0 and its
// pop (a read with a side effect, which the slot's published window can't make) never arises. A store to UART_TX shifts
// out as ref's; the bytes go to a fixed log (serial_out, D24).
#pragma once

#include <cstdint>
#include <cstring>

#include "sim.h"
#include "units/event_counts.h"
#include "units/peripheral.h"

namespace units {

struct Cnc1Peripheral : PeripheralBase<Cnc1Peripheral> {
    // window words (8.14)
    enum : int { ERROR_LINES = 32, READY_LINES = 35, READY_EXPECT = 37, PULSE_GO = 39, WATCHDOG = 40, LED = 41,
                 UART_TX = 42, UART_RX = 43, UART_DIVISOR = 44, UART_STATUS = 45 };
    enum : unsigned { EV_RUN = 0, EV_DMA = 1, EV_ALL_READY = 2, EV_WATCHDOG = 3, EV_UART_RX = 4, EV_UART_TX = 5 };
    static constexpr unsigned FIFO = 16, RESET_DIVISOR = 434, kSerial = 4096;
    static constexpr int DIGEST_N = 4;

    // ---- the own ports (ports(); wired by the chip) ----
    // the own ports' indices in the slot's storage (peripheral.h, OWN PORTS; W15 for W16): rows at row0 .. row0 + 7,
    // cc_line at line0 .. line0 + 2 (OwnPorts::link_in), go at go0 .. go0 + 63 (OwnPorts::link_out)
    unsigned row0 = 0, line0 = 0, go0 = 0;

    // ---- configuration ----
    unsigned ref_cycle = 40;           // Config.refCycle: core cycles per reference-oscillator cycle (the UART)

    // ---- state (ref's fields) ----
    EventCounts ev;
    uint64_t err_line[2] = {};         // ids 0-63, 64-127 (ERROR_LINES_0-2; ref's errLine[128])
    int64_t ready_at[64];              // the latest READY arrival (INT64_MIN: none)
    int64_t cleared_at = INT64_MIN;
    uint64_t expect = 0;               // READY_EXPECT_0-1
    uint64_t wd_count = 0, wd_n = 0;
    bool toast = false; int64_t toast_at = -1;
    uint32_t led = 0;
    uint8_t tx[FIFO] = {}; unsigned tx_head = 0, tx_n = 0;
    unsigned rx_n = 0;                 // always 0 in M3 (no receive source)
    uint32_t divisor = RESET_DIVISOR, tx_divisor = RESET_DIVISOR, rx_divisor = RESET_DIVISOR;
    int64_t tx_busy_until = INT64_MIN;
    bool rx_overrun = false, framing = false;
    // error-line changes that arrived at t, applied by step(t) (ref's errChanges due at t): {id, level}
    uint8_t err_due_id[67] = {}, err_due_lvl[67] = {}; unsigned err_due_n = 0;
    bool go_due = false;               // a PULSE_GO applied at level 0 of this cycle: GO is put in end()
    // ---- statistics and logs (not state) ----
    uint64_t gos = 0, last_go_store = 0, violations = 0, ready_repulses = 0;
    char serial_out[kSerial] = {}; unsigned serial_n = 0;

    Cnc1Peripheral() {
        static const uint8_t depth[6] = {1, 1, 0, 1, 0, 0};
        static const bool flags[6] = {};
        ev.init(6, depth, flags);
        reset(0);
    }

    // ---- identity ----
    int type() const { return 1; }
    int params() const { return 1; }                  // type 1, Ingress/Egress with the UART (4.17.1, D46), version 0

    // ---- the framework hooks ----
    void ports(OwnPortsDecl& d) {
        for (unsigned r = 0; r < 8; r++) { const unsigned i = d.link_in("row", 1); if (r == 0) row0 = i; }
        for (unsigned c = 0; c < 3; c++) { const unsigned i = d.link_in("cc-line", 1); if (c == 0) line0 = i; }
        for (unsigned k = 0; k < 64; k++) { const unsigned i = d.link_out("go", 0); if (k == 0) go0 = i; }
    }
    // Level 0 of t, first: READY arrivals (visible at t), error-line changes due at t (applied by step(t)), C&C 1's own
    // error line (the toast pin).
    void begin(sim::Ctx& c, int64_t t, OwnPorts& o) {
        const sim::LinkIn* rows = o.link_in + row0;
        const sim::LinkIn* cc_line = o.link_in + line0;
        for (unsigned r = 0; r < 8; r++) {
            if (!c.ready(rows[r])) continue;
            const uint64_t w = c.peek(rows[r])[0];
            c.take(rows[r]);
            for (unsigned i = 0; i < 8; i++) {
                if (!(w >> (8 + i) & 1)) continue;
                const unsigned k = 8 * r + i;
                if (w >> i & 1) {
                    if (ready(k, t - 1)) ready_repulses++;       // D29: the spec's sticky bit kept, the case counted
                    ready_at[k] = t;
                }
                due(k, unsigned(w >> (16 + i) & 1));
            }
        }
        for (unsigned cc = 1; cc < 3; cc++)
            if (c.ready(cc_line[cc])) { due(64 + cc, unsigned(c.peek(cc_line[cc])[0] >> 1 & 1)); c.take(cc_line[cc]); }
        if (c.ready(cc_line[0])) {                               // C&C 1's own ERRORS (6.1): the toast pin
            const bool high = c.peek(cc_line[0])[0] >> 1 & 1;
            c.take(cc_line[0]);
            if (high && !toast) { toast = true; toast_at = t - 2; }
        }
    }
    void end(sim::Ctx& c, int64_t, OwnPorts& o) {
        if (!go_due) return;
        for (unsigned k = 0; k < 64; k++) c.put(o.link_out[go0 + k]);
        go_due = false;
    }

    // ---- inputs ----
    void accept(int64_t, int, uint32_t, int, uint64_t, uint64_t) { violations++; }   // its map is Ingress/Egress's, deferred
    void run(int64_t) {}
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    // 1 written, 0 read-only (a bus error), -1 undefined. A UART_TX store never arrives with the FIFO full: the core
    // retried it while R_STALL was set (store_stalls).
    // W15 for W16 (peripheral.h's static map): the words a store writes; 32-36 and 43 are read-only (a bus error)
    bool writable(int w) const { return w >= 37 && w <= 45 && w != 43; }
    int write_word(int64_t t, int w, uint32_t v) {
        switch (w) {
        case 37: expect = (expect & ~0xFFFFFFFFull) | v; return 1;
        case 38: expect = (expect & 0xFFFFFFFFull) | uint64_t(v) << 32; return 1;
        case 39: if (v & 1) pulse_go(t); return 1;
        case 40: wd_n = v; wd_count = wd_n; return 1;
        case 41: led = v & 1; return 1;
        case 42:
            if (tx_n >= FIFO) FATAL("cnc1 peripheral: UART_TX stored at %lld with the TX FIFO full", (long long)t);
            tx[(tx_head + tx_n++) % FIFO] = uint8_t(v); return 1;
        case 44: divisor = v & 0xFFFF; return 1;
        case 45: if (v >> 16 & 1) rx_overrun = false; if (v >> 17 & 1) framing = false; return 1;
        case 32: case 33: case 34: case 35: case 36: case 43: return 0;
        default: return -1;
        }
    }

    // ---- level reads (the end of t - 1, READY arrivals of t included) ----
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return (err_line[0] | err_line[1]) ? 1 : 0; }
    EventCounts& events() { return ev; }
    bool word(int64_t t, int w, uint32_t& v) {
        switch (w) {
        case 32: v = uint32_t(err_line[0]); return true;
        case 33: v = uint32_t(err_line[0] >> 32); return true;
        case 34: v = uint32_t(err_line[1]) & 6; return true;
        case 35: v = uint32_t(ready_bits(t)); return true;
        case 36: v = uint32_t(ready_bits(t) >> 32); return true;
        case 37: v = uint32_t(expect); return true;
        case 38: v = uint32_t(expect >> 32); return true;
        case 39: v = 0; return true;
        case 40: v = uint32_t(wd_count); return true;
        case 41: v = led; return true;
        case 42: v = 0; return true;
        case 43: v = 0; return true;                     // RX is always empty in M3 (header)
        case 44: v = divisor; return true;
        case 45: v = tx_n | rx_n << 8 | (rx_overrun ? 1u << 16 : 0) | (framing ? 1u << 17 : 0); return true;
        default: return false;
        }
    }
    bool store_stalls(int64_t, int w) const { return w == UART_TX && tx_n >= FIFO; }
    // The static map (peripheral.h, W15): the defined words a store writes; ERROR_LINES_0-2, READY_LINES_0-1 and
    // UART_RX are read-only (ref's writeWord returns 0: a bus error, raised by the core at once).
    bool writable(int w) const { return (w >= READY_EXPECT && w <= UART_TX) || w == UART_DIVISOR || w == UART_STATUS; }

    // ---- the work of t (ref's runCycle(t)) ----
    void step(int64_t t) {
        for (unsigned i = 0; i < err_due_n; i++) {
            const unsigned id = err_due_id[i];
            const uint64_t b = 1ull << (id & 63);
            if (err_due_lvl[i]) err_line[id >> 6] |= b; else err_line[id >> 6] &= ~b;
        }
        err_due_n = 0;
        if (wd_count > 0 && --wd_count == 0) { ev.post(t, EV_WATCHDOG); if (!toast) { toast = true; toast_at = t; } }
        if (errors(t) && !toast) { toast = true; toast_at = t; }
        if (t >= tx_busy_until && tx_n > 0) {
            tx_divisor = divisor;
            const uint8_t b = tx[tx_head]; tx_head = (tx_head + 1) % FIFO; tx_n--;
            if (serial_n < kSerial) serial_out[serial_n] = char(b);
            serial_n++;
            tx_busy_until = t + int64_t(10) * tx_divisor * (ref_cycle ? ref_cycle : 1);
        }
        set_live(t);
    }

    // ---- wake ----
    bool idle(int64_t) const { return wd_count == 0 && tx_n == 0 && rx_n == 0 && err_due_n == 0; }
    uint64_t violation_count() const { return violations; }
    void digest_words(int64_t* out) const {
        uint64_t h = sim::hash_fields(err_line, ready_at, cleared_at, expect, wd_count, wd_n, toast, toast_at, led);
        out[0] = int64_t(h);
        out[1] = int64_t(sim::hash_fields(tx, tx_head, tx_n, divisor, tx_divisor, rx_divisor, tx_busy_until, rx_overrun, framing));
        out[2] = int64_t(sim::hash_fields(ev.count, ev.live_bits, ev.last_post_at));
        out[3] = int64_t(err_due_n);
    }
    void reset(int64_t) {
        std::memset(err_line, 0, sizeof err_line);
        for (auto& r : ready_at) r = INT64_MIN;
        cleared_at = INT64_MIN; expect = 0; wd_count = wd_n = 0; toast = false; toast_at = -1; led = 0;
        tx_head = tx_n = rx_n = 0; divisor = tx_divisor = rx_divisor = RESET_DIVISOR; tx_busy_until = INT64_MIN;
        rx_overrun = framing = false; err_due_n = 0; go_due = false;
        ev.clear_all();
        ev.live_bits = 0;
        ev.live(EV_UART_TX, true);                               // the TX FIFO is empty: not full (ref's runCycle sets it)
    }

    // ---- ref's helpers ----
    bool ready(unsigned k, int64_t t) const { return ready_at[k] != INT64_MIN && ready_at[k] >= cleared_at && ready_at[k] <= t; }
    uint64_t ready_bits(int64_t t) const { uint64_t b = 0; for (unsigned k = 0; k < 64; k++) if (ready(k, t)) b |= 1ull << k; return b; }
    bool all_ready(int64_t t) const { return expect != 0 && (ready_bits(t) & expect) == expect; }

private:
    void due(unsigned id, unsigned level) {
        if (err_due_n >= 67) FATAL("cnc1 peripheral: more than 67 error-line changes in one cycle");
        err_due_id[err_due_n] = uint8_t(id); err_due_lvl[err_due_n] = uint8_t(level); err_due_n++;
    }
    // PULSE_GO stored at t (applied at level 0 of t + 1): clear READY 0-63 visible at t (a pulse arriving during t is
    // kept), drive GO (put in end()), restart the watchdog (6.1, 6.3).
    void pulse_go(int64_t t) {
        cleared_at = t + 1; gos++; last_go_store = uint64_t(t); go_due = true;
        if (wd_n > 0) wd_count = wd_n;
    }
    void set_live(int64_t t) {
        ev.live(EV_ALL_READY, all_ready(t));
        ev.live(EV_UART_RX, rx_n > 0);
        ev.live(EV_UART_TX, tx_n < FIFO);
    }
};

// C&C 2's and C&C 3's engines as stubs (CcCores.Stub; types 2 and 3, 4.17.1): present, typed, never running, no events,
// no errors, no words beyond the standard ones; a write into the region is flagged. Always idle.
struct CcStub : PeripheralBase<CcStub> {
    int stub_type = 2;
    EventCounts ev;
    uint64_t violations = 0;
    explicit CcStub(int type = 2) : stub_type(type) {
        static const uint8_t depth[2] = {1, 1};
        static const bool flags[2] = {};
        ev.init(2, depth, flags);
    }
    int type() const { return stub_type; }
    int params() const { return stub_type; }
    void accept(int64_t, int, uint32_t, int, uint64_t, uint64_t) { violations++; }
    void run(int64_t) {}
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return 0; }
    EventCounts& events() { return ev; }
    void step(int64_t) {}
    bool idle(int64_t) const { return true; }
    uint64_t violation_count() const { return violations; }
    void reset(int64_t) { ev.clear_all(); }
};

}  // namespace units
