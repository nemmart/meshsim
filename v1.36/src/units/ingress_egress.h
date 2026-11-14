// ingress_egress.h: Ingress/Egress, the master tile's peripheral 1 (machine-spec 17.6; the RTL's
// rtl/blocks/ingress_egress_unit), and the RW engine's stand-in on the memory tile (17.7). Both are peripherals of the
// slot (units/peripheral.h's method set); the control block and the DMA block (17.1-17.4) are the slot's.
//
// INGRESS/EGRESS holds the chip's control lines: every other tile's error line, the barrier's master side, the watchdog
// and the toast pin, LED, the UART and the host interface. It does no work and sends nothing: a START and a WRITE into
// its region are UB (17.6), and stop the simulator.
//
// TIMING (the slot's rule, periph_slot.h): a store the core issues in t takes effect at the end of t + 1 (6.4): the slot
// calls write_word(t + 1, ...) at level 0 of t + 2, before the reads of t + 2; step(u) is the edge that ends u.
//   - RELEASE: its effect at the end of p = t + 1 clears ARRIVALS but an arrival that reached Ingress/Egress in p, sets
//     the watchdog's count to its limit, and BARRIER_RELEASE's flip-flop is 1 in p + 1: put in end(p + 1) on the 64
//     `release` links, whose latency is RELEASE_LATENCY (the flip-flop's cycle is its first: 18, the RTL's chip.veryl).
//   - ARRIVALS: bit i is set at the end of the cycle in which tile i's BARRIER_ARRIVE reaches Ingress/Egress: the row
//     wire's bit at u is a pulse in u - 1 (its link is 1 + ARRIVE_LATENCY), so a load presented in u reads it.
//   - WATCHDOG: a store of N presented in p sets the limit and the count at the end of p; the count decreases at the end
//     of every later cycle and expires at the end of p + N: EVENTS.watchdog posted, the toast pin set. A store or a
//     RELEASE presented in the cycle the watchdog would expire in wins, and it does not expire (17.6: "nothing sets
//     it"; ingress_egress_unit.veryl: watchdog_expires = !watchdog_store && !release_store && watchdog_count == 1):
//     step(p) has run when the store of p is applied, so the store takes that expiry back (unexpire).
//   - The toast pin: set at the end of a cycle in which the master tile's own error line is 1, or the watchdog expires.
//
// THE NETS, through the peripheral's own ports (peripheral.h, OWN PORTS; wired by the chip, chip.cpp):
//   rows (8)     own WireIns, from the chip's NetRow units (chip.h): [7:0] the row's tiles whose BARRIER_ARRIVE reaches
//                Ingress/Egress in this cycle, [15:8] the row's error lines' levels.
//   lines (3)    own LinkIns, width 1: [0] the master tile's own error line (the toast pin), [1] the memory tile's, [2]
//                the tracer tile's (ERROR_LINES_2 [1], [2]).
//   release (64) own LinkOuts, width 0, to every compute tile's tile control unit: BARRIER_RELEASE.
//
// WHAT IS NOT THE RTL'S YET (PENDING-v1.36.md):
//   - An error line's level reaches ERROR_LINES a cycle later than ERROR_LATENCY says: the RTL reads the wire in the
//     cycle the tile's flip-flop drives it; here the level crosses a Link(1 + ERROR_LATENCY).
//   - The UART: the transmitter takes a byte from the TX FIFO every 10 bits of UART_DIVISOR cycles and logs it
//     (serial_out); the line itself, the divisor's change at an idle point and the receiver are not modelled: RX has no
//     source, so UART_RX reads 0 and pops nothing.
//   - The host interface (words 64-72): the receive side reads empty, as with the LVDS IO block offering no beat (the
//     chip's tests' idle inputs); a store to STREAM_OUT_W0-W2, STREAM_SEND or STREAM_SEND_LAST stops the simulator: the
//     transmit FIFO and the lvds ports are not modelled.
//
// THE RW ENGINE'S STAND-IN (RwEngineStub): the memory tile's peripheral 1 with 17.7's PARAMS (type 2, version 0), its
// words, an idle STATUS, no events and no errors, and the slot's DMA block in front of two output queues that never hold
// an entry. Whatever would make the engine work stops the simulator, named: START, a store to its own words, a DMA
// transfer of one packet or more, a WRITE into its region. (EVENTS_MASK and the DMA block's registers are the slot's.)
#pragma once

