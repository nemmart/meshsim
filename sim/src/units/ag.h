// ag.h: the address generator (sim/ref/meshsim/src/AddressGenerator.java; AUDIT-M2 3.6).
// CONFIGURATION PART BY W8 (G1); G4 EXTENDS THIS FILE with the behaviour (stream and group runs, the read-ahead queues,
// its holding register into the read queue, the start pulse, its status), in this same unit.
//
// What exists now: the unit and its configuration (AddressGenerator.config at DMA_CONFIG's endCycle: kind, run start,
// run count), derived from the register block's `dma` RegFile (write_engine.h), never copied. Its start pulse
// (PULSE_DMA) and status come with G4.
//
// G4'S PORTS (WORKER12; DECLARATIONS ONLY until P3 wires them: the build refuses a declared port left unwired, so the
// members below are declared to the framework in the constructor only when add_g1 wires them). Agreed with W11 (G3,
// src/units/mem.h on sim-w11) and the integrator, 2026-11-08; a change goes through the integrator.
//
//   port            kind               from -> to                          words                        latency
//   dma             RegFile            block -> AG                         DMA_CONFIG, record base      (level: the block's
//                                                                                                       endCycle(t) at level 0
//                                                                                                       of t + 1, read here)
//   start           Wire               block -> AG                         the count of PULSE_DMA       written at level 0 of
//                                                                          starts (a change = a start)  t + 1 (endCycle(t)),
//                                                                                                       acted on at t + 1
//   rd              Hold, width 2      AG -> srq client 1 (W11)            mem::req_a, mem::req_b       (Hold) refused at
//                                                                                                       t + 1, retried
//   stream_req      Link, width 3      AG -> table store (W11)             OP_READ64, IDX_STREAM + i, 0 ts_engine_latency
//   group_req       Link, width 3      AG -> table store (W11)             OP_READ64, IDX_GROUP + 2g, 0 ts_engine_latency
//   stream_ret      Link, width 3      table store -> AG                   SRC | DEST << 32, COUNT, idx ts_engine_latency
//   group_ret       Link, width 3      table store -> AG                   idx, the GROUP entry, 0      ts_engine_latency
//   status          Reg, width 1       AG -> block                         running | done << 1, at the  (Reg)
//                                                                          end of t; the block adds its
//                                                                          own pulse of t (runningFor-
//                                                                          Status, doneForStatus)
//   drift           Link(1), width 0   AG -> block                         a refusal in a group run     1 (ERRORS [3], D29)
//                                                                          (Conditions.DRIFT)
//   ptr_misaligned  Reg, width 1       table store (W11) -> AG             (PTR[me] & 3) != 0 at the    (Reg): at the AG's
//                                                                          end of t, put on change      step at t, the end of
//                                                                                                       t - 1, as ref's AG
//                                                                                                       (stepped before ts)
//                                                                                                       reads ts.peek
//   (proposed to W11, 2026-11-08: Tile.checkGatherAlignment's PTR half; if refused, that half is an explicit fatal.)
#pragma once

#include <cstdint>

#include "sim.h"
#include "units/write_engine.h"

namespace units {

struct AddrGen {
    sim::RegFileIn dma;
    // G4 (P3): declared when wired (above)
    sim::WireIn start;
    sim::HoldOut rd;
    sim::LinkOut stream_req, group_req;
    sim::LinkIn stream_ret, group_ret;
    sim::RegOut status;
    sim::LinkOut drift;
    sim::RegIn ptr_misaligned;
    enum : sim::Word { ST_RUNNING = 1, ST_DONE = 2 };   // `status`'s bits

    explicit AddrGen(sim::Ports& p) { p.in(dma, "dma"); }
    sim::Wake step(sim::Ctx&) { return sim::Wake::none(); }
    static uint32_t kind(uint32_t dma_config) { return mc_kind(dma_config); }
    static uint32_t run_start(uint32_t dma_config) { return mc_start(dma_config); }
    static uint32_t run_count(uint32_t dma_config) { return mc_count(dma_config); }
};

}  // namespace units
