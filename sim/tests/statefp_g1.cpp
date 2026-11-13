// statefp_g1.cpp: G1's fingerprint writers (statefp_g1.h), StateFp.java's streams word for word.
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "reqbus_fp.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "units/ag.h"
#include "units/chip.h"
#include "units/gather.h"
#include "units/write_engine.h"

namespace fp {

namespace {
const G1Tile& tile(const void* p) { return *static_cast<const G1Tile*>(p); }
constexpr uint32_t kMax = 64;

// The entries of one input port (a Link's in arrival order, in flight included).
template <class Port>
uint32_t entries(const G1Tile& g, const Port& port, sim::PortEntry* buf) { return g.sim->observe(port, buf, kMax).n; }

// The register block as ref has it at the end of c (units::block_at_end, tile_ctl.h).
units::RegBlockState block_end(const G1Tile& g, uint64_t c) {
    if (g.sim && g.uctl) return units::block_at_end(*g.sim, *g.uctl, c);
    if (g.sim && g.m1ctl) return units::block_at_end(*g.sim, *g.m1ctl, c);
    return g.ctl ? *g.ctl : units::RegBlockState{};
}
// the engines' copies (W20: equal to the registers until a reset clears only those; ref's engines keep theirs)
uint32_t dma_config(const G1Tile& g, uint64_t c) { return block_end(g, c).dma_master; }
uint32_t record_base(const G1Tile& g, uint64_t c) { return block_end(g, c).record_master; }

// A memory's sum with the writes landing at c + 1 overlaid (ref committed them at the end of c).
struct Overlay {
    uint32_t idx[32], val[32]; unsigned n = 0;
    void put(uint32_t i, uint32_t v) {
        for (unsigned k = 0; k < n; k++) if (idx[k] == i) { val[k] = v; return; }
        idx[n] = i; val[n] = v; n++;
    }
    uint64_t sum(uint32_t region, const uint32_t* m, size_t words) const {
        uint64_t s = MemSum::scan(region, m, words);
        for (unsigned k = 0; k < n; k++) {
            if (m[idx[k]]) s -= term(region, idx[k], m[idx[k]]);
            if (val[k]) s += term(region, idx[k], val[k]);
        }
        return s;
    }
};
}  // namespace

G1Tile g1_tile(sim::Sim& s, unsigned k) {
    const std::string t = units::unit_prefix(k);           // W16: "t<k>." or, for units 64-66, "cc1." .. "cc3."
    G1Tile g;
    g.tile = k; g.sim = &s;
    g.ucore = &s.unit<units::TileCore>(t + "core"); g.uctl = &s.unit<units::TileCtl>(t + "ctl");
    g.udir = &s.unit<units::Director>(t + "dir"); g.uts = &s.unit<units::TableStore>(t + "ts");
    g.uhist = &s.unit<units::HistStore>(t + "hist");
    g.uwe = &s.unit<units::WriteEngine>(t + "we"); g.usram = &s.unit<units::BigSram>(t + "sram");
    g.uag = &s.unit<units::AddrGen>(t + "ag"); g.ugather = &s.unit<units::Gather>(t + "gather");
    g.core = &g.ucore->rv; g.ctl = &g.uctl->s; g.ts = &g.uts->m; g.hist = &g.uhist->m;
    return g;
}

void w_core(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    const units::Rv32& k = *g.core;
    const uint64_t c = x.cycle;
    uint64_t mcycle = k.mcycle;
    uint32_t stall = k.stall;
    if (k.stop != units::Stop::running) {                               // ref's halted core: mcycle++ every cycle
        // W16: as a sleeping one, under M3's cyc one less when ref's idle-skip has put it to sleep (a halted core can)
        // (ref's idleAt == t: a core halted in the cycle just run hasn't been idle for a whole cycle yet)
        const bool asleep = x.label_shift && c > k.stop_cycle + 1 && (!g.ref_asleep || g.ref_asleep(g.ref_asleep_ctx, g.tile, x));
        mcycle += c - k.stop_cycle - (asleep ? x.label_shift : 0);
    }
    else if (c > k.last) {                                              // asleep since `last`: in a WFI or a stall
        uint64_t gap = c - k.last;
        // ref's lag: cyc - countedTo, only for a unit its idle-skip put to sleep (W16: under M3's cyc, one less)
        const bool asleep = x.label_shift && k.in_wfi && (!g.ref_asleep || g.ref_asleep(g.ref_asleep_ctx, g.tile, x));
        mcycle += gap - (asleep ? x.label_shift : 0);
        if (!k.in_wfi) stall -= uint32_t(gap);
    }
    o.w("pc", k.pc);
    for (unsigned r = 1; r < 32; r++) o.wi("x", r, k.x[r]);
    o.w("mstatus", k.mstatus); o.w("mie", k.mie); o.w("mtvec", k.mtvec); o.w("mscratch", k.mscratch);
    o.w("mepc", k.mepc); o.w("mcause", k.mcause); o.w("mtval", k.mtval);
    o.w("mcycle", mcycle);
    o.w("halt", uint64_t(int64_t(k.halt)));
    o.w("inWfi", k.in_wfi); o.w("inHandler", k.in_handler);
    o.w("stall", stall);
    o.w("sendRetry", k.pending_t == c - x.label_shift);                  // a send awaiting its acceptance at c + 1
    bool lw = k.load_wait_t == c - x.label_shift;
    o.w("regLoad", lw);
    if (lw) o.w("regLoadPc", k.load_wait_pc);
}

// The two memories at the end of c, port B's writes landing at c + 1 overlaid; `bus`: port B's later entries.
static void memories(const G1Tile& g, FpOut& o, uint64_t c, bool bus) {
    const units::Rv32& k = *g.core;
    Overlay od, op;
    sim::PortEntry e[kMax];
    BusEntry later[kMax];
    uint32_t nl = 0;
    if (g.sim && g.ucore) {                                             // M2: link words, Link(cm_latency + 1)
        for (uint32_t i = 0, n = entries(g, g.ucore->port_b, e); i < n; i++) {
            if (e[i].at > c + 1) { later[nl++] = BusEntry{e[i].at - 1, e[i].w[0], e[i].w[1], e[i].w[2]}; continue; }
            uint64_t h = e[i].w[1];
            uint32_t a = units::lw::addr(h), cnt = units::lw::chunks(units::lw::size(h));
            uint32_t w0 = (units::lw::offset(a) & (units::MEM_BYTES - 1)) >> 2;
            Overlay& ov = units::lw::region(a) == units::lw::R_DMEM ? od : op;
            for (uint32_t j = 0; j < cnt; j++)
                ov.put((w0 + j) & (units::MEM_WORDS - 1), uint32_t((j < 2 ? e[i].w[0] : e[i].w[2]) >> (32 * (j & 1))));
        }
    } else if (g.sim && g.m1core) {                                     // M1's bench: {addr | n << 32, v0 | v1 << 32, ...}
        for (uint32_t i = 0, n = entries(g, g.m1core->memw, e); i < n; i++) {
            if (e[i].at != c + 1) continue;
            uint32_t a = uint32_t(e[i].w[0]), cnt = uint32_t(e[i].w[0] >> 32);
            uint32_t w0 = (units::offset(a) & (units::MEM_BYTES - 1)) >> 2;
            Overlay& ov = units::region(a) == units::R_PMEM ? op : od;
            for (uint32_t j = 0; j < cnt; j++)
                ov.put((w0 + j) & (units::MEM_WORDS - 1), uint32_t(e[i].w[1 + j / 2] >> (32 * (j & 1))));
        }
    }
    o.w("dmem", od.sum(R_DMEM, k.dmem, units::MEM_WORDS));
    o.w("pmem", op.sum(R_PMEM, k.imem, units::MEM_WORDS));
    if (bus) link(o, "portB", later, nl);
}
void w_core_mem(const void* p, FpOut& o, const FpCtx& x) { memories(tile(p), o, x.cycle, false); }
void w_cmem(const void* p, FpOut& o, const FpCtx& x) { memories(tile(p), o, x.cycle, true); }

static void ctl_words(const units::RegBlockState& b, FpOut& o) {
    o.w("routerHdr", b.router_hdr); o.w("routerAddr", b.router_addr); o.w("dmaConfig", b.dma_config);
    o.w("recordBase", b.record_base); o.w("rdSetup", b.rd_setup);
    for (unsigned k = 0; k < 3; k++) o.wi("setW", k, b.router_w[k]);
    o.w("done", b.done); o.w("landed", b.landed); o.w("gate", b.gate); o.w("readyLevel", b.ready_level);
    o.w("events", b.events); o.w("mask", b.mask); o.w("required", b.required);
    o.w("qAll", b.q_all);
    for (unsigned k = 1; k < 8; k++) o.wi("qOne", k, b.q_one[k]);
    o.w("condAllPrev", b.cond_all_prev);
    for (unsigned k = 1; k < 8; k++) o.wi("condOnePrev", k, b.cond_one_prev[k]);
    o.w("rdDonePrev", b.rd_done_prev); o.w("requestBusyPrev", b.request_busy_prev);
    o.w("conditions", b.conditions); o.w("status", b.status);
    o.w("simExit", b.sim_exit_at != 0); o.w("simExitValue", b.sim_exit_value);
}
void w_ctl_nogo(const void* p, FpOut& o, const FpCtx& x) { ctl_words(block_end(tile(p), x.cycle), o); }
void w_ctl(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    ctl_words(block_end(g, x.cycle), o);
    sim::PortEntry e[kMax];
    uint64_t at[kMax]; uint32_t n = 0;
    if (g.sim && g.uctl)                                                 // GO's queue: effective after c, ref's cycle
        for (uint32_t i = 0, m = entries(g, g.uctl->go, e); i < m; i++) if (e[i].at > x.cycle + 1) at[n++] = e[i].at - 1;
    o.w("go.n", n);
    for (uint32_t i = 0; i < n; i++) o.w("go.at", at[i]);
}
void w_dir(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    const units::RegBlockState b = block_end(g, x.cycle);
    for (unsigned c = 1; c <= 7; c++) o.wi("remaining", c, b.remaining[c]);
    sim::PortEntry e[kMax];
    uint32_t n = 0;
    uint64_t due[kMax], ctr[kMax];
    if (g.sim && g.uctl)                                                 // the K-line: decrements due after c
        for (uint32_t i = 0, m = entries(g, g.uctl->kline, e); i < m; i++)
            if (e[i].at > x.cycle + 1) { due[n] = e[i].at - 1; ctr[n] = e[i].w[0]; n++; }
    o.w("line.n", n);
    for (uint32_t i = 0; i < n; i++) { o.w("line.due", due[i]); o.w("line.ctr", ctr[i]); }
    BusEntry be[kMax];
    uint32_t nb = 0;
    if (g.sim && g.udir)                                                 // the eject bus: unshifted (intra_tile_latency)
        for (uint32_t i = 0, m = entries(g, g.udir->in, e); i < m; i++) be[nb++] = BusEntry{e[i].at, e[i].w[0], e[i].w[1], e[i].w[2]};
    link(o, "in", be, nb);
}

// G3 (W11): a port's entries as BusEntries; a Link's arrival as the observer gives it (G3's Links are unshifted).
template <class Port>
uint32_t bus_entries(const G1Tile& g, const Port& port, BusEntry* out, BusEntry& put, bool& has_put, bool& valid) {
    sim::PortEntry e[kMax];
    const sim::PortView v = g.sim->observe(port, e, kMax);
    for (uint32_t i = 0; i < v.n; i++) out[i] = BusEntry{e[i].at, e[i].w[0], e[i].w[1], e[i].w[2]};
    has_put = v.put; valid = v.valid;
    if (v.put) put = BusEntry{0, v.put_w[0], v.put_w[1], v.put_w[2]};
    return v.n;
}
template <class Port> void w_link(FpOut& o, const char* nm, const G1Tile& g, const Port* port) {
    if (!g.sim || !port) { link_empty(o, nm); return; }
    BusEntry e[kMax], p; bool hp, v;
    const uint32_t n = bus_entries(g, *port, e, p, hp, v);
    link(o, nm, e, n);
}
template <class Port> void w_fifo(FpOut& o, const char* nm, const G1Tile& g, const Port* port) {
    if (!g.sim || !port) { fifo_empty(o, nm); return; }
    BusEntry e[kMax], p; bool hp, v;
    const uint32_t n = bus_entries(g, *port, e, p, hp, v);
    fifo(o, nm, e, n, hp ? &p : nullptr);
}
template <class Port> void w_hold(FpOut& o, const char* nm, const G1Tile& g, const Port* port) {
    if (!g.sim || !port) { hold_empty(o, nm); return; }
    BusEntry e[kMax], p; bool hp, v;
    bus_entries(g, *port, e, p, hp, v);
    hold(o, nm, v ? &e[0] : nullptr, hp ? &p : nullptr);
}
// A request FIFO inside the table store (ReqFifo): the entries visible at the end of c, then a put made at c.
void w_reqfifo(FpOut& o, const char* nm, const units::ReqFifo* f, uint64_t c) {
    if (!f) { fifo_empty(o, nm); return; }
    BusEntry e[units::ReqFifo::N], p;
    for (unsigned k = 0; k < f->n; k++) e[k] = BusEntry{0, f->at(k)[0], f->at(k)[1], f->at(k)[2]};
    const bool hp = f->pend && f->put_at == c;
    if (f->pend && f->put_at != c) { std::fprintf(stderr, "statefp: %s: a put of %llu not judged at %llu\n", nm, (unsigned long long)f->put_at, (unsigned long long)c); std::abort(); }
    if (hp) p = BusEntry{0, f->pw[0], f->pw[1], f->pw[2]};
    fifo(o, nm, e, f->n, hp ? &p : nullptr);
}
// StateFp.pipe: the write-backs pending at the end of c (due, idx, val).
void w_pipe(FpOut& o, const char* nm, const units::Rmw* r) {
    const bool v = r && r->valid;
    o.wp(nm, ".n", v ? 1 : 0);
    if (v) { o.wp(nm, ".due", r->due); o.wp(nm, ".idx", r->idx); o.wp(nm, ".val", r->val); }
}

// StateFp.we: bucketDigit, histDigit, augmentBase; the pointer stage (d, h, c, have, ptr if have); awaiting (as stage
// positions); its inputs: in (we-in), tsRet.
void w_we(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    o.w("bucketDigit", units::WriteEngine::bucket_digit(dma_config(g, x.cycle)));
    o.w("histDigit", units::WriteEngine::hist_digit(dma_config(g, x.cycle)));
    o.w("augmentBase", units::WriteEngine::augment_base(record_base(g, x.cycle)));
    const units::WriteEngine* w = g.uwe;
    o.w("stage.n", w ? w->count : 0);
    if (w)
        for (unsigned k = 0; k < w->count; k++) {
            const unsigned j = (w->head + k) % w->cap;
            o.w("d", w->st_d[j]); o.w("h", w->st_h[j]); o.w("c", w->st_c[j]); o.w("have", w->st_have[j]);
            if (w->st_have[j]) o.w("ptr", w->st_ptr[j]);
        }
    o.w("awaiting.n", w ? w->aw_n : 0);
    if (w) for (unsigned k = 0; k < w->aw_n; k++) o.w("awaiting", (w->aw[(w->aw_head + k) % w->cap] + w->cap - w->head) % w->cap);
    w_fifo(o, "in", g, w ? &w->in : nullptr);
    w_link(o, "tsRet", g, w ? &w->ts_ret : nullptr);
}

// StateFp.ag (W12): forty fields (the configuration first, from the block's registers; pStart is the block's PULSE_DMA of
// endCycle(c), which the AG takes at c + 1), the read-ahead rings in queue order, the two table-return lines. Java's
// int fields are sign-extended (a STREAM_DEST past 2^31), its longs as they are.
void w_ag(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    static const units::AddrGen kIdle(units::AddrGen::idle());
    const units::AddrGen* a = g.uag ? g.uag : &kIdle;       // a bench without the unit: the reset state
    auto i = [&](const char* nm, uint32_t v) { o.w(nm, uint64_t(int64_t(int32_t(v)))); };
    const uint32_t mc = dma_config(g, x.cycle);
    i("kind", units::AddrGen::kind(mc)); i("runStart", units::AddrGen::run_start(mc)); i("runCount", units::AddrGen::run_count(mc));
    o.w("pStart", g.sim ? block_end(g, x.cycle).dma_pulse : 0);
    o.w("running", a->running); o.w("done", a->done); o.w("leadIn", a->lead_in); o.w("finishedIssuing", a->finished_issuing);
    o.w("gatherChecked", a->gather_checked);
    i("sqOut", a->sq_out); i("cqOut", a->cq_out); i("streamDiscard", a->stream_discard); i("groupDiscard", a->group_discard);
    i("sReads", a->s_reads); i("cReads", a->c_reads); i("rg", a->rg); i("rr", a->rr); i("rv", a->rv);
    i("slRun", a->sl_run); o.w("cur", a->cur);
    i("cSrc", a->c_src); i("cDest", a->c_dest); i("cCount", a->c_count); i("cJ", a->c_j); i("cD", a->c_d); i("cMode", a->c_mode);
    i("cSize", a->c_size); i("cGap", a->c_gap); i("cCtr", a->c_ctr); i("gapLeft", a->gap_left);
    i("g", a->g); i("r", a->r); i("v", a->v); i("gR", a->g_r); i("gT", a->g_t); i("gGap", a->g_gap);
    o.w("groupS", uint64_t(a->group_s)); o.w("rotSched", uint64_t(a->rot_sched)); o.w("inRot", a->in_rot);
    o.w("stalledSinceGroup", a->stalled_since_group);
    i("sq.n", a->sq_buf);
    for (uint32_t k = 0; k < a->sq_buf; k++) { const uint32_t j = (a->sq_head + k) % a->lookahead; o.w("sq.e0", a->sq[j]); o.w("sq.e1", a->sq1[j]); }
    i("cq.n", a->cq_buf);
    for (uint32_t k = 0; k < a->cq_buf; k++) o.w("cq.e", a->cq[(a->cq_head + k) % a->lookahead]);
    w_link(o, "streamRet", g, g.uag ? &a->stream_ret : nullptr);
    w_link(o, "groupRet", g, g.uag ? &a->group_ret : nullptr);
}

// StateFp.gather (W12): k, base (the block's registers), the active request (left, ord, i, ctr, wide), its input queue.
void w_gather(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    static const units::Gather kIdle(units::Gather::idle());
    const units::Gather* a = g.ugather ? g.ugather : &kIdle;  // a bench without the unit: the reset state
    o.w("k", units::Gather::k(dma_config(g, x.cycle)));
    o.w("base", units::Gather::base(record_base(g, x.cycle)));
    o.w("left", a->left); o.w("ord", a->ord); o.w("i", a->i); o.w("ctr", a->ctr); o.w("wide", a->wide);
    w_fifo(o, "in", g, g.ugather ? &a->in : nullptr);
}

// StateFp.ts: the five memories (with the window's store of c, committed by ref at c unless a write-back was done at c:
// figure 12), PTR's pending write-backs, the three request lines and request FIFOs.
void w_ts(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    units::TableMems m = g.ts ? *g.ts : units::TableMems{};
    const units::TableStore* u = g.uts;
    if (g.sim && u) {
        sim::PortEntry e[kMax];
        for (uint32_t i = 0, n = entries(g, u->window, e); i < n; i++)
            if (e[i].at == x.cycle + 1) {
                const uint32_t table = uint32_t(e[i].w[0] >> 32);
                if (!(table == units::TBL_PTR && u->pipe.last_wb == x.cycle)) m.write(table, uint32_t(e[i].w[0]), uint32_t(e[i].w[1]));
            }
    }
    o.w("STREAM_SRC", m.s_src.sum); o.w("STREAM_DEST", m.s_dest.sum); o.w("STREAM_COUNT", m.s_count.sum);
    o.w("GROUP", m.s_group.sum); o.w("PTR", m.s_ptr.sum);
    w_pipe(o, "wb", u ? &u->pipe : nullptr);
    const bool ag = u && u->with_ag;
    w_link(o, "reqStream", g, ag ? &u->req_stream : nullptr);
    w_link(o, "reqGroup", g, ag ? &u->req_group : nullptr);
    w_link(o, "reqWe", g, u ? &u->req_we : nullptr);
    w_reqfifo(o, "qStream", u ? &u->q_stream : nullptr, x.cycle);
    w_reqfifo(o, "qGroup", u ? &u->q_group : nullptr, x.cycle);
    w_reqfifo(o, "qWe", u ? &u->q_we : nullptr, x.cycle);
}

// StateFp hist: HIST (with the window's store of c, as above), its write-backs, its request line and FIFO.
void w_hist(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    units::HistMem h = g.hist ? *g.hist : units::HistMem{};
    const units::HistStore* u = g.uhist;
    if (g.sim && u) {
        sim::PortEntry e[kMax];
        for (uint32_t i = 0, n = entries(g, u->window, e); i < n; i++)
            if (e[i].at == x.cycle + 1 && u->pipe.last_wb != x.cycle) h.write(uint32_t(e[i].w[0]), uint32_t(e[i].w[1]));
    }
    o.w("HIST", h.s_hist.sum);
    w_pipe(o, "wb", u ? &u->pipe : nullptr);
    w_link(o, "req", g, u ? &u->req : nullptr);
    w_reqfifo(o, "q", u ? &u->q : nullptr, x.cycle);
}

// StateFp.sramQ: per area its queue in order (w, row, mask, then d0-d3 for a write or tok, part for a read), then the
// read and write holding registers.
void w_sram(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    const units::BigSram* s = g.usram;
    for (unsigned a = 0; a < 16; a++) {
        const unsigned n = s ? s->count[a] : 0;
        o.wpi("area", a, ".n", n);
        for (unsigned k = 0; k < n; k++) {
            const units::BigSram::Entry& e = s->at(a, k);
            o.w("w", e.write); o.w("row", e.row); o.w("mask", e.mask);
            if (e.write) for (unsigned j = 0; j < 4; j++) o.wi("d", j, e.d[j]);
            else { o.w("tok", e.token); o.w("part", e.part); }
        }
    }
    w_hold(o, "rd", g, s && s->reads ? &s->rd : nullptr);
    w_hold(o, "wr", g, s ? &s->wr : nullptr);
}
void w_sram_data(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    o.w("data", g.usram ? g.usram->data_sum() : 0);
}
void w_rdport(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    const uint64_t st = g.ucore ? g.ucore->rp_stored : 0;
    o.w("inUse", st - (g.uinj ? g.uinj->rs.rp_injected_n : 0));
    o.w("outstanding", st - (g.udir ? g.udir->rp_accepted : 0));
}
// StateFp.req: the request FIFO (empty), beatsSent, W0-W2 (REQUEST_SET_W*, never stored in M2's programs).
void w_req(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    if (g.uleaf && x.sim) {   // W14: the port as the bus holds it (reqbus_fp.h); REQUEST_SET_W0-W2 as the core stored them
        reqfp::port(o, *x.sim, g.uleaf, g.ucore ? g.ucore->req_set_w : g.ctl->set_w, x.cycle);
        return;
    }
    o.w("fifo.n", 0);
    o.w("beatsSent", 0);
    for (unsigned k = 0; k < 3; k++) o.wi("setW", k, g.ctl ? g.ctl->set_w[k] : 0);
}

void attach_reqbus(sim::Sim& s, G1Tile* g1, unsigned n_tiles, G2Chip& gc, units::reqbus::ReqBusUnits& u,
                   const units::reqbus::ReqBusConfig& rc) {
    u = units::reqbus::resolve(s, ~uint64_t(0) >> (64 - n_tiles), 0, rc);
    if (s.has_unit("reqbus.host")) {                      // the bus driven from the test: its receive queues and host
        static const char* kRecv[units::reqbus::TARGETS] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
        for (unsigned c = 0; c < units::reqbus::TARGETS; c++)
            if (s.has_unit(kRecv[c])) u.recv[c] = &s.unit<units::reqbus::ReqRecv>(kRecv[c]);
        u.host = &s.unit<units::reqbus::ReqHost>("reqbus.host");
    }
    for (unsigned k = 0; k < n_tiles; k++) g1[k].uleaf = u.leaf[k];
    gc.reqbus = &u;
}

void register_g1(FpRegistry& r, const G1Tile& g) {
    const std::string t = units::unit_prefix(g.tile);      // W16: the controllers' "cc1." .. "cc3."
    if (g.core) { r.add(t + "core", w_core, &g); r.add(t + "cmem", w_cmem, &g); }
    r.add(t + "ctl", w_ctl, &g);
    r.add(t + "dir", w_dir, &g);
    r.add(t + "sram", w_sram, &g);
    r.add(t + "sram.data", w_sram_data, &g);
    r.add(t + "we", w_we, &g);
    r.add(t + "ag", w_ag, &g);
    r.add(t + "ts", w_ts, &g);
    r.add(t + "hist", w_hist, &g);
    r.add(t + "gather", w_gather, &g);
    r.add(t + "rdport", w_rdport, &g);
    r.add(t + "req", w_req, &g);
}

}  // namespace fp
