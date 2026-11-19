// ingress_egress.h: Ingress/Egress, the master tile's peripheral 1, type 1, version 1 (machine-spec 17.6; v1.37 stage 3,
// the INTEGRATOR). It is a peripheral of the slot (units/peripheral.h's method set); the control block and the DMA block
// (17.1-17.4) are the slot's.
//
// INGRESS/EGRESS holds the chip's control lines and its external IO: every other tile's error line, the barrier's master
// side, the watchdog and the toast pin, LED, the UART, and the host link with what the core sees of it: Ingress, the
// host's words in two incoming queues, read by its DMA or forwarded to the Updater; and Egress, the chip's replies in one
// outgoing queue. A START is UB (17.6). The control lines are v1.36's (the RTL's ingress_egress_unit.veryl, register for
// register, cycle for cycle); the UART is redesigned (17.6.9, RCR 2) on v1.36's line, characters and FIFOs; v1.36's host
// interface, the stream words, is gone (RCR C).
//
// ITS WORDS (17.6.1). In block 1, beside the DMA's: 30 UART_TX (W), 31 UART_RX (R, pops). Its own: 32-34 ERROR_LINES,
// 35-36 ARRIVALS, 37-38 ARRIVE_EXPECT, 39 RELEASE, 40 WATCHDOG, 41 LED, 42 UART_CONTROL, 64 FOREGROUND_WORDS, 65
// BACKGROUND_WORDS, 66 DMA_FORWARD_COUNT, 67 EGRESS_COUNT. Every other word unmapped. STATUS [2] foreground_ready, [3]
// background_ready, [12:8] uart_tx_bytes, [20:16] uart_rx_bytes, [24] rx_overrun, [25] framing_error. ERRORS [0]
// tile_error (live), [1] link_error, [2] format_error, [3] egress_error (sticky). EVENTS [2] all_arrived (live), [3]
// watchdog, [4] uart_rx (live), [5] uart_tx (live), [6] foreground_service, [7] background_service (counted, 1), [8]
// egress_credit (counted, 2). PARAMS type 1, version 1.
//
// TIMING (the slot's rule, periph_slot.h): a store the core issues in t is presented in p = t + 1 and takes effect at
// the end of p (6.4): the slot calls write_word(p, ...) at level 0 of p + 1, before the reads of p + 1. A load presented
// in p reads the state at the end of p - 1, and one that pops (UART_RX) pops at the end of p: load_effect(p, w). step(u)
// is the edge that ends u, without the core's access of u, which the slot applies after it.
//   - RELEASE: its effect at the end of p clears ARRIVALS but an arrival that reached Ingress/Egress in p, sets the
//     watchdog's count to its limit, and BARRIER_RELEASE's flip-flop is 1 in p + 1: put in end(p + 1) on the 64
//     `release` links, whose latency is RELEASE_LATENCY.
//   - ARRIVALS: bit i is set at the end of the cycle in which tile i's BARRIER_ARRIVE reaches Ingress/Egress.
//   - WATCHDOG: a store of N presented in p sets the limit and the count at the end of p; the count decreases at the end
//     of every later cycle and expires at the end of p + N: EVENTS.watchdog posted, the toast pin set. A store or a
//     RELEASE presented in the cycle the watchdog would expire in wins (unexpire).
//   - The toast pin: set at the end of a cycle in which the master tile's own error line is 1, or the watchdog expires.
//
// THE UART (17.6.9). UART_CONTROL [15:0] divider, the cycles a bit; [16] enable_read; [17] enable_write. 0, its value at
// the reset, is the UART's reset: both FIFOs empty, the line errors 0, the line out idle at 1, the line in ignored, the
// events uart_rx and uart_tx 0, a store to UART_TX dropped, UART_RX 0. A non-zero UART_CONTROL with a divider of 0 is UB.
// Characters: a start bit, 0; 8 data bits, the least significant first; a stop bit, 1; the line 1 while idle; each bit D
// cycles, D the divider a character takes at its start (so a new divider takes effect at each direction's next idle
// point). The line, the characters and the FIFOs are v1.36's uart.veryl's:
//   - The TX FIFO (UART_FIFO_DEPTH = 16): a UART_TX store presented in p, with the UART running and enable_write 1 and
//     the FIFO not full at the start of p, pushes its character at the end of p; with it full the store stalls (the
//     reg interface's ready). With the UART in reset or enable_write 0 the store is dropped and never stalls.
//   - The transmitter: a character starts in a cycle at an idle point (no character, or the last cycle of a stop bit)
//     with the TX FIFO not empty and enable_write 1: the head is popped at the end of that cycle, and the start bit is on
//     uart_tx from the next (t + 3 from a store into an empty FIFO: 21.5). uart_tx is a flip-flop.
//   - The receiver: two flip-flops synchronise uart_rx and a third holds the line's last value, so a level on the pin in
//     cycle c is the line the receiver reads in c + 2. With enable_read 1, a character starts in a cycle e in which the
//     receiver is idle and the line is 0 after 1; bit k (1-8 data, 9 the stop bit) is sampled in e + k D + floor(D / 2).
//     At the stop bit's sample the character ends: a stop bit of 0 drops it and sets framing_error; else the RX FIFO full
//     at the start of that cycle drops it and sets rx_overrun; else it enters the RX FIFO at the end of that cycle. With
//     enable_read 0 no character starts (one under way finishes) and what is in the RX FIFO stays.
//   - UART_RX reads [7:0] the RX FIFO's head and [8] valid, 0 while it is empty; a load pops the head at the end of its
//     cycle if the FIFO was not empty at its start. The line errors are status and only the UART's reset clears them.
//
// THE INCOMING QUEUES (17.6.3 to 17.6.7). Two queues, foreground (0) and background (1), INGRESS_QUEUE_WORDS each
// (`queue_words`, 1,024 by default; a parameter: MachConfig::ingress_queue_words). A link packet taken goes to its
// stream's queue; its words land a 128-bit word a cycle from the packet buffer, the first at the end of the cycle the
// link packet is taken, each counting in ..._WORDS as it lands (`held`); the room is of the words landed and still to
// land (`held` + `landing`), so a link packet is taken only with room for all its words. Two ways out, from the words
// landed: the DMA (block 1, 17.4; DMA_HDR.queue names the queue), and the forward count, from the background queue to the
// Updater's word port (word_port.h) or dropped:
//   - THE FORWARD COUNT, a cycle t, from the registers at its start: with count not 0 and the background queue holding a
//     word, the head is dropped at the end of t (discard 1), or offered on the port (discard 0) and taken at the end of t
//     if the sink's ready was 1 in t. Either way count - 1. A word that lands at the end of t is the queue's from t + 1.
//   - THE SERVICE FLAGS (17.6.7): a queue asks while it holds words, no DMA transfer from it is in flight, and (the
//     background) count is 0; a transfer is in flight from DMA_START's effect to dma_done (out_start, out_end). The flag
//     is posted at the end of a cycle in which the level rises, evaluated at the end of every cycle (settle, after every
//     step, the port's move and the landings) and after a store of the count or a DMA_START.
//   - UNDEFINED, each stopping the simulator: a DMA transfer from the background queue with count not 0; a count stored
//     while a transfer from the background queue is in flight; a transfer of more words than the queue holds at its start.
//
// EGRESS (17.6.8). The outgoing queue, EGRESS_QUEUE_WORDS = 512, is filled by 128-bit WRITEs into region 0x04 part 1 (the
// rest of the address ignored), landing in the order they arrive. EGRESS_COUNT counts down as each word lands; the word
// that takes it to 0 is marked the frame's last. A word into a full queue, or with EGRESS_COUNT at 0, is dropped and sets
// egress_error. A store to EGRESS_COUNT while it is not 0, and a WRITE of another size or to another part, are UB. Egress
// offers a link packet when the queue holds 32 words of a frame, or the frame's last word: until the link end is joined
// (stage 4) it is offered to `egress_out`, a harness's (null: nothing leaves). egress_credit, counted of depth 2, 2 at the
// reset: a credit comes back each time 256 words have left the queue.
//
// THE PINS (18): uart_tx, uart_rx, toast, led. A test bench plays the serial line's far end through `boundary`, called in
// every cycle t, in step(t), with the outputs' levels in t (Pins::Out) and the input it gives for t (Pins::In). Without
// one the line in is idle (1). With one the peripheral steps in every cycle (idle() is false).
//
// THE NETS, through the peripheral's own ports (peripheral.h, OWN PORTS; wired by the chip, chip.cpp):
//   rows (8)     own WireIns: [7:0] the row's tiles whose BARRIER_ARRIVE reaches Ingress/Egress in this cycle, [15:8] the
//                row's error lines' levels.
//   lines (3)    own LinkIns, width 1: [0] the master tile's own error line (the toast pin), [1] the memory tile's, [2] the
//                tracer tile's (ERROR_LINES_2 [1], [2]).
//   release (64) own LinkOuts, width 0, to every compute tile's tile control unit: BARRIER_RELEASE.
//
// THE LINK (chapter 28; v1.37 stage 4): WORKER8's `hostlink::Node` (link/, used through hostlink.h
// alone): the link end towards the host, on the port `link_up`, and with `chain_next` 1 the one away from it, on
// `link_down`, and the four forwarding buffers. A link cycle is a byte on the line; `link_rate` link cycles a chip
// cycle (1, 2, 4 or 8; 28.1's "about 1 GB/s" is one at the chip's 1 GHz, item 186), or a link cycle every
// `link_every` chip cycles (a slower link, for the tests that vary the timing). The ports' bytes are the pins'
// (Pins::In, Pins::Out: one byte a link cycle each way), so the far end, the host's end or a neighbour, is a test
// bench's or a chain harness's, through `boundary`; the output bytes are flip-flops. The Node is clocked in every cycle
// with a far end attached (and not at all without one: a chip with no host is a chip whose link never starts).
//   - THE NODE'S QUEUES ARE INGRESS'S: a link packet the upper end takes goes into the Node's queue for its stream and
//     is counted there at once (the room's count, and the stop bit's at half full: 17.6.3, 28.10); Ingress counts the
//     same words as landing, a 128-bit word a cycle (the packet buffer's hand-over, 28.13.6), one link packet after
//     another, the first at the end of the cycle the Node took it; the DMA and the forward count take landed words out
//     of the Node, oldest first.
//   - EGRESS's link packets go into the Node's own queue towards the host (`Node::push`), one at a time: the Node's
//     queue is sized to one link packet, so a link packet leaves Egress's outgoing queue, and is counted for the credit,
//     when the link end has started to send the one before (17.6.8). Without a far end they stay in the outgoing queue,
//     or go to `egress_out`, a unit test's.
//   - ERRORS: link_error is either link end's `error`, format_error either's `made_wrong` (28.9, 28.7); nothing calls
//     `clear_error()`, so only the reset clears them (28.13.2). A damaged link packet is no error (28.14).
//   - THE RESET (17.6.14): `Node::power_on()`, a new Node: both link ends say "I have started again".
//   - THE LINK'S MODEL ALLOCATES as it runs (its queues are std::deques): its calls are made with the simulator's
//     allocation guard lifted (LinkAllocs), the one exception to it.
//
// WHAT IS NOT THE RTL'S (OPEN-v1.36.md, (a)): an error line's level reaches ERROR_LINES a cycle later than ERROR_LATENCY
// says (it crosses a Link(1 + ERROR_LATENCY)).
#pragma once

