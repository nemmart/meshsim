// gather.h: the GATHER engine (sim/ref/meshsim/src/GatherEngine.java; AUDIT-M2 3.6).
// CONFIGURATION PART BY W8 (G1); G4 EXTENDS THIS FILE with the behaviour (the active request, its holding register into
// the read queue, busy), in this same unit.
//
// What exists now: the unit, its input from the director (`in`, the gather queue, mode 4) and its configuration (k from
// DMA_CONFIG, base from DMA_RECORD_BASE_ADDRESS: Tile.setMasterConfig / setRecordBase), derived from the register
// block's `dma` RegFile (write_engine.h). ref's reset value of k is 1 (DMA_CONFIG 0 gives k = 1 too).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/write_engine.h"

namespace units {

struct Gather {
    sim::FifoIn in;            // width 3: mode-4 packets from the director, Fifo(gather_depth)
    sim::RegFileIn dma;

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
