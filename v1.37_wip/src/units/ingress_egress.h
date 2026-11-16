// ingress_egress.h: Ingress/Egress, the master tile's peripheral 1 (machine-spec 17.6; the RTL's
// rtl/blocks/ingress_egress_unit). It is a peripheral of the slot (units/peripheral.h's method set); the control block
// (17.1-17.4) is the slot's. (The RW engine, the memory tile's peripheral 1, is units/rw_engine.h.)
//
// INGRESS/EGRESS holds the chip's control lines and its external IO: every other tile's error line, the barrier's
// master side, the watchdog and the toast pin, LED, the UART and the host interface. It does no work and sends
// nothing: a START and a WRITE into its region are UB (17.6), and stop the simulator. The UART and the host interface
// are the RTL's uart.veryl and ingress_egress_unit.veryl, register for register, cycle for cycle at the pins.
//
// TIMING (the slot's rule, periph_slot.h): a store the core issues in t is presented in p = t + 1 and takes effect at
// the end of p (6.4): the slot calls write_word(p, ...) at level 0 of p + 1, before the reads of p + 1. A load
// presented in p reads the state at the end of p - 1, and one that pops (UART_RX, STREAM_RECEIVE) pops at the end of
// p: load_effect(p, w), called the same way. step(u) is the edge that ends u, without the core's access of u, which
// the slot applies after it: every rule below that reads "at the start of the cycle" is kept across the two calls.
//   - RELEASE: its effect at the end of p clears ARRIVALS but an arrival that reached Ingress/Egress in p, sets
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
// THE UART (17.6; uart.veryl). Frames: a start bit, 0; 8 data bits, the least significant first; a stop bit, 1; the
// line 1 while idle; each bit D cycles, D the divisor a frame takes at its start (UART_DIVISOR as it stands at the
// start of the frame's first cycle), so a UART_DIVISOR store takes effect at each direction's next idle point. A
// divisor of 0 is UB (17.6); here, as in the RTL, the counters wrap (a bit of 65,536 cycles; samples 131,072 apart).
//   - The TX FIFO (UART_FIFO_DEPTH = 16): a UART_TX store presented in p with the FIFO not full at the start of p
//     pushes its byte at the end of p; with it full the store stalls (store_stalls: the reg interface's ready), a pop
//     in that cycle notwithstanding, and is taken in the first cycle the FIFO starts not full.
//   - The transmitter: a frame starts in a cycle at an idle point (no frame, or the last cycle of a stop bit) with the
//     TX FIFO not empty: the head is popped at the end of that cycle, and the start bit is on uart_tx from the next.
//     So a byte pushed into an empty FIFO in p, the transmitter idle, is popped at the end of p + 1 and its start bit
//     is on the pin from p + 2; frames follow back to back, 10 D cycles apart. uart_tx is a flip-flop.
//   - The receiver: two flip-flops synchronise uart_rx and a third holds the line's last value, so a level on the pin
//     in cycle c is the line the receiver reads in c + 2. A frame starts in a cycle e in which the receiver is idle and
//     the line is 0 after 1; bit k (1-8 data, 9 the stop bit) is sampled in e + k D + floor(D / 2); the start bit is
//     not sampled. At the stop bit's sample the frame ends: a stop bit of 0 drops it and sets framing_error; else the
//     RX FIFO full at the start of that cycle (a pop in it notwithstanding) drops the byte and sets rx_overrun; else
//     the byte enters the RX FIFO at the end of that cycle. The receiver is idle again from the next cycle.
//   - UART_RX reads [7:0] the RX FIFO's head and [8] valid, 0 while it is empty; a load pops the head at the end of its
//     cycle if the FIFO was not empty at its start. UART_STATUS: [4:0] the TX FIFO's bytes (the byte on the line has
//     left it), [12:8] the RX FIFO's, [16] rx_overrun, [17] framing_error; a store clears each line error it writes 1,
//     but one raised at the end of the store's own cycle stays set.
//
// THE HOST INTERFACE (17.6, 3; ingress_egress_unit.veryl): lvds_in and lvds_out, AXI4-Stream, TDATA of STREAM_BYTES
// bytes (`stream_bytes`, 1-16; the chip's is 4) and TLAST, each behind a FIFO of STREAM_DEPTH = 4 beats. A beat is
// STREAM_BEAT_WORDS = ceil(STREAM_BYTES / 4) words, its top word the last; a word's bits past TDATA and the words past
// the beat are ignored on a store and read 0.
//   - Out: STREAM_OUT_Ww holds word w below the top; STREAM_SEND or STREAM_SEND_LAST presented in p with the transmit
//     FIFO not full at the start of p pushes the beat (its top word the store's, TLAST 0 or 1) and clears
//     STREAM_OUT_W0-W2 at the end of p; with it full the store stalls. lvds_out_tvalid is 1 in a cycle at whose start
//     the FIFO holds a beat (from p + 1 for a push into an empty FIFO), with the head's TDATA and TLAST, held until
//     the handshake: the head leaves at the end of a cycle in which lvds_out_tvalid and lvds_out_tready are 1.
//   - In: lvds_in_tready is 1 in a cycle at whose start the receive FIFO is not full, but 0 in the first cycle after
//     the reset (a flip-flop). A beat enters at the end of a cycle in which lvds_in_tvalid and lvds_in_tready are 1,
//     and is read from the next. STREAM_IN_Ww reads word w below the top of the oldest beat and STREAM_RECEIVE its
//     top word, 0 while the FIFO is empty; a STREAM_RECEIVE load pops the beat at the end of its cycle if the FIFO was
//     not empty at its start. STATUS [2] stream_last is the oldest beat's TLAST, 0 while there is none.
//   - EVENTS, live, from the registers a load of the cycle reads: [4] uart_rx, a byte in the RX FIFO; [5] uart_tx, room
//     in the TX FIFO; [6] stream_in, a beat in the receive FIFO; [7] stream_out, room in the transmit FIFO.
//
// THE PINS AND THE PORTS (18, 3) are the chip's boundary: the serial line's far end and the LVDS IO block are not chip
// hardware. A test bench plays them through `boundary`, called once in every cycle t, in step(t), with the outputs'
// levels in t (every one a flip-flop: Pins::Out) and the inputs' it gives for t (Pins::In). Without one the inputs
// are idle, the RTL chip's tests' idle_inputs: uart_rx 1, no beat offered on lvds_in, lvds_out_tready 0. With one the
// peripheral steps in every cycle (idle() is false). `out()` is the outputs' levels in the next cycle to step.
//
// THE NETS, through the peripheral's own ports (peripheral.h, OWN PORTS; wired by the chip, chip.cpp):
//   rows (8)     own WireIns, from the chip's NetRow units (chip.h): [7:0] the row's tiles whose BARRIER_ARRIVE reaches
//                Ingress/Egress in this cycle, [15:8] the row's error lines' levels.
//   lines (3)    own LinkIns, width 1: [0] the master tile's own error line (the toast pin), [1] the memory tile's, [2]
//                the tracer tile's (ERROR_LINES_2 [1], [2]).
//   release (64) own LinkOuts, width 0, to every compute tile's tile control unit: BARRIER_RELEASE.
//
// v1.37: THE HOST LINK (../v1.4_wip/planning/INGRESS-INTERFACE.md, "II"; units/host_link.h), built only when a machine
// asks for it (`host_link_on()` at configuration: MachConfig::host_link); without it this unit is v1.36's, word for
// word. It is the new host interface, beside the stream words above, which the RTL's images use: a black box that
// reads a packet file (the link's bytes) in place of the LVDS pins, checks every packet, and presents the packets of
// each stream, foreground (queue 0) and background (queue 1), to the master tile's core:
//   STATUS [3] foreground_ready, [4] background_ready   a packet is waiting on the stream (live)
//   EVENTS [8] foreground_service, [9] background_service   counted, depth 1: a packet asks to be read out
//   ERRORS [1] crc_error, [2] sequence_error, [3] format_error   sticky; after one nothing more is presented
//   word 80 FOREGROUND_WORDS, word 81 BACKGROUND_WORDS   R: the words of the packet that asked; 0 when none waits
//   block 1, the DMA (17.4): DMA_HDR.queue 0 the foreground, 1 the background; reads the packet's next words out
//   - THE RULE FOR A SERVICE FLAG (II): a packet reaches the front of its stream; its flag is posted; the core reads
//     its words out with the DMA, in one transfer or several; the stream posts no further flag until as many words as
//     the register showed have been read out; then the next packet, if there is one, is at the front, and its flag is
//     posted. `..._WORDS` holds the packet's length until then. The two streams are independent.
//   - A DMA transfer of more words than are left of the packet at the front is UB (II), and stops the simulator.
//   - A packet of length 0 is checked, and asks for no service.
//   - A packet that fails a check sets its error bit in the cycle its last byte would have arrived, and the link is
//     dead: nothing more is read from the file. Packets already at a stream stay, and can be read out.
//   - TIMING, PLAIN: the file arrives at `link_cycles_per_byte` cycles a byte (1: HL's 1 GB a second at a 1 GHz clock),
//     its first byte in cycle 1, the first after the reset. A packet whose last byte arrives in cycle c is at its
//     stream at the end of c, and is seen by the loads of c + 1. Each stream holds LINK_QUEUE packets; the file's next
//     packet is not taken while its stream is full (what "stop sending" does on the real link), and its bytes then
//     start in the cycle after the one that made room. It is not the hardware's timing.
//   - The file ending inside a packet: that packet never arrives, and `link_cut` says so. The reset rewinds the file.
//
// WHAT IS NOT THE RTL'S (OPEN-v1.36.md, (a)):
//   - An error line's level reaches ERROR_LINES a cycle later than ERROR_LATENCY says: the RTL reads the wire in the
//     cycle the tile's flip-flop drives it; here the level crosses a Link(1 + ERROR_LATENCY).
#pragma once

