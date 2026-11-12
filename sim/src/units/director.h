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
#include <memory>

#include "check.h"
#include "sim.h"
#include "base/fatal.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/rv32.h"
#include "units/s1.h"

namespace units {

struct DirectorConfig {
    unsigned tile = 0;                 // this tile's endpoint id (0-63)
    uint32_t periph_present = 1;       // PARAMS [2:0]: which peripheral regions are reachable (bad packets)
    uint32_t mem_chunks = 1u << 20;    // big SRAM's size, for mode 0 region 0x03's bounds (flagged, invariant 19)
    bool g4 = false;                   // the tile has G4's gather engine wired (W12): `gather_put` is declared
    bool periph_slot = false;          // W15 (M3): the tile has the peripheral slot: `periph` is declared
    unsigned K = 8;                    // W20: the delay line's length (the stray check's window)
    bool stray_check = false;          // W20 (harness-only): `regs` declared, ref's stray at acceptance (invariant 9)
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
    sim::CountIn rp_stores;    // W11: the core's read-port stores (read through t - 1: ref's director runs before the core)
    sim::LinkOut periph;       // W15 (M3): width 3: mode 0 to region 0x04-0x06 -> the peripheral slot (S1), Link(3)
                               // (rp_latency + 1: it lands at the end of t + 2, AUDIT-M3 3.1); when cfg.periph_slot

    DirectorConfig cfg;
    std::unique_ptr<S1Tile> s1;        // W26: S1's measurement (s1.h), the tile's; SIM_CHECKS only (add_g1), null otherwise
    void s1_accept([[maybe_unused]] uint64_t t) {
#ifdef SIM_CHECKS
        if (s1 && s1->on) s1->accept(t);
#endif
    }
    // statistics (not state: outside the fingerprint)
    uint64_t accepted = 0, per_mode[lw::N_MODES] = {}, count_only = 0, undefined_size = 0, violations = 0, bad_packets = 0;
    // W11: read-port results accepted (ReadPort.acceptedAt; the "rdport" fingerprint's outstanding = stores - this) and
    // packets accepted with results outstanding (TrafficDirector.readPortOverlaps: the software rule of 4.3, counted)
    uint64_t rp_accepted = 0, rp_overlaps = 0;
    // W20 (harness-only; P2): ref's director log (TrafficDirector.log, tests): {t, payload [63:0], header, payload
    // [127:64]} of every accepted packet, into the harness's array (null on every other machine; fatal when full).
    uint64_t (*log)[4] = nullptr;
    uint32_t log_cap = 0, log_n = 0;