#include <cstdint>
#include <cstring>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include <memory>

#include "base/alloc_guard.h"
#include "hostlink.h"                  // link/: WORKER8's link (namespace ::hostlink)
#include "units/peripheral.h"
#include "units/word_port.h"

namespace units {

struct IngressEgress : PeripheralBase<IngressEgress> {
    // window words (17.6.1)
    enum : int { UART_TX = 30, UART_RX = 31, ERROR_LINES_0 = 32, ARRIVALS_0 = 35, ARRIVE_EXPECT_0 = 37, RELEASE = 39, WATCHDOG = 40,
                 LED = 41, UART_CONTROL = 42, FOREGROUND_WORDS = 64, BACKGROUND_WORDS = 65, DMA_FORWARD_COUNT = 66, EGRESS_COUNT = 67 };
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_ALL_ARRIVED = 2, EV_WATCHDOG = 3, EV_UART_RX = 4, EV_UART_TX = 5,
                      EV_FOREGROUND_SERVICE = 6, EV_BACKGROUND_SERVICE = 7, EV_EGRESS_CREDIT = 8, EV_BITS = 9 };
    enum : int { ERR_TILE = 1 << 0, ERR_LINK = 1 << 1, ERR_FORMAT = 1 << 2, ERR_EGRESS = 1 << 3 };
    enum : uint32_t { ST_FOREGROUND_READY = 1u << 2, ST_BACKGROUND_READY = 1u << 3, ST_RX_OVERRUN = 1u << 24, ST_FRAMING = 1u << 25 };
    enum : uint32_t { UC_DIVIDER = 0xFFFF, UC_ENABLE_READ = 1u << 16, UC_ENABLE_WRITE = 1u << 17, UC_BITS = 0x3FFFF };
    static constexpr int VERSION = 1;
    static constexpr unsigned MAX_QUEUE_WORDS = 4096;        // the most INGRESS_QUEUE_WORDS the model holds
    static constexpr unsigned EGRESS_QUEUE_WORDS = 512, EGRESS_CREDIT_WORDS = 256, LINK_PACKET_WORDS = 32;
    static constexpr uint32_t FWD_COUNT = 0x7FFFFFFFu, FWD_DISCARD = 0x80000000u;   // DMA_FORWARD_COUNT's fields
    static constexpr unsigned PART_EGRESS = 1;               // the region's part the outgoing queue is (17.6.8)
    static constexpr unsigned FIFO = 16, kSerial = 4096;     // UART_FIFO_DEPTH (19)
    static constexpr int DIGEST_N = 5;