#include <cstdint>
#include <cstring>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/peripheral.h"

namespace units {

struct IngressEgress : PeripheralBase<IngressEgress> {
    // window words (17.6)
    enum : int { ERROR_LINES_0 = 32, ARRIVALS_0 = 35, ARRIVE_EXPECT_0 = 37, RELEASE = 39, WATCHDOG = 40, LED = 41,
                 UART_TX = 42, UART_RX = 43, UART_DIVISOR = 44, UART_STATUS = 45,
                 STREAM_OUT_W0 = 64, STREAM_SEND = 67, STREAM_SEND_LAST = 68, STREAM_IN_W0 = 69, STREAM_RECEIVE = 72 };
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_ALL_ARRIVED = 2, EV_WATCHDOG = 3, EV_UART_RX = 4, EV_UART_TX = 5,
                      EV_STREAM_IN = 6, EV_STREAM_OUT = 7 };
    static constexpr unsigned FIFO = 16, kSerial = 4096;      // UART_FIFO_DEPTH (19)
    static constexpr int DIGEST_N = 3;

    // ---- the own ports' indices in the slot's storage (peripheral.h, OWN PORTS) ----
    unsigned row0 = 0, line0 = 0, release0 = 0;

    // ---- configuration ----
    uint32_t reset_divisor = 8681;     // UART_RESET_DIVISOR (19): the chip's, its clock over 115,200 (the RTL chip's default)

    // ---- state ----
    EventCounts ev;
    uint64_t err_line[2] = {};         // ids 0-63, 64-127 (ERROR_LINES_0-2)
    int64_t arrived_at[64];            // the cycle whose reads first see the tile's latest arrival (INT64_MIN: none)
    int64_t cleared_at = INT64_MIN;    // arrivals seen before it are cleared (a RELEASE)
    uint64_t expect = 0;               // ARRIVE_EXPECT_0-1
    uint64_t wd_count = 0, wd_limit = 0;
    int64_t wd_expired_at = INT64_MIN; // the cycle of the last expiry, and what it did: a store of that cycle takes it back
    bool wd_posted = false, wd_toasted = false;
    bool toast = false; int64_t toast_at = -1;   // the toast pin; the cycle at whose end it was set
    uint32_t led = 0;
    uint8_t tx[FIFO] = {}; unsigned tx_head = 0, tx_n = 0;
    uint32_t divisor = 0;
    int64_t tx_busy_until = INT64_MIN;
    bool rx_overrun = false, framing = false;
    bool release_due = false;          // a RELEASE applied at level 0 of this cycle: BARRIER_RELEASE is put in end()
    // ---- statistics and logs (not state) ----
    uint64_t releases = 0, last_release_at = 0;
    char serial_out[kSerial] = {}; unsigned serial_n = 0;

    IngressEgress() {
        static const uint8_t depth[8] = {1, 1, 0, 1, 0, 0, 0, 0};
        static const bool flags[8] = {};
        ev.init(8, depth, flags);
        reset(0);
    }

    // ---- identity ----
    int type() const { return 1; }
    int params() const { return 1; }                  // type 1, version 0, no constants (17.6)

