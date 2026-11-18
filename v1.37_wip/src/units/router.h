// router.h: the router (machine-spec 16; the RTL's rtl/blocks/router: router.veryl, router_output.veryl), every tile's
// node of the NoC, a compute tile's and a special tile's. At the packet and the cycle it is the RTL's, which
// tests/test_router.cpp checks against the RTL's own router under scripted load (tests/routerconf/README.md).
//
// THE HARDWARE'S CYCLES (router.veryl's header). A route block and its queue's write take a cycle: a packet at an
// input in cycle t is at its queue's head from t + 1, is taken by its output's arbiter in t + 1 when no other
// queue is served first, and is in the output's register, the first of its link's three stages, in t + 2. A queue
// is full by its count at the start of a cycle: a push then is lost, even if the head leaves in that cycle, and
// `overflow` is 1 in the push's cycle (ERRORS.router_overflow is set at its end: 7.4). Each output's arbiter is
// round-robin over its non-empty queues in their inputs' order, local, west, east, north, south, the attach port, its
// pointer 0 after the reset; the eject's does not see the loopback queue, which is served only in a cycle when every
// other eject queue is empty, the pointer staying (16.3).
//
// THE MODEL'S STEP at t: the arbiters, each taking one packet from the queues as they stood at the start of t and
// putting it on its output (a Link whose first cycle is the output register's: a put of t is in the register in
// t + 1); then the route blocks, pushing the packets at the inputs in t (delivered at t), which are at their queues'
// heads from t + 1. The overflows of t's pushes are counted, and `ovf_ev` put, in t.
//
// PORTS (link words are three words, net.h). The numbering is ref's (Router.java), kept: inputs FROM_LEFT (west) 0,
// FROM_RIGHT (east) 1, FROM_UP (north) 2, FROM_DOWN (south) 3, LOOPBACK (local) 4, FROM_CNC (the attach port) 5;
// outputs TO_RIGHT (east) 0, TO_LEFT (west) 1, TO_DOWN (south) 2, TO_UP (north) 3, EJECT 4, TO_CNC (the attach port) 5.
//   in   in[4]       LinkIn. A compute tile's: the mesh links from the neighbours (the chip gives them link_latency).
//                    A special tile's: its peripherals' DMAs (16.4), those of `dma_inputs` (bit 0 west, 1 east,
//                    2 north, 3 south, as the RTL's DMA_INPUTS): a `packet` interface, below.
//        inj         LinkIn, the injection point's one packet a cycle (latency intra_tile_latency); src stamped here.
//        from_cc     LinkIn, the attach port (an attach tile's, 16.1; a special tile's own).
//        from_cnc    HoldIn, FIFTH_HOLD only: a test bench's endpoint at an attach tile's attach port.
//   out  out[4]      LinkOut, a compute tile's mesh links.
//        eject       LinkOut to the traffic director (latency intra_tile_latency); the whole header kept.
//        loop_left   LinkOut, width 0, latency 1: a token-holding packet left the loopback queue at t (the RTL's
//                    `eject_loopback` mark, 16.3), its token is free from t + 1.
//        to_cc       LinkOut, the attach port.
//        to_cnc      FifoOut, FIFTH_HOLD only: the bench's receive FIFO.
//        ovf         CountOut, every packet lost to a full queue, counted in its push's cycle.
//        ovf_ev      LinkOut, width 0, latency 1, to the tile control unit, beside the Count: put in a cycle that lost a
//                    packet, so that ERRORS.router_overflow is set at that cycle's end (tile_ctl.h). Declared when asked.
//        dma_room    WireOut, a special tile's with DMA inputs: for DMA input d, bit 2d the eject's queue from it
//                    has room, bit 2d + 1 the attach output's, by their counts at the start of the cycle.
//
// A DMA INPUT (16.4; router.veryl: `*_in_ready`). The DMA's packet register offers its packet with `valid`, and the
// router's `ready` is 1 while the queue the packet goes to has room (the eject's for the tile's own id, the attach
// output's for any other; a packet for a reserved id, which goes nowhere, is always taken). The packet moves in a
// cycle with both, into the queue at that cycle's end. Here the DMA reads `dma_room` (a Wire: this cycle's), judges
// `ready` for its packet by dma_ready() and, in a cycle its packet moves, puts it on the input's Link(1); the router
// writes it into its queue as the first thing of the next step, before the arbiters, which is the queue's state after
// that edge. A packet put without room is fatal.
//
// STATE. One queue per (output, input a route can bring to it), ref's `FEEDERS_TILE0` on every router: more than
// the RTL builds on a tile with no attach port or on a special tile (router.veryl, `feeders_of`), where no route
// pushes the others. The round-robin pointers `rr[6]`, each the queue last served (the RTL's pointer less one).
//
// A special tile's router (`cc`: tiles 64-66) has the eject, with queues from local (the loopback queue), its DMA
// inputs and its attach port, and its attach output, with queues from local and its DMA inputs: from local or a DMA,
// a packet for the tile ejects and any other, a broadcast included, leaves by the attach port; from the attach port, a
// packet for the tile ejects and any other goes nowhere (router.veryl, `route`).
// FIFTH_HOLD (an endpoint's Hold and Fifo at the attach port) is a test bench's (tests/bench_master.h).
//
// THE LINE (v1.37, 16.1 to 16.4; the INTEGRATOR's stage 2; `line`): the special tiles joined east and west, the master
// tile's east to the memory tile's west and the memory tile's east to the tracer tile's west, each a link of three
// stages with no flow control. A special tile's router with the line has in[FROM_LEFT] and out[TO_LEFT] towards the
// tile id - 1 and in[FROM_RIGHT] and out[TO_RIGHT] towards id + 1 (where they exist: the master tile has no west, the
// tracer tile no east), and its peripherals' DMA on the south input alone (in[FROM_DOWN], dma_inputs 8). Its routes
// (16.2): a packet for the tile ejects; a packet for another special tile, from local or the DMA, takes the line by the
// ring's rule (east if its dst is above the tile's id, west if below), and one arriving on the line goes on the way it
// came; every other packet from local or the DMA leaves by the attach port; from the attach port, a packet not for the
// tile goes nowhere, as before. Its queues (16.3): east from local, west and south; west from local, east and south;
// the eject from local, west, east, south and the attach port; the attach output from local and south: each output's
// arbiter in that order. dma_room carries the line's queues too (bits 8 + 2d east, 9 + 2d west, and bit 16: the line).
// Without `line` the special tile's router is the RTL's as built (v1.36; tests/test_router.cpp compares them).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/net.h"

