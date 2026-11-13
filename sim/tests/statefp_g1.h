// statefp_g1.h: the canonical fingerprint's writers for G1's units and for the configuration parts built of G3's and
// G4's units (the streams are StateFp.java's, word for word), plus stubs that write the reset stream of units
// nobody has built yet (sram, sram.data, rdport, req). A writer reads a G1Tile: the tile's state as the units hold it.
//
// With a machine (G1Tile::sim and the units), the writers read the units' inputs in flight at the end of c through the
// port observer (Sim::observe; the machine built with Options::observe_arrivals) and give ref's view at the end of c:
//   - a shifted input (one whose latency carries the +1 of the shifted end of cycle: port B, command landed, the
//     K-line, GO, the bad-packet event, the core's bus and table-window stores) arriving at c + 1 has been applied by
//     ref at c: the writer applies it to a copy (the block's endCycle(c), a memory's write); one arriving later is
//     listed with ref's arrival (one less);
//   - an unshifted bus (the eject link) is listed as it is.
// Without a machine (plain state: the direct driver, the tests), the state is taken as it is.
#pragma once

#include "statefp.h"
#include "units/regblock.h"
#include "units/rv32.h"
#include "units/controller.h"
#include "units/big_sram.h"
#include "units/inject.h"
#include "units/table_store.h"
#include "units/write_engine.h"
#include "units/ag.h"
#include "units/gather.h"
#include "units/reqbus.h"
#include "units/tile_ctl.h"

namespace fp {

// regs is checked, every unit of tile 0 every cycle, THROUGH THIS CYCLE ONLY: from 825 its mode-1 self-sends take G3's
// path (the write engine, PTR's read-modify-write, big SRAM), and a packet at the write engine is fatal until G3 lands
// (owner, 2026-11-07; PLAN-M2). Lift it on purpose, in the commit that lands that path.
// G3 (2026-11-08) landed that path: regs is checked to its end.
constexpr uint64_t kRegsCheckedThrough = ~uint64_t(0);

struct G1Tile {
    unsigned tile = 0;
    const units::Rv32* core = nullptr;           // its memories: core->dmem, core->imem
    const units::RegBlockState* ctl = nullptr;   // null: the reset state
    const units::TableMems* ts = nullptr;
    const units::HistMem* hist = nullptr;
    // with a machine: the units whose inputs the observer reads
    const sim::Sim* sim = nullptr;
    const units::TileCore* ucore = nullptr;
    const units::TileCtl* uctl = nullptr;
    const units::Director* udir = nullptr;
    const units::TableStore* uts = nullptr;
    const units::HistStore* uhist = nullptr;
    const units::WriteEngine* uwe = nullptr;      // G3
    const units::BigSram* usram = nullptr;        // G3
    const units::Injection* uinj = nullptr;       // G3: the read port's injections (rdport); set by a chip's
                                                  // driver (g1_tile doesn't: G1's tests have no injection unit)
    const units::AddrGen* uag = nullptr;          // G4
    const units::Gather* ugather = nullptr;       // G4
    const units::reqbus::ReqLeaf* uleaf = nullptr;   // the request port's leaf (null: no bus, the idle stream)
    // (M3): ref's idle-skip sleep (Chip.asleep, Tile.canSleep), which the framework doesn't model, as the M3 chip's
    // writer judges it (a sleeping core's mcycle under M3's labelling); null: a core in a WFI not stepped at c
    bool (*ref_asleep)(const void* ctx, unsigned k, const FpCtx& x) = nullptr;
    const void* ref_asleep_ctx = nullptr;
    const units::Core* m1core = nullptr;          // the M1 core: the bench's port-B words on `memw`
    const units::RegBlock* m1ctl = nullptr;       // the M1 block: its inputs in flight (bus, GO, landed, freed)
};
// An M2 tile's G1 units by name ("t<k>.core", ...), in a machine built with Options::observe_arrivals.
G1Tile g1_tile(sim::Sim& s, unsigned k);
// a chip with the request bus (build_chip): each tile's `req` stream from its leaf, the chip's `reqbus` from the
// bus (the G2Chip's). `units` must outlive the registry.
struct G2Chip;
void attach_reqbus(sim::Sim& s, G1Tile* g1, unsigned n_tiles, G2Chip& gc, units::reqbus::ReqBusUnits& units, const units::reqbus::ReqBusConfig& rc);

// StateFp.core: pc, x1-x31, the CSRs, mcycle (caught up to the end of c for a core asleep since `last`, as Chip.catchUp
// does; a stopped core's keeps counting, as ref's halted core does), halt, inWfi, inHandler, stall (as at the end of c),
// sendRetry, regLoad and its pc.
void w_core(const void* g1tile, FpOut& o, const FpCtx& x);
// StateFp cmem: data and program memory (scanned: 2 x 8 K words, ~30 us; only big SRAM is kept incrementally), then the
// port-B bus (empty until the observer). `w_core_mem` is the same without the bus (CoreFp's "mem", M1's goldens).
void w_cmem(const void* g1tile, FpOut& o, const FpCtx& x);
void w_core_mem(const void* g1tile, FpOut& o, const FpCtx& x);
// StateFp.ctl: the registers, the events and qualifiers, STATUS, SIM_EXIT; then the GO line's pending arrivals (an
// in-flight Link: empty until the observer). `w_ctl_nogo` stops before them (CoreFp's "ctlx": M1's bench keeps no queue).
void w_ctl(const void* g1tile, FpOut& o, const FpCtx& x);
void w_ctl_nogo(const void* g1tile, FpOut& o, const FpCtx& x);
// StateFp.dir: the write counters (the block's here), the K-stage line's pending decrements (a Link(K + 1) into the
// block: empty until the observer), the eject bus (likewise).
void w_dir(const void* g1tile, FpOut& o, const FpCtx& x);
void w_we(const void* g1tile, FpOut& o, const FpCtx& x);
void w_ag(const void* g1tile, FpOut& o, const FpCtx& x);
void w_gather(const void* g1tile, FpOut& o, const FpCtx& x);
void w_ts(const void* g1tile, FpOut& o, const FpCtx& x);
void w_hist(const void* g1tile, FpOut& o, const FpCtx& x);
// G3: the big SRAM's area queues and holds (StateFp.sramQ), its contents (sram.data: the incremental sum).
void w_sram(const void* g1tile, FpOut& o, const FpCtx& x);
void w_sram_data(const void* g1tile, FpOut& o, const FpCtx& x);
// G3: StateFp "rdport": inUse (stores - injected), outstanding (stores - accepted).
void w_rdport(const void* g1tile, FpOut& o, const FpCtx& x);
void w_req(const void* g1tile, FpOut& o, const FpCtx& x);   // the request port: idle in M2 (AUDIT-M2 1.2 (C))

// Registers every writer above for tile g.tile ("t<k>.we", ...). `g` outlives the registry.
void register_g1(FpRegistry& r, const G1Tile& g);

}  // namespace fp
