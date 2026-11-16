// director.h: the traffic director (sim/ref/meshsim/src/TrafficDirector.java; AUDIT-M2 3.3), the only path into the
// tile, and the source of the controller memories' port B. DECLARATIONS: the ports are the agreed
// interface; the step comes with the implementation.
//
// At t (one packet a cycle, as ref): the packet visible on `in` is taken and switched on its mode (and, mode 0, its
// address's region):
//   bad (3.1: mode 0 to a region it can't reach, mode 3 at 128 bits) -> dropped, uncounted, an event on `bad` (ERRORS [1]
//   at endCycle(t): an event, not a Count, so it steps the block in ref's cycle: PORTS.md 8);
//   size 11 -> dropped, uncounted, no error (flagged); a misaligned mode 0, modes 6-7 -> fatal (UB in ref);
//   mode 0, region 0x00/0x01 -> `port_b` (DMEM and IMEM: the NoC port, 6.3; v1.36 has no command block: 6.5);
//   mode 0, region 0x03; modes 1, 2, 3 -> `we_in` (the write engine), and for modes 1-3, in the same cycle, PTR's
//   read-modify-write on `ts_req` and, with a histogram digit, HIST's on `hist_req` (12.4, 12.5: Links of
//   TABLE_LATENCY to the tables, so PTR and HIST are read at the end of a + L and written at the end of a + L + 1, as
//   the RTL's receive unit; the old pointer goes from the table store to the write engine: write_engine.h);
//   mode 4 -> `gather` (G4); mode 5 -> count only; mode 0 to a peripheral's region -> fatal, not modelled in M2.
// Counting (modes 0-3 and 5, counter 1-7): the counter id goes on `kline`, a Link(K + 1) into the register block, which
// applies the decrement in its endCycle(t + K) (level 0 of t + K + 1): ref's K-stage line, with no wake of its own. The
// judgement (a counter at 0 stays 0: unexpected, ERRORS [2]) is the block's, without a same-cycle store (D27).
//
// Port B. ref: the director puts on toPortB, Link(cm_latency), at t; ControllerMemories writes it at t + cm_latency
// (committed at the end of that cycle, seen by a load at t + cm_latency + 1). Here the memories stay in the core:
// `port_b` is a Link(cm_latency + 1) of link words into the core, applied before its step.
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
    bool g4 = false;                   // the tile has G4's gather engine wired: `gather_put` is declared
    bool periph_slot = false;          // (M3): the tile has the peripheral slot: `periph` is declared
    bool engine = false;               // the tile has the write engine's tables: `dma`, `ts_req`, `hist_req` are declared
    bool gather_note = false;          // the tile has the inject unit's read side: `gather_note` is declared
};

struct Director {
    // ---- inputs ----
    sim::LinkIn in;            // width 3: link words from the router (eject), Link(intra_tile_latency = 1)
    // ---- outputs ----
    sim::LinkOut port_b;       // width 3: mode 0 to regions 0x00/0x01 -> the core's port B, Link(cm_latency + 1)
    sim::LinkOut kline;        // width 1: a counted packet's counter id (1-7) -> the block, Link(K + 1)
    sim::LinkOut we_in;        // width 3: mode 0 region 0x03, modes 1-3 -> the write engine's pipeline, Link(1) (12.4)
    sim::RegFileIn dma;        // RECEIVE_CONFIG's digits (12.5), from the tile control unit; with cfg.engine
    sim::LinkOut ts_req;       // width 3, Link(TABLE_LATENCY): PTR's read-modify-write {OP_ADD_RET, PTR + i, step}; cfg.engine
    sim::LinkOut hist_req;     // width 3, Link(TABLE_LATENCY): count-on-arrival {OP_ADD_RET, digit, 1}; cfg.engine
    sim::FifoOut gather;       // width 3: mode 4 -> the GATHER engine's queue, Fifo(gather_depth) (G4)
    sim::LinkOut bad;          // width 0: a bad packet dropped at t (Conditions.BAD_MODE_OR_SIZE) -> the block, Link(1)
    sim::LinkOut gather_put;   // width 0: a mode-4 put on `gather` at t -> the block, Link(1) (G4: one of STATUS's
                               // three gather-busy inputs: ref's gatherIn.isEmpty counts the pending put); when cfg.g4
    sim::WireIn gather_full;   // the gather queue holds its depth at the start of this cycle (gather.h); when cfg.g4
    sim::LinkOut gather_ovf;   // width 0: a GATHER lost to the full queue in t -> the block, Link(1): ERRORS.gather_overflow is
                               // set at the end of t (13, 7.4; the RTL's gather_unit: the pulse is in the request's cycle); cfg.g4
    sim::LinkOut gather_note;  // width 0: the same, to the read arbiter (inject.h: gather_busy in the next cycle), Link(1);
                               // when cfg.gather_note
    sim::CountIn rp_stores;    // the core's read-port stores (read through t - 1: ref's director runs before the core)
    sim::LinkOut periph;       // (M3): width 3: mode 0 to region 0x04-0x06 -> the peripheral slot (S1), Link(3)
                               // (rp_latency + 1: it lands at the end of t + 2, AUDIT-M3 3.1); when cfg.periph_slot