namespace units {

struct Router {
    // ref's port and output numbering (Router.java)
    enum : unsigned { FROM_LEFT = 0, FROM_RIGHT = 1, FROM_UP = 2, FROM_DOWN = 3, LOOPBACK = 4, FROM_CNC = 5 };
    enum : unsigned { TO_RIGHT = 0, TO_LEFT = 1, TO_DOWN = 2, TO_UP = 3, EJECT = 4, TO_CNC = 5 };
    enum : unsigned { M_RIGHT = 1, M_LEFT = 2, M_DOWN = 4, M_UP = 8, M_EJ = 16, M_CNC = 32 };
    enum Fifth : uint8_t { FIFTH_HOLD, FIFTH_LINK };                       // an attach tile's attach port: a bench's, or a link
    static constexpr unsigned kOut = 6, kFifos = 27;
    // Each output's queues, in the arbiter's order (16.3): local, west, east, north, south, the attach port.
    static constexpr uint8_t kFeeders[kOut][6] = {                         // FEEDERS_TILE0, in slot order: 27 FIFOs
        {LOOPBACK, FROM_LEFT, FROM_CNC}, {LOOPBACK, FROM_RIGHT, FROM_CNC},
        {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_CNC}, {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_DOWN, FROM_CNC},
        {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN, FROM_CNC}, {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN}};
    static constexpr uint8_t kNFeeders[kOut] = {3, 3, 5, 5, 6, 5};
    static constexpr uint8_t kFirst[kOut] = {0, 3, 6, 11, 16, 22};          // output o's first FIFO index
    static constexpr unsigned kMaxDepth = 64;                              // the loopback FIFO's (read_tokens)
    // A special tile's router with the line (16.3): its queues, by output, in the arbiter's order
    static constexpr uint8_t kFeedersLine[kOut][6] = {
        {LOOPBACK, FROM_LEFT, FROM_DOWN}, {LOOPBACK, FROM_RIGHT, FROM_DOWN}, {}, {},
        {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_DOWN, FROM_CNC}, {LOOPBACK, FROM_DOWN}};
    static constexpr uint8_t kNFeedersLine[kOut] = {3, 3, 0, 0, 5, 2};
    static constexpr uint8_t kFirstLine[kOut] = {0, 3, 6, 6, 6, 11};
    static constexpr sim::Word ROOM_LINE = sim::Word(1) << 16;             // dma_room: the router has the line

