// tile_ctl.cpp: G1's part of a tile (WORKER8): add_g1, a ControlBuilder (chip.h).
#include "units/tile_ctl.h"

#include "units/ag.h"
#include "units/gather.h"
#include "units/write_engine.h"

namespace units {

// D29: ROUTER_OVF as an event, once W9's chip.h offers it (ControlIO::router_ovf_ev, width 0, Link(1)); until then the
// block reads the Count. Templates so the check is on a dependent type.
template <class IO> constexpr bool kHasOvfEvent = requires(IO io) { io.router_ovf_ev; };
template <class IO> void set_ovf_event(IO& io, const sim::LinkIn& h) {
    if constexpr (kHasOvfEvent<IO>) io.router_ovf_ev = h;
}

ControlIO add_g1(sim::Config& cfg, unsigned k, void* ctx) {
    G1Ctx& g = *static_cast<G1Ctx*>(ctx);
    if (!g.mem || !g.bootrom) FATAL("add_g1: tile %u: the context has no memories or no boot ROM", k);
    if (g.cfg.K + 1 > 63 || g.cfg.cm_latency + 1 > 63) FATAL("add_g1: K %u or cm_latency %u past the wheel", g.cfg.K, g.cfg.cm_latency);
    const std::string t = "t" + std::to_string(k) + ".";

    Rv32 core;
    core.timing = g.cfg.timing;
    core.periph_present = g.cfg.periph_present;
    core.dmem = g.mem + size_t(k) * 2 * MEM_WORDS;
    core.imem = core.dmem + MEM_WORDS;
    core.bootrom = g.bootrom; core.bootrom_words = g.bootrom_words;
    RegBlockState block;
    block.params = params_value(g.cfg.periph_present, k, g.cfg.mem_chunks);
    block.done_gate = g.cfg.done_gate;
    DirectorConfig dc;
    dc.tile = k; dc.periph_present = g.cfg.periph_present; dc.mem_chunks = g.cfg.mem_chunks;

    auto& c = cfg.add<TileCore>(t + "core", core);
    auto& b = cfg.add<TileCtl>(t + "ctl", block, kHasOvfEvent<ControlIO>);
    auto& d = cfg.add<Director>(t + "dir", dc);
    auto& we = cfg.add<WriteEngine>(t + "we", g.we_in_depth);
    auto& ag = cfg.add<AddrGen>(t + "ag");
    auto& ga = cfg.add<Gather>(t + "gather", g.gather_depth);
    auto& ts = cfg.add<TableStore>(t + "ts");
    auto& hs = cfg.add<HistStore>(t + "hist");

    cfg.link(c.bus, b.bus, 1);
    cfg.connect(b.regs, c.regs);
    cfg.connect(b.wake, c.wake);
    cfg.connect(b.irq, c.irq);
    cfg.link(d.port_b, c.port_b, g.cfg.cm_latency + 1);
    cfg.link(d.landed, b.landed, g.cfg.cm_latency + 1);
    cfg.link(d.kline, b.kline, g.cfg.K + 1);
    cfg.link(d.bad, b.bad, 1);
    cfg.connect(d.we_in, we.in);
    cfg.count_overflows(we.in, b.we_in_ovf);
    cfg.connect(d.gather, ga.in);
    cfg.count_overflows(ga.in, b.gather_ovf);
    cfg.connect(b.dma, we.dma);
    cfg.connect(b.dma, ag.dma);
    cfg.connect(b.dma, ga.dma);
    cfg.link(c.tables, ts.window, 1);
    cfg.link(c.hist_w, hs.window, 1);
    cfg.connect(hs.out, c.hist);

    ControlIO io;
    io.send = c.send; io.send_busy = b.send_busy; io.eject = d.in; io.go = b.go; io.line = b.line;
    io.router_ovf = b.router_ovf;
    set_ovf_event(io, b.ovf);
    return io;
}

}  // namespace units
