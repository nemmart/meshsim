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
    uint64_t loop_left_at = 0;        // the last cycle a token-holding packet left the loopback FIFO (tests)

    // non-empty FIFOs, one bit per FIFO index (exact, unlike ref's `maybe` hint)
    uint32_t occ = 0;
    // overflows of this cycle's puts, counted on `ovf` at t + 1: a put of t is judged at t + 1 (PORTS.md 3), so the
    // register block's endCycle(t + 1) is the first to see it. D28: ref counts it lazily, at the FIFO's next touch (a
    // variable number of cycles late, differently with idle-skip on and off); the framework follows PORTS.md's rule and
    // references avoid the case.
    uint32_t ovf_due = 0;

    Router(sim::Ports& p, unsigned tile, const NetConfig& c, const Hops* h);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;          // the state above (not the statistics, not the pointer)

    static constexpr uint8_t kFirst[kOut] = {0, 3, 6, 11, 16, 22};          // output o's first FIFO index
    static unsigned fifo_index(unsigned o, unsigned slot) { return kFirst[o] + slot; }
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
};

// ref's routing decisions as pure functions (the router at tile `me`): output masks (Router::M_*).
unsigned route_unicast(const Hops& hops, unsigned me, unsigned src, unsigned dst);
unsigned route_broadcast(unsigned me, unsigned port, unsigned src);
inline unsigned Router::unicast(unsigned src, unsigned dst) const { return route_unicast(*hops, me, src, dst); }
inline unsigned Router::broadcast(unsigned port, unsigned src) const { return route_broadcast(me, port, src); }

// ---- the step (inline: the run function inlines it, config.h DISPATCH) --------------------------------------------

// One selector: the first FIFO after rr[o] in round-robin order among `m` (FIFO bits of output o, relative) that holds a
// word as at the start of t. ref's rr is updated except for the eject's loopback slot.
inline bool Router::select(unsigned o, uint32_t m, Sel& s, uint32_t& taken) {
    const unsigned n = kNFeeders[o], base = kFirst[o];
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
    const unsigned i = kFirst[o] + f;
    Fifo& q = this->q[i];
    const uint64_t t = c.now();
    q.put_at = t; q.put_w[0] = w[0]; q.put_w[1] = w[1]; q.put_w[2] = w[2];
    if (q.count + (taken >> i & 1) >= q.cap) {
        q.put_kept = 0;
        if (q.overflows++ == 0) q.first_overflow = t;
        ovf_due++;
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
    const unsigned mask = dst == lw::BROADCAST ? broadcast(port, lw::src(a)) : unicast(lw::src(a), dst);
    const uint64_t w[3] = {d, a, cw};
    if (mask & M_RIGHT) push(c, TO_RIGHT, port, w, taken);
    if (mask & M_LEFT) push(c, TO_LEFT, port, w, taken);
    if (mask & M_DOWN) push(c, TO_DOWN, port, w, taken);
    if (mask & M_UP) push(c, TO_UP, port, w, taken);
    if (mask & M_EJ) push(c, EJECT, port, w, taken);
    if (mask & M_CNC) push(c, TO_CNC, port, w, taken);
}

inline sim::Wake Router::step(sim::Ctx& c) {
    if (ovf_due) { c.inc(ovf, ovf_due); ovf_due = 0; }
    uint32_t taken = 0;
    Sel s;
    // selectors: one word per output, from the FIFOs as at the start of t
    for (unsigned o = 0; o < 4; o++)
        if (select(o, 0x3F, s, taken)) c.put(out[o], s.w[0], s.w[1], s.w[2]);
    const unsigned loop = slot_of[EJECT][LOOPBACK];
    bool ej;
    if (loopback_last) ej = select(EJECT, 0x3F & ~(1u << loop), s, taken) || select(EJECT, 1u << loop, s, taken);
    else { ej = select(EJECT, 0x3F, s, taken); if (ej && s.slot == loop) rr[EJECT] = uint8_t(loop); }
    if (ej) {
        if (s.slot == loop && (s.w[1] & lw::TAG_TOKEN)) { c.put(loop_left); loop_left_at = c.now(); }
        ejected++;
        const unsigned dst = lw::dst(s.w[1]);
        if (dst == me || dst == lw::BROADCAST) c.put(eject, s.w[0], s.w[1], s.w[2]);
        else eject_errors++;
    }
    if (attached && select(TO_CNC, 0x3F, s, taken)) { to_cnc_words++; c.put(to_cnc, s.w[0], s.w[1], s.w[2]); }
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
    if (attached && c.ready(from_cnc)) {
        const sim::Word* w = c.peek(from_cnc);
        const uint64_t d = w[0], a = w[1], cw = w[2];
        c.take(from_cnc);
        from_cnc_words++;
        route(c, FROM_CNC, d, a, cw, taken);
    }
    // FIFOs that will be selected (the to-controller output only where attached) hold words: look again next cycle
    const uint32_t live = attached ? occ : occ & ((1u << kFirst[TO_CNC]) - 1);
    return live || ovf_due ? c.next() : sim::Wake::none();
}

template <class Out>
void Router::fp_own(Out& o, uint64_t cyc) const {
    for (unsigned k = 0; k < kOut; k++) o.w("rr", rr[k]);
    for (unsigned i = 0; i < kFifos; i++) {
        const Fifo& f = q[i];
        const bool now = f.put_at == cyc && cyc != 0;
        const unsigned n = now && f.put_kept ? f.count - 1u : f.count;   // a put of cyc is listed apart
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
