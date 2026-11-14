// tile_ctl.cpp: a tile's control part: add_g1, a ControlBuilder (chip.h).
#include "units/tile_ctl.h"

#include "units/ag.h"
#include "units/cnc1_periph.h"
#include "units/gather.h"
#include "units/write_engine.h"

namespace units {

Straps default_straps(unsigned k, uint32_t periph_present) {
    Straps s;
    s.tile_id = k;
    s.peripherals = periph_present & 7;
    if (k < TILE_COUNT) {                                  // a compute tile (2.2)
        const int special = lw::attached_special(k);
        s.attach = special >= 0 ? 1u << special : 0;
        s.barrier = true;
    } else {                                               // a special tile (2.1): the master, memory and tracer tiles
        const unsigned special = k - TILE_COUNT;
        s.receiver = true; s.request_target = special;
        s.attach = 1u << special;
        s.boot_local = special != 2;                       // 6.5: the tracer tile is loaded at its LOAD
    }
    return s;
}

ControlIO add_g1(sim::Config& cfg, unsigned k, void* ctx) {
    G1Ctx& g = *static_cast<G1Ctx*>(ctx);
    if (!g.mem || !g.bootrom) FATAL("add_g1: tile %u: the context has no memories or no boot ROM", k);
    if (!g.sram) FATAL("add_g1: tile %u: the context has no scratchpad blocks (G1Ctx::sram)", k);
    if (g.cfg.K + 1 > 63 || g.cfg.cm_latency + 1 > 63) FATAL("add_g1: K %u or cm_latency %u past the wheel", g.cfg.K, g.cfg.cm_latency);
    if (k >= g.units) FATAL("add_g1: unit %u, but the context's memories hold %u units", k, g.units);
    const std::string t = unit_prefix(k);
    // (harness-only): the test peripheral on one tile (G1Config::periph_only): the others have no peripheral
    const bool test_k = int(k) == g.cfg.test_periph_tile;
    const bool has_periph = k >= 64 || test_k || g.cfg.periph_only < 0 || int(k) == g.cfg.periph_only;
    const uint32_t periph_present = test_k ? 1u : has_periph ? g.cfg.periph_present : 0;
    const bool periph_slot = test_k || (has_periph && g.cfg.periph_slot);
    const bool receiver = k >= 64;                         // a special tile's request unit has a receive side (11.3)

    Rv32 core;
    core.timing = g.cfg.timing;
    core.dmem = g.mem + size_t(k) * 2 * MEM_WORDS;
    core.imem = core.dmem + MEM_WORDS;
    core.bootrom = g.bootrom; core.bootrom_words = g.bootrom_words;
    check_bootrom(g.bootrom_words, "the machine's");
    core.reset_mtvec = BOOTROM_BASE + g.bootrom_handler;   // mtvec resets to the ROM's handler (6.2)
    core.reset();
    RegBlockState block;
    G1Config tile_cfg = g.cfg;                             // this tile's: its id and the peripherals it has
    tile_cfg.tile = k; tile_cfg.periph_present = periph_present;
    block.straps = g.straps ? g.straps(k, tile_cfg) : default_straps(k, periph_present);
    block.request_queue_depth = g.cfg.request_queue_depth;
    bus::Straps bs;
    bs.peripherals = block.straps.peripherals;
    bs.peripheral_dmas = k >= 64 ? block.straps.peripherals : 0;   // a special tile's peripherals have DMAs (17.4)
    DirectorConfig dc;
    dc.tile = k; dc.periph_present = periph_present; dc.mem_chunks = g.cfg.mem_chunks;
    dc.g4 = true;
    dc.periph_slot = periph_slot;
    const unsigned lookahead = g.cfg.ag_lookahead ? g.cfg.ag_lookahead : 2 * g.cfg.ts_engine_latency + 2;

    auto& c = cfg.add<TileCore>(t + "core", core, k, g.cfg.read_port_depth, periph_slot, receiver, bs, g.cfg.request_queue_depth);
    auto& b = cfg.add<TileCtl>(t + "ctl", block, true, periph_slot, receiver);
    auto& d = cfg.add<Director>(t + "dir", dc);
    auto& we = cfg.add<WriteEngine>(t + "we", g.we_in_depth, g.cfg.ts_engine_latency, g.cfg.mem_chunks);
    auto& ag = cfg.add<AddrGen>(t + "ag", k, lookahead, true, false);
    auto& ga = cfg.add<Gather>(t + "gather", g.gather_depth, k, true);
    auto& ts = cfg.add<TableStore>(t + "ts", true, false);
    auto& hs = cfg.add<HistStore>(t + "hist", false);
    hs.ck_tile = int(k);                                  // invariant 6's fact
    SramConfig sc;
    sc.mem_chunks = g.cfg.mem_chunks; sc.area_queue_depth = g.cfg.area_queue_depth;
    sc.write_combining = g.cfg.write_combining; sc.fifo_count_before_pop = g.cfg.fifo_count_before_pop;
    sc.running_sum = g.cfg.sram_running_sum;
    auto& sr = cfg.add<BigSram>(t + "sram", sc, g.sram->block(k, g.cfg.mem_chunks), true, false);
#ifdef SIM_CHECKS
    sr.ck_tile = int(k);                                  // invariant 2's facts
    d.s1 = std::make_unique<S1Tile>();                    // S1's measurement (s1.h): the director owns it
    d.s1->tile = int(k); d.s1->K = g.cfg.K;
    we.s1 = ts.s1 = hs.s1 = sr.s1 = d.s1.get();
#endif

    // the register bus (6.4): window stores and the core's status at the end of t + 1; a load's side effect likewise
    cfg.link(c.bus, b.bus, 2);
    cfg.link(c.side, b.side, 1);
    cfg.connect(b.regs, c.regs);
    cfg.connect(b.wake, c.wake);
    cfg.connect(b.irq, c.irq);
    cfg.link(d.port_b, c.port_b, g.cfg.cm_latency + 1);
    cfg.link(d.kline, b.kline, g.cfg.K + 1);
    cfg.link(d.bad, b.bad, 1);
    cfg.connect(c.rp_stores, d.rp_stores);                // the read unit's results outstanding (the overlap check)
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
    cfg.connect(ts.view, c.tview);                        // 5.2: every table is loadable
    cfg.connect(ts.ptr_view, c.ptrv);
    // the write engine's PTR read-modify-writes, count-on-arrival, and its writes into the scratchpad
    cfg.link(we.ts_req, ts.req_we, g.cfg.ts_engine_latency);
    cfg.link(ts.ret_we, we.ts_ret, g.cfg.ts_engine_latency);
    cfg.link(we.hist_req, hs.req, g.cfg.ts_engine_latency);
    cfg.connect(we.to_sram, sr.wr);
    // A full pointer stage waits for PTR's return, which sits in the table store's request FIFO (unit state, not a port)
    // between its request's arrival and its service: the write engine sleeps with we-in non-empty and nothing on its
    // way to it. Longer than the detector's default (a request FIFO holds 16).
    cfg.missed_wake_after(t + "we", 1u << 10);
    cfg.link(we.ovf_ev, b.we_ovf, 1);
    cfg.link(ts.ovf_ev, b.ts_ovf, 1);
    cfg.link(hs.ovf_ev, b.hist_ovf, 1);
    // the DMA unit's table reads and the PTR view, its start and status, drift; the gather unit's busy half and its
    // queue's overflow; the director's GATHER put (STATUS.gather_busy is the OR of three inputs, gather.h)
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
    // The peripheral slot (17): the director's region writes, Link(3); the core's window stores and consuming loads,
    // Link(1); its window RegFile, read by the core (loads, stalls) and the tile control unit (EVENTS.peripheral_n,
    // ERRORS.peripheral_n). On a special tile (k 64-66) the slot has the DMAs: the master tile's peripheral 1 is
    // Ingress/Egress (type 1), the memory tile's v1.35's stub, the tracer tile's the Tracer (cc3_tracer) or the stub;
    // the chip wires the master tile's own ports and the DMA through `io.slot` and `io.dma`.
    PeriphSlot* slot = nullptr;
    if (periph_slot) {
        if ((periph_present & 7) != 1) FATAL("add_g1: tile %u: the slot's tile has peripheral 1 only (PARAMS.peripherals = %u)", k, periph_present);
        PeriphSlotConfig pc;
        pc.tile = k;
        pc.replayer_log_entries = g.cfg.replayer_log_entries;
        if (k < 64) pc.type[1] = test_k ? PT_TEST : g.cfg.periph1;   // the replayer by default (or the test's)
        else {
            pc.controller = true;
            pc.type[1] = k == lw::CC1 ? PT_CNC1 : k == lw::CC3 && g.cfg.cc3_tracer ? PT_TRACER : PT_CC_STUB;
        }
        auto& sl = cfg.add<PeriphSlot>(t + "slot", pc);
        cfg.link(d.periph, sl.region, 3);
        cfg.link(c.periph, sl.core, 1);
        cfg.connect(sl.window, c.win);
        cfg.connect(sl.window, b.slot_win);
        if (k == lw::CC1) sl.s.cnc1(1).ref_cycle = g.cfg.timing.ref_cycle;
        slot = &sl;
    }

    ControlIO io;
    if (slot && slot->cfg.controller) { io.dma = slot->dma; io.has_dma = true; }
    io.slot = slot;
    io.send = c.send; io.send_busy = b.send_busy; io.eject = d.in; io.release = b.release; io.line = b.line;
    io.router_ovf = b.router_ovf;
    io.router_ovf_ev = b.ovf;
    // the read queue's ports (in the inject unit), wired by tile_cycle; the two clients and the tile control unit's
    // gather_srq_busy input with them
    io.mem.present = true;
    io.mem.read_port_depth = g.cfg.read_port_depth; io.mem.sram_read_latency = g.cfg.sram_read_latency;
    io.mem.mem_chunks = g.cfg.mem_chunks;
    io.mem.rd_port = c.rd_port; io.mem.rp_injected = c.rp_injected;
    io.mem.sram_rd = sr.rd;
    for (unsigned a = 0; a < BigSram::AREAS; a++) io.mem.sram_ret[a] = sr.ret[a];
    io.mem.gather = io.mem.ag = true;                     // the read arbiter's clients 0 and 1
    io.mem.rd_gather = ga.rd; io.mem.rd_ag = ag.rd;
    io.mem.gather_srq_busy = b.gather_srq_busy;
    // the request unit, wired to the request tree by build_chip
    io.req.present = true;
    io.req.push = c.req_push; io.req.freed = c.req_freed; io.req.freed_ev = b.req_freed_ev;
    io.req.recv = receiver;
    if (io.req.recv) { io.req.head = c.recv; io.req.head_ctl = b.recv; io.req.pop = c.recv_pop; io.req.qcredit = c.recv_credit; }
    return io;
}

}  // namespace units
