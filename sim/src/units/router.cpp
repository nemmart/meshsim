// router.cpp (WORKER9): the router's configuration, ref's routing functions (Router.unicast, Router.broadcast) and its
// digest. The step is inline in router.h.
#include "units/router.h"

#include <cstring>

#include "base/fatal.h"

namespace units {

Router::Router(sim::Ports& p, unsigned tile, const NetConfig& c, const Hops* h, bool overflow_event)
    : ovf_event(overflow_event), me(uint8_t(tile)), attached(tile < 3), loopback_last(c.loopback_last), hops(h) {
    static const char* kIn[4] = {"from-left", "from-right", "from-up", "from-down"};
    static const char* kOutName[4] = {"to-right", "to-left", "to-down", "to-up"};
    for (unsigned d = 0; d < 4; d++) { p.in(in[d], kIn[d], 3); p.out(out[d], kOutName[d], 3); }
    p.in(inj, "inj", 3);
    p.out(eject, "eject", 3);
    p.out(loop_left, "loop-left", 0);
    p.out(ovf, "ovf");
    if (ovf_event) p.out(ovf_ev, "ovf-ev", 0);
    if (attached) { p.in(from_cnc, "from-cnc", 3); p.out(to_cnc, "to-cnc", 3); }
    if (tile > 63) FATAL("router: tile %u (a controller's router is not modelled in M2)", tile);
    if (!h) FATAL("router t%u: no hops table", tile);
    if (c.queue_depth < 1 || c.queue_depth > 16) FATAL("router t%u: queue_depth %u (1-16 modelled)", tile, c.queue_depth);
    const unsigned loop_depth = c.loopback_fifo_depth();
    if (loop_depth < 1 || loop_depth > kMaxDepth) FATAL("router t%u: loopback FIFO depth %u (1-%u modelled)", tile, loop_depth, kMaxDepth);
    std::memset(slot_of, 0xFF, sizeof slot_of);
    unsigned base = 0;
    for (unsigned o = 0; o < kOut; o++) {
        for (unsigned f = 0; f < kNFeeders[o]; f++) {
            Fifo& q = this->q[fifo_index(o, f)];
            q.cap = uint8_t(o == EJECT && kFeeders[o][f] == LOOPBACK ? loop_depth : c.queue_depth);
            q.base = uint16_t(base);
            base += q.cap;
            slot_of[o][kFeeders[o][f]] = uint8_t(f);
        }
        rr[o] = uint8_t(kNFeeders[o] - 1);
    }
    if (base * 3 > sizeof store / sizeof store[0]) FATAL("router t%u: FIFO storage", tile);
}

// ref: s, d the tiles of src and dst; k = (d - s) mod 64; (b, a) = hops[k]; horizontal first until this column is b
// columns from the source's (with the sign of b), then vertical; at d, the eject (or the to-controller output for a
// controller's dst).
unsigned route_unicast(const Hops& hops, unsigned me, unsigned src, unsigned dst) {
    using R = Router;
    const bool to_cnc_port = lw::controller(dst);
    const int s = int(lw::tile_of(src) & 63), d = int(lw::tile_of(dst) & 63);
    const int k = (d - s) & 63;
    const int b = hops.b[k], a = hops.a[k];
    const int cs = s & 7, cm = me & 7;
    const int hb = b >= 0 ? (cm - cs) & 7 : -((cs - cm) & 7);
    if (hb != b) return b > 0 ? R::M_RIGHT : R::M_LEFT;
    if (int(me) != d) return a > 0 ? R::M_DOWN : R::M_UP;
    return to_cnc_port ? R::M_CNC : R::M_EJ;
}

unsigned route_broadcast(unsigned me, unsigned port, unsigned src) {
    using R = Router;
    const int s = int(lw::tile_of(src) & 63);
    const int cs = s & 7, cm = me & 7, rs = s >> 3, rm = me >> 3;
    switch (port) {
    case R::LOOPBACK: case R::FROM_CNC: return R::M_EJ | R::M_RIGHT | R::M_LEFT | R::M_DOWN | R::M_UP;
    case R::FROM_LEFT: { unsigned m = R::M_EJ | R::M_DOWN | R::M_UP; if (((cm - cs) & 7) != 4) m |= R::M_RIGHT; return m; }
    case R::FROM_RIGHT: { unsigned m = R::M_EJ | R::M_DOWN | R::M_UP; if (((cs - cm) & 7) != 3) m |= R::M_LEFT; return m; }
    case R::FROM_UP: { unsigned m = R::M_EJ; if (((rm - rs) & 7) != 4) m |= R::M_DOWN; return m; }
    default: { unsigned m = R::M_EJ; if (((rs - rm) & 7) != 3) m |= R::M_UP; return m; }
    }
}

uint64_t Router::digest() const {
    uint64_t h = sim::hash_fields(me, attached, loopback_last, ovf_event, rr, occ, ovf_due);
    for (unsigned i = 0; i < kFifos; i++) {
        const Fifo& f = q[i];
        h = sim::hash_mix(h, sim::hash_fields(f.cap, f.count, f.put_kept, f.put_at, f.put_w, f.overflows, f.first_overflow));
        for (unsigned k = 0; k < f.count; k++) {
            unsigned j = f.head + k; if (j >= f.cap) j -= f.cap;
            h = sim::hash_mix(h, sim::hash_bytes(store + (f.base + j) * 3u, 24));
        }
    }
    return h;
}

}  // namespace units