#include <cstdint>
#include <cstring>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/host_link.h"
#include "units/peripheral.h"

namespace units {

struct IngressEgress : PeripheralBase<IngressEgress> {
    // window words (17.6)
    enum : int { ERROR_LINES_0 = 32, ARRIVALS_0 = 35, ARRIVE_EXPECT_0 = 37, RELEASE = 39, WATCHDOG = 40, LED = 41,
                 UART_TX = 42, UART_RX = 43, UART_DIVISOR = 44, UART_STATUS = 45,
                 STREAM_OUT_W0 = 64, STREAM_SEND = 67, STREAM_SEND_LAST = 68, STREAM_IN_W0 = 69, STREAM_RECEIVE = 72,
                 FOREGROUND_WORDS = 80, BACKGROUND_WORDS = 81 };                       // v1.37: the host link's
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_ALL_ARRIVED = 2, EV_WATCHDOG = 3, EV_UART_RX = 4, EV_UART_TX = 5,
                      EV_STREAM_IN = 6, EV_STREAM_OUT = 7, EV_FOREGROUND_SERVICE = 8, EV_BACKGROUND_SERVICE = 9 };
    enum : int { ERR_TILE = 1 << 0, ERR_CRC = 1 << 1, ERR_SEQUENCE = 1 << 2, ERR_FORMAT = 1 << 3 };
    enum : uint32_t { ST_STREAM_LAST = 1u << 2, ST_FOREGROUND_READY = 1u << 3, ST_BACKGROUND_READY = 1u << 4 };
    static constexpr unsigned LINK_QUEUE = 4;                 // v1.37: the packets a stream of the host link holds
    static constexpr unsigned FIFO = 16, kSerial = 4096;      // UART_FIFO_DEPTH (19)
    static constexpr unsigned STREAM_DEPTH = 4;               // (19)
    static constexpr int DIGEST_N = 5;