    // ---- the pins (header, THE PINS): a cycle's levels ----
    static constexpr unsigned MAX_LINK_RATE = 8;            // link cycles a chip cycle, at most
    struct Pins {
        struct Out {
            bool uart_tx = true, toast = false, led = false;
            uint8_t link_up_tx[MAX_LINK_RATE] = {}, link_down_tx[MAX_LINK_RATE] = {};   // a byte a link cycle (18: host_link)
        };
        struct In {
            bool uart_rx = true;                             // the idle input
            uint8_t link_up_rx[MAX_LINK_RATE] = {}, link_down_rx[MAX_LINK_RATE] = {};   // idle: 0
        };
    };
    using Boundary = void (*)(void* far_end, int64_t t, const Pins::Out& out, Pins::In& in);
    Boundary boundary = nullptr;       // the far end, a test bench's (null: the idle input)
    void* far_end = nullptr;
    // Egress's link packets, until the link end is joined (header, EGRESS): a harness's; true if it takes the packet
    using EgressOut = bool (*)(void* ctx, int64_t t, const uint64_t* lo, const uint64_t* hi, unsigned words, bool frame_ends);
    EgressOut egress_out = nullptr;
    void* egress_ctx = nullptr;

    // ---- the own ports' indices in the slot's storage (peripheral.h, OWN PORTS) ----
    unsigned row0 = 0, line0 = 0, release0 = 0;

    // ---- configuration (set before the run; reset() takes it) ----
    unsigned queue_words = 1024;       // INGRESS_QUEUE_WORDS (17.6.15, 19)
    unsigned link_rate = 1;            // link cycles a chip cycle: 1, 2, 4, 8 (header, THE LINK)
    unsigned link_every = 1;           // or chip cycles a link cycle (a slower link): the link's cycles are the chip's
                                       //   cycles t with t % link_every == 0
    bool chain_next = false;           // the pin chain_next (18): another ASIC follows on the chain; link_down exists
    unsigned link_window = 8;          // the link's window, the chip's (28.13.5)