    DirectorConfig cfg;
    std::unique_ptr<S1Tile> s1;        // S1's measurement (s1.h), the tile's; SIM_CHECKS only (add_g1), null otherwise
    void s1_accept([[maybe_unused]] uint64_t t) {
#ifdef SIM_CHECKS
        if (s1 && s1->on) s1->accept(t);
#endif
    }
    // statistics (not state: outside the fingerprint)
    uint64_t accepted = 0, per_mode[lw::N_MODES] = {}, count_only = 0, undefined_size = 0, violations = 0, bad_packets = 0;
    uint64_t gather_overflows = 0;     // GATHERs lost to a full gather queue (13: ERRORS.gather_overflow)
    uint64_t gather_first_overflow = 0;   // the cycle of the first (0: none): the checker's fact (invariant 1)
    // read-port results accepted (ReadPort.acceptedAt; the "rdport" fingerprint's outstanding = stores - this) and
    // packets accepted with results outstanding (TrafficDirector.readPortOverlaps: the software rule of 4.3, counted)
    uint64_t rp_accepted = 0, rp_overlaps = 0;
    // (harness-only; P2): ref's director log (TrafficDirector.log, tests): {t, payload [63:0], header, payload
    // [127:64]} of every accepted packet, into the harness's array (null on every other machine; fatal when full).
    uint64_t (*log)[4] = nullptr;
    uint32_t log_cap = 0, log_n = 0;