    // One of ref's WordFIFOs inside the router. `put_at` and `put_w` keep the last put (accepted or dropped) so the
    // fingerprint can list a put of cycle c apart from the entries, as StateFp does (AUDIT-M2 6.2).
    struct Fifo {
        uint16_t base = 0;            // its first entry in `store`
        uint8_t cap = 0, head = 0, count = 0;
        uint8_t put_kept = 0;         // the last put was pushed (else dropped: an overflow)
        uint8_t took_after_put = 0;   // a take since the last put (M3's labelling: ref's take settles a pending put)
        uint64_t put_at = 0;          // the cycle of the last put (0: none)
        uint64_t put_w[3] = {};
        uint64_t overflows = 0, first_overflow = 0;
        uint32_t peak = 0;            // statistics (NetTest's queue peaks): not state
    };

    // ---- ports ----
    sim::LinkIn in[4], inj;           // in[d]: a compute tile's links; a special tile's DMA inputs (those of dma_inputs)
    sim::HoldIn from_cnc;             // attached, FIFTH_HOLD only
    sim::LinkOut out[4], eject, loop_left;   // out[d]: a compute tile's links
    sim::FifoOut to_cnc;              // attached, FIFTH_HOLD only
    sim::LinkIn from_cc;              // the attach port in: an attach tile's with FIFTH_LINK, a special tile's
    sim::LinkOut to_cc;               // the attach port out
    sim::WireOut dma_room;            // a special tile's with DMA inputs: the room of each one's two queues
    sim::CountOut ovf;
    sim::LinkOut ovf_ev;              // ovf_event only

    // ---- configuration ----
    bool ovf_event = false;
    uint8_t me = 0;
    bool attached = false;            // an attach tile (NetConfig::attach): the fifth port is connected
    uint8_t attach[3] = {0, 7, 15};   // the attach tiles (NetConfig::attach)
    bool cc = false;                  // a special tile's router (me 64-66)
    bool fifth_link = false;          // attached: the attach port is a link (the chip's); else a bench's Hold and Fifo
    uint8_t dma_inputs = 0;           // a special tile's: the link inputs that carry DMAs (bit 0 west, 1 east, 2 north,
                                      // 3 south: the RTL's DMA_INPUTS)
    bool line = false;                // a special tile's: the line between the special tiles (16.1; THE LINE above)
    bool line_west = false, line_east = false;   // with the line: the links towards id - 1 and id + 1 exist
    uint64_t line_in = 0, line_out = 0;          // statistics: packets taken from the line, put on it
    const uint8_t (*feed)[6] = kFeeders;              // the feeder table
    const uint8_t* nfeed = kNFeeders;
    const uint8_t* first = kFirst;
    uint8_t n_fifos = 27;
    bool loopback_last = true;
    const Hops* hops = nullptr;       // the shared, read-only offset table
    uint8_t slot_of[kOut][6] = {};    // feeder slot of an input port at an output (0xFF: none)

    // ---- state ----
    uint8_t rr[kOut] = {};
    Fifo q[kFifos];                   // output-major: output o's feeders at fifo_index(o, 0 ..)
    uint64_t store[(kFifos - 1) * 16 * 3 + kMaxDepth * 3] = {};   // the FIFOs' entries, three words each