    // ---- state: the control lines ----
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
    // ---- the UART (17.6.9) ----
    uint32_t uart_control = 0;         // UART_CONTROL [17:0]
    uint8_t tx[FIFO] = {}; unsigned tx_head = 0, tx_n = 0;
    bool tx_busy = false, tx_line = true;           // a character on the line; uart_tx
    uint16_t tx_cycles_left = 0, tx_bit_divisor = 0, tx_bits = 0;   // the bit's cycles from this one on; the character's D; the bits to send
    bool rx_meta = true, rx_line = true, rx_line_last = true;       // the synchroniser, and the line's last value
    bool rx_busy = false;
    uint8_t rx_bit = 0, rx_data = 0;                // the bit sampled next (1-8 data, 9 the stop bit); the data bits sampled
    uint32_t rx_cycles_left = 0;                    // 17 bits: the cycles to its sample, the sample's included
    uint16_t rx_bit_divisor = 0;
    uint8_t rx[FIFO] = {}; unsigned rx_head = 0, rx_n = 0;
    bool rx_overrun = false, framing = false;
    int64_t stepped_at = INT64_MIN;    // the cycle last stepped, and the RX FIFO's count at its start: what a popping load saw
    unsigned rx_n_start = 0;
    // ---- the incoming queues (17.6.3) ----
    struct Queue {
        unsigned held = 0, landing = 0;   // of the Node's words for the stream: landed, and still landing (header, THE LINK)
        bool in_flight = false;        // a DMA transfer from it: DMA_START to dma_done
        bool asking = false;           // it asked for service at the end of the last cycle settled
    } queue[2];                        // 0 the foreground, 1 the background
    struct Landing { uint8_t q; unsigned words; };   // the packet buffer's link packets still landing, oldest first
    static constexpr unsigned MAX_LANDING = 64;
    Landing landing[MAX_LANDING] = {}; unsigned landing_head = 0, landing_n = 0;
    uint32_t forward = 0;              // DMA_FORWARD_COUNT
    // ---- Egress (17.6.8) ----
    uint64_t eg_lo[EGRESS_QUEUE_WORDS] = {}, eg_hi[EGRESS_QUEUE_WORDS] = {};
    bool eg_last[EGRESS_QUEUE_WORDS] = {};
    unsigned eg_head = 0, eg_n = 0;
    uint32_t egress_count = 0;         // EGRESS_COUNT
    unsigned eg_gone = 0;              // the words gone since the last credit came back (17.6.11: Egress's credit)
    int egress_src = -1;               // the source of the frame being queued (invariant 35's check; not hardware)
    int errors_sticky = 0;             // ERRORS [3:1]
    // ---- the link (header, THE LINK) ----
    std::unique_ptr<::hostlink::Node> node;   // made by the reset
    uint8_t up_tx[MAX_LINK_RATE] = {}, down_tx[MAX_LINK_RATE] = {};   // the output flip-flops: the bytes of the next cycle
    uint8_t up_rx[MAX_LINK_RATE] = {}, down_rx[MAX_LINK_RATE] = {};   // this cycle's bytes in, from the far end
    uint64_t link_packets = 0, link_words = 0;   // statistics: the link packets with data the Node took, their words
    uint64_t link_cycles = 0;          // statistics: the link cycles clocked
    int64_t link_last_at = 0;          // statistics: the chip cycle the Node last took a link packet with data
    uint64_t link_forwarded = 0, link_discarded = 0, link_count_stores = 0;   // statistics: the forward count's work
    uint64_t egress_words_in = 0, egress_words_out = 0, egress_packets = 0, egress_dropped = 0;   // statistics: Egress
    // ---- statistics and logs (not state) ----
    uint64_t releases = 0, last_release_at = 0;
    char serial_out[kSerial] = {}; unsigned serial_n = 0;   // the characters the transmitter took, each as it starts

    IngressEgress() {
        static const uint8_t depth[EV_BITS] = {1, 1, 0, 1, 0, 0, 1, 1, 2};
        static const bool flags[EV_BITS] = {};
        ev.init(EV_BITS, depth, flags);
        reset(0);
    }
    // The link's calls, with the simulator's allocation guard lifted (header, THE LINK).
    struct LinkAllocs {
        bool was;
        LinkAllocs() : was(sim::in_run()) { if (was) sim::end_run(); }
        ~LinkAllocs() { if (was) sim::begin_run(); }
    };
    bool link_on() const { return boundary != nullptr; }

