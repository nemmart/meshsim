// director.h: the traffic director (sim/ref/meshsim/src/TrafficDirector.java; AUDIT-M2 3.3), the only path into the
// tile, and the source of the controller memories' port B (WORKER8, G1). DECLARATIONS: the ports are the agreed
// interface (sessions/WORKER8/NOTES.md); the step comes with the implementation.
//
// At t (one packet a cycle, as ref): the packet visible on `in` is taken and switched on its mode (and, mode 0, its
// address's region):
//   bad (3.1: mode 0 to a region it can't reach, mode 3 at 128 bits) -> dropped, uncounted, an event on `bad` (ERRORS [1]
//   at endCycle(t): an event, not a Count, so it steps the block in ref's cycle: PORTS.md 8);
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
#include "base/fatal.h"
#include "units/linkword.h"
#include "units/rv32.h"

namespace units {

struct DirectorConfig {
    unsigned tile = 0;                 // this tile's endpoint id (0-63)
    uint32_t periph_present = 1;       // PARAMS [2:0]: which peripheral regions are reachable (bad packets)
    uint32_t mem_chunks = 1u << 20;    // big SRAM's size, for mode 0 region 0x03's bounds (flagged, invariant 19)
    bool g4 = false;                   // the tile has G4's gather engine wired (W12): `gather_put` is declared
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
    sim::LinkOut bad;          // width 0: a bad packet dropped at t (Conditions.BAD_MODE_OR_SIZE) -> the block, Link(1)
    sim::LinkOut gather_put;   // width 0: a mode-4 put on `gather` at t -> the block, Link(1) (W12, G4: one of STATUS's
                               // three gather-busy inputs: ref's gatherIn.isEmpty counts the pending put); when cfg.g4

    DirectorConfig cfg;
    // statistics (not state: outside the fingerprint)
    uint64_t accepted = 0, per_mode[lw::N_MODES] = {}, count_only = 0, undefined_size = 0, violations = 0, bad_packets = 0;

    Director(sim::Ports& p, const DirectorConfig& c) : cfg(c) {
        p.in(in, "in", 3);
        p.out(port_b, "port_b", 3); p.out(landed, "landed", 0); p.out(kline, "kline", 1);
        p.out(we_in, "we_in", 3); p.out(gather, "gather", 3);
        p.out(bad, "bad", 0);
        if (cfg.g4) p.out(gather_put, "gather_put", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        if (!c.ready(in)) return sim::Wake::none();
        const sim::Word* w = c.peek(in);
        const uint64_t d = w[0], h = w[1], hi = w[2];
        c.take(in);
        accepted++;
        const unsigned mode = lw::mode(h), ctr = lw::counter(h), size = lw::size(h);
        const uint32_t a = lw::addr(h);
        per_mode[mode]++;
        if (lw::read_port_tag(h)) FATAL("director: a read-port result at cycle %llu: not modelled in M2 yet (G3)", (unsigned long long)c.now());
        if (mode == lw::M_WRITE && size != lw::SIZE_UNDEF && !lw::aligned(a, size))
            FATAL("director (tile %u): a mode-0 packet at 0x%x not aligned to its size, cycle %llu: UB (v1.25 s8b)", cfg.tile, a, (unsigned long long)c.now());
        if (lw::bad_packet(mode, size, a, cfg.periph_present)) { c.put(bad); bad_packets++; return sim::Wake::none(); }
        if (size == lw::SIZE_UNDEF) { undefined_size++; violations++; return sim::Wake::none(); }
        if (lw::reserved_mode(mode))
            FATAL("director (tile %u): a packet in the reserved mode %u at cycle %llu: UB (v1.25 s8e)", cfg.tile, mode, (unsigned long long)c.now());
        if (ctr != 0 && lw::counted_mode(mode)) c.put(kline, sim::Word(ctr));   // decremented at endCycle(t + K)
        switch (mode) {
        case lw::M_WRITE: {
            unsigned r = lw::region(a);
            if (r == lw::R_DMEM || r == lw::R_PMEM) {
                c.put(port_b, d, h, hi);
                uint32_t w0 = (lw::offset(a) & 0x7FFF) >> 2, n = lw::chunks(size);
                if (r == lw::R_DMEM && w0 <= kCmdWord && w0 + n > kCmdWord) c.put(landed);   // covers command slot 0
            } else if (r == lw::R_SRAM) c.put(we_in, d, h, hi);
            else FATAL("director (tile %u): a write to peripheral region 0x%x at cycle %llu: not modelled in M2", cfg.tile, r, (unsigned long long)c.now());
            break;
        }
        case lw::M_SRC: case lw::M_BUCKET: case lw::M_AUG: c.put(we_in, d, h, hi); break;   // the write engine (G3, G4)
        case lw::M_GATHER:                                                                  // the gather engine (G4)
            c.put(gather, d, h, hi);
            if (cfg.g4) c.put(gather_put);
            break;
        default: count_only++; break;                                                       // mode 5: counted only
        }
        return sim::Wake::none();                                     // the eject link brings at most one word a cycle
    }
    uint64_t digest() const {
        uint64_t h = digest_mix(accepted, count_only);
        for (uint64_t v : per_mode) h = digest_mix(h, v);
        return digest_mix(digest_mix(digest_mix(h, undefined_size), violations), bad_packets);
    }
    static constexpr uint32_t kCmdWord = 0x7FC0 >> 2;                 // command slot 0 (ControllerMemories.COMMAND)
};

}  // namespace units
