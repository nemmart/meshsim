// director.h: the traffic director (sim/ref/meshsim/src/TrafficDirector.java; AUDIT-M2 3.3), the only path into the
// tile, and the source of the controller memories' port B (WORKER8, G1). DECLARATIONS: the ports are the agreed
// interface (sessions/WORKER8/NOTES.md); the step comes with the implementation.
//
// At t (one packet a cycle, as ref): the packet visible on `in` is taken and switched on its mode (and, mode 0, its
// address's region):
//   bad (3.1: mode 0 to a region it can't reach, mode 3 at 128 bits) -> dropped, uncounted, `bad` counted (ERRORS [1]);
//   size 11 -> dropped, uncounted, no error (flagged); a misaligned mode 0, modes 6-7 -> fatal (UB in ref);
//   mode 0, region 0x00/0x01 -> `port_b` (and `landed` if it covers command slot 0);
//   mode 0, region 0x03; modes 1, 2, 3 -> `we_in` (the write engine, G3);
//   mode 4 -> `gather` (G4); mode 5 -> count only; mode 0 to a peripheral's region -> fatal, not modelled in M2.
// Counting (modes 0-3 and 5, counter 1-7): the counter id goes on `kline`, a Link(K + 1) into the register block, which
// applies the decrement in its endCycle(t + K) (level 0 of t + K + 1): ref's K-stage line, with no wake of its own. The
// judgement (a counter at 0 stays 0: unexpected, ERRORS [2]) is the block's, without a same-cycle store (D27).
//
// Port B. ref: the director puts on toPortB, Link(cm_latency), at t; ControllerMemories writes it at t + cm_latency
// (committed at the end of that cycle, seen by a load at t + cm_latency + 1) and latches command landed for
// endCycle(t + cm_latency). Here the memories stay in the core (W5): `port_b` is a Link(cm_latency + 1) of link words into
// the core, applied before its step, and `landed` a width-0 Link(cm_latency + 1) into the block (its shifted endCycle).
//
// Modelled in M2 by G1: modes 0 and 1 and the counting. Modes 2-4 have their place in the switch, fatal until G4 (the
// write engine's and gather's inputs exist from the start, wired by the tile builder to stubs until G3/G4). Couplings
// G3 adds: the read port's `acceptedAt` (a result accepted) and `outstanding` (the overlap check).
#pragma once

#include <cstdint>

#include "sim.h"
#include "units/linkword.h"

namespace units {

struct DirectorConfig {
    unsigned tile = 0;                 // this tile's endpoint id (0-63)
    uint32_t periph_present = 1;       // PARAMS [2:0]: which peripheral regions are reachable (bad packets)
    uint32_t mem_chunks = 1u << 20;    // big SRAM's size, for mode 0 region 0x03's bounds (flagged, invariant 19)
};

struct Director {
    // ---- inputs ----
    sim::LinkIn in;            // width 3: link words from W9's router (eject), Link(intra_tile_latency = 1)
    // ---- outputs ----
    sim::LinkOut port_b;       // width 3: mode 0 to regions 0x00/0x01 -> the core's port B, Link(cm_latency + 1)
    sim::LinkOut landed;       // width 0: that write covers command slot 0 (0x7FC0) -> the block, Link(cm_latency + 1)
    sim::LinkOut kline;        // width 1: a counted packet's counter id (1-7) -> the block, Link(K + 1)
    sim::FifoOut we_in;        // width 3: mode 0 region 0x03, modes 1-3 -> the write engine, Fifo(we_in_depth) (G3)
    sim::FifoOut gather;       // width 3: mode 4 -> the GATHER engine's queue, Fifo(gather_depth) (G4)
    sim::CountOut bad;         // bad packets dropped (Conditions.BAD_MODE_OR_SIZE, ERRORS [1])

    DirectorConfig cfg;
    // statistics (not state: outside the fingerprint)
    uint64_t accepted = 0, per_mode[lwx::N_MODES] = {}, count_only = 0, undefined_size = 0, violations = 0;

    Director(sim::Ports& p, const DirectorConfig& c) : cfg(c) {
        p.in(in, "in", 3);
        p.out(port_b, "port_b", 3); p.out(landed, "landed", 0); p.out(kline, "kline", 1);
        p.out(we_in, "we_in", 3); p.out(gather, "gather", 3);
        p.out(bad, "bad");
    }
    sim::Wake step(sim::Ctx& c);
};

}  // namespace units