    Director(sim::Ports& p, const DirectorConfig& c) : cfg(c) {
        p.in(in, "in", 3);
        p.out(port_b, "port_b", 3); p.out(kline, "kline", 1);
        p.out(we_in, "we_in", 3); p.out(gather, "gather", 3);
        p.out(bad, "bad", 0);
        if (cfg.g4) { p.out(gather_put, "gather_put", 0); p.in(gather_full, "gather_full"); p.out(gather_ovf, "gather_ovf", 0); }
        if (cfg.gather_note) p.out(gather_note, "gather_note", 0);
        if (cfg.engine) { p.in(dma, "dma"); p.out(ts_req, "ts_req", 3); p.out(hist_req, "hist_req", 3); }
        if (cfg.periph_slot) p.out(periph, "periph", 3);
        p.in(rp_stores, "rp_stores");
    }
    // (harness-only): a test's own request on a table's line, made at this unit's step at t: [0] PTR's line, [1]
    // HIST's; {t, op, idx, add}, valid when t != 0
    uint64_t host_req[2][4] = {};
    // 12.4, 12.5: the packet's PTR entry (APPEND and AUGMENT: PTR[src]; BUCKET: PTR[the bucket digit]) advances by the
    // write's words; with a histogram digit, HIST at that digit counts one.
    void table_requests(sim::Ctx& c, unsigned mode, uint64_t d, uint64_t h) {
        const uint32_t cfgw = c.read(dma, 1);                  // DMA_R_RECEIVE (write_engine.h)
        const uint32_t bd = cfgw & 7, hd = cfgw >> 3 & 7;
        if ((mode == lw::M_BUCKET && bd > 4) || hd > 4)
            FATAL("traffic director (tile %u): RECEIVE_CONFIG 0x%x names a reserved digit (5-7: machine-spec 12.5) at cycle %llu",
                  cfg.tile, cfgw, (unsigned long long)c.now());
        const auto digit = [](uint64_t payload, uint32_t sel) { return sel == 0 ? 0u : uint32_t(payload >> (8 * (sel - 1)) & 0xFF); };
        const uint32_t idx = mode == lw::M_BUCKET ? digit(d, bd) : lw::src(h);
        const uint32_t stp = mode == lw::M_AUG ? 2 : 1u << lw::size(h);
        c.put(ts_req, 3 /* mem::OP_ADD_RET */, 0x100 /* mem::IDX_PTR */ + idx, stp);
        if (hd) c.put(hist_req, 3, digit(d, hd), 1);
#ifdef SIM_CHECKS
        if (s1 && s1->on) { s1->push(0, idx, c.now()); if (hd) s1->push(1, digit(d, hd), c.now()); }   // S1
#endif
    }
    sim::Wake step(sim::Ctx& c) {
        sim::Wake wake = sim::Wake::none();
        if (cfg.engine)                                        // (harness-only): a test's request on a line at t
            for (unsigned l = 0; l < 2; l++) {
                if (host_req[l][0] && host_req[l][0] == c.now()) {
                    c.put(l == 0 ? ts_req : hist_req, host_req[l][1], host_req[l][2], host_req[l][3]);
                    host_req[l][0] = 0;
                } else if (host_req[l][0] > c.now()) wake = sim::Wake::at_cycle(host_req[l][0]);
            }
        accept(c);
        return wake;
    }
    // One packet a cycle from the router's eject; the eject link brings at most one word a cycle, so nothing waits.
    void accept(sim::Ctx& c) {
        if (!c.ready(in)) return;
        const sim::Word* w = c.peek(in);
        const uint64_t d = w[0], h = w[1], hi = w[2];
        c.take(in);
        accepted++;
        if (log) {                                             // (harness-only)
            if (log_n == log_cap) FATAL("director (tile %u): the harness's log is full (%u entries)", cfg.tile, log_cap);
            uint64_t* e = log[log_n++];
            e[0] = c.now(); e[1] = d; e[2] = h; e[3] = hi;
        }
        const unsigned mode = lw::mode(h), ctr = lw::counter(h), size = lw::size(h);
        const uint32_t a = lw::addr(h);
        per_mode[mode]++;
        namespace ck = sim::check;                             // the checker's facts (src/check.h)
        if (lw::read_port_tag(h)) rp_accepted++;
        else if (c.through_last(rp_stores) > rp_accepted && lw::src(h) != cfg.tile) {
            rp_overlaps++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_READPORT_OVERLAP, c.through_last(rp_stores) - rp_accepted, d, h, hi);
        }
        if (mode == lw::M_WRITE && size != lw::SIZE_UNDEF && !lw::aligned(a, size))
            FATAL("traffic director (tile %u): a mode-0 packet at 0x%x not aligned to its size, cycle %llu: UB (v1.25 s8b)", cfg.tile, a, (unsigned long long)c.now());
        if (lw::bad_packet(mode, size, a, cfg.periph_present)) {
            c.put(bad); bad_packets++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_BAD_PACKET, 0, d, h, hi);
            return;
        }
        if (size == lw::SIZE_UNDEF) {
            undefined_size++; violations++;
            ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_SIZE_UNDEF, 0, d, h, hi);
            return;
        }
        if (lw::reserved_mode(mode))
            FATAL("traffic director (tile %u): a packet in the reserved mode %u at cycle %llu: UB (v1.25 s8e)", cfg.tile, mode, (unsigned long long)c.now());
        if (mode == lw::M_COUNT && ctr == 0) { ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_COUNT_CTR0, 0, d, h, hi); }   // a fact only (the count stays as before)
        if (ctr != 0 && lw::counted_mode(mode)) {
            c.put(kline, sim::Word(ctr));                      // decremented at endCycle(t + K)
        }
        switch (mode) {
        case lw::M_WRITE: {
            unsigned r = lw::region(a);
            if (r == lw::R_DMEM || r == lw::R_PMEM) {
                c.put(port_b, d, h, hi);
            } else if (r == lw::R_SRAM) {
                if ((lw::offset(a) >> 2) + lw::chunks(size) > cfg.mem_chunks)   // a fact only (invariant 19)
                    ck::record(c.now(), int(cfg.tile), ck::Kind::DIR_PAST_SRAM, lw::offset(a) >> 2, d, h, hi);
                c.put(we_in, d, h, hi);
                s1_accept(c.now());                             // S1 (ref's acceptQ.add)
            }
            else if (cfg.periph_slot && r >= 0x04 && r <= 0x06) c.put(periph, d, h, hi);   /* the slot (S1) */
            else FATAL("director (tile %u): a write to peripheral region 0x%x at cycle %llu: not modelled in M2", cfg.tile, r, (unsigned long long)c.now());
            break;
        }
        case lw::M_SRC: case lw::M_BUCKET: case lw::M_AUG:                                  // the write engine, through PTR
            c.put(we_in, d, h, hi); s1_accept(c.now());
            if (cfg.engine) table_requests(c, mode, d, h);
            break;
        case lw::M_GATHER:                                                                  // the gather unit (13)
            // the queue full at the start of this cycle: the request is lost, even if the head leaves in this cycle
            if (cfg.g4 && c.read(gather_full) != 0) {
                if (!gather_overflows++) gather_first_overflow = c.now();
                c.put(gather_ovf);
                break;
            }
            c.put(gather, d, h, hi);
            if (cfg.g4) c.put(gather_put);
            if (cfg.gather_note) c.put(gather_note);
            break;
        default: count_only++; break;                                                       // mode 5: counted only
        }
        return;
    }
    uint64_t digest() const {
        uint64_t h = digest_mix(accepted, count_only);
        for (uint64_t v : per_mode) h = digest_mix(h, v);
        return digest_mix(digest_mix(digest_mix(digest_mix(h, undefined_size), violations), bad_packets), rp_accepted);
    }
};

}  // namespace units