    // ---- identity ----
    int type() const { return 1; }
    int params() const { return 1 | VERSION << 8; }          // type 1, version 1, no constants (17.6.1)

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
    // A WRITE into region 0x04 (17.6.8): part 1, 128 bits, a word for the outgoing queue, landing as it arrives.
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
        const unsigned part = offset >> 20 & 7;
        if (size != 2 || part != PART_EGRESS)                    // 2: lw::SIZE_128 (net.h)
            FATAL("Ingress/Egress: undefined behavior at %lld: a WRITE into its region of size %d or to part %u (offset 0x%x, from %d): the "
                  "outgoing queue takes 128-bit WRITEs to part 1 alone (17.6.8)", (long long)t, size, part, offset, src);
        if (eg_n == EGRESS_QUEUE_WORDS || egress_count == 0) { errors_sticky |= ERR_EGRESS; egress_dropped++; return; }
        if (egress_src >= 0 && src != egress_src)
            FATAL("Ingress/Egress: undefined behavior at %lld: a WRITE into the outgoing queue from %d while the frame's words come from %d "
                  "(17.6.13, invariant 35: one source at a time)", (long long)t, src, egress_src);
        egress_src = src;
        const unsigned at = (eg_head + eg_n++) % EGRESS_QUEUE_WORDS;
        eg_lo[at] = lo; eg_hi[at] = hi;
        eg_last[at] = --egress_count == 0;
        if (egress_count == 0) egress_src = -1;                  // the frame's last: the next may come from another
        egress_words_in++;
    }
    void run(int64_t t) { FATAL("Ingress/Egress: undefined behavior at %lld: a START (17.6: it has no work)", (long long)t); }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    bool uart_on() const { return uart_control != 0; }
    // The store presented in t, after step(t): 1 written, 0 read-only (an access fault, the core's), -1 undefined. A store
    // that stalls never arrives: the core holds it while R_STALL is set (store_stalls).
    int write_word(int64_t t, int w, uint32_t v) {
        switch (w) {
        case UART_TX:
            if (!uart_on() || !(uart_control & UC_ENABLE_WRITE)) return 1;   // dropped (17.6.9)
            if (tx_n >= FIFO) FATAL("Ingress/Egress: UART_TX stored at %lld with the TX FIFO full", (long long)t);
            tx[(tx_head + tx_n++) % FIFO] = uint8_t(v); return 1;
        case 37: expect = (expect & ~0xFFFFFFFFull) | v; return 1;
        case 38: expect = (expect & 0xFFFFFFFFull) | uint64_t(v) << 32; return 1;
        case RELEASE: if (v & 1) release(t); return 1;
        case WATCHDOG: unexpire(t); wd_limit = v; wd_count = v; return 1;
        case LED: led = v & 1; return 1;
        case UART_CONTROL:
            v &= UC_BITS;
            if (v != 0 && (v & UC_DIVIDER) == 0)
                FATAL("Ingress/Egress: undefined behavior at %lld: UART_CONTROL 0x%x, not 0 with a divider of 0 (17.6.9)", (long long)t, v);
            uart_control = v;
            if (v == 0) uart_reset();
            return 1;
        case DMA_FORWARD_COUNT:                                   // replaces the count, at the end of t
            if (queue[1].in_flight)
                FATAL("Ingress/Egress: undefined behavior at %lld: DMA_FORWARD_COUNT stored (0x%x) while a DMA transfer from the background "
                      "queue is in flight (17.6.5)", (long long)t, v);
            forward = v; link_count_stores++;
            levels(t);
            return 1;
        case EGRESS_COUNT:
            if (egress_count != 0)
                FATAL("Ingress/Egress: undefined behavior at %lld: EGRESS_COUNT stored (%u) while it is %u, not 0 (17.6.8)", (long long)t, v, egress_count);
            egress_count = v;
            return 1;
        case UART_RX: case 32: case 33: case 34: case 35: case 36: case FOREGROUND_WORDS: case BACKGROUND_WORDS: return 0;
        default: return -1;
        }
    }
    // The load that pops (UART_RX), presented in t, after step(t): the head leaves at the end of t, if the RX FIFO was
    // not empty at the start of t.
    bool load_pops(int w) const { return w == UART_RX; }
    void load_effect(int64_t t, int w) {
        if (w == UART_RX && (stepped_at == t ? rx_n_start : rx_n) > 0) { rx_head = (rx_head + 1) % FIFO; rx_n--; }
    }

    // ---- level reads (the end of t - 1, with the nets as begin(t) took them) ----
    bool running(int64_t) const { return false; }
    int errors(int64_t) const {
        int e = ((err_line[0] | err_line[1]) ? ERR_TILE : 0) | errors_sticky;
        if (node) {
            const ::hostlink::LinkStatus a = node->above(), b = node->below();
            if (a.error || b.error) e |= ERR_LINK;
            if (a.made_wrong || b.made_wrong) e |= ERR_FORMAT;
        }
        return e;
    }
    uint32_t status_bits(int64_t) const {
        uint32_t s = 0;
        if (queue[0].held > 0) s |= ST_FOREGROUND_READY;
        if (queue[1].held > 0) s |= ST_BACKGROUND_READY;
        s |= tx_n << 8 | rx_n << 16;
        if (rx_overrun) s |= ST_RX_OVERRUN;
        if (framing) s |= ST_FRAMING;
        return s;
    }
    EventCounts& events() { return ev; }
    void sync(int64_t t) { set_live(t); }
    bool word(int64_t t, int w, uint32_t& v) {
        switch (w) {
        case UART_TX: v = 0; return true;                        // W: a load reads 0
        case UART_RX: v = rx_n > 0 ? 1u << 8 | rx[rx_head] : 0; return true;
        case 32: v = uint32_t(err_line[0]); return true;
        case 33: v = uint32_t(err_line[0] >> 32); return true;
        case 34: v = uint32_t(err_line[1]) & 6; return true;      // the master tile's own bit and those past 66 read 0
        case 35: v = uint32_t(arrivals(t)); return true;
        case 36: v = uint32_t(arrivals(t) >> 32); return true;
        case 37: v = uint32_t(expect); return true;
        case 38: v = uint32_t(expect >> 32); return true;
        case RELEASE: v = 0; return true;
        case WATCHDOG: v = uint32_t(wd_count); return true;
        case LED: v = led; return true;
        case UART_CONTROL: v = uart_control; return true;
        case FOREGROUND_WORDS: case BACKGROUND_WORDS: v = queue[w - FOREGROUND_WORDS].held; return true;
        case DMA_FORWARD_COUNT: v = forward; return true;
        case EGRESS_COUNT: v = egress_count; return true;
        default: return false;
        }
    }
    // The stores that stall now: UART_TX with the UART running, writing, and the TX FIFO full at the start of the cycle.
    bool store_stalls(int64_t, int w) const { return w == UART_TX && uart_on() && (uart_control & UC_ENABLE_WRITE) && tx_n >= FIFO; }
    bool may_stall(int w) const { return w == UART_TX; }
    // The static map (peripheral.h): the defined words a store writes; the others are R: a store is an access fault.
    bool writable(int w) const {
        return w == UART_TX || (w >= ARRIVE_EXPECT_0 && w <= UART_CONTROL) || w == DMA_FORWARD_COUNT || w == EGRESS_COUNT;
    }

    // ---- the DMA's side (17.4, 17.6.4): output queue q is incoming queue q's landed words, across link packets ----
    int out_queues() const { return 2; }
    bool out_ready(int64_t, int q) const { return queue[q].held > 0; }
    // DMA_START presented in t (the slot calls it at level 0 of t + 1): judged against the words landed by the end of t.
    void out_start(int64_t t, int q, int64_t words) {
        const int64_t held = queue[q].held;
        if (q == 1 && (forward & FWD_COUNT) != 0)
            FATAL("Ingress/Egress: undefined behavior at %lld: a DMA transfer from the background queue started with DMA_FORWARD_COUNT at %u "
                  "(17.6.4)", (long long)t, forward & FWD_COUNT);
        if (words > held)
            FATAL("Ingress/Egress: undefined behavior at %lld: a DMA transfer of %lld words from the %s queue, which holds %lld "
                  "(17.6.4: a count beyond the words in the queue)", (long long)t, (long long)words, q ? "background" : "foreground", (long long)held);
        queue[q].in_flight = true;
        levels(t);
    }
    void out_end(int64_t, int q) { queue[q].in_flight = false; }
    void out_take(int64_t t, int q, uint64_t w[2]) {
        if (!out_ready(t, q)) FATAL("Ingress/Egress: out_take at %lld with no word in queue %d", (long long)t, q);
        pop(q, w);
    }

    // ---- the word port's source (17.6.6; the slot wires it: periph_slot.h, THE WORD PORT), from the registers alone ----
    wordport::SourceOut port_source() const {
        wordport::SourceOut o;
        const Queue& s = queue[1];
        o.valid = (forward & FWD_COUNT) != 0 && !(forward & FWD_DISCARD) && s.held > 0;
        if (o.valid) {                                       // the head, read where it is (hostlink.h: Node::peek)
            ::hostlink::Word x;
            if (!node->peek(::hostlink::BACKGROUND, x)) FATAL("Ingress/Egress: a landed word, and the link's background queue has none");
            std::memcpy(&o.data[0], x.byte, 8); std::memcpy(&o.data[1], x.byte + 8, 8);
        }
        return o;
    }
    void port_sent(int64_t t) {
        if (!port_source().valid) FATAL("Ingress/Egress: port_sent at %lld with no word offered", (long long)t);
        uint64_t w[2];
        pop(1, w);
        forward--; link_forwarded++;
    }
    // The end of t, after every step and the port's move: the landings of t and the link's arrivals of t, which see the
    // room every way out made in t; Egress's offer; then the service levels.
    void settle(int64_t t) {
        land(t);
        if (link_on()) link_clock(t);
        egress_step(t);
        levels(t);
    }
    void levels(int64_t t) {
        for (unsigned q = 0; q < 2; q++) {
            Queue& s = queue[q];
            const bool asks = s.held > 0 && !s.in_flight && (q == 0 || (forward & FWD_COUNT) == 0);
            if (asks && !s.asking) ev.post(t, EV_FOREGROUND_SERVICE + q);
            s.asking = asks;
        }
    }

    // The outputs' levels in the next cycle to step: every one a flip-flop (18).
    Pins::Out out() const {
        Pins::Out o;
        o.uart_tx = tx_line; o.toast = toast; o.led = led != 0;
        std::memcpy(o.link_up_tx, up_tx, sizeof up_tx); std::memcpy(o.link_down_tx, down_tx, sizeof down_tx);
        return o;
    }

    // ---- the edge that ends t (without the core's access of t: write_word(t, ...) and load_effect(t, ...) follow) ----
    void step(int64_t t) {
        const Pins::Out o = out();
        Pins::In in;
        if (boundary) boundary(far_end, t, o, in);
        std::memcpy(up_rx, in.link_up_rx, sizeof up_rx); std::memcpy(down_rx, in.link_down_rx, sizeof down_rx);
        stepped_at = t; rx_n_start = rx_n;
        if (wd_count > 0 && --wd_count == 0) {                 // the expiry, unless a store or a RELEASE of t sets the count
            wd_expired_at = t; wd_posted = ev.count_of(EV_WATCHDOG) == 0; wd_toasted = !toast;
            ev.post(t, EV_WATCHDOG);
            if (!toast) { toast = true; toast_at = t; }
        }
        if (uart_on()) { step_transmitter(); step_receiver(in.uart_rx); }
        discard_step(t);
    }

    // ---- wake ----
    bool idle(int64_t) const {
        return wd_count == 0 && tx_n == 0 && !tx_busy && !rx_busy && rx_meta && rx_line && rx_line_last && !boundary && !discarding() &&
               landing_n == 0 && !egress_offers();
    }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    uint64_t violation_count() const { return 0; }
    void digest_words(int64_t* out) const {
        out[0] = int64_t(sim::hash_fields(err_line, arrived_at, cleared_at, expect, wd_count, wd_limit, toast, toast_at, led));
        out[1] = int64_t(sim::hash_fields(tx, tx_head, tx_n, uart_control, tx_busy, tx_line, tx_cycles_left, tx_bit_divisor, tx_bits));
        out[2] = int64_t(sim::hash_fields(ev.count, ev.live_bits, ev.last_post_at));
        out[3] = int64_t(sim::hash_fields(rx, rx_head, rx_n, rx_meta, rx_line, rx_line_last, rx_busy, rx_bit, rx_data, rx_cycles_left,
                                          rx_bit_divisor, rx_overrun, framing));
        const ::hostlink::LinkCounts ca = node ? node->counts_above() : ::hostlink::LinkCounts(), cb = node ? node->counts_below() : ::hostlink::LinkCounts();
        out[4] = int64_t(sim::hash_fields(queue[0].held, queue[0].landing, queue[0].in_flight, queue[0].asking, queue[1].held, queue[1].landing,
                                          queue[1].in_flight, queue[1].asking, forward, errors_sticky, eg_head, eg_n, egress_count, eg_gone, link_cycles,
                                          ca.good, ca.sent_data, ca.sent_status, ca.restarts, cb.good, cb.sent_data, cb.restarts, up_tx[0], down_tx[0]));
    }
    // The reset (17.6.14): the control lines, the UART in its reset (UART_CONTROL 0), the queues empty, both counts 0,
    // egress_credit at 2, the service levels low, the word port's valid 0, the link from its start.
    void reset(int64_t t) {
        if (queue_words < 64 || queue_words > MAX_QUEUE_WORDS || (queue_words & (queue_words - 1)) != 0)
            FATAL("Ingress/Egress: INGRESS_QUEUE_WORDS must be a power of two from 64 to %u (19): %u", MAX_QUEUE_WORDS, queue_words);
        if (link_rate != 1 && link_rate != 2 && link_rate != 4 && link_rate != 8)
            FATAL("Ingress/Egress: the link's rate must be 1, 2, 4 or 8 link cycles a chip cycle: %u", link_rate);
        if (link_every < 1 || (link_every > 1 && link_rate != 1))
            FATAL("Ingress/Egress: a link cycle every %u chip cycles, at %u link cycles a chip cycle", link_every, link_rate);
        std::memset(err_line, 0, sizeof err_line);
        for (auto& r : arrived_at) r = INT64_MIN;
        cleared_at = INT64_MIN; expect = 0; wd_count = wd_limit = 0; toast = false; toast_at = -1; led = 0;
        wd_expired_at = INT64_MIN; wd_posted = wd_toasted = false;
        release_due = false;
        uart_control = 0;
        uart_reset();
        stepped_at = INT64_MIN; rx_n_start = 0;
        ev.clear_all();
        ev.live_bits = 0;
        ev.set(t, EV_EGRESS_CREDIT, 2);
        eg_head = eg_n = 0; egress_count = 0; eg_gone = 0; egress_src = -1;
        errors_sticky = 0;
        link_reset();
    }
    // The UART's reset (UART_CONTROL 0, 17.6.9): both FIFOs empty, the line errors 0, the line out idle, the receiver idle.
    void uart_reset() {
        tx_head = tx_n = 0; tx_busy = false; tx_line = true; tx_cycles_left = tx_bit_divisor = tx_bits = 0;
        rx_meta = rx_line = rx_line_last = true; rx_busy = false; rx_bit = rx_data = 0; rx_cycles_left = 0; rx_bit_divisor = 0;
        rx_head = rx_n = 0; rx_overrun = framing = false;
    }
    // The link at the reset (17.6.14, 28.14): both queues empty, the Node as at power-on (a new one, at the
    // configuration: the reset is never inside a run), the output lines idle.
    void link_reset() {
        for (Queue& q : queue) { q.held = q.landing = 0; q.in_flight = q.asking = false; }
        landing_head = landing_n = 0;
        forward = 0;
        ::hostlink::Sizes z;
        z.window = link_window;
        z.queue_words = queue_words;                     // Ingress's queues (17.6.3)
        z.send_words = LINK_PACKET_WORDS;                // one link packet from Egress at a time (header, THE LINK)
        node = std::make_unique<::hostlink::Node>(chain_next, z);
        std::memset(up_tx, 0, sizeof up_tx); std::memset(down_tx, 0, sizeof down_tx);
        std::memset(up_rx, 0, sizeof up_rx); std::memset(down_rx, 0, sizeof down_rx);
        link_packets = link_words = link_cycles = 0; link_last_at = 0;
        link_forwarded = link_discarded = link_count_stores = 0;
    }

    // ARRIVALS as a load presented in t reads it (17.6.2)
    bool arrived(unsigned k, int64_t t) const { return arrived_at[k] != INT64_MIN && arrived_at[k] >= cleared_at && arrived_at[k] <= t; }
    uint64_t arrivals(int64_t t) const { uint64_t b = 0; for (unsigned k = 0; k < 64; k++) if (arrived(k, t)) b |= 1ull << k; return b; }
    bool all_arrived(int64_t t) const { return expect != 0 && (arrivals(t) & expect) == expect; }
    unsigned room(unsigned q) const { return queue_words - queue[q].held - queue[q].landing; }   // of the words landed and to land
    // the Node's words of stream q: Ingress's landed and landing words, as the Node counts them
    unsigned node_words(unsigned q) const { LinkAllocs a; return node->waiting(::hostlink::Stream(q)); }