    // ---- statistics (ref's counters; not state) ----
    uint64_t injected = 0, routed = 0, ejected = 0, eject_errors = 0, route_errors = 0, from_cnc_words = 0, to_cnc_words = 0;
    uint64_t dropped = 0;             // packets for a reserved tile id or for none, dropped where they were routed (16.2)
    uint64_t dma_packets = 0;         // packets a special tile's DMA inputs moved in (16.4)
    uint64_t loop_left_at = 0;        // the last cycle a token-holding packet left the loopback FIFO (tests)

    // non-empty FIFOs, one bit per FIFO index (exact, unlike ref's `maybe` hint)
    uint32_t occ = 0;
    // the packets this cycle's pushes lost to full queues: counted on `ovf`, and `ovf_ev` put, as the step ends, in the
    // push's cycle (router_output.veryl: `overflow` is 1 for that cycle). Always 0 between steps.
    uint32_t ovf_now = 0;
    sim::Word room_out = 0xFF;        // dma_room as last driven (its reset value: every queue empty)

    // `tile` 0-63 a compute tile's router (`fifth`: an attach tile's attach port), 64-66 a special tile's (`dma`: its
    // DMA inputs, the RTL's DMA_INPUTS).
    Router(sim::Ports& p, unsigned tile, const NetConfig& c, const Hops* h, bool overflow_event = false,
           Fifth fifth = FIFTH_HOLD, unsigned dma = 0, bool line = false);
    sim::Wake step(sim::Ctx& c);
    sim::Wake step_cc(sim::Ctx& c);   // a special tile's router (step dispatches to it)
    // 16.4: `ready` of DMA input d (0 west .. 3 south) of tile `me`'s router for a packet to `dst`, from dma_room's
    // value in that cycle: the room of the queue the packet goes to; a packet that goes nowhere is always taken.
    static bool dma_ready(sim::Word room, unsigned d, unsigned me, unsigned dst) {
        if (no_tile(dst)) return true;
        if ((room & ROOM_LINE) && dst != me && lw::controller(dst)) return (room >> (8 + 2 * d + (dst > me ? 0 : 1)) & 1) != 0;   // the line
        return (room >> (2 * d + (dst == me ? 0 : 1)) & 1) != 0;
    }
    uint64_t digest() const;          // the state above (not the statistics, not the pointer)

    unsigned fifo_index(unsigned o, unsigned slot) const { return first[o] + slot; }
    unsigned unicast(unsigned src, unsigned dst) const;      // ref's Router.unicast: the output mask
    unsigned broadcast(unsigned port, unsigned src) const;   // ref's Router.broadcast

