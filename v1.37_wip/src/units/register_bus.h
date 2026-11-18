// register_bus.h: the decode of region 0xFF (machine-spec 5; the RTL's rtl/blocks/register_bus). It holds the whole map
// of region 0xFF and refuses every access fault there itself (5.1, 5.2, 5.3): the core hands it each load and store to
// region 0xFF, and it names the owner the access reaches, a table, a word of the tile window or a word of a present
// peripheral's window, or a fault, or a load that reaches no owner and reads 0. Constant code only: the owners are
// reached through the core's environment (tile_ctl.h, TileCore::Env).
//
//   - The tables (5.2): windows 0-10; every word of a table is loaded and stored.
//   - The tile window (5.5), window 12: each defined word R (a store faults), W (a load reads 0 and reaches no owner) or
//     R/W; block 7 and every word a block does not define are unmapped.
//   - Peripheral n's window (17.1), window 12 + n, present by the strap `peripherals`: the control block's words 0-6
//     (as the tile's; START W); block 1 its DMA's (DMA_HDR, DMA_DEST_ADDR R/W, DMA_START W), or, for a peripheral
//     without a DMA, every word of it reading 0 and a store there a fault; blocks 2-7 the peripheral's own, each word
//     as its type defines it (the owner says: `own`).
//   - Unmapped, an access fault: the rest of region 0xFF, window 11, an absent peripheral's window, a block a window
//     does not use and a word a block does not define. And: any access of other than a whole word, and a store to an R
//     word.
// The latencies are the core's to model (6.4; regmap.h's WINDOW_LOAD_LATENCY and TABLE_LOAD_LATENCY).
#pragma once

#include <cstdint>

#include "units/regmap.h"

namespace units::bus {

// The bus's straps (the RTL's parameters PERIPHERALS and PERIPHERAL_DMAS): bit n - 1 for peripheral n.
struct Straps {
    uint32_t peripherals = 0;      // the peripherals present
    uint32_t peripheral_dmas = 0;  // those with a DMA (block 1)
};

enum class Target : uint8_t {
    FAULT,           // an access fault: reaches no owner
    ZERO,            // a load that reaches no owner and reads 0 (a W word, a DMA-less peripheral's block 1)
    TABLE,           // `table`, `index` (table_store.h's TBL_*; GROUP's index is its word)
    TILE,            // the tile window's word `word` (regmap.h's Reg)
    PERIPH_CONTROL,  // peripheral `n`'s control block or DMA block, word `word` (regmap.h's PeriphReg)
    PERIPH_OWN,      // peripheral `n`'s own word `word` (32-127): its owner says whether the word is defined and what
                     // the access is (a fault otherwise)
};
struct Decoded {
    Target target = Target::FAULT;
    uint8_t n = 0;       // the peripheral, 1-3
    uint8_t word = 0;    // the window word, 0-127
};

// The access to address `a` of region 0xFF: `store`, a store or a load; `whole_word`, the access is a whole aligned word.
inline Decoded decode(const Straps& s, uint32_t a, bool store, bool whole_word) {
    if (!whole_word || !in_regs(a)) return {};
    const unsigned window = window_index(a);
    if (window < WINDOW_UNMAPPED) return {Target::TABLE, 0, 0};
    if (window == WINDOW_UNMAPPED) return {};
    const unsigned w = window_word(a);
    auto by_access = [&](units::Access acc, Target t, unsigned n) -> Decoded {
        if (acc == A_NONE) return {};
        if (store) return acc & A_W ? Decoded{t, uint8_t(n), uint8_t(w)} : Decoded{};
        return acc & A_R ? Decoded{t, uint8_t(n), uint8_t(w)} : Decoded{Target::ZERO, uint8_t(n), uint8_t(w)};
    };
    if (window == WINDOW_TILE) return by_access(reg_info(w).access, Target::TILE, 0);
    const unsigned n = window - WINDOW_TILE;
    if (!(s.peripherals >> (n - 1) & 1)) return {};
    if (w < BLOCK_WORDS) {                                   // the control block (17.1): as the tile's, START W
        static constexpr units::Access kControl[7] = {A_R, A_R, A_R, A_R, A_R, A_RW, A_W};
        return w < 7 ? by_access(kControl[w], Target::PERIPH_CONTROL, n) : Decoded{};
    }
    if (w < 2 * BLOCK_WORDS) {                               // block 1: the DMA's
        if (!(s.peripheral_dmas >> (n - 1) & 1)) return store ? Decoded{} : Decoded{Target::ZERO, uint8_t(n), uint8_t(w)};
        // v1.37: own words in block 1 beside the DMA's (17.6.1's UART), the owner's to define; without a DMA the block
        // reads 0 all the same (17.1)
        if (w == 30 || w == 31) return {Target::PERIPH_OWN, uint8_t(n), uint8_t(w)};
        static constexpr units::Access kDma[3] = {A_RW, A_RW, A_W};
        return w - BLOCK_WORDS < 3 ? by_access(kDma[w - BLOCK_WORDS], Target::PERIPH_CONTROL, n) : Decoded{};
    }
    return {Target::PERIPH_OWN, uint8_t(n), uint8_t(w)};
}

}  // namespace units::bus
