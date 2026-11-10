// statefp_m3.cpp (WORKER16): the M3 chip's fingerprint. See statefp_m3.h.
#include "statefp_m3.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "units/chip.h"
#include "units/cnc1_periph.h"
#include "units/tracer.h"

namespace fp {

thread_local bool g_dma_pulse_at_point = false;

namespace {

LineEdge line_edge_m3(const void* ctx, unsigned k, uint64_t c) {
    const G1Tile& g = static_cast<const G1Tile*>(ctx)[k];
    const units::LineEdge e = units::line_edge(*g.sim, *g.uctl, c);
    LineEdge le;
    le.put = e.put; le.ready = e.word & 1; le.level = e.word >> 1 & 1;
    return le;
}

// The core's ops on the slot's S2/S3 Link at the end of t: put at ref's t (framework t + 1), arriving at now().
struct OpsAtT { uint32_t n = 0; uint64_t op[8]; };
OpsAtT ops_at(const sim::Sim& s, const units::PeriphSlot& sl) {
    OpsAtT r;
    sim::PortEntry e[8];
    sim::PortView v = s.observe(sl.core, e, 8);
    for (uint32_t i = 0; i < v.n && r.n < 8; i++) if (e[i].at == s.now()) r.op[r.n++] = e[i].w[0];
    if (std::getenv("W16_OPS")) for (uint32_t i = 0; i < v.n; i++) std::fprintf(stderr, "ops: at %llu now %llu op %llx\n", (unsigned long long)e[i].at, (unsigned long long)s.now(), (unsigned long long)e[i].w[0]);
    return r;
}

// 6.7: per peripheral present: EVENTS_MASK; the DMA (with output queues); the consume and the EVENTS_MASK store of t.
// The slot's own store effects of t (DMA_HDR, DMA_ADDR) are applied in ref: written as applied.
void w_slot(const void* p, FpOut& o, const FpCtx& x) {
    const units::PeriphSlot& sl = *static_cast<const units::PeriphSlot*>(p);
    units::SlotCore& s = const_cast<units::SlotCore&>(sl.s);
    const OpsAtT ops = ops_at(*x.sim, sl);
    for (unsigned n = 1; n <= 3; n++) {
        if (s.per[n].type == units::PT_ABSENT) continue;
        o.wi("mask", n, s.mask[n]);
        bool consume = false, mstore = false, pulse = false;
        uint32_t cbits = 0, mval = 0, hdr = s.dma[n].hdr, addr = s.dma[n].addr;
        for (uint32_t i = 0; i < ops.n; i++) {
            const uint64_t op = ops.op[i];
            if (units::CoreOp::n(op) != n) continue;
            if (units::CoreOp::kind(op) == units::CoreOp::CONSUME) { consume = true; cbits = units::CoreOp::value(op); continue; }
            switch (units::CoreOp::word(op)) {
            case 7: mstore = true; mval = units::CoreOp::value(op); break;
            case 8: hdr = units::CoreOp::value(op); break;
            case 9: addr = units::CoreOp::value(op); break;
            case 4: pulse = units::CoreOp::value(op) & 1; break;   // SlotCore::dma_start, as ref applied it at t
            default: break;
            }
        }
        if (s.per[n].type == units::PT_TRACER) {                  // the peripherals with output queues (ref: the Tracer)
            const units::SlotCore::Dma& d = s.dma[n];
            const bool busy = pulse || d.busy;
            const int64_t left = pulse ? int64_t(hdr & 0xFFF) : d.left;
            const int q = pulse ? int((hdr >> 12) & 3) : d.q;
            if (pulse) g_dma_pulse_at_point = true;            // the peripheral's out_start of t isn't in its stream
            o.w("dma.hdr", hdr); o.w("dma.addr", addr); o.w("dma.done", d.done);
            o.w("dma.busy", busy); o.w("dma.left", uint64_t(left)); o.w("dma.q", uint64_t(int64_t(q)));
        }
        if (consume) { o.w("consume", 1); o.w("consume.bits", cbits); } else o.w("consume", 0);
        if (mstore) { o.w("maskStore", 1); o.w("maskStore.v", mval); } else o.w("maskStore", 0);
    }
}

// 6.8: W15's (asked); a placeholder until then.
void w_rp(const void*, FpOut& o, const FpCtx&) { o.w("rp-not-yet", 0); }

struct TrRef { const units::tracer::Tracer* tr; int unit; };
TrRef g_tr[5];
void w_tr(const void* p, FpOut& o, const FpCtx& x) {
    const TrRef& r = *static_cast<const TrRef*>(p);
    r.tr->fingerprint(r.unit, o, int64_t(x.cycle));
}

// 6.9 cc1p: C&C 1's peripheral. READY per tile (0 none or cleared, 1 visible at t, else its arrival), expect, the error
// changes on their way (visible-at, id, level), errLine as two words, the watchdog, the toast, the LED, the UART, the
// events' counts. In flight at the end of t and applied in ref already: C&C 1's stores of t (READY_EXPECT, PULSE_GO:
// on its slot's Link); the line words on their way (tiles: pending_lines; C&C 2's and 3's: their Links and their blocks'
// endCycle(t)).
struct Cc1pRef { const M3Chip* c; };
Cc1pRef g_cc1p;
void w_cc1p_m3(const void* p, FpOut& o, const FpCtx& x) {
    const M3Chip& c = *static_cast<const Cc1pRef*>(p)->c;
    const units::PeriphSlot& sl = *c.slot[64];
    const units::Cnc1Peripheral& cp = *static_cast<const units::Cnc1Peripheral*>(sl.s.per[1].p);
    const int64_t t = int64_t(x.cycle);
    uint64_t expect = cp.expect;
    int64_t cleared = cp.cleared_at;
    uint64_t wd_count = cp.wd_count, wd_n = cp.wd_n;
    uint32_t led = cp.led;
    const OpsAtT ops = ops_at(*x.sim, sl);
    for (uint32_t i = 0; i < ops.n; i++) {
        const uint64_t op = ops.op[i];
        if (units::CoreOp::n(op) != 1 || units::CoreOp::kind(op) != units::CoreOp::STORE) continue;
        const uint32_t v = units::CoreOp::value(op);
        switch (units::CoreOp::word(op)) {
        case 37: expect = (expect & ~0xFFFFFFFFull) | v; break;
        case 38: expect = (expect & 0xFFFFFFFFull) | uint64_t(v) << 32; break;
        case 39: if (v & 1) { cleared = t + 1; if (wd_n) wd_count = wd_n; } break;
        case 40: wd_n = v; wd_count = v; break;                 // D33: ref's own work of t (the watchdog's count) differs
        case 41: led = v & 1; break;
        default: break;
        }
    }
    // the tiles' lines on their way, and the arrivals of t not yet taken (begin(t + 1) reads them)
    FpCtx xm = x;                                              // pending_lines reads the framework's Links at now()
    xm.cycle = x.cycle + 1;
    std::vector<PendingLine> pd = pending_lines(c.gc, xm);
    for (PendingLine& e : pd) e.vis -= 1;                      // the framework's arrival cycles in ref's
    int64_t ready_at[64];
    for (unsigned k = 0; k < 64; k++) ready_at[k] = cp.ready_at[k];
    for (const PendingLine& e : pd) if (e.ready) ready_at[e.k] = std::max(ready_at[e.k], int64_t(e.vis));
    for (unsigned k = 0; k < 64; k++) {
        const int64_t a = ready_at[k];
        o.wi("ready", k, a == INT64_MIN || a < cleared ? 0 : a <= t ? 1 : uint64_t(a));
    }
    o.w("expect", expect);
    // errChanges: the tiles' (changes against the level as it will stand), then the controllers' (C&C 2, 3) line words
    struct Ch { uint64_t vis, id, level; };
    std::vector<Ch> ch;
    uint64_t lv[2] = {cp.err_line[0], cp.err_line[1]};
    for (unsigned i = 0; i < cp.err_due_n; i++) {              // arrived at t + 1's begin? no: due at t, applied by step(t)
        std::fprintf(stderr, "statefp_m3: cc1p: error-line changes due but not applied at %lld\n", (long long)t);
        std::abort();
    }
    std::vector<PendingLine> all = pd;
    for (unsigned cc = 1; cc < 3; cc++) {                      // C&C 2's and 3's endCycle(t), not on their Links yet
        const LineEdge le = line_edge_m3(c.g1, 64 + cc, xm.cycle);
        if (le.put) all.push_back({xm.cycle + c.gc.ready_latency - 1, 64 + cc, false, le.level});
    }
    sim::PortEntry e[64];
    for (unsigned cc = 1; cc < 3; cc++) {                      // C&C 2, 3: the block -> C&C 1 Link (arrival = visible)
        sim::PortView v = x.sim->observe(sl.own.link_in[cp.line0 + cc], e, 64);
        for (uint32_t i = 0; i < v.n; i++) all.push_back({e[i].at - 1, 64 + cc, false, bool(e[i].w[0] >> 1 & 1)});
    }
    std::stable_sort(all.begin(), all.end(), [](const PendingLine& a, const PendingLine& b) { return a.vis != b.vis ? a.vis < b.vis : a.k < b.k; });
    for (const PendingLine& p : all) {
        const uint64_t b = 1ull << (p.k & 63);
        const bool cur = lv[p.k >> 6] & b;
        if (cur != p.level) { ch.push_back({p.vis, p.k, p.level}); lv[p.k >> 6] ^= b; }
    }
    o.w("errChanges.n", ch.size());
    for (const Ch& z : ch) { o.w("errChanges", z.vis); o.w("errChanges", z.id); o.w("errChanges", z.level); }
    o.w("errLine0", cp.err_line[0]); o.w("errLine1", cp.err_line[1]);
    o.w("wdCount", wd_count); o.w("wdN", wd_n); o.w("toast", cp.toast); o.w("led", led);
    o.w("tx.n", cp.tx_n);
    for (unsigned i = 0; i < cp.tx_n; i++) o.w("tx", cp.tx[(cp.tx_head + i) % units::Cnc1Peripheral::FIFO]);
    o.w("rx.n", cp.rx_n);
    o.w("divisor", cp.divisor); o.w("txDivisor", cp.tx_divisor); o.w("rxDivisor", cp.rx_divisor);
    o.w("txBusyUntil", uint64_t(std::max(cp.tx_busy_until, t))); o.w("rxOverrun", cp.rx_overrun); o.w("framing", cp.framing);
    for (unsigned b = 0; b < 6; b++) o.wi("ev", b, cp.ev.count[b]);
}

// cc.host: per controller, the host queue (count, then each packet's three words).
void w_cchost(const void* p, FpOut& o, const FpCtx&) {
    const M3Chip& c = *static_cast<const M3Chip*>(p);
    for (unsigned k = 0; k < 3; k++) {
        const units::HostPort& h = *c.host[k];
        o.w("hostQ.n", h.count);
        for (uint32_t i = 0; i < h.count; i++)
            for (unsigned w = 0; w < 3; w++) o.w("hostQ", h.q[size_t((h.head + i) % h.capacity) * 3 + w]);
    }
}

// cnc, ep: ref's Java model of C&C and its endpoints, built and otherwise idle with cores: C&C 1's PULSE_GO drives
// GO through CnC.goNet, which sets lastGoAt (-1 before any; a PULSE_GO stored at t, in flight to the slot, is set);
// lastCommandWord stays -1.
void w_cnc_idle(const void* p, FpOut& o, const FpCtx& x) {
    const M3Chip& c = *static_cast<const M3Chip*>(p);
    const units::PeriphSlot& sl = *c.slot[64];
    const units::Cnc1Peripheral& cp = *static_cast<const units::Cnc1Peripheral*>(sl.s.per[1].p);
    int64_t go = cp.gos ? int64_t(cp.last_go_store) : -1;
    const OpsAtT ops = ops_at(*x.sim, sl);
    for (uint32_t i = 0; i < ops.n; i++)
        if (units::CoreOp::n(ops.op[i]) == 1 && units::CoreOp::kind(ops.op[i]) == units::CoreOp::STORE &&
            units::CoreOp::word(ops.op[i]) == 39 && (units::CoreOp::value(ops.op[i]) & 1)) go = int64_t(x.cycle);
    o.w("steps", 0); o.w("lastGoAt", uint64_t(go)); o.w("lastCommandWord", 0xFFFFFFFFu); o.w("parity", 0);
    o.w("rxq.n", 0); o.w("errorBit", 0); o.w("errLineSeen", 0); o.w("errLineLast", 0); o.w("errLine.n", 0);
    fifo_empty(o, "rx");
}
void w_ep_idle(const void*, FpOut& o, const FpCtx&) { fifo_empty(o, "memsub.rx"); fifo_empty(o, "cc3.rx"); }


// ref's idle-skip sleep at the end of t (Chip.asleep: Tile.canSleep held), judged on the framework's state: the core is
// in a WFI (the writer's caller checks), and nothing of the tile is in motion: no router FIFO occupied, no word on its
// way into the router, the director's K-line or the send hold, nothing on the slot's core Link, the slot idle.
bool ref_asleep_m3(const void* ctx, unsigned k, const FpCtx& x) {
    const M3Chip& c = *static_cast<const M3Chip*>(ctx);
    const sim::Sim& s = *x.sim;
    sim::PortEntry e[64];
    const units::Router& r = *c.g2[k].router;
    if (r.occ) return false;
    if (r.cc) {
        if (s.observe(r.in[units::Router::CC_LINK_IN], e, 64).n || s.observe(r.host, e, 64).n) return false;
        if (r.has_dma && s.observe(r.dma, e, 64).n) return false;
    } else {
        for (unsigned d = 0; d < 4; d++) if (s.observe(r.in[d], e, 64).n) return false;
        if (r.attached && r.fifth_link && s.observe(r.from_cc, e, 64).n) return false;
    }
    if (s.observe(r.inj, e, 64).n) return false;
    if (s.observe(c.g1[k].uctl->kline, e, 64).n) return false;
    const sim::PortView hv = s.observe(c.g2[k].inj->send, e, 4);
    if (hv.valid || hv.put) return false;
    const units::PeriphSlot& sl = *c.slot[k];
    if (s.observe(sl.core, e, 8).n) return false;
    if (!const_cast<units::SlotCore&>(sl.s).idle(int64_t(x.cycle - x.label_shift))) return false;
    return true;
}

}  // namespace

void m3_attach(sim::Sim& s, M3Chip& c, const units::reqbus::ReqBusConfig& rc) {
    c.m = &s;
    for (unsigned k = 0; k < 67; k++) {
        c.g1[k] = g1_tile(s, k);
        c.g1[k].uinj = &s.unit<units::Injection>(units::tile_unit(k, "inj"));
        c.g2[k] = g2_tile(s, k);
        c.slot[k] = &s.unit<units::PeriphSlot>(units::tile_unit(k, "slot"));
    }
    for (unsigned k = 0; k < 3; k++) c.host[k] = &s.unit<units::HostPort>(units::tile_unit(64 + k, "host"));
    c.rbu = units::reqbus::resolve(s, ~uint64_t(0), 3, rc);
    for (unsigned k = 0; k < 67; k++) c.g1[k].uleaf = c.rbu.leaf[k];
    c.gc.reqbus = &c.rbu;
    for (unsigned r = 0; r < 8; r++) c.gc.rows[r] = &s.unit<units::RowCollector>("row" + std::to_string(r));
    const units::PeriphSlot& s1 = *c.slot[64];
    const units::Cnc1Peripheral& cp = *static_cast<const units::Cnc1Peripheral*>(s1.s.per[1].p);
    c.gc.cc_rows = s1.own.link_in + cp.row0;
    c.gc.edge = line_edge_m3; c.gc.edge_ctx = c.g1;
    for (unsigned k = 0; k < 67; k++) { c.g1[k].ref_asleep = ref_asleep_m3; c.g1[k].ref_asleep_ctx = &c; }
    c.tr = c.slot[66]->s.per[1].type == units::PT_TRACER ? static_cast<const units::tracer::Tracer*>(c.slot[66]->s.per[1].p) : nullptr;
    c.names.clear();
    for (unsigned k = 0; k < 67; k++) {
        for (const char* u : kTileUnits) c.names.push_back(units::tile_unit(k, u));
        c.names.push_back(units::tile_unit(k, "slot"));
        if (k < 64) c.names.push_back(units::tile_unit(k, "rp"));
    }
    if (c.tr) for (const char* u : {"tr", "tr.ic", "tr.dc", "tr.mem", "tr.files"}) c.names.push_back(u);
    for (const char* u : {"cc1p", "cc.host", "reqbus", "cnc", "ep"}) c.names.push_back(u);
}

void m3_register(FpRegistry& r, M3Chip& c) {
    for (unsigned k = 0; k < 67; k++) {
        register_g1(r, c.g1[k]);
        register_g2_tile(r, c.g2[k]);
        r.add(units::tile_unit(k, "slot"), w_slot, c.slot[k]);
        if (k < 64) r.add(units::tile_unit(k, "rp"), w_rp, c.slot[k]);
    }
    if (c.tr) {
        static const char* kTr[5] = {"tr", "tr.ic", "tr.dc", "tr.mem", "tr.files"};
        for (int u = 0; u < 5; u++) { g_tr[u] = {c.tr, u}; r.add(kTr[u], w_tr, &g_tr[u]); }
    }
    g_cc1p.c = &c;
    r.add("cc1p", w_cc1p_m3, &g_cc1p);
    r.add("cc.host", w_cchost, &c);
    r.add("reqbus", w_reqbus, &c.gc);
    r.add("cnc", w_cnc_idle, &c);
    r.add("ep", w_ep_idle, nullptr);
}

bool m3_own_writer(const std::string& n) {
    auto ends = [&](const char* s) { const size_t l = std::strlen(s); return n.size() >= l && n.compare(n.size() - l, l, s) == 0; };
    return ends(".slot") || ends(".rp") || n.rfind("tr", 0) == 0 || n == "cc1p" || n == "cc.host" || n == "cnc" || n == "ep";
}

void m3_point(const FpRegistry& r, const M3Chip& c, const FpCtx& x, std::vector<std::string>& out, unsigned m2_offset) {
    std::vector<uint64_t> hs;
    char b[128];
    for (const std::string& n : c.names) {
        FpCtx y = x;
        if (!m3_own_writer(n)) { y.cycle += m2_offset; y.label_shift = m2_offset; }
        g_hide_last_put = y.label_shift != 0;
        const uint64_t h = r.hash(n, y);
        g_hide_last_put = false;
        hs.push_back(h);
        std::snprintf(b, sizeof b, "%llu %s %016llx", (unsigned long long)x.cycle, n.c_str(), (unsigned long long)h);
        out.push_back(b);
    }
    std::snprintf(b, sizeof b, "%llu chip %016llx", (unsigned long long)x.cycle, (unsigned long long)combine(hs.data(), hs.size()));
    out.push_back(b);
}

}  // namespace fp
