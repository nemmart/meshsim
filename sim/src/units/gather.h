// gather.h: the GATHER engine (sim/ref/meshsim/src/GatherEngine.java; AUDIT-M2 3.6).
// CONFIGURATION PART BY W8 (G1); G4 EXTENDS THIS FILE with the behaviour (the active request, its holding register into
// the read queue, busy), in this same unit.
//
// What exists now: the unit, its input from the director (`in`, the gather queue, mode 4) and its configuration (k from
// DMA_CONFIG, base from DMA_RECORD_BASE_ADDRESS: Tile.setMasterConfig / setRecordBase), derived from the register
// block's `dma` RegFile (write_engine.h). ref's reset value of k is 1 (DMA_CONFIG 0 gives k = 1 too).
//
// G4'S PORTS (WORKER12; DECLARATIONS ONLY until P2 wires them: declared to the framework only when add_g1 wires them).
// Agreed with W11 (src/units/mem.h on sim-w11) and the integrator, 2026-11-08.
//
//   port   kind              from -> to                      words                                  latency
//   in     Fifo, width 3     director -> gather              mode-4 link words, Fifo(gather_depth);  1
//                                                            its overflow reaches the block as an
//                                                            event by W10's Fifo hook (GATHER_OVF, D29)
//   dma    RegFile           block -> gather                 k (DMA_CONFIG), the record base         (level, as the AG)
//   rd     Hold, width 2     gather -> srq client 0 (W11)    mem::req_a, mem::req_b                  (Hold)
//   busy   Reg, width 1      gather -> block                 its own half of GatherEngine.busy at    (Reg)
//                                                            the end of t: the queue non-empty after
//                                                            t's take, OR left > 0, OR a put on `rd`
//                                                            at t
//
// STATUS's gather_busy at the block's endCycle(t) is the OR of three inputs (owner, 2026-11-08), together ref's
//   !gatherIn.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(GATHER) > 0:
//   `busy` above; the srq's Reg (W11): the gather Hold occupied at the end of t, OR inFlightOf(GATHER) > 0 (only the
//   consumer knows whether it took the word at t); the director's `gather_put`, a width-0 Link(1) put with a mode-4
//   packet at t (ref's isEmpty counts the queue's pending put, which reaches this unit only at t + 1).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/write_engine.h"

namespace units {

struct Gather {
    sim::FifoIn in;            // width 3: mode-4 packets from the director, Fifo(gather_depth)
    sim::RegFileIn dma;
    // G4 (P2): declared when wired (above)
    sim::HoldOut rd;
    sim::RegOut busy;

    Gather(sim::Ports& p, unsigned gather_depth) {
        p.in(in, "in", 3, gather_depth);
        p.in(dma, "dma");
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(in)) FATAL("gather engine: a request arrived at cycle %llu: not modelled in M2 yet (G4)", (unsigned long long)c.now());
        return sim::Wake::none();
    }
    static uint32_t k(uint32_t dma_config) { return mc_k(dma_config); }
    static uint32_t base(uint32_t record_base) { return record_base & 0x1FFFFF; }
};

}  // namespace units
