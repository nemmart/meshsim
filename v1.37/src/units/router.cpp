// router.cpp: the router's configuration, ref's routing functions (Router.unicast, Router.broadcast) and its
// digest. The step is inline in router.h.
#include "units/router.h"

#include <cstring>

#include "base/fatal.h"

namespace units {

Router::Router(sim::Ports& p, unsigned tile, const NetConfig& c, const Hops* h, bool overflow_event, Fifth fifth, unsigned dma, bool with_line)
    : ovf_event(overflow_event), me(uint8_t(tile)), attached(tile < 64 && c.attached_special(tile) >= 0), loopback_last(c.loopback_last), hops(h) {
    if (tile > 63 && !lw::controller(tile)) FATAL("router: tile %u (0-63 a compute tile, 64-66 a special tile)", tile);
    cc = tile > 63;
    for (unsigned s = 0; s < 3; s++) attach[s] = c.attach[s];
    if (cc) {
        if (dma > 15) FATAL("router t%u: DMA inputs 0x%x (bit 0 west, 1 east, 2 north, 3 south)", tile, dma);
        dma_inputs = uint8_t(dma);
        line = with_line;
        if (line) {                                  // 16.1: the line; the DMA on south alone (16.4)
            if (dma & 7) FATAL("router t%u: with the line, a special tile's DMA enters on the south input alone (16.4), not 0x%x", tile, dma);
            line_west = tile > lw::CC1; line_east = tile < lw::CC3;
            feed = kFeedersLine; nfeed = kNFeedersLine; first = kFirstLine; n_fifos = 13;
            if (line_west) { p.in(in[FROM_LEFT], "line-west-in", 3); p.out(out[TO_LEFT], "line-west-out", 3); }
            if (line_east) { p.in(in[FROM_RIGHT], "line-east-in", 3); p.out(out[TO_RIGHT], "line-east-out", 3); }
        }
        static const char* kDma[4] = {"dma-west", "dma-east", "dma-north", "dma-south"};
        for (unsigned d = 0; d < 4; d++) if (dma_inputs >> d & 1) p.in(in[d], kDma[d], 3);
        if (line) room_out = ROOM_LINE | 0xFFFF;
        if (dma_inputs) p.out(dma_room, "dma-room", room_out);
        p.in(from_cc, "attach-in", 3);
        p.out(to_cc, "attach-out", 3);
    } else {
        if (dma) FATAL("router t%u: a DMA input on a compute tile's router", tile);
        static const char* kIn[4] = {"from-left", "from-right", "from-up", "from-down"};
        static const char* kOutName[4] = {"to-right", "to-left", "to-down", "to-up"};
        for (unsigned d = 0; d < 4; d++) { p.in(in[d], kIn[d], 3); p.out(out[d], kOutName[d], 3); }
        fifth_link = attached && fifth == FIFTH_LINK;
        if (attached && !fifth_link) { p.in(from_cnc, "from-cnc", 3); p.out(to_cnc, "to-cnc", 3); }
        if (fifth_link) { p.in(from_cc, "from-cc", 3); p.out(to_cc, "to-cc", 3); }
    }
    p.in(inj, "inj", 3);
    p.out(eject, "eject", 3);
    p.out(loop_left, "loop-left", 0);
    p.out(ovf, "ovf");
    if (ovf_event) p.out(ovf_ev, "ovf-ev", 0);
    if (!h && !cc) FATAL("router t%u: no hops table", tile);
    if (c.queue_depth < 1 || c.queue_depth > 16) FATAL("router t%u: queue_depth %u (1-16 modelled)", tile, c.queue_depth);
    const unsigned loop_depth = c.loopback_fifo_depth();
    if (loop_depth < 1 || loop_depth > kMaxDepth) FATAL("router t%u: loopback FIFO depth %u (1-%u modelled)", tile, loop_depth, kMaxDepth);
    std::memset(slot_of, 0xFF, sizeof slot_of);
    unsigned base = 0;
    for (unsigned o = 0; o < kOut; o++) {
        for (unsigned f = 0; f < nfeed[o]; f++) {
            Fifo& q = this->q[fifo_index(o, f)];
            q.cap = uint8_t(o == EJECT && feed[o][f] == LOOPBACK ? loop_depth : c.queue_depth);
            q.base = uint16_t(base);
            base += q.cap;
            slot_of[o][feed[o][f]] = uint8_t(f);
        }
        // 16.3: the pointer is 0 at the reset: the first queue served is the first in the inputs' order
        rr[o] = uint8_t(nfeed[o] - 1);
    }
    if (base * 3 > sizeof store / sizeof store[0]) FATAL("router t%u: FIFO storage", tile);
}

// ref: s, d the tiles of src and dst; k = (d - s) mod 64; (b, a) = hops[k]; horizontal first until this column is b
// columns from the source's (with the sign of b), then vertical; at d, the eject (or the to-controller output for a
// controller's dst).
unsigned route_unicast(const Hops& hops, unsigned me, unsigned src, unsigned dst, const uint8_t* attach) {
    using R = Router;
    const bool to_cnc_port = lw::controller(dst);
    const int s = int(lw::tile_of(src, attach) & 63), d = int(lw::tile_of(dst, attach) & 63);
    const int k = (d - s) & 63;
    const int b = hops.b[k], a = hops.a[k];
    const int cs = s & 7, cm = me & 7;
    const int hb = b >= 0 ? (cm - cs) & 7 : -((cs - cm) & 7);
    // (b = 0 with the tile off the source's column: a packet at a tile its route does not pass, which no link delivers;
    // the RTL's unicast_output sends it east, the way of b >= 0, where ref sent it west)
    if (hb != b) return b >= 0 ? R::M_RIGHT : R::M_LEFT;
    if (int(me) != d) return a >= 0 ? R::M_DOWN : R::M_UP;      // (a = 0 off the destination: south, as the RTL's)
    return to_cnc_port ? R::M_CNC : R::M_EJ;
}

// A broadcast's outputs at tile `me`, rooted at `src`'s tile, arriving on `port` (16.2; the RTL's router.veryl,
// broadcast_outputs): one copy ejects; a copy from local or the attach port starts east and west, one from the west
// goes on east and one from the east west, until the reach (4 east, 3 west); every copy on the ring turns south and
// north, and one from the north goes on south and one from the south north, until the rows 4 below and 3 above the
// source's, rows counted around the column. (A ring copy is at most a row from its source's, where the ring passed a
// row's end, so it always turns both ways; the rows' test there is the RTL's, for a copy no link delivers.)
unsigned route_broadcast(unsigned me, unsigned port, unsigned src, const uint8_t* attach) {
    using R = Router;
    const int s = int(lw::tile_of(src, attach) & 63);
    const int cs = s & 7, cm = me & 7, rs = s >> 3, rm = me >> 3;
    const bool south = ((rm - rs) & 7) != 4, north = ((rs - rm) & 7) != 3;
    unsigned m = R::M_EJ;
    switch (port) {
    case R::FROM_UP: if (south) m |= R::M_DOWN; return m;
    case R::FROM_DOWN: if (north) m |= R::M_UP; return m;
    case R::FROM_LEFT: if (((cm - cs) & 7) != 4) m |= R::M_RIGHT; break;
    case R::FROM_RIGHT: if (((cs - cm) & 7) != 3) m |= R::M_LEFT; break;
    default: m |= R::M_RIGHT | R::M_LEFT; break;               // local, the attach port
    }
    if (south) m |= R::M_DOWN;
    if (north) m |= R::M_UP;
    return m;
}

uint64_t Router::digest() const {
    uint64_t h = sim::hash_fields(me, attached, loopback_last, ovf_event, rr, occ, ovf_now);
    for (unsigned i = 0; i < n_fifos; i++) {
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