private:
    void pop(int q, uint64_t w[2]) {
        Queue& s = queue[q];
        ::hostlink::Word x;
        bool ok;
        { LinkAllocs a; ok = node->take(::hostlink::Stream(q), x); }
        if (!ok || s.held == 0) FATAL("Ingress/Egress: queue %d: a landed word to take, and the link's queue has none", q);
        std::memcpy(&w[0], x.byte, 8); std::memcpy(&w[1], x.byte + 8, 8);   // its least significant byte first (28.1)
        s.held--;
    }
    bool discarding() const { return (forward & FWD_COUNT) != 0 && (forward & FWD_DISCARD) && queue[1].held > 0; }
    // The forward count dropping a word in t: only a word landed by the start of t.
    void discard_step(int64_t) {
        if (!discarding()) return;
        uint64_t w[2];
        pop(1, w);
        forward--; link_discarded++;
    }
    // The packet buffer's landing in t (17.6.3, 28.13.6): a word of the oldest link packet still landing.
    void land(int64_t) {
        if (landing_n == 0) return;
        Landing& l = landing[landing_head];
        Queue& s = queue[l.q];
        s.held++; s.landing--;
        if (--l.words == 0) { landing_head = (landing_head + 1) % MAX_LANDING; landing_n--; }
    }
    // The link's cycles of chip cycle t (header, THE LINK): the Node clocked `link_rate` times, the bytes of t in and the
    // next cycle's out; a link packet it took with data lands from this cycle, its first word at once if none is landing.
    void link_clock(int64_t t) {
        if (link_every > 1 && t % int64_t(link_every) != 0) return;   // no link cycle in this chip cycle: the bytes held
        LinkAllocs a;
        for (unsigned k = 0; k < link_rate; k++) {
            const ::hostlink::Node::Bytes b = node->clock(up_rx[k], chain_next ? down_rx[k] : 0);
            up_tx[k] = b.up; down_tx[k] = chain_next ? b.down : 0;
            link_cycles++;
            for (unsigned q = 0; q < 2; q++) {
                Queue& s = queue[q];
                const unsigned w = node->waiting(::hostlink::Stream(q)), known = s.held + s.landing;
                if (w < known) FATAL("Ingress/Egress: the link's queue %u holds %u words, Ingress counts %u", q, w, known);
                if (w == known) continue;
                if (landing_n == MAX_LANDING) FATAL("Ingress/Egress: more than %u link packets landing at once", MAX_LANDING);
                s.landing += w - known;
                landing[(landing_head + landing_n++) % MAX_LANDING] = {uint8_t(q), w - known};
                link_packets++; link_words += w - known; link_last_at = t;
                if (landing_n == 1) land(t);                     // the first word lands in the cycle it is taken
            }
        }
    }
    // Egress's offer at the end of t (17.6.8): a link packet when the queue holds 32 words of a frame or the frame's last.
    unsigned egress_packet() const {
        const unsigned n = eg_n < LINK_PACKET_WORDS ? eg_n : LINK_PACKET_WORDS;
        for (unsigned k = 0; k < n; k++) if (eg_last[(eg_head + k) % EGRESS_QUEUE_WORDS]) return k + 1;
        return eg_n >= LINK_PACKET_WORDS ? LINK_PACKET_WORDS : 0;
    }
    bool egress_offers() const { return (egress_out || link_on()) && egress_packet() > 0; }
    void egress_step(int64_t t) {
        const unsigned n = egress_packet();
        if (n == 0 || (!egress_out && !link_on())) return;
        uint64_t lo[LINK_PACKET_WORDS], hi[LINK_PACKET_WORDS];
        for (unsigned k = 0; k < n; k++) { const unsigned at = (eg_head + k) % EGRESS_QUEUE_WORDS; lo[k] = eg_lo[at]; hi[k] = eg_hi[at]; }
        const bool ends = eg_last[(eg_head + n - 1) % EGRESS_QUEUE_WORDS];
        if (egress_out) {
            if (!egress_out(egress_ctx, t, lo, hi, n, ends)) return;
        } else {                                                 // into the Node, when its one link packet's room is free
            LinkAllocs a;
            if (node->room() < n) return;
            ::hostlink::Word w[LINK_PACKET_WORDS];
            for (unsigned k = 0; k < n; k++) { std::memcpy(w[k].byte, &lo[k], 8); std::memcpy(w[k].byte + 8, &hi[k], 8); }
            if (!node->push(w, n)) FATAL("Ingress/Egress: the link took no link packet of %u words with room for %u", n, node->room());
        }
        eg_head = (eg_head + n) % EGRESS_QUEUE_WORDS; eg_n -= n;
        egress_words_out += n; egress_packets++;
        for (eg_gone += n; eg_gone >= EGRESS_CREDIT_WORDS; eg_gone -= EGRESS_CREDIT_WORDS) ev.post(t, EV_EGRESS_CREDIT);
    }
    // The transmitter's edge (uart.veryl), from the registers at the cycle's start; enable_write gates a new character.
    void step_transmitter() {
        const bool bit_ends = tx_cycles_left == 1;
        const bool frame_ends = tx_busy && tx_bits == 0 && bit_ends;   // the stop bit, the last sent, in its last cycle
        const bool pop = (!tx_busy || frame_ends) && tx_n > 0 && (uart_control & UC_ENABLE_WRITE);
        if (pop) {
            const uint8_t b = tx[tx_head]; tx_head = (tx_head + 1) % FIFO; tx_n--;
            if (serial_n < kSerial) serial_out[serial_n] = char(b);
            serial_n++;
            tx_busy = true; tx_line = false;                           // the start bit
            tx_cycles_left = tx_bit_divisor = uint16_t(uart_control & UC_DIVIDER);
            tx_bits = uint16_t(0x100 | b);                             // the data bits, then the stop bit
        } else if (frame_ends) {
            tx_busy = false; tx_line = true;
        } else if (tx_busy && bit_ends) {
            tx_line = tx_bits & 1;
            tx_cycles_left = tx_bit_divisor; tx_bits >>= 1;
        } else if (tx_busy) {
            tx_cycles_left--;
        }
    }
    // The receiver's edge (uart.veryl), with the pin's level in t; enable_read gates a new character.
    void step_receiver(bool pin) {
        const unsigned divider = uart_control & UC_DIVIDER;
        const bool frame_starts = !rx_busy && rx_line_last && !rx_line && (uart_control & UC_ENABLE_READ);
        const bool sample = rx_busy && rx_cycles_left == 1;
        const bool frame_ends = sample && rx_bit == 9;
        if (frame_ends && !rx_line) framing = true;                    // a stop bit of 0: dropped
        else if (frame_ends && rx_n >= FIFO) rx_overrun = true;        // full at the cycle's start: dropped
        else if (frame_ends) rx[(rx_head + rx_n++) % FIFO] = rx_data;
        if (frame_starts) {
            rx_busy = true; rx_bit = 1;
            rx_cycles_left = divider + (divider >> 1);                 // the first data bit's middle
            rx_bit_divisor = uint16_t(divider);
        } else if (sample) {
            if (rx_bit != 9) rx_data = uint8_t((rx_line ? 0x80 : 0) | rx_data >> 1);   // the latest at bit 7
            else rx_busy = false;
            rx_bit++; rx_cycles_left = rx_bit_divisor;
        } else if (rx_busy) {
            rx_cycles_left = (rx_cycles_left - 1) & 0x1FFFF;           // 17 bits
        }
        rx_line_last = rx_line; rx_line = rx_meta; rx_meta = pin;      // the synchroniser
    }
    // RELEASE's effect at the end of p (17.6.2).
    void release(int64_t p) {
        unexpire(p);
        cleared_at = p + 1; releases++; last_release_at = uint64_t(p); release_due = true;
        wd_count = wd_limit;
    }
    void unexpire(int64_t p) {
        if (wd_expired_at != p) return;
        if (wd_posted) ev.clear(EV_WATCHDOG);
        if (wd_toasted) { toast = false; toast_at = -1; }
        wd_expired_at = INT64_MIN;
    }
    void set_live(int64_t t) {
        ev.live(EV_ALL_ARRIVED, all_arrived(t));
        ev.live(EV_UART_RX, uart_on() && rx_n > 0);
        ev.live(EV_UART_TX, uart_on() && tx_n < FIFO);
    }
};

}  // namespace units