    // ---- the framework hooks ----
    void ports(OwnPortsDecl& d) {
        for (unsigned r = 0; r < 8; r++) { const unsigned i = d.wire_in("row"); if (r == 0) row0 = i; }
        for (unsigned c = 0; c < 3; c++) { const unsigned i = d.link_in("error-line", 1); if (c == 0) line0 = i; }
        for (unsigned k = 0; k < 64; k++) { const unsigned i = d.link_out("release", 0); if (k == 0) release0 = i; }
    }
    // Level 0 of t, first: the arrivals and the error lines' levels as the reads of t see them; the master tile's own
    // error line (the toast pin).
    void begin(sim::Ctx& c, int64_t t, OwnPorts& o) {
        for (unsigned r = 0; r < 8; r++) {
            const uint64_t w = c.read(o.wire_in[row0 + r]);
            for (uint64_t a = w & 0xFF; a; a &= a - 1) arrived_at[8 * r + unsigned(__builtin_ctzll(a))] = t;
            err_line[0] = (err_line[0] & ~(0xFFull << (8 * r))) | (w >> 8 & 0xFF) << (8 * r);
        }
        const sim::LinkIn* line = o.link_in + line0;
        for (unsigned s = 1; s < 3; s++)
            if (c.ready(line[s])) {
                const uint64_t b = 1ull << s;                    // id 64 + s: bit s of ERROR_LINES_2
                if (c.peek(line[s])[0] & 1) err_line[1] |= b; else err_line[1] &= ~b;
                c.take(line[s]);
            }
        if (c.ready(line[0])) {                                  // the level in t - 1: the pin is set at its end
            const bool high = c.peek(line[0])[0] & 1;
            c.take(line[0]);
            if (high && !toast) { toast = true; toast_at = t - 1; }
            if (high) wd_toasted = false;                        // the pin is the line's too: no store takes it back
        }
    }
    void end(sim::Ctx& c, int64_t, OwnPorts& o) {
        if (!release_due) return;
        for (unsigned k = 0; k < 64; k++) c.put(o.link_out[release0 + k]);
        release_due = false;
    }

    // ---- inputs ----
    void accept(int64_t t, int src, uint32_t offset, int, uint64_t, uint64_t) {
        FATAL("Ingress/Egress: undefined behavior at %lld: a WRITE into the master tile's region 0x04 (offset 0x%x, from %d): its "
              "region takes none (17.6)", (long long)t, offset, src);
    }
    void run(int64_t t) { FATAL("Ingress/Egress: undefined behavior at %lld: a START (17.6: it has no work)", (long long)t); }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    // 1 written, 0 read-only (an access fault, the core's), -1 undefined. A UART_TX store never arrives with the FIFO
    // full: the core retried it while R_STALL was set (store_stalls).
    int write_word(int64_t t, int w, uint32_t v) {
        switch (w) {
        case 37: expect = (expect & ~0xFFFFFFFFull) | v; return 1;
        case 38: expect = (expect & 0xFFFFFFFFull) | uint64_t(v) << 32; return 1;
        case RELEASE: if (v & 1) release(t); return 1;
        case WATCHDOG: unexpire(t); wd_limit = v; wd_count = v; return 1;
        case LED: led = v & 1; return 1;
        case UART_TX:
            if (tx_n >= FIFO) FATAL("Ingress/Egress: UART_TX stored at %lld with the TX FIFO full", (long long)t);
            tx[(tx_head + tx_n++) % FIFO] = uint8_t(v); return 1;
        case UART_DIVISOR: divisor = v & 0xFFFF; return 1;
        case UART_STATUS: if (v >> 16 & 1) rx_overrun = false; if (v >> 17 & 1) framing = false; return 1;
        case 64: case 65: case 66: case STREAM_SEND: case STREAM_SEND_LAST:
            FATAL("Ingress/Egress: a store to window word %d at %lld: the host interface's transmit side is not modelled "
                  "(17.6; PENDING-v1.36.md)", w, (long long)t);
        case 32: case 33: case 34: case 35: case 36: case UART_RX: case 69: case 70: case 71: case STREAM_RECEIVE: return 0;
        default: return -1;
        }
    }