    // ---- the pins and the ports (header, THE PINS AND THE PORTS): a cycle's levels ----
    struct Pins {
        struct Out {
            bool uart_tx = true, toast = false, led = false;
            bool lvds_in_tready = false;
            bool lvds_out_tvalid = false, lvds_out_tlast = false;
            uint32_t lvds_out_tdata[4] = {};                 // word w: TDATA's bits [32w + 31 : 32w]; the head's (a register)
        };
        struct In {                                          // the idle inputs
            bool uart_rx = true;
            bool lvds_in_tvalid = false, lvds_in_tlast = false;
            uint32_t lvds_in_tdata[4] = {};
            bool lvds_out_tready = false;
        };
    };
    using Boundary = void (*)(void* far_end, int64_t t, const Pins::Out& out, Pins::In& in);
    Boundary boundary = nullptr;       // the far ends, a test bench's (null: the idle inputs)
    void* far_end = nullptr;

    // ---- the own ports' indices in the slot's storage (peripheral.h, OWN PORTS) ----
    unsigned row0 = 0, line0 = 0, release0 = 0;

    // ---- configuration (set before the run; reset() takes it) ----
    uint32_t reset_divisor = 8681;     // UART_RESET_DIVISOR (19): the chip's, its clock over 115,200 (the RTL chip's default)
    unsigned stream_bytes = 4;         // STREAM_BYTES (19), 1-16: the chip's (the RTL's Veryl.toml)

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
    bool release_due = false;          // a RELEASE applied at level 0 of this cycle: BARRIER_RELEASE is put in end()
    // the UART (uart.veryl's registers, by its names)
    uint32_t divisor = 0;              // UART_DIVISOR [15:0]
    uint8_t tx[FIFO] = {}; unsigned tx_head = 0, tx_n = 0;
    bool tx_busy = false, tx_line = true;           // a frame on the line; uart_tx
    uint16_t tx_cycles_left = 0, tx_bit_divisor = 0, tx_bits = 0;   // the bit's cycles from this one on; the frame's D; the bits to send
    bool rx_meta = true, rx_line = true, rx_line_last = true;       // the synchroniser, and the line's last value
    bool rx_busy = false;
    uint8_t rx_bit = 0, rx_data = 0;                // the bit sampled next (1-8 data, 9 the stop bit); the data bits sampled
    uint32_t rx_cycles_left = 0;                    // 17 bits: the cycles to its sample, the sample's included
    uint16_t rx_bit_divisor = 0;
    uint8_t rx[FIFO] = {}; unsigned rx_head = 0, rx_n = 0;
    bool rx_overrun = false, framing = false;
    int64_t overrun_at = INT64_MIN, framing_at = INT64_MIN;   // the cycle at whose end each was last raised
    // the host interface
    struct Beat { uint32_t w[4]; bool last; };
    Beat out_q[STREAM_DEPTH] = {}; unsigned out_head = 0, out_n = 0;   // the transmit FIFO
    Beat in_q[STREAM_DEPTH] = {}; unsigned in_head = 0, in_n = 0;      // the receive FIFO
    uint32_t out_low[3] = {};          // STREAM_OUT_W0-W2
    bool in_ready_ff = false;          // lvds_in_tready's flip-flop out of its reset value: 1 from the first edge
    // the cycle last stepped, and the RX and receive FIFOs' counts at its start: what a popping load of it saw
    int64_t stepped_at = INT64_MIN;
    unsigned rx_n_start = 0, in_n_start = 0;
    // ---- v1.37: the host link (header, THE HOST LINK) ----
    bool host_link = false;            // configuration: host_link_on()
    int64_t link_cycles_per_byte = 1;  // configuration: the link's rate; 0: a packet is there as soon as there is room
    hostlink::Reader link;             // the packet file (set_link)
    struct LinkStream {
        hostlink::Packet q[LINK_QUEUE];
        unsigned head = 0, n = 0;
        unsigned read = 0;             // the words of the packet at the front read out so far
    } link_stream[2];                  // 0 the foreground, 1 the background
    int link_errors = 0;               // ERRORS [3:1]
    bool link_dead = false, link_cut = false;   // a check failed, or the file is at its end: nothing more is read
    bool link_has_next = false;        // the file's next packet is read, and on its way or waiting for room
    bool link_scheduled = false;       //   on its way: it arrives at the end of cycle link_next_at
    bool link_waited = false;          //   it waited for room at its stream: its bytes start after the wait
    hostlink::Packet link_next;
    hostlink::Check link_next_check = hostlink::Check::OK;
    size_t link_next_bytes = 0;
    int64_t link_next_at = 0, link_at = 0;   // link_at: the cycle at whose end the last byte taken arrived
    uint64_t link_packets = 0, link_words = 0, link_bytes = 0;   // statistics: what has arrived
    // ---- statistics and logs (not state) ----
    uint64_t releases = 0, last_release_at = 0;
    char serial_out[kSerial] = {}; unsigned serial_n = 0;   // the bytes the transmitter took, each as its frame starts

