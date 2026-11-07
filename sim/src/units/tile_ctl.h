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
//   line        Link(K + 1), width 1, from the director: counter id, a decrement due at t (judged without the same
//               cycle's store, D27; then the store's add)
//   send_empty  Reg, width 1, from W9's injection unit: the send hold empty at the end of t (STATUS [3]); none landed
//               yet reads as empty
//   router_ovf, we_in_ovf, gather_ovf, bad: Counts read through_last (endCycle(t) sees the increments of t)
// Its outputs:
//   regs        RegFile, read by the core at level 1: the view for t (see TileCtl::kView)
//   wake, irq   Wires to the core
//   lines       Link(ready_latency - 2), width 1, to W9's row collector: bit 0 READY rose at the end of t, bit 1 the
//               error line's level after endCycle(t); put only in a cycle where READY rises or the error line changes
// Added with G3/G4 (not declared yet, so the tile builder needs no stubs for them): the DMA configuration and the AG's
// start pulse (Wires written at level 0 of t + 1), the AG's and the gather's status (Regs), the read port (a Fifo(64)
// from the core, its free count), the table window, and the condition Counts of the table FIFOs, AG drift, read port.
#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "sim.h"
#include "units/director.h"
#include "units/regblock.h"
#include "units/regmap.h"
#include "units/rv32.h"

namespace units {

// ------------------------------------------------------------------------------------------------- the register block
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, go, landed, line;
    sim::RegIn send_empty;
    sim::CountIn router_ovf, we_in_ovf, gather_ovf, bad;
    // ---- outputs ----
    sim::RegFileOut regs;
    sim::WireOut wake, irq;
    sim::LinkOut lines;
    RegBlockState s;

    // The registers the core reads through `regs` (indices: the register numbers): PARAMS .. SRAM_RD_DEST_ADDR, the
    // counters (their value at the start of t), ROUTER_HDR/ADDR and ROUTER_SET_W0-W2 (a send store at t builds its packet
    // from them as they stand at t).
    static constexpr unsigned kRegs = SRAM_RD_DEST_ADDR + 1;

    TileCtl(sim::Ports& p, const RegBlockState& initial) : s(initial) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(line, "line", 1);
        p.in(send_empty, "send_empty", 1);
        p.in(router_ovf, "router_ovf"); p.in(we_in_ovf, "we_in_ovf"); p.in(gather_ovf, "gather_ovf"); p.in(bad, "bad");
        p.out(regs, "regs", kRegs); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(lines, "lines", 1);
    }
    sim::Wake step(sim::Ctx& c);
};

// ---------------------------------------------------------------------------------------------- the core in an M2 tile
// W5's Core gains, in an M2 tile (TileConfig::m2): `send`, a HoldOut of width 3 to W9's injection unit (a send store at
// t puts the packet at t; refused at t + 1: retried, the core stalls); its `memw` input becomes port B, link words from
// the director (Link(cm_latency + 1)). M1's request ports (req_busy, req_freed) are not declared in an M2 tile:
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

// What W9's tile builder wires: every handle is an endpoint of a port agreed in NOTES.md.
struct G1IO {
    sim::LinkIn* eject;                // director.in: W9's router -> Link(intra_tile_latency) -> here
    sim::HoldOut* send;                // core.send -> W9's injection unit (Hold)
    sim::RegIn* send_empty;            // W9's injection unit (Reg) -> here
    sim::LinkIn* go;                   // W9's C&C -> Link(go_latency + skew[k] + 1) -> here
    sim::LinkOut* lines;               // here -> Link(ready_latency - 2) -> W9's row collector
    sim::CountIn* router_ovf;          // W9's router's overflow Count -> here
    sim::FifoOut* we_in;               // director -> the write engine (G3), until then a stub (g1_stubs.h)
    sim::FifoOut* gather;              // director -> the GATHER engine (G4), until then a stub
    sim::CountIn* we_in_ovf;           // the we-in Fifo's overflows (Config::count_overflows on its consumer's input)
    sim::CountIn* gather_ovf;          // the gather Fifo's overflows
};

// Adds tile `prefix`'s G1 units (`prefix.core`, `prefix.ctl`, `prefix.dir`), wires them to each other (bus, regs, wake,
// irq, port B, landed, line, bad) and returns the external handles. `core` is the core's initial state (boot ROM,
// memories), as W5's configure_tile takes it.
G1IO add_g1(sim::Config& cfg, const std::string& prefix, const G1Config& g1, const Rv32& core);

}  // namespace units
