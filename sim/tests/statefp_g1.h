// statefp_g1.h: the canonical fingerprint's writers for G1's units and for the configuration parts W8 built of G3's and
// G4's units (WORKER8; the streams are StateFp.java's, word for word), plus stubs that write the reset stream of units
// nobody has built yet (sram, sram.data, rdport, req). A writer reads a G1Tile: the tile's state as the units hold it.
//
// Until the port observer lands (NOTES.md), the bus parts of every stream are written empty, and state applied at the
// shifted end of cycle (the register block's endCycle(c), a table-window store of c) shows one cycle late.
#pragma once

#include "statefp.h"
#include "units/regblock.h"
#include "units/rv32.h"
#include "units/table_store.h"

namespace fp {

// regs is checked, every unit of tile 0 every cycle, THROUGH THIS CYCLE ONLY: from 825 its mode-1 self-sends take G3's
// path (the write engine, PTR's read-modify-write, big SRAM), and a packet at the write engine is fatal until G3 lands
// (owner, 2026-11-07; PLAN-M2). Lift it on purpose, in the commit that lands that path.
constexpr uint64_t kRegsCheckedThrough = 824;

struct G1Tile {
    unsigned tile = 0;
    const units::Rv32* core = nullptr;           // its memories: core->dmem, core->imem
    const units::RegBlockState* ctl = nullptr;   // null: the reset state
    const units::TableMems* ts = nullptr;
    const units::HistMem* hist = nullptr;
};

// StateFp.core: pc, x1-x31, the CSRs, mcycle (caught up to the end of c for a core asleep since `last`, as Chip.catchUp
// does; a stopped core's keeps counting, as ref's halted core does), halt, inWfi, inHandler, stall (as at the end of c),
// sendRetry, regLoad and its pc.
void w_core(const void* g1tile, FpOut& o, const FpCtx& x);
// StateFp cmem: data and program memory (scanned: 2 x 8 K words, ~30 us; only big SRAM is kept incrementally), then the
// port-B bus (empty until the observer). `w_core_mem` is the same without the bus (CoreFp's "mem", M1's goldens).
void w_cmem(const void* g1tile, FpOut& o, const FpCtx& x);
void w_core_mem(const void* g1tile, FpOut& o, const FpCtx& x);
void w_we(const void* g1tile, FpOut& o, const FpCtx& x);
void w_ag(const void* g1tile, FpOut& o, const FpCtx& x);
void w_gather(const void* g1tile, FpOut& o, const FpCtx& x);
void w_ts(const void* g1tile, FpOut& o, const FpCtx& x);
void w_hist(const void* g1tile, FpOut& o, const FpCtx& x);
// Stubs: units not built yet, idle (their reset stream). G3 replaces sram, sram.data and rdport with its units' writers.
void w_sram_stub(const void*, FpOut& o, const FpCtx& x);
void w_sram_data_stub(const void*, FpOut& o, const FpCtx& x);
void w_rdport_stub(const void*, FpOut& o, const FpCtx& x);
void w_req(const void* g1tile, FpOut& o, const FpCtx& x);   // the request port: idle in M2 (AUDIT-M2 1.2 (C))

// Registers every writer above for tile g.tile ("t<k>.we", ...). `g` outlives the registry.
void register_g1(FpRegistry& r, const G1Tile& g);

}  // namespace fp
