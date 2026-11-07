// tile_ctl.h: the tile's control in an M2 tile (WORKER8, G1): the register block as a unit with the M2 ports
// (TileControl.java; AUDIT-M2 3.7), the core's M2 ports, and the fragment of the tile builder that adds G1's units and
// returns the handles W9's builder wires. DECLARATIONS: the ports are the agreed interface (sessions/WORKER8/NOTES.md);
// the steps come with the implementation. W5's M1 units (controller.h: Core, RegBlock, TestBench) stay for M1's goldens.
//
// Levels (from the wires): 0 the block (TileControl.endCycle(t - 1) from what reached it at t, then the view for t);
// 1 the core (Controller.runCycle(t)). The director has no wire: it runs at any level.
//
// The register block's inputs, each registered (so endCycle(t) runs at level 0 of t + 1 from inputs delivered at t + 1):
//   bus         Link(1), width 2   the core's store, consuming load or bus error of t (W5's encoding)
//   go          Link(go_latency + skew[k] + 1), width 0, from W9's C&C model
//   landed      Link(cm_latency + 1), width 0, from the director: a port-B write covering command slot 0 landed at t
//   kline       Link(K + 1), width 1, from the director: counter id, a decrement due at t (judged without the same
//               cycle's store, D27; then the store's add)
//   send_busy   Reg, width 1, from W9's injection unit: the send hold occupied at the end of t; STATUS [3] is that OR
//               the core's own put at t (a send store or a retry: ref's `!sendHold.isEmpty(t)`, WordHold.isEmpty =
//               !occ && !pendPut), which the block sees on `bus`; none landed yet reads as not busy
//   router_ovf, we_in_ovf, gather_ovf, bad: Counts read through_last (endCycle(t) sees the increments of t)
// Its outputs:
//   regs        RegFile, read by the core at level 1: the view for t (see TileCtl::kView)
//   wake, irq   Wires to the core
//   dma         RegFile (2: DMA_CONFIG, the record base), read by the write engine, the AG and the gather engine
//               (write_engine.h): their configuration, from t + 1
//   line        Link(ready_latency - 2), width 1, to W9's row collector: bit 0 READY rose at the end of t, bit 1 the
//               error line's level after endCycle(t); put only in a cycle where READY rises or the error line changes
// Added with G3/G4: the AG's start pulse (a Wire written at level 0 of t + 1), the AG's and the gather's status (Regs),
// the read port (a Fifo(64) from the core, its free count), and the condition Counts of the table FIFOs, AG drift, the
// read port.
#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "sim.h"
#include "units/director.h"
#include "units/write_engine.h"
#include "units/regblock.h"
#include "units/regmap.h"
#include "units/rv32.h"

namespace units {

// ------------------------------------------------------------------------------------------------- the register block
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, go, landed, kline;
    sim::RegIn send_busy;
    sim::CountIn router_ovf, we_in_ovf, gather_ovf, bad;
    // ---- outputs ----
    sim::RegFileOut regs, dma;
    sim::WireOut wake, irq;
    sim::LinkOut line;
    RegBlockState s;

    // The registers the core reads through `regs` (indices: the register numbers): PARAMS .. SRAM_RD_DEST_ADDR, the
    // counters (their value at the start of t), ROUTER_HDR/ADDR and ROUTER_SET_W0-W2 (a send store at t builds its packet
    // from them as they stand at t).
    static constexpr unsigned kRegs = SRAM_RD_DEST_ADDR + 1;

    TileCtl(sim::Ports& p, const RegBlockState& initial) : s(initial) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(kline, "kline", 1);
        p.in(send_busy, "send_busy", 1);
        p.in(router_ovf, "router_ovf"); p.in(we_in_ovf, "we_in_ovf"); p.in(gather_ovf, "gather_ovf"); p.in(bad, "bad");
        p.out(regs, "regs", kRegs); p.out(dma, "dma", DMA_REGS); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(line, "line", 1);
    }
    sim::Wake step(sim::Ctx& c);
};

// ---------------------------------------------------------------------------------------------- the core in an M2 tile
// W5's Core gains, in an M2 tile (TileConfig::m2): `send`, a HoldOut of width 3 to W9's injection unit (a send store at
// t puts the packet at t; refused at t + 1: retried, the core stalls); its `memw` input becomes port B, link words from
// the director (Link(cm_latency + 1)); `tables` and `hist_w`, Link(1)s of width 2 to the table store and HIST
// (table_store.h: a table-window store of t, {table << 32 | index, value}); `hist`, HIST's RegFile (a HIST load is one
// cycle, as ref's). A load of PTR, STREAM or GROUP is a bus error, as in ref. M1's request ports (req_busy, req_freed) are not declared in an M2 tile:
// REQUEST_* is fatal there, not modelled in M2 (AUDIT-M2 1.2 (C)).

// ------------------------------------------------------------------------------------- G1's part of the tile builder
// The configuration G1's units depend on (the D12 header's keys that reach them).
struct G1Config {
    unsigned tile = 0;
    Rv32Timing timing;                 // reg_load_latency, branch_penalty, mul_latency, ref_cycle
    bool done_gate = true;
    uint32_t mem_chunks = 1u << 20;
    uint32_t periph_present = 1;
    unsigned K = 8, cm_latency = 2;    // the write counters' line, director -> port B
};

// G1's part of a tile, as W9's tile builder meets it: W9's chip.h (`ControlIO`, `ControlBuilder`) is the contract
// (owner, 2026-11-07). Until chip.h reaches main, G1IO has ControlIO's fields, names and kinds exactly, so the switch is
// a rename. ControlIO's ports (agreed in NOTES.md, plus ROUTER_OVF, agreed 2026-11-07):
struct G1IO {
    sim::HoldOut send;                 // core.send -> W9's injection unit (Hold, 3 words)
    sim::RegIn send_busy;              // W9's injection unit (Reg, 1 word) -> the block
    sim::LinkIn eject;                 // W9's router -> Link(intra_tile_latency) -> director.in
    sim::LinkIn go;                    // W9's C&C -> Link(go_latency + skew[k] + 1) -> the block
    sim::LinkOut line;                 // the block -> Link(ready_latency - 2) -> W9's row collector
    sim::CountIn router_ovf;           // W9's router's FIFO overflows -> the block, read through_last
};
// The units G1 adds for tile k (names "t<k>.core", "t<k>.ctl", "t<k>.dir", and the configuration parts of G3/G4's
// units it owns until they extend them: "t<k>.we", "t<k>.ag", "t<k>.gather", "t<k>.ts", "t<k>.hist"), wired to each
// other inside; the director's we-in and gather outputs go to the write engine's and the gather engine's inputs. `ctx`
// is a G1Ctx. The signature is ControlBuilder's.
struct G1Ctx {
    G1Config cfg;                      // per tile: cfg.tile is overwritten with k
    const Rv32* core = nullptr;        // the core's initial state (boot ROM, memories), the same for every tile
    unsigned we_in_depth = 4, gather_depth = 16;
};
G1IO add_g1(sim::Config& cfg, unsigned k, void* ctx);

}  // namespace units
