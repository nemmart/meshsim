// statefp_g1.cpp: G1's fingerprint writers (statefp_g1.h), StateFp.java's streams word for word.
#include "statefp_g1.h"

#include <string>

#include "units/ag.h"
#include "units/gather.h"
#include "units/write_engine.h"

namespace fp {

namespace {
const G1Tile& tile(const void* p) { return *static_cast<const G1Tile*>(p); }
constexpr uint32_t kMax = 64;

// The entries of one input port (a Link's in arrival order, in flight included).
template <class Port>
uint32_t entries(const G1Tile& g, const Port& port, sim::PortEntry* buf) { return g.sim->observe(port, buf, kMax).n; }

// The register block as ref has it at the end of c: endCycle(c) applied to a copy, from the inputs arriving at c + 1
// (the block's step at c + 1 applies exactly these; with none, endCycle is a no-op).
template <class U>
units::RegBlockState block_end_of(const G1Tile& g, const U& u, uint64_t c) {
    units::RegBlockState b = u.s;
    units::RegBlockState::In in;
    sim::PortEntry e[kMax];
    bool send_put = false;
    for (uint32_t i = 0, n = entries(g, u.bus, e); i < n; i++)
        if (e[i].at == c + 1) {
            in.op = uint32_t(e[i].w[0] >> 32); in.arg = uint32_t(e[i].w[0]); in.v = uint32_t(e[i].w[1]);
            send_put = in.op == units::BUS_STORE && in.arg >= units::ROUTER_SEND_32 && in.arg <= units::ROUTER_SEND_128;
        }
    for (uint32_t i = 0, n = entries(g, u.go, e); i < n; i++) if (e[i].at == c + 1) in.go = true;
    for (uint32_t i = 0, n = entries(g, u.landed, e); i < n; i++) if (e[i].at == c + 1) in.landed = true;
    if constexpr (requires { u.kline; }) {                              // M2's block
        for (uint32_t i = 0, n = entries(g, u.kline, e); i < n; i++) if (e[i].at == c + 1) in.kline = uint32_t(e[i].w[0]);
        for (uint32_t i = 0, n = entries(g, u.bad, e); i < n; i++) if (e[i].at == c + 1) in.bad = true;
        sim::PortView r = g.sim->observe(u.send_busy, e, kMax);
        in.send_busy = send_put || (r.put ? r.put_w[0] != 0 : (r.valid && e[0].w[0] != 0));
        in.router_ovf = b.ev_prev[0]; in.we_in_ovf = b.ev_prev[4]; in.gather_ovf = b.ev_prev[6];   // D28: Counts, not here
    } else {                                                             // M1's block (W5's bench)
        for (uint32_t i = 0, n = entries(g, u.freed, e); i < n; i++) if (e[i].at == c + 1) in.freed = true;
    }
    b.end_cycle(c, in);
    return b;
}
units::RegBlockState block_end(const G1Tile& g, uint64_t c) {
    if (g.sim && g.uctl) return block_end_of(g, *g.uctl, c);
    if (g.sim && g.m1ctl) return block_end_of(g, *g.m1ctl, c);
    return g.ctl ? *g.ctl : units::RegBlockState{};
}
uint32_t dma_config(const G1Tile& g, uint64_t c) { return block_end(g, c).dma_config; }
uint32_t record_base(const G1Tile& g, uint64_t c) { return block_end(g, c).record_base; }

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
    const std::string t = "t" + std::to_string(k) + ".";
    G1Tile g;
    g.tile = k; g.sim = &s;
    g.ucore = &s.unit<units::TileCore>(t + "core"); g.uctl = &s.unit<units::TileCtl>(t + "ctl");
    g.udir = &s.unit<units::Director>(t + "dir"); g.uts = &s.unit<units::TableStore>(t + "ts");
    g.uhist = &s.unit<units::HistStore>(t + "hist");
    g.core = &g.ucore->rv; g.ctl = &g.uctl->s; g.ts = &g.uts->m; g.hist = &g.uhist->m;
    return g;
}

void w_core(const void* p, FpOut& o, const FpCtx& x) {
    const units::Rv32& k = *tile(p).core;
    const uint64_t c = x.cycle;
    uint64_t mcycle = k.mcycle;
    uint32_t stall = k.stall;
    if (k.stop != units::Stop::running) mcycle += c - k.stop_cycle;      // ref's halted core: mcycle++ every cycle
    else if (c > k.last) {                                              // asleep since `last`: in a WFI or a stall
        uint64_t gap = c - k.last;
        mcycle += gap;
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
    o.w("sendRetry", k.pending_t == c);                                  // a send awaiting its acceptance at c + 1
    bool lw = k.load_wait_t == c;
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

// StateFp.we: bucketDigit, histDigit, augmentBase; the pointer stage; awaiting; in (we-in), tsRet. (G3: stage, buses.)
void w_we(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    o.w("bucketDigit", units::WriteEngine::bucket_digit(dma_config(g, x.cycle)));
    o.w("histDigit", units::WriteEngine::hist_digit(dma_config(g, x.cycle)));
    o.w("augmentBase", units::WriteEngine::augment_base(record_base(g, x.cycle)));
    o.w("stage.n", 0);
    o.w("awaiting.n", 0);
    fifo_empty(o, "in");
    link_empty(o, "tsRet");
}

// StateFp.ag: forty fields (the configuration first), the read-ahead queues, the two table-return lines. (G4: the rest.)
void w_ag(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    static const char* const kIdle[] = {"pStart", "running", "done", "leadIn", "finishedIssuing", "gatherChecked", "sqOut",
                                        "cqOut", "streamDiscard", "groupDiscard", "sReads", "cReads", "rg", "rr", "rv",
                                        "slRun", "cur", "cSrc", "cDest", "cCount", "cJ", "cD", "cMode", "cSize", "cGap",
                                        "cCtr", "gapLeft", "g", "r", "v", "gR", "gT", "gGap", "groupS", "rotSched",
                                        "inRot", "stalledSinceGroup"};
    o.w("kind", units::AddrGen::kind(dma_config(g, x.cycle)));
    o.w("runStart", units::AddrGen::run_start(dma_config(g, x.cycle)));
    o.w("runCount", units::AddrGen::run_count(dma_config(g, x.cycle)));
    for (const char* f : kIdle) o.w(f, 0);
    o.w("sq.n", 0);
    o.w("cq.n", 0);
    link_empty(o, "streamRet");
    link_empty(o, "groupRet");
}

// StateFp.gather: k, base, the active request (left, ord, i, ctr, wide), its input queue. (G4: the request.)
void w_gather(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    o.w("k", units::Gather::k(dma_config(g, x.cycle)));
    o.w("base", units::Gather::base(record_base(g, x.cycle)));
    for (const char* f : {"left", "ord", "i", "ctr", "wide"}) o.w(f, 0);
    fifo_empty(o, "in");
}

// StateFp.ts: the five memories, PTR's write-backs, the three request lines and request FIFOs. (G3: pipe, buses.)
void w_ts(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    units::TableMems m = g.ts ? *g.ts : units::TableMems{};
    if (g.sim && g.uts) {                                                // a window store of c: committed by ref at c
        sim::PortEntry e[kMax];
        for (uint32_t i = 0, n = entries(g, g.uts->window, e); i < n; i++)
            if (e[i].at == x.cycle + 1) m.write(uint32_t(e[i].w[0] >> 32), uint32_t(e[i].w[0]), uint32_t(e[i].w[1]));
    }
    o.w("STREAM_SRC", m.s_src.sum); o.w("STREAM_DEST", m.s_dest.sum); o.w("STREAM_COUNT", m.s_count.sum);
    o.w("GROUP", m.s_group.sum); o.w("PTR", m.s_ptr.sum);
    o.w("wb.n", 0);
    link_empty(o, "reqStream"); link_empty(o, "reqGroup"); link_empty(o, "reqWe");
    fifo_empty(o, "qStream"); fifo_empty(o, "qGroup"); fifo_empty(o, "qWe");
}

// StateFp hist: HIST, its write-backs, its request line and FIFO. (G3: pipe, buses.)
void w_hist(const void* p, FpOut& o, const FpCtx& x) {
    const G1Tile& g = tile(p);
    units::HistMem h = g.hist ? *g.hist : units::HistMem{};
    if (g.sim && g.uhist) {
        sim::PortEntry e[kMax];
        for (uint32_t i = 0, n = entries(g, g.uhist->window, e); i < n; i++)
            if (e[i].at == x.cycle + 1) h.write(uint32_t(e[i].w[0]), uint32_t(e[i].w[1]));
    }
    o.w("HIST", h.s_hist.sum);
    o.w("wb.n", 0);
    link_empty(o, "req");
    fifo_empty(o, "q");
}

// StateFp.sramQ: 16 area queues, the read and write holds.
void w_sram_stub(const void*, FpOut& o, const FpCtx&) {
    for (unsigned a = 0; a < 16; a++) o.wpi("area", a, ".n", 0);
    hold_empty(o, "rd");
    hold_empty(o, "wr");
}
void w_sram_data_stub(const void*, FpOut& o, const FpCtx&) { o.w("data", 0); }
void w_rdport_stub(const void*, FpOut& o, const FpCtx&) { o.w("inUse", 0); o.w("outstanding", 0); }
// StateFp.req: the request FIFO (empty), beatsSent, W0-W2 (REQUEST_SET_W*, never stored in M2's programs).
void w_req(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    o.w("fifo.n", 0);
    o.w("beatsSent", 0);
    for (unsigned k = 0; k < 3; k++) o.wi("setW", k, g.ctl ? g.ctl->set_w[k] : 0);
}

void register_g1(FpRegistry& r, const G1Tile& g) {
    const std::string t = "t" + std::to_string(g.tile) + ".";
    if (g.core) { r.add(t + "core", w_core, &g); r.add(t + "cmem", w_cmem, &g); }
    r.add(t + "ctl", w_ctl, &g);
    r.add(t + "dir", w_dir, &g);
    r.add(t + "sram", w_sram_stub, &g);
    r.add(t + "sram.data", w_sram_data_stub, &g);
    r.add(t + "we", w_we, &g);
    r.add(t + "ag", w_ag, &g);
    r.add(t + "ts", w_ts, &g);
    r.add(t + "hist", w_hist, &g);
    r.add(t + "gather", w_gather, &g);
    r.add(t + "rdport", w_rdport_stub, &g);
    r.add(t + "req", w_req, &g);
}

}  // namespace fp
