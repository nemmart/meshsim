// statefp_g1.cpp: G1's fingerprint writers (statefp_g1.h), StateFp.java's streams word for word.
#include "statefp_g1.h"

#include <string>

#include "units/ag.h"
#include "units/gather.h"
#include "units/write_engine.h"

namespace fp {

namespace {
const G1Tile& tile(const void* p) { return *static_cast<const G1Tile*>(p); }
uint32_t dma_config(const G1Tile& g) { return g.ctl ? g.ctl->dma_config : 0; }
uint32_t record_base(const G1Tile& g) { return g.ctl ? g.ctl->record_base : 0; }
}  // namespace

// StateFp.we: bucketDigit, histDigit, augmentBase; the pointer stage; awaiting; in (we-in), tsRet. (G3: stage, buses.)
void w_we(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    o.w("bucketDigit", units::WriteEngine::bucket_digit(dma_config(g)));
    o.w("histDigit", units::WriteEngine::hist_digit(dma_config(g)));
    o.w("augmentBase", units::WriteEngine::augment_base(record_base(g)));
    o.w("stage.n", 0);
    o.w("awaiting.n", 0);
    fifo_empty(o, "in");
    link_empty(o, "tsRet");
}

// StateFp.ag: forty fields (the configuration first), the read-ahead queues, the two table-return lines. (G4: the rest.)
void w_ag(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    static const char* const kIdle[] = {"pStart", "running", "done", "leadIn", "finishedIssuing", "gatherChecked", "sqOut",
                                        "cqOut", "streamDiscard", "groupDiscard", "sReads", "cReads", "rg", "rr", "rv",
                                        "slRun", "cur", "cSrc", "cDest", "cCount", "cJ", "cD", "cMode", "cSize", "cGap",
                                        "cCtr", "gapLeft", "g", "r", "v", "gR", "gT", "gGap", "groupS", "rotSched",
                                        "inRot", "stalledSinceGroup"};
    o.w("kind", units::AddrGen::kind(dma_config(g)));
    o.w("runStart", units::AddrGen::run_start(dma_config(g)));
    o.w("runCount", units::AddrGen::run_count(dma_config(g)));
    for (const char* f : kIdle) o.w(f, 0);
    o.w("sq.n", 0);
    o.w("cq.n", 0);
    link_empty(o, "streamRet");
    link_empty(o, "groupRet");
}

// StateFp.gather: k, base, the active request (left, ord, i, ctr, wide), its input queue. (G4: the request.)
void w_gather(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    o.w("k", units::Gather::k(dma_config(g)));
    o.w("base", units::Gather::base(record_base(g)));
    for (const char* f : {"left", "ord", "i", "ctr", "wide"}) o.w(f, 0);
    fifo_empty(o, "in");
}

// StateFp.ts: the five memories, PTR's write-backs, the three request lines and request FIFOs. (G3: pipe, buses.)
void w_ts(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    const units::TableMems z{};
    const units::TableMems& m = g.ts ? *g.ts : z;
    o.w("STREAM_SRC", m.s_src.sum); o.w("STREAM_DEST", m.s_dest.sum); o.w("STREAM_COUNT", m.s_count.sum);
    o.w("GROUP", m.s_group.sum); o.w("PTR", m.s_ptr.sum);
    o.w("wb.n", 0);
    link_empty(o, "reqStream"); link_empty(o, "reqGroup"); link_empty(o, "reqWe");
    fifo_empty(o, "qStream"); fifo_empty(o, "qGroup"); fifo_empty(o, "qWe");
}

// StateFp hist: HIST, its write-backs, its request line and FIFO. (G3: pipe, buses.)
void w_hist(const void* p, FpOut& o, const FpCtx&) {
    const G1Tile& g = tile(p);
    o.w("HIST", g.hist ? g.hist->s_hist.sum : 0);
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
