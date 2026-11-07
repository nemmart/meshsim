// ag.h: the address generator (sim/ref/meshsim/src/AddressGenerator.java; AUDIT-M2 3.6).
// CONFIGURATION PART BY W8 (G1); G4 EXTENDS THIS FILE with the behaviour (stream and group runs, the read-ahead queues,
// its holding register into the read queue, the start pulse, its status), in this same unit.
//
// What exists now: the unit and its configuration (AddressGenerator.config at DMA_CONFIG's endCycle: kind, run start,
// run count), derived from the register block's `dma` RegFile (write_engine.h), never copied. Its start pulse
// (PULSE_DMA) and status come with G4.
#pragma once

#include <cstdint>

#include "sim.h"
#include "units/write_engine.h"

namespace units {

struct AddrGen {
    sim::RegFileIn dma;

    explicit AddrGen(sim::Ports& p) { p.in(dma, "dma"); }
    sim::Wake step(sim::Ctx&) { return sim::Wake::none(); }
    static uint32_t kind(uint32_t dma_config) { return mc_kind(dma_config); }
    static uint32_t run_start(uint32_t dma_config) { return mc_start(dma_config); }
    static uint32_t run_count(uint32_t dma_config) { return mc_count(dma_config); }
};

}  // namespace units