    // ---- level reads (the end of t - 1, with the nets as begin(t) took them) ----
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return (err_line[0] | err_line[1]) ? 1 : 0; }   // [0] tile_error, live
    EventCounts& events() { return ev; }
    void sync(int64_t t) { set_live(t); }
    bool word(int64_t t, int w, uint32_t& v) {
        switch (w) {
        case 32: v = uint32_t(err_line[0]); return true;
        case 33: v = uint32_t(err_line[0] >> 32); return true;
        case 34: v = uint32_t(err_line[1]) & 6; return true;      // the master tile's own bit and those past 66 read 0
        case 35: v = uint32_t(arrivals(t)); return true;
        case 36: v = uint32_t(arrivals(t) >> 32); return true;
        case 37: v = uint32_t(expect); return true;
        case 38: v = uint32_t(expect >> 32); return true;
        case WATCHDOG: v = uint32_t(wd_count); return true;
        case LED: v = led; return true;
        case UART_RX: v = 0; return true;                       // the RX FIFO is always empty (header)
        case UART_DIVISOR: v = divisor; return true;
        case UART_STATUS: v = tx_n | (rx_overrun ? 1u << 16 : 0) | (framing ? 1u << 17 : 0); return true;
        case RELEASE: case UART_TX: case 64: case 65: case 66: case STREAM_SEND: case STREAM_SEND_LAST:   // W: a load reads 0
        case 69: case 70: case 71: case STREAM_RECEIVE:           // the receive FIFO is always empty (header)
            v = 0; return true;
        default: return false;
        }
    }
    bool store_stalls(int64_t, int w) const { return w == UART_TX && tx_n >= FIFO; }
    // The stores that may stall (the RTL's register_bus, own_stalling: block 2 word 10, block 4 words 3 and 4): UART_TX
    // while the TX FIFO is full, the sends while the host interface's transmit FIFO is (17.6).
    bool may_stall(int w) const { return w == UART_TX || w == STREAM_SEND || w == STREAM_SEND_LAST; }
    // The static map (peripheral.h): the defined words a store writes; the others are R: a store is an access fault.
    bool writable(int w) const {
        return (w >= ARRIVE_EXPECT_0 && w <= UART_TX) || w == UART_DIVISOR || w == UART_STATUS || (w >= STREAM_OUT_W0 && w <= STREAM_SEND_LAST);
    }

    // ---- the edge that ends t ----
    void step(int64_t t) {
        if (wd_count > 0 && --wd_count == 0) {                 // the expiry, unless a store or a RELEASE of t sets the count
            wd_expired_at = t; wd_posted = ev.count_of(EV_WATCHDOG) == 0; wd_toasted = !toast;
            ev.post(t, EV_WATCHDOG);
            if (!toast) { toast = true; toast_at = t; }
        }
        if (t >= tx_busy_until && tx_n > 0) {
            const uint8_t b = tx[tx_head]; tx_head = (tx_head + 1) % FIFO; tx_n--;
            if (serial_n < kSerial) serial_out[serial_n] = char(b);
            serial_n++;
            tx_busy_until = t + int64_t(10) * (divisor ? divisor : 1);
        }
    }

    // ---- wake ----
    bool idle(int64_t) const { return wd_count == 0 && tx_n == 0; }
    uint64_t violation_count() const { return 0; }
    void digest_words(int64_t* out) const {
        out[0] = int64_t(sim::hash_fields(err_line, arrived_at, cleared_at, expect, wd_count, wd_limit, toast, toast_at, led));
        out[1] = int64_t(sim::hash_fields(tx, tx_head, tx_n, divisor, tx_busy_until, rx_overrun, framing));
        out[2] = int64_t(sim::hash_fields(ev.count, ev.live_bits, ev.last_post_at));
    }
    void reset(int64_t) {
        std::memset(err_line, 0, sizeof err_line);
        for (auto& r : arrived_at) r = INT64_MIN;
        cleared_at = INT64_MIN; expect = 0; wd_count = wd_limit = 0; toast = false; toast_at = -1; led = 0;
        wd_expired_at = INT64_MIN; wd_posted = wd_toasted = false;
        tx_head = tx_n = 0; divisor = reset_divisor; tx_busy_until = INT64_MIN;
        rx_overrun = framing = false; release_due = false;
        ev.clear_all();
        ev.live_bits = 0;
        ev.live(EV_UART_TX, true); ev.live(EV_STREAM_OUT, true);   // room in the TX FIFO and in the transmit FIFO
    }

    // ARRIVALS as a load presented in t reads it (17.6)
    bool arrived(unsigned k, int64_t t) const { return arrived_at[k] != INT64_MIN && arrived_at[k] >= cleared_at && arrived_at[k] <= t; }
    uint64_t arrivals(int64_t t) const { uint64_t b = 0; for (unsigned k = 0; k < 64; k++) if (arrived(k, t)) b |= 1ull << k; return b; }
    bool all_arrived(int64_t t) const { return expect != 0 && (arrivals(t) & expect) == expect; }

