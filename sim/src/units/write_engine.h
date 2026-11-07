// write_engine.h: the write engine (sim/ref/meshsim/src/WriteEngine.java; AUDIT-M2 3.5).
// CONFIGURATION PART BY W8 (G1); G3 EXTENDS THIS FILE with the behaviour (the pointer stage, PTR's returns, the SRAM
// write hold, modes 0-3), in this same unit.
//
// What exists now: the unit, its input from the director (`in`, the we-in Fifo) and its configuration. The
// configuration is the register block's (TileControl.apply -> Tile.setMasterConfig / setRecordBase at endCycle(t), used
// from t + 1): the block's `dma` RegFile, read at this unit's step (a later level than the block's), so a store at t
// is seen at t + 1 as in ref. The digits and the augment base are derived from it, never copied.
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"

namespace units {

// The block's `dma` RegFile: the registers as TileControl holds them (dmaConfig; recordBase, already masked to a chunk).
enum : unsigned { DMA_R_CONFIG = 0, DMA_R_RECORD_BASE = 1, DMA_REGS = 2 };

// DMA_CONFIG's fields (Fields.java).
inline uint32_t mc_kind(uint32_t v) { return v & 1; }
inline uint32_t mc_start(uint32_t v) { return v >> 1 & 0xFF; }
inline uint32_t mc_count(uint32_t v) { return v >> 9 & 0x1FF; }
inline uint32_t mc_bucket_digit(uint32_t v) { return v >> 18 & 7; }   // 5-7: UB, stopped by the block
inline uint32_t mc_hist_digit(uint32_t v) { return v >> 21 & 7; }
inline uint32_t mc_k(uint32_t v) { return (v >> 24 & 7) + 1; }

struct WriteEngine {
    sim::FifoIn in;            // width 3: link words from the director (mode 0 region 0x03, modes 1-3), Fifo(we_in_depth)
    sim::RegFileIn dma;        // the block's DMA_CONFIG and record base

    WriteEngine(sim::Ports& p, unsigned we_in_depth) {
        p.in(in, "in", 3, we_in_depth);
        p.in(dma, "dma");
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(in)) {
            const sim::Word* w = c.peek(in);
            FATAL("write engine: a packet arrived at cycle %llu (header 0x%llx): not modelled in M2 yet (G3)",
                  (unsigned long long)c.now(), (unsigned long long)w[1]);
        }
        return sim::Wake::none();
    }
    // The configuration as the engine uses it (WriteEngine's fields), from the block's registers.
    static uint32_t bucket_digit(uint32_t dma_config) { return mc_bucket_digit(dma_config); }
    static uint32_t hist_digit(uint32_t dma_config) { return mc_hist_digit(dma_config); }
    static uint32_t augment_base(uint32_t record_base) { return record_base & 0x1FFFFF; }
};

}  // namespace units
