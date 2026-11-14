// statefp_g2.cpp: G2's chip-level writers. See statefp_g2.h.
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
using Pending = PendingLine;

// Every line word on its way to C&C at the end of x.cycle, in ref's insertion order (visible cycle, then tile).
std::vector<Pending> pending(const G2Chip& g, const FpCtx& x) {
    std::vector<Pending> p;
    if (!x.sim) { std::fprintf(stderr, "statefp_g2: no machine to observe\n"); std::abort(); }
    sim::PortEntry e[64];
    for (unsigned r = 0; r < 8; r++) {
        sim::PortView v = x.sim->observe(g.cnc ? g.cnc->rows[r] : g.cc_rows[r], e, 64);   // collector -> C&C: arrival = visible
        for (uint32_t i = 0; i < v.n; i++)
            for (unsigned b = 0; b < 8; b++)
                if (e[i].w[0] >> (units::RowCollector::kPresent + b) & 1)
                    p.push_back({e[i].at, 8 * r + b, bool(e[i].w[0] >> (units::RowCollector::kEdges + b) & 1),
                                 bool(e[i].w[0] >> (units::RowCollector::kLevels + b) & 1)});
        for (unsigned b = 0; b < 8; b++) {                           // block -> collector: visible one cycle after arrival
            // (v1.36: a line word is the tile control unit's two nets, put together: the level's word, and READY's edge
            // when BARRIER_ARRIVE is on its link for the same cycle)
            sim::PortEntry a[64];
            const sim::PortView ua = x.sim->observe(g.rows[r]->arrive[b], a, 64);
            sim::PortView u = x.sim->observe(g.rows[r]->error[b], e, 64);
            for (uint32_t i = 0; i < u.n; i++) {
                bool edge = false;
                for (uint32_t j = 0; j < ua.n; j++) edge |= a[j].at == e[i].at;
                p.push_back({e[i].at + 1, 8 * r + b, edge, bool(e[i].w[0] & 1)});
            }
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

std::vector<PendingLine> pending_lines(const G2Chip& g, const FpCtx& x) { return pending(g, x); }

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

// StateFp "reqbus": the writer (reqbus_fp.h) when the chip has the bus; else the bus idle (RequestBus's reset state).
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
    r.fp_own(o, x.cycle - x.label_shift);                             // ref's cyc (M3: puts of c + 1 not yet in)
    static const char* const kLink[4] = {"link0", "link1", "link2", "link3"};
    if (r.cc) {                                                       // a special tile's router: its attach port, then
        link_port(o, kLink[0], x.sim, r.from_cc);                    // its DMA inputs (v1.36: no ref's stream to equal)
        for (unsigned d = 0; d < 3; d++)
            if (r.dma_inputs >> d & 1) link_port(o, kLink[d + 1], x.sim, r.in[d]); else none(o, kLink[d + 1]);
        link_port(o, "inj", x.sim, r.inj);                           // ref's fromCnc is null on a controller
        return;
    }
    for (unsigned d = 0; d < 4; d++) link_port(o, kLink[d], x.sim, r.in[d]);
    link_port(o, "inj", x.sim, r.inj);
    if (r.attached && r.fifth_link) link_port(o, "fromCnc", x.sim, r.from_cc);   // C&C as cores
    else if (r.attached) hold_port(o, "fromCnc", x.sim, r.from_cnc);
}

// StateFp "inj" and "srq" were ref's fingerprints of v1.35's read queue (its tokens' slots, its free list, its two
// in-order queues, the read-return holding register, the area returns). v1.36's inject unit has none of them (14, 15:
// a counted token pool, one return line of READ_LATENCY stages, no holding register), so the two streams cannot be
// written: the writers stop. Their only users are the pending fingerprint tests (PENDING-v1.36.md, the platform
// stage, section 3).
void w_inj(const void*, FpOut&, const FpCtx&) {
    std::fprintf(stderr, "statefp_g2: 'inj': v1.35's read-return holding register is not in v1.36's inject unit (PENDING-v1.36.md)\n");
    std::abort();
}
void w_srq(const void*, FpOut&, const FpCtx&) {
    std::fprintf(stderr, "statefp_g2: 'srq': v1.35's read queue is not in v1.36's inject unit (PENDING-v1.36.md)\n");
    std::abort();
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