    Director(sim::Ports& p, const DirectorConfig& c) : cfg(c) {
        p.in(in, "in", 3);
        p.out(port_b, "port_b", 3); p.out(landed, "landed", 0); p.out(kline, "kline", 1);
        p.out(we_in, "we_in", 3); p.out(gather, "gather", 3);
        p.out(bad, "bad", 0);
        if (cfg.g4) p.out(gather_put, "gather_put", 0);
        if (cfg.periph_slot) p.out(periph, "periph", 3);
        p.in(rp_stores, "rp_stores");
        if (cfg.stray_check) p.in(regs, "regs");
    }
    // W20 (harness-only; P2): ref's TrafficDirector.schedule's stray (invariant 9, at acceptance): a counted packet with
    // nothing armed for it beyond what is already on the delay line. The counters as the register block publishes them
    // for t (before the decrements landing at the end of t, which this window's lines still count: the same difference
    // as ref's); a counter store of the same cycle (ref's pendingAdd) isn't seen here.
    sim::RegFileIn regs;
    uint64_t line_t[64] = {};          // the delay line's puts: cycle, counter (one a cycle at most)
    uint8_t line_c[64] = {};
    uint32_t line_n = 0;
    bool stray(uint64_t t, unsigned ctr, sim::Ctx& c) {
        uint32_t prev = 0;
        for (uint32_t i = 0; i < 64 && i < line_n; i++) {
            const uint32_t j = (line_n - 1 - i) & 63;
            if (line_t[j] + cfg.K < t) break;                  // applied in the view by now (put at t' seen at t' + K + 1)
            if (line_c[j] == ctr) prev++;
        }
        line_t[line_n & 63] = t; line_c[line_n & 63] = uint8_t(ctr); line_n++;
        return int64_t(c.read(regs, WRITE_COUNTER_1 + ctr - 1)) - int64_t(prev) <= 0;
    }
    sim::Wake step(sim::Ctx& c) {
        if (!c.ready(in)) return sim::Wake::none();
        const sim::Word* w = c.peek(in);
        const uint64_t d = w[0], h = w[1], hi = w[2];
        c.take(in);
        accepted++;
        if (log) {                                             // W20 (harness-only)
            if (log_n == log_cap) FATAL("director (tile %u): the harness's log is full (%u entries)", cfg.tile, log_cap);
            uint64_t* e = log[log_n++];
            e[0] = c.now(); e[1] = d; e[2] = h; e[3] = hi;
        }
        const unsigned mode = lw::mode(h), ctr = lw::counter(h), size = lw::size(h);
        const uint32_t a = lw::addr(h);
        per_mode[mode]++;
        namespace ck = sim::check;                             // W20: the checker's facts (src/check.h)
        if (lw::read_port_tag(h)) rp_accepted++;
        else if (c.through_last(rp_stores) > rp_accepted && lw::src(h) != cfg.tile && !command(h)) {
            rp_overlaps++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_READPORT_OVERLAP, c.through_last(rp_stores) - rp_accepted, d, h, hi);
        }
        if (mode == lw::M_WRITE && size != lw::SIZE_UNDEF && !lw::aligned(a, size))
            FATAL("traffic director (tile %u): a mode-0 packet at 0x%x not aligned to its size, cycle %llu: UB (v1.25 s8b)", cfg.tile, a, (unsigned long long)c.now());
        if (lw::bad_packet(mode, size, a, cfg.periph_present)) {
            c.put(bad); bad_packets++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_BAD_PACKET, 0, d, h, hi);
            return sim::Wake::none();
        }
        if (size == lw::SIZE_UNDEF) {
            undefined_size++; violations++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_SIZE_UNDEF, 0, d, h, hi);
            return sim::Wake::none();
        }
        if (lw::reserved_mode(mode))
            FATAL("traffic director (tile %u): a packet in the reserved mode %u at cycle %llu: UB (v1.25 s8e)", cfg.tile, mode, (unsigned long long)c.now());
        if (mode == lw::M_COUNT && ctr == 0) { ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_COUNT_CTR0, 0, d, h, hi); }   // a fact only (the count stays as before)
        if constexpr (ck::kOn) {                               // W20: ref's checkUncounted, bootKinds (facts only)
            if (ctr == 0 && mode != lw::M_GATHER && mode != lw::M_COUNT) {
                const unsigned r = lw::region(a);
                if (mode != lw::M_WRITE) ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_UNCOUNTED_MODE, 0, d, h, hi);
                else if (r != lw::R_DMEM && r != lw::R_PMEM) ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_UNCOUNTED_REGION, r, d, h, hi);
                else if (!lw::controller(lw::src(h))) ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_UNCOUNTED_SRC, lw::src(h), d, h, hi);
                else if (cfg.stray_check && cfg.tile < 64 && (c.read(regs, STATUS) >> ST_LANDED & 1) &&   // command landed (the view
                         !(r == lw::R_DMEM && in_command(lw::offset(a))))                                  //  at t: ref's ctl.landed)
                    ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_OVERLAY_AFTER_COMMAND, 0, d, h, hi);
            }
            if (ctr == 0 && mode == lw::M_WRITE && lw::controller(lw::src(h)) && (lw::region(a) == lw::R_DMEM || lw::region(a) == lw::R_PMEM)) {
                const unsigned kind = lw::dst(h) == lw::BROADCAST ? 2 : 1;
                const uint32_t off = lw::offset(a);
                const bool slot0 = lw::region(a) == lw::R_DMEM && off <= 0x7FC0 && off + lw::bytes(size) > 0x7FC0;
                if (in_command(off) && lw::region(a) == lw::R_DMEM) {
                    if (slot0) {
                        if (overlay_kinds & ~kind) ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_COMMAND_KIND, kind, d, h, hi);
                        overlay_kinds = 0;
                    }
                } else overlay_kinds |= kind;
            }
        }
        if (ctr != 0 && lw::counted_mode(mode)) {
            if (cfg.stray_check && stray(c.now(), ctr, c)) ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_UNEXPECTED, ctr, d, h, hi);
            c.put(kline, sim::Word(ctr));                      // decremented at endCycle(t + K)
        }
        switch (mode) {
        case lw::M_WRITE: {
            unsigned r = lw::region(a);
            if (r == lw::R_DMEM || r == lw::R_PMEM) {
                if constexpr (ck::kOn) check_cmem(c.now(), d, h, hi, ctr, a, size);   // W20: ref's checkCmem (facts only)
                c.put(port_b, d, h, hi);
                uint32_t w0 = (lw::offset(a) & 0x7FFF) >> 2, n = lw::chunks(size);
                if (r == lw::R_DMEM && w0 <= kCmdWord && w0 + n > kCmdWord) c.put(landed);   // covers command slot 0
            } else if (r == lw::R_SRAM) {
                if ((lw::offset(a) >> 2) + lw::chunks(size) > cfg.mem_chunks)   // W20: a fact only (invariant 19)
                    ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_PAST_SRAM, lw::offset(a) >> 2, d, h, hi);
                c.put(we_in, d, h, hi);
                s1_accept(c.now());                             // W26: S1 (ref's acceptQ.add)
            }
            else if (cfg.periph_slot && r >= 0x04 && r <= 0x06) c.put(periph, d, h, hi);   /* W15: the slot (S1) */
            else FATAL("director (tile %u): a write to peripheral region 0x%x at cycle %llu: not modelled in M2", cfg.tile, r, (unsigned long long)c.now());
            break;
        }
        case lw::M_SRC: case lw::M_BUCKET: case lw::M_AUG: c.put(we_in, d, h, hi); s1_accept(c.now()); break;   // the write engine (G3, G4; W26: S1)
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
        return digest_mix(digest_mix(digest_mix(digest_mix(h, undefined_size), violations), bad_packets), rp_accepted);
    }
    // W20: ref's checkCmem (invariants 11, 19, 22) and bootKinds' state (invariant 25), facts only
    uint8_t overlay_kinds = 0;
    void check_cmem(uint64_t t, uint64_t d, uint64_t h, uint64_t hi, unsigned ctr, uint32_t a, unsigned size) {
        namespace ck = sim::check;
        const uint32_t off = lw::offset(a), n = lw::bytes(size);
        if (off + n > 0x8000) { ck::record(t, int(cfg.tile), ck::Kind::DIR_PAST_CMEM, lw::region(a), d, h, hi); return; }
        if (lw::region(a) != lw::R_DMEM) return;
        if (off < 16) ck::record(t, int(cfg.tile), ck::Kind::DIR_RESERVED_BYTES, 0, d, h, hi);
        auto in_reply = [](uint32_t x) { return x >= 0x7BC0 && x < 0x7BC0 + 1024; };
        const bool cmd = in_command(off) || in_command(off + n - 1), rep = in_reply(off) || in_reply(off + n - 1);
        if (cmd && (ctr != 0 || !lw::controller(lw::src(h)) || !in_command(off) || !in_command(off + n - 1)))
            ck::record(t, int(cfg.tile), ck::Kind::DIR_COMMAND_BLOCK, 0, d, h, hi);
        if (rep && (size == lw::SIZE_128 || off != 0x7BC0 + 8 * lw::src(h)))
            ck::record(t, int(cfg.tile), ck::Kind::DIR_REPLY_BLOCK, 0, d, h, hi);
    }
    static constexpr uint32_t kCmdWord = 0x7FC0 >> 2;                 // command slot 0 (ControllerMemories.COMMAND)
    static bool in_command(uint32_t byte) { return byte >= 0x7FC0 && byte < 0x7FC0 + 64; }   // ControllerMemories.inCommand
    // TrafficDirector's `command`: an uncounted mode-0 write from a controller into data memory's command area
    static bool command(uint64_t h) {
        const uint32_t a = lw::addr(h);
        return lw::mode(h) == lw::M_WRITE && lw::counter(h) == 0 && lw::controller(lw::src(h)) && lw::region(a) == lw::R_DMEM &&
               in_command(lw::offset(a));
    }
};

}  // namespace units
