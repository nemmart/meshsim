// statefp_g2.h: the canonical fingerprint's writers for G2's chip-level units (WORKER9; StateFp.java's "cnc", "cc1p",
// "reqbus", "ep", word for word). A writer reads a G2Chip: the units as built, and the machine for the port observer
// (Options::observe_arrivals on, for the Links' arrival cycles).
//
// What ref holds as pending queues C&C's model holds in flight: a READY pulse or an error-line change made at the end
// of cycle e is in ref's `readyAt` / `errChanges` / `errLine` from e on, visible at e + ready_latency; here it is a line
// word on its way (the register block's Link to the row collector, then the collector's Link to C&C), applied at
// arrival. The writers rebuild ref's queues from those Links' entries. One part is not on any Link at the end of e: the
// block's endCycle(e) runs at level 0 of e + 1 (the shifted end of cycle), so its READY rise and error level of e are
// asked of the tile's part through `edge` (W8: the block's preview of endCycle(e); a test: its stub).
#pragma once

#include <cstdint>

#include "statefp.h"
#include "units/cnc.h"

namespace sim { class Sim; }

namespace fp {

// What tile k's register block will put on its line at the end of cycle c (its endCycle(c), not yet run).
struct LineEdge { bool put = false; bool ready = false; bool level = false; };
using LineEdgeFn = LineEdge (*)(const void* ctx, unsigned k, uint64_t c);

struct G2Chip {
    const units::Cnc* cnc = nullptr;
    const units::RowCollector* rows[8] = {};
    const units::Endpoint* ep[2] = {};        // C&C 2 (memsub) and C&C 3
    unsigned ready_latency = 8;
    LineEdgeFn edge = nullptr;                // null: no edge pending at the end of any cycle
    const void* edge_ctx = nullptr;
};

void w_cnc(const void* g2chip, FpOut& o, const FpCtx& x);
void w_cc1p(const void* g2chip, FpOut& o, const FpCtx& x);
void w_reqbus(const void* g2chip, FpOut& o, const FpCtx& x);   // the request bus: idle in M2, its reset stream
void w_ep(const void* g2chip, FpOut& o, const FpCtx& x);

// Registers "cnc", "cc1p", "reqbus", "ep". `g` outlives the registry.
void register_g2(FpRegistry& r, const G2Chip& g);

}  // namespace fp
