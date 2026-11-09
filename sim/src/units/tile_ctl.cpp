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
    if (!g.sram) FATAL("add_g1: tile %u: the context has no big-SRAM blocks (G1Ctx::sram)", k);
    if (g.cfg.K + 1 > 63 || g.cfg.cm_latency + 1 > 63) FATAL("add_g1: K %u or cm_latency %u past the wheel", g.cfg.K, g.cfg.cm_latency);
    if (k >= g.units) FATAL("add_g1: unit %u, but the context's memories hold %u units", k, g.units);
    const std::string t = unit_prefix(k);

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
    dc.g4 = true;                                         // W12: G4's engines are in every M2 tile
    dc.periph_slot = g.cfg.periph_slot;
    const unsigned lookahead = g.cfg.ag_lookahead ? g.cfg.ag_lookahead : 2 * g.cfg.ts_engine_latency + 2;

    auto& c = cfg.add<TileCore>(t + "core", core, k, g.cfg.read_port_depth, g.cfg.periph_slot);
    auto& b = cfg.add<TileCtl>(t + "ctl", block, kHasOvfEvent<ControlIO>, true, g.cfg.periph_slot);
    auto& d = cfg.add<Director>(t + "dir", dc);
    auto& we = cfg.add<WriteEngine>(t + "we", g.we_in_depth, g.cfg.ts_engine_latency, g.cfg.mem_chunks);
    auto& ag = cfg.add<AddrGen>(t + "ag", k, lookahead, true);
    auto& ga = cfg.add<Gather>(t + "gather", g.gather_depth, k, true);
    auto& ts = cfg.add<TableStore>(t + "ts", true);
    auto& hs = cfg.add<HistStore>(t + "hist");
    SramConfig sc;
    sc.mem_chunks = g.cfg.mem_chunks; sc.area_queue_depth = g.cfg.area_queue_depth;
    sc.write_combining = g.cfg.write_combining; sc.fifo_count_before_pop = g.cfg.fifo_count_before_pop;
    sc.running_sum = g.cfg.sram_running_sum;
    auto& sr = cfg.add<BigSram>(t + "sram", sc, g.sram->block(k, g.cfg.mem_chunks), true);

    cfg.link(c.bus, b.bus, 1);
    cfg.connect(b.regs, c.regs);
    cfg.connect(b.wake, c.wake);
    cfg.connect(b.irq, c.irq);
    cfg.link(d.port_b, c.port_b, g.cfg.cm_latency + 1);
    cfg.link(d.landed, b.landed, g.cfg.cm_latency + 1);
    cfg.link(d.kline, b.kline, g.cfg.K + 1);
    cfg.link(d.bad, b.bad, 1);
    cfg.connect(c.rp_stores, d.rp_stores);                // W11: the read port's results outstanding (overlap check)
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
    // G3 (W11): the write engine's PTR RMWs, count-on-arrival, and its writes into the big SRAM
    cfg.link(we.ts_req, ts.req_we, g.cfg.ts_engine_latency);
    cfg.link(ts.ret_we, we.ts_ret, g.cfg.ts_engine_latency);
    cfg.link(we.hist_req, hs.req, g.cfg.ts_engine_latency);
    cfg.connect(we.to_sram, sr.wr);
    // A full pointer stage waits for PTR's return, which sits in the table store's request FIFO (unit state, not a port)
    // between its request's arrival and its service: the write engine sleeps with we-in non-empty and nothing on its
    // way to it. Longer than the detector's default (a request FIFO holds 16).
    cfg.missed_wake_after(t + "we", 1u << 10);
    cfg.link(we.ovf_ev, b.we_ovf, 1);                     // D29: the overflow events, judged at t, endCycle(t) at t + 1
    cfg.link(ts.ovf_ev, b.ts_ovf, 1);
    cfg.link(hs.ovf_ev, b.hist_ovf, 1);
    // G4 (W12): the AG's table reads and the PTR view, its start and status, drift; the gather's busy half and its
    // queue's overflow event; the director's mode-4 put (STATUS's gather_busy is the OR of three inputs, gather.h)
    cfg.link(ag.stream_req, ts.req_stream, g.cfg.ts_engine_latency);
    cfg.link(ag.group_req, ts.req_group, g.cfg.ts_engine_latency);
    cfg.link(ts.ret_stream, ag.stream_ret, g.cfg.ts_engine_latency);
    cfg.link(ts.ret_group, ag.group_ret, g.cfg.ts_engine_latency);
    cfg.connect(ts.ptr_view, ag.ptr);
    cfg.connect(b.ag_start, ag.start);
    cfg.connect(ag.status, b.ag_status);
    cfg.link(ag.drift, b.drift, 1);
    cfg.connect(ga.busy, b.gather_busy);
    cfg.link(ga.ovf, b.gather_ovf_ev, 1);
    cfg.link(d.gather_put, b.gather_put, 1);
    // W15 (M3, AUDIT-M3 3.1): the peripheral slot, peripheral 1 the replayer (a mesh tile's: no DMA). S1 the director's
    // region writes, Link(3); S2/S3 the core's window stores and consuming loads, Link(1); S4/S5/S8 its window RegFile,
    // read by the core (loads, stalls) and the block (EVENTS [15:13], ERRORS [13:11], the WFI wake, the error line).
    if (g.cfg.periph_slot) {
        if ((g.cfg.periph_present & 7) != 1) FATAL("add_g1: tile %u: the slot's tile has peripheral 1 only (PARAMS [2:0] = %u)", k, g.cfg.periph_present);
        PeriphSlotConfig pc;
        pc.tile = k; pc.type[1] = g.cfg.periph1;
        auto& sl = cfg.add<PeriphSlot>(t + "slot", pc);
        cfg.link(d.periph, sl.region, 3);
        cfg.link(c.periph, sl.core, 1);
        cfg.connect(sl.window, c.win);
        cfg.connect(sl.window, b.slot_win);
    }

    ControlIO io;
    io.send = c.send; io.send_busy = b.send_busy; io.eject = d.in; io.go = b.go; io.line = b.line;
    io.router_ovf = b.router_ovf;
    set_ovf_event(io, b.ovf);
    // W11: the read queue's ports (in the injection unit), wired by tile_cycle; W12's two clients and the block's
    // gather_srq_busy input with them.
    io.mem.present = true;
    io.mem.read_port_depth = g.cfg.read_port_depth; io.mem.sram_read_latency = g.cfg.sram_read_latency;
    io.mem.mem_chunks = g.cfg.mem_chunks;
    io.mem.rd_port = c.rd_port; io.mem.rp_injected = c.rp_injected;
    io.mem.sram_rd = sr.rd;
    for (unsigned a = 0; a < BigSram::AREAS; a++) io.mem.sram_ret[a] = sr.ret[a];
    io.mem.gather = io.mem.ag = true;                     // W12: the read queue's clients 0 and 1
    io.mem.rd_gather = ga.rd; io.mem.rd_ag = ag.rd;
    io.mem.gather_srq_busy = b.gather_srq_busy;
    return io;
}

}  // namespace units
