// statefp_g2.cpp: G2's chip-level writers (WORKER9). See statefp_g2.h.
#include "statefp_g2.h"

#include "reqbus_fp.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "config.h"
#include "units/chip.h"

namespace fp {
namespace {


// One pending line word: visible at `vis`, tile k, its READY bit and its error level.
struct Pending { uint64_t vis; unsigned k; bool ready, level; };

// Every line word on its way to C&C at the end of x.cycle, in ref's insertion order (visible cycle, then tile).
std::vector<Pending> pending(const G2Chip& g, const FpCtx& x) {
    std::vector<Pending> p;
    if (!x.sim) { std::fprintf(stderr, "statefp_g2: no machine to observe\n"); std::abort(); }
    sim::PortEntry e[64];
    for (unsigned r = 0; r < 8; r++) {
        sim::PortView v = x.sim->observe(g.cnc->rows[r], e, 64);     // collector -> C&C: arrival = visible
        for (uint32_t i = 0; i < v.n; i++)
            for (unsigned b = 0; b < 8; b++)
                if (e[i].w[0] >> (units::RowCollector::kPresent + b) & 1)
                    p.push_back({e[i].at, 8 * r + b, bool(e[i].w[0] >> (units::RowCollector::kEdges + b) & 1),
                                 bool(e[i].w[0] >> (units::RowCollector::kLevels + b) & 1)});
        for (unsigned b = 0; b < 8; b++) {                           // block -> collector: visible one cycle after arrival
            sim::PortView u = x.sim->observe(g.rows[r]->line[b], e, 64);
            for (uint32_t i = 0; i < u.n; i++) p.push_back({e[i].at + 1, 8 * r + b, bool(e[i].w[0] & 1), bool(e[i].w[0] >> 1 & 1)});
        }
    }
    if (g.edge)                                                       // the blocks' endCycle(c), not run yet
        for (unsigned k = 0; k < 64; k++) {
            LineEdge le = g.edge(g.edge_ctx, k, x.cycle);
            if (le.put) p.push_back({x.cycle + g.ready_latency, k, le.ready, le.level});
        }
    std::stable_sort(p.begin(), p.end(), [](const Pending& a, const Pending& b) { return a.vis != b.vis ? a.vis < b.vis : a.k < b.k; });
    return p;
}

void fifo_port(FpOut& o, const char* nm, const sim::Sim* s, const sim::FifoIn& port) {
    sim::PortEntry e[256];
    sim::PortView v = s->observe(port, e, 256);
    BusEntry b[256];
    for (uint32_t i = 0; i < v.n; i++) b[i] = {0, e[i].w[0], e[i].w[1], e[i].w[2]};
    BusEntry put{0, v.put_w[0], v.put_w[1], v.put_w[2]};
    fifo(o, nm, b, v.n, v.put ? &put : nullptr);
}

}  // namespace

// StateFp "cnc": steps, lastGoAt, lastCommandWord, the parity bits, the receive FIFO, the error bit, the OR'd line as
// seen and as last recorded with its pending changes, then the endpoint's receive bus.
void w_cnc(const void* p, FpOut& o, const FpCtx& x) {
    const G2Chip& g = *static_cast<const G2Chip*>(p);
    const units::Cnc& c = *g.cnc;
    o.w("steps", c.steps_queued());
    o.w("lastGoAt", uint64_t(c.last_go_at));
    o.w("lastCommandWord", c.last_command_word);
    o.w("parity", c.parity);
    o.w("rxq.n", c.rx_count);
    for (uint32_t i = 0; i < c.rx_count; i++)
        for (unsigned k = 0; k < 5; k++) o.w("rxq", c.rxq[(c.rx_head + i) % units::Cnc::kRx][k]);
    o.w("errorBit", c.error_bit);
    o.w("errLineSeen", c.err_line_seen);
    // the OR'd line's changes on their way (CnC.errLine): replay the tiles' pending levels by visible cycle
    std::vector<Pending> pd = pending(g, x);
    uint64_t lv = c.err_line;
    bool last = c.err_line_seen;
    std::vector<std::pair<uint64_t, bool>> ch;
    for (size_t i = 0; i < pd.size();) {
        const uint64_t vis = pd[i].vis;
        for (; i < pd.size() && pd[i].vis == vis; i++) lv = (lv & ~(1ull << pd[i].k)) | uint64_t(pd[i].level) << pd[i].k;
        const bool orl = lv != 0;
        if (orl != last) { ch.push_back({vis, orl}); last = orl; }
    }
    o.w("errLineLast", last);
    o.w("errLine.n", ch.size());
    for (auto& e : ch) { o.w("errLine", e.first); o.w("errLine", e.second); }
    fifo_port(o, "rx", x.sim, c.rx);
}

// StateFp "cc1p": READY per tile (1 visible, its arrival cycle if pending, 0 none), READY_EXPECT, the error changes on
// their way (Cnc1Peripheral.errChanges).
void w_cc1p(const void* p, FpOut& o, const FpCtx& x) {
    const G2Chip& g = *static_cast<const G2Chip*>(p);
    const units::Cnc& c = *g.cnc;
    std::vector<Pending> pd = pending(g, x);
    uint64_t pend_ready[64] = {};
    for (const Pending& e : pd) if (e.ready) pend_ready[e.k] = e.vis;           // ref keeps the latest
    for (unsigned k = 0; k < 64; k++) {
        uint64_t v;
        if (pend_ready[k]) v = pend_ready[k];
        else v = c.ready(k, int64_t(x.cycle)) ? 1 : 0;
        o.w("ready", v);
    }
    o.w("expect", c.expect);
    uint64_t lv = c.err_line;
    std::vector<Pending> chg;
    for (const Pending& e : pd)
        if (bool(lv >> e.k & 1) != e.level) { chg.push_back(e); lv ^= 1ull << e.k; }
    o.w("errChanges.n", chg.size());
    for (const Pending& e : chg) { o.w("errChanges", e.vis); o.w("errChanges", e.k); o.w("errChanges", e.level); }
}

// StateFp "reqbus": W14's writer (reqbus_fp.h) when the chip has the bus; else the bus idle (RequestBus's reset state).
void w_reqbus(const void* p, FpOut& o, const FpCtx& x) {
    const G2Chip& g = *static_cast<const G2Chip*>(p);
    if (g.reqbus && x.sim) { reqfp::reqbus(o, *x.sim, *g.reqbus, x.cycle); return; }
    for (unsigned b = 0; b < 64 + 11; b++) { o.w("occ", 0); o.w("reqs", 0); o.w("arrivals", 0); }
    for (unsigned r = 0; r < 8; r++) { o.w("rowCur", uint64_t(-1)); o.w("rowRr", 7); }
    o.w("rootCur", uint64_t(-1)); o.w("rootRr", 10); o.w("rootDropping", 0);
    const uint64_t credits[8] = {128, 256, 128, 0, 0, 0, 0, 0};   // Config.receiveQueueDepth
    for (uint64_t c : credits) o.w("credits", c);
    for (unsigned k = 0; k < 3; k++) o.w("queue.n", 0);
    o.w("creditReturns.n", 0);
    o.w("toReceiver.n", 0);
}

void w_ep(const void* p, FpOut& o, const FpCtx& x) {
    const G2Chip& g = *static_cast<const G2Chip*>(p);
    fifo_port(o, "memsub.rx", x.sim, g.ep[0]->rx);
    fifo_port(o, "cc3.rx", x.sim, g.ep[1]->rx);
}

// ---- the tile's units -------------------------------------------------------------------------------------------------
G2Tile g2_tile(sim::Sim& s, unsigned k) {
    G2Tile g;
    g.tile = k;
    g.router = &s.unit<units::Router>(units::tile_unit(k, "router"));
    g.inj = &s.unit<units::Injection>(units::tile_unit(k, "inj"));
    return g;
}

namespace {
void link_port(FpOut& o, const char* nm, const sim::Sim* s, const sim::LinkIn& port) {
    sim::PortEntry e[64];
    sim::PortView v = s->observe(port, e, 64);
    BusEntry b[64];
    for (uint32_t i = 0; i < v.n; i++) b[i] = {e[i].at, e[i].w[0], e[i].w[1], e[i].w[2]};
    link(o, nm, b, v.n);
}
void hold_port(FpOut& o, const char* nm, const sim::Sim* s, const sim::HoldIn& port) {
    sim::PortEntry e[4];
    sim::PortView v = s->observe(port, e, 4);
    BusEntry held{0, e[0].w[0], e[0].w[1], e[0].w[2]}, put{0, v.put_w[0], v.put_w[1], v.put_w[2]};
    hold(o, nm, v.valid ? &held : nullptr, v.put ? &put : nullptr);
}
}  // namespace

void w_router(const void* p, FpOut& o, const FpCtx& x) {
    const G2Tile& g = *static_cast<const G2Tile*>(p);
    const units::Router& r = *g.router;
    r.fp_own(o, x.cycle);
    static const char* const kLink[4] = {"link0", "link1", "link2", "link3"};
    for (unsigned d = 0; d < 4; d++) link_port(o, kLink[d], x.sim, r.in[d]);
    link_port(o, "inj", x.sim, r.inj);
    if (r.attached) hold_port(o, "fromCnc", x.sim, r.from_cnc);
}

// StateFp "inj": the read-return holding register (W11: the read queue's, inside the unit: empty at the end of c, the
// injection takes it the cycle it is visible; a release of c listed as the put), then the send hold (observer).
void w_inj(const void* p, FpOut& o, const FpCtx& x) {
    const G2Tile& g = *static_cast<const G2Tile*>(p);
    const units::InjReadSide& rs = g.inj->rs;
    if (rs.cur_v || (rs.nxt_v && rs.nxt_at != x.cycle)) {
        std::fprintf(stderr, "statefp_g2: t%u.inj: the read-return hold out of step at %llu\n", g.tile, (unsigned long long)x.cycle);
        std::abort();
    }
    BusEntry put{0, rs.nxt_w[0], rs.nxt_w[1], rs.nxt_w[2]};
    hold(o, "readReturn", nullptr, rs.nxt_v ? &put : nullptr);
    hold_port(o, "send", x.sim, g.inj->send);
}

// StateFp "srq" (W11): the free list in order; every held token's slot in token order (reqA, reqB, issueT, readyT
// (Long.MAX_VALUE until ready), partsLeft, client, lo, hi, zero); the two in-order queues; the read port's ready set;
// held (reads and loopback tokens, less a loopback free arriving at c + 1); the clients' buses; the area returns.
void w_srq(const void* p, FpOut& o, const FpCtx& x) {
    const G2Tile& g = *static_cast<const G2Tile*>(p);
    const units::Injection& in = *g.inj;
    const units::InjReadSide& rs = in.rs;
    const unsigned cap = in.tok.cap;
    if (!rs.cfg.present) {                                                  // no read side: free 0..cap-1, nothing held
        if (in.tok.reads) { std::fprintf(stderr, "statefp_g2: t%u.srq: reads held without a read side\n", g.tile); std::abort(); }
        o.w("free.n", cap);
        for (unsigned k = 0; k < cap; k++) o.w("free", k);
    } else {
        o.w("free.n", rs.free_n);
        for (unsigned k = 0; k < rs.free_n; k++) o.w("free", rs.free_q[(rs.free_head + k) % units::InjReadSide::MAX]);
        for (unsigned t = 0; t < cap; t++) {
            if (!rs.held_slot[t]) continue;
            o.w("tok", t); o.w("reqA", rs.reqA[t]); o.w("reqB", rs.reqB[t]); o.w("issueT", rs.issueT[t]);
            o.w("readyT", rs.readyT[t] == ~0ull ? 0x7FFFFFFFFFFFFFFFull : rs.readyT[t]);
            o.w("partsLeft", rs.parts[t]); o.w("client", rs.cl[t]); o.w("lo", rs.lo[t]); o.w("hi", rs.hi[t]); o.w("zero", rs.zero[t]);
        }
    }
    for (unsigned c = 0; c < 2; c++) {
        const unsigned n = rs.cfg.present ? rs.ord_n[c] : 0;
        o.wpi("order", c, ".n", n);
        for (unsigned i = 0; i < n; i++) o.wi("order", c, rs.ord[c][(rs.ord_head[c] + i) % units::InjReadSide::MAX]);
    }
    const unsigned rn = rs.cfg.present ? rs.rp_n : 0;
    o.w("rpReady.n", rn);
    for (unsigned i = 0; i < rn; i++) { o.w("rpReady.t", rs.rp[i].t); o.w("rpReady.tok", rs.rp[i].tok); }
    sim::PortEntry e[64];
    sim::PortView v = x.sim->observe(in.loop_left, e, 64);
    uint64_t freeing = 0;
    for (uint32_t i = 0; i < v.n; i++) if (e[i].at > x.cycle) freeing++;   // a free of c, applied at c + 1
    o.w("held", in.tok.reads + in.tok.loop_held - freeing);
    // the clients (gather, AG: Holds; the read port: a Fifo; an absent one is empty: the two empty streams agree)
    for (unsigned c = 0; c < 2; c++) {
        const bool has = rs.cfg.present && (c == 0 ? rs.cfg.gather : rs.cfg.ag);
        if (!has) { hold_empty(o, "client"); continue; }
        sim::PortView h = x.sim->observe(rs.client_hold[c], e, 64);
        BusEntry held{0, e[0].w[0], e[0].w[1], 0}, put{0, h.put_w[0], h.put_w[1], 0};
        hold(o, "client", h.valid ? &held : nullptr, h.put ? &put : nullptr);
    }
    if (!rs.cfg.present) fifo_empty(o, "client");
    else {
        sim::PortView f = x.sim->observe(rs.rp_q, e, 64);
        BusEntry be[64], put{0, f.put_w[0], f.put_w[1], 0};
        for (uint32_t i = 0; i < f.n; i++) be[i] = BusEntry{0, e[i].w[0], e[i].w[1], 0};
        fifo(o, "client", be, f.n, f.put ? &put : nullptr);
    }
    for (unsigned a = 0; a < units::InjReadSide::AREAS; a++) {
        if (!rs.cfg.present) { link_empty(o, "ret"); continue; }
        sim::PortView l = x.sim->observe(rs.ret[a], e, 64);
        BusEntry be[64];
        for (uint32_t i = 0; i < l.n; i++) be[i] = BusEntry{e[i].at, e[i].w[0], e[i].w[1], e[i].w[2]};
        link(o, "ret", be, l.n);
    }
}

void register_g2_tile(FpRegistry& r, const G2Tile& g) {
    r.add(units::tile_unit(g.tile, "router"), w_router, &g);
    r.add(units::tile_unit(g.tile, "inj"), w_inj, &g);
    r.add(units::tile_unit(g.tile, "srq"), w_srq, &g);
}

void register_g2(FpRegistry& r, const G2Chip& g) {
    r.add("cnc", w_cnc, &g);
    r.add("cc1p", w_cc1p, &g);
    r.add("reqbus", w_reqbus, &g);
    r.add("ep", w_ep, &g);
}

}  // namespace fp
