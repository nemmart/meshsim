// statefp_g2.cpp: G2's chip-level writers (WORKER9). See statefp_g2.h.
#include "statefp_g2.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "config.h"

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

// StateFp "reqbus" with the bus idle (RequestBus's reset state; nothing in M2 sends a request).
void w_reqbus(const void*, FpOut& o, const FpCtx&) {
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

void register_g2(FpRegistry& r, const G2Chip& g) {
    r.add("cnc", w_cnc, &g);
    r.add("cc1p", w_cc1p, &g);
    r.add("reqbus", w_reqbus, &g);
    r.add("ep", w_ep, &g);
}

}  // namespace fp
