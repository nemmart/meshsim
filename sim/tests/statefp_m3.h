// statefp_m3.h (WORKER16, G7): the canonical fingerprint of the M3 chip (C&C 1-3 as cores; AUDIT-M3 6.6-6.9; ref's side
// oracle/ref/StateFpM3.java). Every unit's stream at the end of a point, then the chip line, in StateFpM3.write's order:
// tiles 0-63 (M2's 16 units, `slot`, `rp`), C&C 1-3 (M2's 16 as cc1.* .. cc3.*, `slot`), the Tracer's five (`tr`,
// `tr.ic`, `tr.dc`, `tr.mem`, `tr.files`, when C&C 3's peripheral 1 is the Tracer), then `cc1p`, `cc.host`, `reqbus`,
// `cnc`, `ep`.
//
// The writers:
//   - M2's 16 (statefp_g1, statefp_g2): the controllers' names by units::unit_prefix; the router's controller variant
//     (link0 the C&C link, link1 the host Fifo, link2 C&C 3's DMA Fifo, link3 none, no fromCnc) and tiles 0-2's
//     fromCnc as the C&C link (statefp_g2's w_router); `req` and `reqbus` W14's (reqbus_fp.h);
//   - `slot` (6.7): mask, the DMA (peripherals with output queues), the consume and EVENTS_MASK store of c (the core's
//     ops on the slot's S2/S3 Link, put at c);
//   - `rp` (6.8): W15's PeriphSlot::fingerprint_replayer;
//   - the Tracer's five: W14's Tracer::fingerprint;
//   - `cc1p` (6.9): C&C 1's peripheral, its READY and error lines rebuilt from the Links on their way (statefp_g2's
//     pending_lines through C&C 1's own ports; the controllers' line words; the blocks' endCycle(c) previews);
//   - `cc.host`: the three host queues;
//   - `cnc`, `ep`: M2's Java model of C&C and its endpoints, not built with cores: their idle streams (ref builds them
//     and they stay idle).
// The cycle: a point labelled t (M3Seg: ref's c.cycle - 1) is the framework's state at now() == t + 2 (AUDIT-M3 7);
// the writers take FpCtx::cycle = t.
#pragma once

#include <string>
#include <vector>

#include "statefp.h"
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "units/host_port.h"
#include "units/periph_slot.h"
#include "units/reqbus.h"

namespace fp {

struct M3Chip {
    sim::Sim* m = nullptr;
    G1Tile g1[67];
    G2Tile g2[67];
    G2Chip gc;
    units::reqbus::ReqBusUnits rbu;
    const units::PeriphSlot* slot[67] = {};
    const units::HostPort* host[3] = {};
    const units::tracer::Tracer* tr = nullptr;     // C&C 3's peripheral 1, or null
    unsigned link_latency = units::NetConfig{}.link_latency, cc_link_latency = units::NetConfig{}.cc_link_latency;   // the sleep judgment's
    std::vector<std::string> names;                // every unit, in StateFpM3.write's order
    // EVENTS_MASK stores in flight one cycle before a point (m3_note_before): ref records a store executed at s as
    // pending at s + 1 (pendMaskT = s + 1, the mask already applied at s); the framework's has been applied by then.
    struct MaskStore { bool seen = false; uint64_t at = 0; uint32_t v = 0; };
    MaskStore mask_store[67][4];
    MaskStore consume[67][4];                      // the same for a consuming EVENTS load (ref's consumeT = s + 1)
};

// Resolve every unit of the cores chip `s` (t2::build_machine's), and the reqbus with `rc`.
void m3_attach(sim::Sim& s, M3Chip& c, const units::reqbus::ReqBusConfig& rc);
// Between cycles, at the cycle before a point (label t - 1): note every slot's EVENTS_MASK store in flight (executed at
// t - 1), which ref shows as pending at point t.
void m3_note_before(M3Chip& c, const sim::Sim& s, uint64_t label);
// Register every writer. `c` outlives the registry.
void m3_register(FpRegistry& r, M3Chip& c);
// A point: every unit's line "t name hash" and the chip line, appended to `out` (x.cycle = t). M2's writers (the 16
// tile units, reqbus) read the framework's state as at the end of their x.cycle (M2's convention: now() - 1): they are
// called with t + m2_offset.
void m3_point(const FpRegistry& r, const M3Chip& c, const FpCtx& x, std::vector<std::string>& out, unsigned m2_offset = 1);
bool m3_own_writer(const std::string& name);   // M3's writers (cycle t), not M2's
// Set by the slot's writer when a PULSE_DMA stored at the point's cycle is in flight to the slot (ref applied it at t,
// including the peripheral's out_start): the slot's stream is written as applied, the peripheral's (the Tracer's) isn't
// (asked of W14: an out_start overlay). The caller clears it before a point and reads it after.
extern thread_local bool g_dma_pulse_at_point;

}  // namespace fp