private:
    // RELEASE's effect at the end of p: the arrivals seen through p are cleared (one whose pulse reached Ingress/Egress
    // in p is first seen by the reads of p + 1, and kept), the watchdog restarts, BARRIER_RELEASE's flip-flop is set.
    void release(int64_t p) {
        unexpire(p);
        cleared_at = p + 1; releases++; last_release_at = uint64_t(p); release_due = true;
        wd_count = wd_limit;
    }
    // A WATCHDOG store or a RELEASE presented in p sets the count at the end of p, so the watchdog does not expire in p
    // (header): what step(p)'s expiry did is taken back, the event's count it added and the toast pin it set.
    void unexpire(int64_t p) {
        if (wd_expired_at != p) return;
        if (wd_posted) ev.clear(EV_WATCHDOG);
        if (wd_toasted) { toast = false; toast_at = -1; }
        wd_expired_at = INT64_MIN;
    }
    void set_live(int64_t t) {
        ev.live(EV_ALL_ARRIVED, all_arrived(t));
        ev.live(EV_UART_RX, false);
        ev.live(EV_UART_TX, tx_n < FIFO);
        ev.live(EV_STREAM_IN, false);
        ev.live(EV_STREAM_OUT, true);
    }
};

// The RW engine's stand-in (header): present, typed, idle; whatever would start it stops the simulator.
struct RwEngineStub : PeripheralBase<RwEngineStub> {
    enum : int { RW_CONFIG = 32, RW_LPDDR_ADDR = 33, RW_BUFFER_ADDR = 34, RW_REPLY_W0 = 35, RW_REPLY_SEND = 36 };
    EventCounts ev;
    RwEngineStub() {
        static const uint8_t depth[2] = {1, 1};
        static const bool flags[2] = {};
        ev.init(2, depth, flags);
    }
    int type() const { return 2; }
    int params() const { return 2; }                  // type 2, version 0, no constants (17.7)
    void accept(int64_t t, int src, uint32_t offset, int, uint64_t, uint64_t) {
        FATAL("the RW engine is a stub (PENDING-v1.36.md): a WRITE into the memory tile's write buffer at %lld (offset 0x%x, from %d)",
              (long long)t, offset, src);
    }
    void run(int64_t t) { FATAL("the RW engine is a stub (PENDING-v1.36.md): START at %lld", (long long)t); }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return 0; }
    EventCounts& events() { return ev; }
    bool word(int64_t, int w, uint32_t& v) { v = 0; return w >= RW_CONFIG && w <= RW_REPLY_SEND; }
    bool writable(int w) const { return w >= RW_CONFIG && w <= RW_REPLY_SEND; }
    int write_word(int64_t t, int w, uint32_t v) {
        FATAL("the RW engine is a stub (PENDING-v1.36.md): a store of 0x%x to its window word %d at %lld", v, w, (long long)t);
    }
    // the DMA block's two output queues (17.7: 0 the read's beats, 1 the replies): never an entry
    int out_queues() const { return 2; }
    void out_start(int64_t t, int q, int64_t packets) {
        FATAL("the RW engine is a stub (PENDING-v1.36.md): a DMA transfer of %lld packets from output queue %d at %lld",
              (long long)packets, q, (long long)t);
    }
    void step(int64_t) {}
    bool idle(int64_t) const { return true; }
    uint64_t violation_count() const { return 0; }
    void reset(int64_t) { ev.clear_all(); }
};

}  // namespace units