    // The router's own part of its canonical stream (StateFp.router, AUDIT-M2 6.2): rr[0..5], then every FIFO as
    // StateFp.fifo writes it (entries, then the put made at `cyc`, not yet judged). The buses delivering into it (the
    // four links, the injection link, from C&C) follow, written from the ports by the fingerprint library.
    template <class Out> void fp_own(Out& o, uint64_t cyc) const;

private:
    struct Sel { uint64_t w[3]; unsigned slot; };
    bool select(unsigned o, uint32_t m, Sel& s, uint32_t& taken);
    void push(sim::Ctx& c, unsigned o, unsigned port, const uint64_t* w, uint32_t taken);
    void route(sim::Ctx& c, unsigned port, uint64_t d, uint64_t a, uint64_t cw, uint32_t taken);
    // 16.2: a tile id that is no compute tile, special tile or broadcast: reserved, or none
    static bool no_tile(unsigned dst) { return dst > lw::CC3 && dst != lw::BROADCAST; }
    // the queue of output o from input `port` has room for a push in this cycle (WordFIFO's rule, as push judges it)
    bool room(unsigned o, unsigned port, uint32_t taken) const {
        const unsigned i = first[o] + slot_of[o][port];
        return q[i].count + (taken >> i & 1) < q[i].cap;
    }
    void route_mask(sim::Ctx& c, unsigned mask, unsigned port, uint64_t d, uint64_t a, uint64_t cw, uint32_t taken);
    bool eject_select(sim::Ctx& c, uint32_t& taken);   // the eject selector and its put (both variants)
    void dma_arrivals(sim::Ctx& c);                    // a special tile's: the DMAs' packets that moved in the cycle before
    unsigned line_output(unsigned dst) const { return dst > me ? unsigned(TO_RIGHT) : unsigned(TO_LEFT); }   // the ring's rule (16.2)
    sim::Wake step_line(sim::Ctx& c);                  // a special tile's router with the line
    void end_step(sim::Ctx& c, uint32_t taken);        // this cycle's overflows, and dma_room
};

// ref's routing decisions as pure functions (the router at tile `me`): output masks (Router::M_*).
// (`attach`: the three attach tiles, NetConfig::attach; null: the specification's)
unsigned route_unicast(const Hops& hops, unsigned me, unsigned src, unsigned dst, const uint8_t* attach = nullptr);
unsigned route_broadcast(unsigned me, unsigned port, unsigned src, const uint8_t* attach = nullptr);
inline unsigned Router::unicast(unsigned src, unsigned dst) const { return route_unicast(*hops, me, src, dst, attach); }
inline unsigned Router::broadcast(unsigned port, unsigned src) const { return route_broadcast(me, port, src, attach); }

// ---- the step (inline: the run function inlines it, config.h DISPATCH) --------------------------------------------

// One selector: the first FIFO after rr[o] in round-robin order among `m` (FIFO bits of output o, relative) that holds a
// word as at the start of t. ref's rr is updated except for the eject's loopback slot.
inline bool Router::select(unsigned o, uint32_t m, Sel& s, uint32_t& taken) {
    const unsigned n = nfeed[o], base = first[o];
    m &= occ >> base;
    if (!m) return false;
    unsigned f = rr[o];
    for (unsigned i = 0; i < n; i++) {
        f = f + 1 == n ? 0 : f + 1;
        if (!(m >> f & 1)) continue;
        Fifo& q = this->q[base + f];
        const uint64_t* e = store + (q.base + q.head) * 3u;
        s.w[0] = e[0]; s.w[1] = e[1]; s.w[2] = e[2]; s.slot = f;
        q.head = uint8_t(q.head + 1 == q.cap ? 0 : q.head + 1);
        if (--q.count == 0) occ &= ~(1u << (base + f));
        q.took_after_put = 1;
        taken |= 1u << (base + f);
        if (o != EJECT || f != slot_of[EJECT][LOOPBACK]) rr[o] = uint8_t(f);
        return true;
    }
    return false;
}

// WordFIFO.put then its settle: full by the occupancy at the start of t (count + this cycle's take).
inline void Router::push(sim::Ctx& c, unsigned o, unsigned port, const uint64_t* w, uint32_t taken) {
    const unsigned f = slot_of[o][port];
    if (f == 0xFF) { route_errors++; return; }
    const unsigned i = first[o] + f;
    Fifo& q = this->q[i];
    const uint64_t t = c.now();
    q.put_at = t; q.put_w[0] = w[0]; q.put_w[1] = w[1]; q.put_w[2] = w[2]; q.took_after_put = 0;
    if (q.count + (taken >> i & 1) >= q.cap) {
        q.put_kept = 0;
        q.overflows++;
        if (!q.first_overflow) q.first_overflow = t;   // ref's overflowCycle: the first since a reset cleared it
        ovf_now++;
        return;
    }
    q.put_kept = 1;
    unsigned tail = q.head + q.count; if (tail >= q.cap) tail -= q.cap;
    uint64_t* e = store + (q.base + tail) * 3u;
    e[0] = w[0]; e[1] = w[1]; e[2] = w[2];
    if (++q.count > q.peak) q.peak = q.count;
    occ |= 1u << i;
}

inline void Router::route(sim::Ctx& c, unsigned port, uint64_t d, uint64_t a, uint64_t cw, uint32_t taken) {
    routed++;
    const unsigned dst = lw::dst(a);
    if (no_tile(dst)) { dropped++; return; }
    route_mask(c, dst == lw::BROADCAST ? broadcast(port, lw::src(a)) : unicast(lw::src(a), dst), port, d, a, cw, taken);
}

inline void Router::route_mask(sim::Ctx& c, unsigned mask, unsigned port, uint64_t d, uint64_t a, uint64_t cw, uint32_t taken) {
    const uint64_t w[3] = {d, a, cw};
    if (mask & M_RIGHT) push(c, TO_RIGHT, port, w, taken);
    if (mask & M_LEFT) push(c, TO_LEFT, port, w, taken);
    if (mask & M_DOWN) push(c, TO_DOWN, port, w, taken);
    if (mask & M_UP) push(c, TO_UP, port, w, taken);
    if (mask & M_EJ) push(c, EJECT, port, w, taken);
    if (mask & M_CNC) push(c, TO_CNC, port, w, taken);
}

// The eject selector (4.7): the loopback FIFO only in a cycle when every other eject FIFO is empty; a word not for this
// router (nor a broadcast) is counted, not delivered.
inline bool Router::eject_select(sim::Ctx& c, uint32_t& taken) {
    Sel s;
    const unsigned loop = slot_of[EJECT][LOOPBACK];
    bool ej;
    if (loopback_last) ej = select(EJECT, 0x3F & ~(1u << loop), s, taken) || select(EJECT, 1u << loop, s, taken);
    else { ej = select(EJECT, 0x3F, s, taken); if (ej && s.slot == loop) rr[EJECT] = uint8_t(loop); }
    if (!ej) return false;
    if (s.slot == loop && (s.w[1] & lw::TAG_TOKEN)) { c.put(loop_left); loop_left_at = c.now(); }
    ejected++;
    const unsigned dst = lw::dst(s.w[1]);
    if (dst == me || dst == lw::BROADCAST) c.put(eject, s.w[0], s.w[1], s.w[2]);
    else eject_errors++;
    return true;
}

inline sim::Wake Router::step(sim::Ctx& c) {
    if (cc) return step_cc(c);
    uint32_t taken = 0;
    Sel s;
    // selectors: one word per output, from the FIFOs as at the start of t
    for (unsigned o = 0; o < 4; o++)
        if (select(o, 0x3F, s, taken)) c.put(out[o], s.w[0], s.w[1], s.w[2]);
    eject_select(c, taken);
    if (attached && select(TO_CNC, 0x3F, s, taken)) {
        to_cnc_words++;
        if (fifth_link) c.put(to_cc, s.w[0], s.w[1], s.w[2]); else c.put(to_cnc, s.w[0], s.w[1], s.w[2]);
    }
    // route blocks: the links, the injection (src stamped), C&C's input
    for (unsigned p = 0; p < 4; p++)
        if (c.ready(in[p])) {
            const sim::Word* w = c.peek(in[p]);
            const uint64_t d = w[0], a = w[1], cw = w[2];
            c.take(in[p]);
            route(c, p, d, a, cw, taken);
        }
    if (c.ready(inj)) {
        const sim::Word* w = c.peek(inj);
        const uint64_t d = w[0], a = lw::with_src(w[1], me), cw = w[2];
        c.take(inj);
        injected++;
        route(c, LOOPBACK, d, a, cw, taken);
    }
    if (attached && !fifth_link && c.ready(from_cnc)) {
        const sim::Word* w = c.peek(from_cnc);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(from_cnc);
        from_cnc_words++;
        route(c, FROM_CNC, d, a, cw, taken);
    }
    if (attached && fifth_link && c.ready(from_cc)) {             // tile c: C&C c's link (src stamped by its router)
        const sim::Word* w = c.peek(from_cc);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(from_cc);
        from_cnc_words++;
        route(c, FROM_CNC, d, a, cw, taken);
    }
    // FIFOs that will be selected (the attach output's only where attached) hold words: look again next cycle
    const uint32_t live = attached ? occ : occ & ((1u << first[TO_CNC]) - 1);
    end_step(c, taken);
    return live ? c.next() : sim::Wake::none();
}

// As the step ends: the packets its pushes lost are counted, in the push's cycle, and a special tile's DMA inputs'
// room is driven: each queue's count at the start of the cycle (after the arrivals of the cycle before, before this
// cycle's takes) below its depth (router_output.veryl: `room`, from the queue's registers).
inline void Router::end_step(sim::Ctx& c, uint32_t taken) {
    if (ovf_now) { c.inc(ovf, ovf_now); if (ovf_event) c.put(ovf_ev); ovf_now = 0; }
    if (!dma_inputs) return;
    sim::Word r = line ? ROOM_LINE : 0;
    for (unsigned d = 0; d < 4; d++) {
        if (!(dma_inputs >> d & 1)) continue;
        if (room(EJECT, d, taken)) r |= sim::Word(1) << (2 * d);
        if (room(TO_CNC, d, taken)) r |= sim::Word(1) << (2 * d + 1);
        if (line && line_east && room(TO_RIGHT, d, taken)) r |= sim::Word(1) << (8 + 2 * d);
        if (line && line_west && room(TO_LEFT, d, taken)) r |= sim::Word(1) << (9 + 2 * d);
    }
    if (r != room_out) { room_out = r; c.drive(dma_room, r); }
}

// A special tile's DMA inputs (16.4): a packet delivered at t moved in t - 1, with valid and ready, and is written
// into its queue at the edge that ends t - 1: here, before t's arbiters. Its src is the tile's id.
inline void Router::dma_arrivals(sim::Ctx& c) {
    for (unsigned d = 0; d < 4; d++) {
        if (!(dma_inputs >> d & 1) || !c.ready(in[d])) continue;
        const sim::Word* w = c.peek(in[d]);
        const uint64_t w3[3] = {w[0], lw::with_src(w[1], me), w[2]};
        c.take(in[d]);
        routed++; dma_packets++;
        const unsigned dst = lw::dst(w3[1]);
        if (no_tile(dst)) { dropped++; continue; }
        const unsigned o = dst == me ? unsigned(EJECT) : line && lw::controller(dst) ? line_output(dst) : unsigned(TO_CNC);
        if (o == TO_RIGHT || o == TO_LEFT) { if (!(o == TO_RIGHT ? line_east : line_west)) { route_errors++; continue; } line_out++; }
        if (!room(o, d, 0)) FATAL("router %u: DMA input %u moved a packet into a full queue (16.4: its ready was 0)", unsigned(me), d);
        push(c, o, d, w3, 0);
        q[first[o] + slot_of[o][d]].put_at = c.now() - 1;
    }
}

// A special tile's router (router.veryl, `route` with SPECIAL): the DMAs' packets of the cycle before; the attach
// output's arbiter and the eject's; then the route blocks: from the attach port, a packet for the tile ejects and any
// other goes nowhere (16.2 names the packets sent on, those from the local input or a DMA); from local, a packet for
// the tile ejects and any other, a broadcast included, leaves by the attach port; a reserved dst is dropped.
inline sim::Wake Router::step_cc(sim::Ctx& c) {
    if (line) return step_line(c);
    dma_arrivals(c);
    uint32_t taken = 0;
    Sel s;
    if (select(TO_CNC, 0x3F, s, taken)) { to_cnc_words++; c.put(to_cc, s.w[0], s.w[1], s.w[2]); }
    eject_select(c, taken);
    if (c.ready(from_cc)) {
        const sim::Word* w = c.peek(from_cc);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(from_cc);
        routed++; from_cnc_words++;
        if (no_tile(lw::dst(a))) dropped++;
        else if (lw::dst(a) == me) route_mask(c, M_EJ, FROM_CNC, d, a, cw, taken);
    }
    if (c.ready(inj)) {
        const sim::Word* w = c.peek(inj);
        const uint64_t d = w[0], a = lw::with_src(w[1], me), cw = w[2];
        c.take(inj);
        injected++; routed++;
        if (no_tile(lw::dst(a))) dropped++;
        else route_mask(c, lw::dst(a) == me ? M_EJ : M_CNC, LOOPBACK, d, a, cw, taken);
    }
    end_step(c, taken);
    return occ ? c.next() : sim::Wake::none();
}

// A special tile's router with the line (THE LINE, in the header): the DMA's packets of the cycle before; the arbiters of
// the attach output, the eject, and the line's two outputs; then the route blocks: the attach port, local, the line.
inline sim::Wake Router::step_line(sim::Ctx& c) {
    dma_arrivals(c);
    uint32_t taken = 0;
    Sel s;
    if (select(TO_CNC, 0x3F, s, taken)) { to_cnc_words++; c.put(to_cc, s.w[0], s.w[1], s.w[2]); }
    eject_select(c, taken);
    if (line_east && select(TO_RIGHT, 0x3F, s, taken)) c.put(out[TO_RIGHT], s.w[0], s.w[1], s.w[2]);
    if (line_west && select(TO_LEFT, 0x3F, s, taken)) c.put(out[TO_LEFT], s.w[0], s.w[1], s.w[2]);
    if (c.ready(from_cc)) {                       // from the attach port: for the tile, or nowhere
        const sim::Word* w = c.peek(from_cc);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(from_cc);
        routed++; from_cnc_words++;
        if (no_tile(lw::dst(a))) dropped++;
        else if (lw::dst(a) == me) route_mask(c, M_EJ, FROM_CNC, d, a, cw, taken);
    }
    if (c.ready(inj)) {                           // from local: for the tile; another special tile by the line; else attach
        const sim::Word* w = c.peek(inj);
        const uint64_t d = w[0], a = lw::with_src(w[1], me), cw = w[2];
        c.take(inj);
        injected++; routed++;
        const unsigned dst = lw::dst(a);
        if (no_tile(dst)) dropped++;
        else if (dst == me) route_mask(c, M_EJ, LOOPBACK, d, a, cw, taken);
        else if (lw::controller(dst)) {
            const unsigned o = line_output(dst);
            if (!(o == TO_RIGHT ? line_east : line_west)) route_errors++;
            else { line_out++; route_mask(c, o == TO_RIGHT ? M_RIGHT : M_LEFT, LOOPBACK, d, a, cw, taken); }
        } else route_mask(c, M_CNC, LOOPBACK, d, a, cw, taken);
    }
    for (unsigned p : {unsigned(FROM_LEFT), unsigned(FROM_RIGHT)}) {   // from the line: for the tile, or on the way it came
        if (!(p == FROM_LEFT ? line_west : line_east) || !c.ready(in[p])) continue;
        const sim::Word* w = c.peek(in[p]);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(in[p]);
        routed++; line_in++;
        const unsigned dst = lw::dst(a);
        if (dst == me) { route_mask(c, M_EJ, p, d, a, cw, taken); continue; }
        const bool on = p == FROM_LEFT ? line_east : line_west;
        if (!on || !lw::controller(dst)) { route_errors++; continue; }   // 16.2: a packet on the line is a special tile's
        line_out++;
        route_mask(c, p == FROM_LEFT ? M_RIGHT : M_LEFT, p, d, a, cw, taken);
    }
    end_step(c, taken);
    return occ ? c.next() : sim::Wake::none();
}

template <class Out>
void Router::fp_own(Out& o, uint64_t cyc) const {
    for (unsigned k = 0; k < kOut; k++) o.w("rr", rr[k]);
    for (unsigned i = 0; i < n_fifos; i++) {
        const Fifo& f = q[i];
        // A put of cyc is listed apart (ref's pending put, lastT == cyc). M3's labelling (cyc the cycle before the
        // last one run): a put after cyc (ref's lastT = cyc + 1) isn't in ref's FIFO yet; a put of cyc followed by a take
        // in the last cycle was settled by it (ref commits a pending put on an access): an entry like the others.
        const bool later = f.put_at > cyc;
        const bool now = !later && f.put_at == cyc && cyc != 0 && !f.took_after_put;
        const unsigned n = (now || later) && f.put_kept ? f.count - 1u : f.count;
        o.w("n", n);
        for (unsigned k = 0; k < n; k++) {
            unsigned j = f.head + k; if (j >= f.cap) j -= f.cap;
            const uint64_t* e = store + (f.base + j) * 3u;
            o.w("a", e[0]); o.w("b", e[1]); o.w("c", e[2]);
        }
        o.w("put", now ? 1 : 0);
        if (now) { o.w("pa", f.put_w[0]); o.w("pb", f.put_w[1]); o.w("pc", f.put_w[2]); }
    }
}

}  // namespace units
