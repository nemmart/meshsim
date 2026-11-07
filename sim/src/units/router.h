// router.h (WORKER9, G2): one tile's router (spec 4.6, 4.7, 4.16; ref's Router.java; AUDIT-M2 3.1). Declarations:
// the ports and the state; the step comes with stage 1.
//
// PORTS (link words are three words, net.h)
//   in   in[4]       LinkIn, the mesh links from the neighbours (latency link_latency, wired by the chip), indexed by
//                    ref's arrival port: FROM_LEFT 0, FROM_RIGHT 1, FROM_UP 2, FROM_DOWN 3. Every arrival is taken.
//        inj         LinkIn, the injection unit's one packet per cycle (latency intra_tile_latency); src stamped here.
//        from_cnc    HoldIn, tiles 0-2 only: the C&C endpoint's send holding register (the from-controller route block).
//   out  out[4]      LinkOut, the mesh links, indexed by direction: TO_RIGHT 0, TO_LEFT 1, TO_DOWN 2, TO_UP 3.
//        eject       LinkOut to W8's director (latency intra_tile_latency); the whole header kept.
//        loop_left   LinkOut, width 0, latency 1, to the injection unit: a token-holding packet left the loopback FIFO
//                    at t, its token is free from t + 1 (ref: `srq.loopbackLeft(t)`).
//        to_cnc      FifoOut, tiles 0-2 only: the to-controller output into the endpoint's receive FIFO.
//        ovf         CountOut, every overflow of the router's FIFOs (ROUTER_OVF, read by W8's register block).
//
// STATE. One FIFO per (output, feeder), ref's `FEEDERS_TILE0` on every tile (every router has the fifth port's FIFOs;
// `attached`, tiles 0-2, only decides whether the to-controller selector runs). The FIFOs are the router's own (owner,
// 2026-11-07: as ref first; W10's Link with a declared depth may replace them later) with `WordFIFO`'s rules: a put at t
// is visible from t + 1; full is judged against the occupancy at the start of t; an overflowing put is dropped and
// counted. The round-robin pointers `rr[6]`. (`maybe` is ref's hint, not state.)
//
// THE STEP AT t (ref's order; it only changes the router's own state): the four link selectors, the eject selector
// (loopback last), the to-controller selector on tiles 0-2, each taking one word from its FIFOs as they stood at the start
// of t; then the route blocks (the four links, the injection, from C&C) push into FIFOs, visible t + 1. It asks for the
// next cycle while any FIFO holds a word.
#pragma once

#include <cstdint>

#include "sim.h"
#include "units/net.h"

namespace units {

struct Router {
    // ref's port and output numbering (Router.java)
    enum : unsigned { FROM_LEFT = 0, FROM_RIGHT = 1, FROM_UP = 2, FROM_DOWN = 3, LOOPBACK = 4, FROM_CNC = 5 };
    enum : unsigned { TO_RIGHT = 0, TO_LEFT = 1, TO_DOWN = 2, TO_UP = 3, EJECT = 4, TO_CNC = 5 };
    enum : unsigned { M_RIGHT = 1, M_LEFT = 2, M_DOWN = 4, M_UP = 8, M_EJ = 16, M_CNC = 32 };
    static constexpr unsigned kOut = 6, kFifos = 27;                       // FEEDERS_TILE0: 3 + 3 + 5 + 5 + 6 + 5
    static constexpr uint8_t kFeeders[kOut][6] = {                         // the feeders of each output, in slot order
        {LOOPBACK, FROM_LEFT, FROM_CNC}, {LOOPBACK, FROM_RIGHT, FROM_CNC},
        {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_CNC}, {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_DOWN, FROM_CNC},
        {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN, FROM_CNC}, {LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN}};
    static constexpr uint8_t kNFeeders[kOut] = {3, 3, 5, 5, 6, 5};
    static constexpr unsigned kMaxDepth = 64;                              // the loopback FIFO's (read_tokens)

    // One of ref's WordFIFOs inside the router. `put_at` and `put_w` keep the last put (accepted or dropped) so the
    // fingerprint can list a put of cycle c apart from the entries, as StateFp does (AUDIT-M2 6.2).
    struct Fifo {
        uint16_t base = 0;            // its first entry in `store`
        uint8_t cap = 0, head = 0, count = 0;
        uint8_t put_kept = 0;         // the last put was pushed (else dropped: an overflow)
        uint64_t put_at = 0;          // the cycle of the last put (0: none)
        uint64_t put_w[3] = {};
        uint64_t overflows = 0, first_overflow = 0;
        uint32_t peak = 0;            // statistics (NetTest's queue peaks): not state
    };

    // ---- ports ----
    sim::LinkIn in[4], inj;
    sim::HoldIn from_cnc;             // attached only
    sim::LinkOut out[4], eject, loop_left;
    sim::FifoOut to_cnc;              // attached only
    sim::CountOut ovf;

    // ---- configuration ----
    uint8_t me = 0;
    bool attached = false;            // tiles 0-2: the fifth port is connected (C&C 1-3)
    bool loopback_last = true;
    const Hops* hops = nullptr;       // the shared, read-only offset table
    uint8_t slot_of[kOut][6] = {};    // feeder slot of an input port at an output (0xFF: none)

    // ---- state ----
    uint8_t rr[kOut] = {};
    Fifo q[kFifos];                   // output-major: output o's feeders at fifo_index(o, 0 ..)
    uint64_t store[(kFifos - 1) * 16 * 3 + kMaxDepth * 3] = {};   // the FIFOs' entries, three words each

    // ---- statistics (ref's counters; not state) ----
    uint64_t injected = 0, routed = 0, ejected = 0, eject_errors = 0, route_errors = 0, from_cnc_words = 0, to_cnc_words = 0;

    Router(sim::Ports& p, unsigned tile, const NetConfig& c, const Hops* h);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;          // the state above (not the statistics, not the pointer)

    static unsigned fifo_index(unsigned o, unsigned slot);
    unsigned unicast(unsigned src, unsigned dst) const;      // ref's Router.unicast: the output mask
    unsigned broadcast(unsigned port, unsigned src) const;   // ref's Router.broadcast
};

}  // namespace units