    IngressEgress() {
        static const uint8_t depth[8] = {1, 1, 0, 1, 0, 0, 0, 0};
        static const bool flags[8] = {};
        ev.init(8, depth, flags);
        reset(0);
    }

    // ---- v1.37: configuration: the host link built, and the file it reads (the bytes outlive the unit) ----
    void host_link_on() {
        static const uint8_t depth[10] = {1, 1, 0, 1, 0, 0, 0, 0, 1, 1};
        static const bool flags[10] = {};
        host_link = true;
        ev.init(10, depth, flags);
        reset(0);
    }
    void set_link(const uint8_t* bytes, size_t n) {
        if (!host_link) FATAL("Ingress/Egress: a packet file with no host link built (MachConfig::host_link)");
        link.open(bytes, n);
        link_reset();
    }

    // ---- identity ----
    int type() const { return 1; }
    int params() const { return 1; }                  // type 1, version 0, no constants (17.6)
    // a beat (17.6): its words, its top word's index, the top word's bits of TDATA
    unsigned beat_words() const { return (stream_bytes + 3) / 4; }
    unsigned top_word() const { return beat_words() - 1; }
    uint32_t top_mask() const { const unsigned bits = 8 * stream_bytes - 32 * top_word(); return bits == 32 ? ~0u : (1u << bits) - 1; }

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
    // The store presented in t, after step(t): 1 written, 0 read-only (an access fault, the core's), -1 undefined. A
    // store that stalls never arrives: the core holds it while R_STALL is set (store_stalls), the FIFO's count at the
    // start of the cycle.
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
        case UART_STATUS:                              // a line error raised at the end of the store's cycle stays set
            if ((v >> 16 & 1) && overrun_at != t) rx_overrun = false;
            if ((v >> 17 & 1) && framing_at != t) framing = false;
            return 1;
        case 64: case 65: case 66:                     // a word at or above the beat's top holds nothing
            if (unsigned(w - STREAM_OUT_W0) < top_word()) out_low[w - STREAM_OUT_W0] = v;
            return 1;
        case STREAM_SEND: case STREAM_SEND_LAST: {
            if (out_n >= STREAM_DEPTH) FATAL("Ingress/Egress: a send stored at %lld with the transmit FIFO full", (long long)t);
            Beat& b = out_q[(out_head + out_n++) % STREAM_DEPTH];
            const unsigned top = top_word();
            for (unsigned k = 0; k < 4; k++) b.w[k] = k < top ? out_low[k] : k == top ? v & top_mask() : 0;
            b.last = w == STREAM_SEND_LAST;
            out_low[0] = out_low[1] = out_low[2] = 0;
            return 1;
        }
        case 32: case 33: case 34: case 35: case 36: case UART_RX: case 69: case 70: case 71: case STREAM_RECEIVE: return 0;
        case FOREGROUND_WORDS: case BACKGROUND_WORDS: return host_link ? 0 : -1;
        default: return -1;
        }
    }
    // The loads that pop (17.6), and the pop of one presented in t, after step(t): the head as the load read it leaves
    // at the end of t, if the FIFO was not empty at the start of t (a byte or a beat that entered at the end of t stays).
    bool load_pops(int w) const { return w == UART_RX || w == STREAM_RECEIVE; }
    void load_effect(int64_t t, int w) {
        if (w == UART_RX) {
            if ((stepped_at == t ? rx_n_start : rx_n) > 0) { rx_head = (rx_head + 1) % FIFO; rx_n--; }
        } else if (w == STREAM_RECEIVE) {
            if ((stepped_at == t ? in_n_start : in_n) > 0) { in_head = (in_head + 1) % STREAM_DEPTH; in_n--; }
        }
    }

    // ---- level reads (the end of t - 1, with the nets as begin(t) took them) ----
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return ((err_line[0] | err_line[1]) ? 1 : 0) | link_errors; }   // [0] tile_error, live; v1.37: [3:1] the link's, sticky
    uint32_t status_bits(int64_t) const {                    // [2] stream_last; v1.37: [3], [4] a packet waiting on a stream
        return (in_n > 0 && in_q[in_head].last ? ST_STREAM_LAST : 0) | (link_stream[0].n > 0 ? ST_FOREGROUND_READY : 0) |
               (link_stream[1].n > 0 ? ST_BACKGROUND_READY : 0);
    }
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
        case UART_RX: v = rx_n > 0 ? 1u << 8 | rx[rx_head] : 0; return true;
        case UART_DIVISOR: v = divisor; return true;
        case UART_STATUS: v = tx_n | rx_n << 8 | (rx_overrun ? 1u << 16 : 0) | (framing ? 1u << 17 : 0); return true;
        case 69: case 70: case 71:                               // a word at or above the top reads 0
            v = in_n > 0 && unsigned(w - STREAM_IN_W0) < top_word() ? in_q[in_head].w[w - STREAM_IN_W0] : 0; return true;
        case STREAM_RECEIVE: v = in_n > 0 ? in_q[in_head].w[top_word()] : 0; return true;
        case RELEASE: case UART_TX: case 64: case 65: case 66: case STREAM_SEND: case STREAM_SEND_LAST:   // W: a load reads 0
            v = 0; return true;
        case FOREGROUND_WORDS: case BACKGROUND_WORDS: {      // v1.37: the words of the packet at the stream's front
            if (!host_link) return false;
            const LinkStream& s = link_stream[w - FOREGROUND_WORDS];
            v = s.n > 0 ? s.q[s.head].length : 0;
            return true;
        }
        default: return false;
        }
    }
    // The stores that stall now: the FIFO full at the start of the cycle (lines_ready, stream_ready).
    bool store_stalls(int64_t, int w) const {
        return (w == UART_TX && tx_n >= FIFO) || ((w == STREAM_SEND || w == STREAM_SEND_LAST) && out_n >= STREAM_DEPTH);
    }
    // The stores that may stall (the RTL's register_bus, own_stalling: block 2 word 10, block 4 words 3 and 4): UART_TX
    // while the TX FIFO is full, the sends while the host interface's transmit FIFO is (17.6).
    bool may_stall(int w) const { return w == UART_TX || w == STREAM_SEND || w == STREAM_SEND_LAST; }
    // The static map (peripheral.h): the defined words a store writes; the others are R: a store is an access fault.
    bool writable(int w) const {
        return (w >= ARRIVE_EXPECT_0 && w <= UART_TX) || w == UART_DIVISOR || w == UART_STATUS || (w >= STREAM_OUT_W0 && w <= STREAM_SEND_LAST);
    }

    // ---- v1.37: the DMA's side (17.4; periph_slot.h, THE CONTRACT): output queue q is stream q's packet at the front ----
    int out_queues() const { return host_link ? 2 : 0; }
    bool out_ready(int64_t, int q) const { const LinkStream& s = link_stream[q]; return s.n > 0 && s.read < s.q[s.head].length; }
    void out_start(int64_t t, int q, int64_t words) {
        const LinkStream& s = link_stream[q];
        const int64_t left = s.n > 0 ? int64_t(s.q[s.head].length) - int64_t(s.read) : 0;
        if (words > left)
            FATAL("Ingress/Egress: undefined behavior at %lld: a DMA transfer of %lld words from the %s stream, whose packet has %lld left "
                  "(INGRESS-INTERFACE.md: a count beyond what is left of the packet)", (long long)t, (long long)words, q ? "background" : "foreground", (long long)left);
    }
    void out_take(int64_t t, int q, uint64_t w[2]) {
        LinkStream& s = link_stream[q];
        if (!out_ready(t, q)) FATAL("Ingress/Egress: out_take at %lld with no word of a packet on stream %d", (long long)t, q);
        const hostlink::Packet& p = s.q[s.head];
        w[0] = p.lo[s.read]; w[1] = p.hi[s.read];
        if (++s.read == p.length) {                          // read out in full: the next packet, if any, asks for service
            s.head = (s.head + 1) % LINK_QUEUE; s.n--; s.read = 0;
            if (s.n > 0) ev.post(t, EV_FOREGROUND_SERVICE + unsigned(q));
        }
    }

    // The outputs' levels in the next cycle to step: every one a flip-flop (18).
    Pins::Out out() const {
        Pins::Out o;
        o.uart_tx = tx_line; o.toast = toast; o.led = led != 0;
        o.lvds_in_tready = in_ready_ff && in_n < STREAM_DEPTH;
        o.lvds_out_tvalid = out_n > 0;
        if (out_n > 0) { for (unsigned k = 0; k < 4; k++) o.lvds_out_tdata[k] = out_q[out_head].w[k]; o.lvds_out_tlast = out_q[out_head].last; }
        return o;
    }

    // ---- the edge that ends t (without the core's access of t: write_word(t, ...) and load_effect(t, ...) follow) ----
    void step(int64_t t) {
        const Pins::Out o = out();
        Pins::In in;
        if (boundary) boundary(far_end, t, o, in);
        stepped_at = t; rx_n_start = rx_n; in_n_start = in_n;
        if (wd_count > 0 && --wd_count == 0) {                 // the expiry, unless a store or a RELEASE of t sets the count
            wd_expired_at = t; wd_posted = ev.count_of(EV_WATCHDOG) == 0; wd_toasted = !toast;
            ev.post(t, EV_WATCHDOG);
            if (!toast) { toast = true; toast_at = t; }
        }
        step_transmitter();
        step_receiver(t, in.uart_rx);
        // the host interface: the head out leaves at a handshake; a beat in enters at one
        if (o.lvds_out_tvalid && in.lvds_out_tready) { out_head = (out_head + 1) % STREAM_DEPTH; out_n--; }
        if (in.lvds_in_tvalid && o.lvds_in_tready) {
            Beat& b = in_q[(in_head + in_n++) % STREAM_DEPTH];
            const unsigned top = top_word();
            for (unsigned k = 0; k < 4; k++) b.w[k] = k < top ? in.lvds_in_tdata[k] : k == top ? in.lvds_in_tdata[k] & top_mask() : 0;
            b.last = in.lvds_in_tlast;
        }
        in_ready_ff = true;
        if (host_link) link_step(t);
    }

    // ---- wake ----
    bool idle(int64_t) const {
        return wd_count == 0 && tx_n == 0 && !tx_busy && !rx_busy && rx_meta && rx_line && rx_line_last && in_ready_ff && !boundary;
    }
    // v1.37: the cycle the link's next packet arrives in (the slot sleeps until then when nothing else is in progress)
    int64_t next_wake_at(int64_t) const { return host_link && link_has_next && link_scheduled ? link_next_at : INT64_MAX; }
    uint64_t violation_count() const { return 0; }
    void digest_words(int64_t* out) const {
        out[0] = int64_t(sim::hash_fields(err_line, arrived_at, cleared_at, expect, wd_count, wd_limit, toast, toast_at, led));
        out[1] = int64_t(sim::hash_fields(tx, tx_head, tx_n, divisor, tx_busy, tx_line, tx_cycles_left, tx_bit_divisor, tx_bits));
        out[2] = int64_t(sim::hash_fields(ev.count, ev.live_bits, ev.last_post_at));
        out[3] = int64_t(sim::hash_fields(rx, rx_head, rx_n, rx_meta, rx_line, rx_line_last, rx_busy, rx_bit, rx_data, rx_cycles_left,
                                          rx_bit_divisor, rx_overrun, framing));
        out[4] = int64_t(sim::hash_fields(out_head, out_n, in_head, in_n, out_low, in_ready_ff));
        if (host_link)
            out[4] ^= int64_t(sim::hash_fields(link.at, link.expect, link_stream[0].head, link_stream[0].n, link_stream[0].read, link_stream[1].head,
                                               link_stream[1].n, link_stream[1].read, link_errors, link_dead, link_has_next, link_scheduled, link_next_at, link_at));
    }
    void reset(int64_t) {
        if (stream_bytes < 1 || stream_bytes > 16) FATAL("Ingress/Egress: STREAM_BYTES must be 1 to 16 (19): %u", stream_bytes);
        if (reset_divisor < 1 || reset_divisor > 65535) FATAL("Ingress/Egress: UART_RESET_DIVISOR must be 1 to 65535 (19): %u", reset_divisor);
        std::memset(err_line, 0, sizeof err_line);
        for (auto& r : arrived_at) r = INT64_MIN;
        cleared_at = INT64_MIN; expect = 0; wd_count = wd_limit = 0; toast = false; toast_at = -1; led = 0;
        wd_expired_at = INT64_MIN; wd_posted = wd_toasted = false;
        release_due = false;
        divisor = reset_divisor;
        tx_head = tx_n = 0; tx_busy = false; tx_line = true; tx_cycles_left = tx_bit_divisor = tx_bits = 0;
        rx_meta = rx_line = rx_line_last = true; rx_busy = false; rx_bit = rx_data = 0; rx_cycles_left = 0; rx_bit_divisor = 0;
        rx_head = rx_n = 0; rx_overrun = framing = false; overrun_at = framing_at = INT64_MIN;
        out_head = out_n = in_head = in_n = 0; out_low[0] = out_low[1] = out_low[2] = 0; in_ready_ff = false;
        stepped_at = INT64_MIN; rx_n_start = in_n_start = 0;
        ev.clear_all();
        ev.live_bits = 0;
        ev.live(EV_UART_TX, true); ev.live(EV_STREAM_OUT, true);   // room in the TX FIFO and in the transmit FIFO
        link_reset();
    }
    // v1.37: the link at the reset: both streams empty, no error, the file from its start.
    void link_reset() {
        for (LinkStream& s : link_stream) { s.head = s.n = s.read = 0; }
        link_errors = 0; link_dead = link_cut = false; link_has_next = link_scheduled = link_waited = false;
        link_at = 0; link_packets = link_words = link_bytes = 0;
        link.rewind();
    }

    // ARRIVALS as a load presented in t reads it (17.6)
    bool arrived(unsigned k, int64_t t) const { return arrived_at[k] != INT64_MIN && arrived_at[k] >= cleared_at && arrived_at[k] <= t; }
    uint64_t arrivals(int64_t t) const { uint64_t b = 0; for (unsigned k = 0; k < 64; k++) if (arrived(k, t)) b |= 1ull << k; return b; }
    bool all_arrived(int64_t t) const { return expect != 0 && (arrivals(t) & expect) == expect; }

private:
    // v1.37: the link's edge that ends t (header, THE HOST LINK): the packets whose last byte has arrived by t, each
    // checked and put at its stream; then the file's next packet taken, if its stream has room, and its arrival set.
    void link_step(int64_t t) {
        for (;;) {
            if (link_dead) return;
            if (!link_has_next) {
                switch (link.next(link_next, link_next_check, link_next_bytes)) {
                    case hostlink::Reader::Next::END: link_dead = true; return;
                    case hostlink::Reader::Next::CUT: link_dead = link_cut = true; return;
                    case hostlink::Reader::Next::PACKET: break;
                }
                link_has_next = true; link_scheduled = false;
            }
            if (!link_scheduled) {
                // a packet that passed its checks waits for room at its stream; one that failed needs none
                if (link_next_check == hostlink::Check::OK && link_stream[link_next.stream - 1].n == LINK_QUEUE) { link_waited = true; return; }
                // its bytes follow the last packet's, or, after a wait for room, start in the next cycle
                const int64_t from = link_waited ? t : link_at;
                link_next_at = link_cycles_per_byte == 0 ? t : from + int64_t(link_next_bytes) * link_cycles_per_byte;
                link_scheduled = true; link_waited = false;
            }
            if (link_next_at > t) return;
            link_at = link_next_at; link_bytes += link_next_bytes;
            link_has_next = false;
            switch (link_next_check) {
                case hostlink::Check::CRC: link_errors |= ERR_CRC; link_dead = true; return;
                case hostlink::Check::SEQUENCE: link_errors |= ERR_SEQUENCE; link_dead = true; return;
                case hostlink::Check::FORMAT: link_errors |= ERR_FORMAT; link_dead = true; return;
                case hostlink::Check::OK: break;
            }
            link_packets++; link_words += link_next.length;
            if (link_next.length == 0) continue;                     // no data: no service
            LinkStream& s = link_stream[link_next.stream - 1];
            s.q[(s.head + s.n++) % LINK_QUEUE] = link_next;
            if (s.n == 1) ev.post(t, EV_FOREGROUND_SERVICE + (link_next.stream - 1));   // at the front: it asks for service
        }
    }
    // The transmitter's edge (uart.veryl, "The TX FIFO and the transmitter"), from the registers at the cycle's start.
    void step_transmitter() {
        const bool bit_ends = tx_cycles_left == 1;
        const bool frame_ends = tx_busy && tx_bits == 0 && bit_ends;   // the stop bit, the last sent, in its last cycle
        const bool pop = (!tx_busy || frame_ends) && tx_n > 0;         // an idle point, the TX FIFO not empty
        if (pop) {
            const uint8_t b = tx[tx_head]; tx_head = (tx_head + 1) % FIFO; tx_n--;
            if (serial_n < kSerial) serial_out[serial_n] = char(b);
            serial_n++;
            tx_busy = true; tx_line = false;                           // the start bit
            tx_cycles_left = tx_bit_divisor = uint16_t(divisor);
            tx_bits = uint16_t(0x100 | b);                             // the data bits, then the stop bit
        } else if (frame_ends) {
            tx_busy = false; tx_line = true;
        } else if (tx_busy && bit_ends) {
            tx_line = tx_bits & 1;                                     // the next bit: data bits 1-8, then the stop bit
            tx_cycles_left = tx_bit_divisor; tx_bits >>= 1;
        } else {
            tx_cycles_left--;                                          // 16 bits: a divisor of 0 wraps (header)
        }
    }
    // The receiver's edge (uart.veryl, "The receiver and the RX FIFO"), with the pin's level in t.
    void step_receiver(int64_t t, bool pin) {
        const bool frame_starts = !rx_busy && rx_line_last && !rx_line;
        const bool sample = rx_busy && rx_cycles_left == 1;
        const bool frame_ends = sample && rx_bit == 9;
        if (frame_ends && !rx_line) { framing = true; framing_at = t; }          // a stop bit of 0: dropped
        else if (frame_ends && rx_n >= FIFO) { rx_overrun = true; overrun_at = t; }   // full at the cycle's start: dropped
        else if (frame_ends) rx[(rx_head + rx_n++) % FIFO] = rx_data;
        if (frame_starts) {
            rx_busy = true; rx_bit = 1;
            rx_cycles_left = divisor + (divisor >> 1);                 // the first data bit's middle
            rx_bit_divisor = uint16_t(divisor);
        } else if (sample) {
            if (rx_bit != 9) rx_data = uint8_t((rx_line ? 0x80 : 0) | rx_data >> 1);   // the latest at bit 7
            else rx_busy = false;
            rx_bit++; rx_cycles_left = rx_bit_divisor;
        } else {
            rx_cycles_left = (rx_cycles_left - 1) & 0x1FFFF;           // 17 bits
        }
        rx_line_last = rx_line; rx_line = rx_meta; rx_meta = pin;      // the synchroniser
    }
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
        ev.live(EV_UART_RX, rx_n > 0);
        ev.live(EV_UART_TX, tx_n < FIFO);
        ev.live(EV_STREAM_IN, in_n > 0);
        ev.live(EV_STREAM_OUT, out_n < STREAM_DEPTH);
    }
};

}  // namespace units
