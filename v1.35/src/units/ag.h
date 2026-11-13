// ag.h: the address generator (sim/ref/meshsim/src/AddressGenerator.java; AUDIT-M2 3.6).
// Configuration part; the behaviour, ported line by line from AddressGenerator.java (its names
// kept, for StateFp.ag): the start, the two read-ahead rings, the STREAM reader, stream runs and group runs, the
// refusal and retry, rd_done, the drift event, checkEntry's counts.
//
// The configuration (AddressGenerator.config at DMA_CONFIG's endCycle: kind, run start, run count) is read from the
// register block's `dma` RegFile at every step, never copied: ref's fields are overwritten by every DMA_CONFIG store,
// and its run reads them live.
//
// Wakes: ref steps it every cycle; here `next()` while running (the timetable counts cycles: gaps, rotations, lead-in),
// and otherwise only on an input (the start Wire, a table return). Timed wakes across gaps are a later speed item.
//
// When the tile has no read side (`wired` false), the G4 ports are not declared and the unit does nothing (the
// configuration part, as before). Ports agreed (src/units/mem.h) and the integrator, 2026-11-08.
//
//   port            kind               from -> to                          words                        latency
//   dma             RegFile            block -> AG                         DMA_CONFIG, record base      (level: the block's
//                                                                                                       endCycle(t) at level 0
//                                                                                                       of t + 1, read here)
//   start           Wire               block -> AG                         the count of PULSE_DMA       written at level 0 of
//                                                                          starts (a change = a start)  t + 1 (endCycle(t)),
//                                                                                                       acted on at t + 1
//   rd              Hold, width 2      AG -> srq client 1            mem::req_a, mem::req_b       (Hold) refused at
//                                                                                                       t + 1, retried
//   stream_req      Link, width 3      AG -> table store             OP_READ64, IDX_STREAM + i, 0 ts_engine_latency
//   group_req       Link, width 3      AG -> table store             OP_READ64, IDX_GROUP + 2g, 0 ts_engine_latency
//   stream_ret      Link, width 3      table store -> AG                   SRC | DEST << 32, COUNT, idx ts_engine_latency
//   group_ret       Link, width 3      table store -> AG                   idx, the GROUP entry, 0      ts_engine_latency
//   status          Reg, width 1       AG -> block                         running | done << 1, at the  (Reg)
//                                                                          end of t; the block adds its
//                                                                          own pulse of t (runningFor-
//                                                                          Status, doneForStatus)
//   drift           Link(1), width 0   AG -> block                         a refusal in a group run     1 (ERRORS [3], D29)
//                                                                          (Conditions.DRIFT)
//   ptr             RegFile, 256       table store -> AG             PTR (mem.h's PTR view)       (level): at the AG's
//                                                                                                       step at t, PTR as at
//                                                                                                       the end of t - 1, as
//                                                                                                       ref's AG (stepped
//                                                                                                       before ts) reads
//                                                                                                       ts.peek (the side)
//   Tile.checkGatherAlignment reads PTR[me] and the record base at a run's first mode-4 stream (UB: fatal).
#pragma once

#include <algorithm>
#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/mem.h"
#include "units/net.h"
#include "units/write_engine.h"

namespace units {

struct AddrGen {
    static constexpr unsigned kMaxLookahead = 64, MAXGROUPS = 64;
    enum : sim::Word { ST_RUNNING = 1, ST_DONE = 2 };   // `status`'s bits
    enum : uint32_t { RUN_STREAMS = 0, RUN_GROUPS = 1 };

    sim::RegFileIn dma;
    // G4: declared when `wired`
    sim::WireIn start;
    sim::HoldOut rd;
    sim::LinkOut stream_req, group_req;
    sim::LinkIn stream_ret, group_ret;
    sim::RegOut status;
    sim::LinkOut drift;
    sim::RegFileIn ptr;

    // ---- configuration ----
    uint32_t me = 0, lookahead = 6;
    bool wired = false;
    // ---- state (AddressGenerator's fields, StateFp.ag's order) ----
    bool running = false, done = false, lead_in = false, finished_issuing = false, gather_checked = false;
    uint32_t sq_out = 0, cq_out = 0, stream_discard = 0, group_discard = 0, s_reads = 0, c_reads = 0, rg = 0, rr = 0, rv = 0;
    uint32_t sl_run = 0; bool cur = false;
    uint32_t c_src = 0, c_dest = 0, c_count = 0, c_j = 0, c_d = 0, c_mode = 0, c_size = 0, c_gap = 0, c_ctr = 0, gap_left = 0;
    uint32_t g = 0, r = 0, v = 0, g_r = 0, g_t = 0, g_gap = 0;
    int64_t group_s = 0, rot_sched = 0;
    bool in_rot = false, stalled_since_group = false;
    uint64_t sq[kMaxLookahead] = {}, sq1[kMaxLookahead] = {}, cq[kMaxLookahead] = {};
    uint32_t sq_buf = 0, sq_head = 0, cq_buf = 0, cq_head = 0;
    // ---- the unit's own (not ref's state): the start Wire's count seen, the Hold's words, status as last put ----
    sim::Word starts_seen = 0, held[2] = {};
    sim::Word status_out = 0;
    bool refused_now = false, put_this_cycle = false;
    uint64_t pop_e1 = 0;
    // ---- statistics (per run and cumulative, as ref's) and checkEntry's counts (Chip's violations) ----
    int64_t started_at = -1, done_at = -1, first_request_at = -1;
    uint64_t requests = 0, stall_cycles = 0, rotations = 0, rotations_late = 0, max_rot_late = 0, groups_late_prefetch = 0,
             max_group_late = 0, underrun_cycles = 0, stream_reads = 0, group_reads = 0, group_stall_cycles = 0,
             zero_groups = 0, zero_groups_with_gap = 0, table_wait_cycles = 0;
    uint32_t stream_q_peak = 0, group_q_peak = 0;
    int64_t group_start[MAXGROUPS] = {}, group_late_by[MAXGROUPS] = {};
    uint64_t drift_events = 0, runs = 0, cum_requests = 0, cum_stall = 0, cum_rot_late = 0, cum_grp_late = 0,
             cum_zero_groups = 0, cum_zero_groups_with_gap = 0, cum_group_stall = 0;
    uint64_t size_violations = 0, mode_violations = 0, aug128_violations = 0, dest_violations = 0, align_violations = 0;
    uint64_t violations() const { return size_violations + mode_violations + aug128_violations + dest_violations + align_violations; }

    AddrGen() = default;
    // (the checker, SIM_CHECKS only): ref's `!ag.idle() || ag.runningForStatus()` as the core's table-window check
    // reads it (ref's core runs after the AG, whose step consumes pStart): driven at a step at t that changes `running`
    // (it changes only in a step): t << 2 | the value before << 1 | the value after (TileCore::table_check).
    sim::WireOut running_out;
    bool running_wire = false, running_driven = false;
    explicit AddrGen(sim::Ports& p, uint32_t tile = 0, uint32_t ag_lookahead = 6, bool wired_ = false, bool running_wire_ = false)
        : me(tile), lookahead(ag_lookahead), wired(wired_), running_wire(running_wire_) {
        p.in(dma, "dma");
        if (!wired) return;
#ifdef SIM_CHECKS
        if (running_wire) p.out(running_out, "running");
#endif
        if (lookahead < 1 || lookahead > kMaxLookahead) FATAL("address generator: ag_lookahead %u (1-%u)", lookahead, kMaxLookahead);
        p.in(start, "start");
        p.out(rd, "rd", mem::kReadReqWidth);
        p.out(stream_req, "stream_req", mem::kTsWidth); p.out(group_req, "group_req", mem::kTsWidth);
        p.in(stream_ret, "stream_ret", mem::kTsWidth); p.in(group_ret, "group_ret", mem::kTsWidth);
        p.out(status, "status", 1);
        p.out(drift, "drift", 0);
        p.in(ptr, "ptr");
    }

    static AddrGen idle() { return AddrGen(); }           // the reset state, outside a machine (fingerprint writers)
    static uint32_t kind(uint32_t dma_config) { return mc_kind(dma_config); }
    static uint32_t run_start(uint32_t dma_config) { return mc_start(dma_config); }
    static uint32_t run_count(uint32_t dma_config) { return mc_count(dma_config); }

    // Fields.java: STREAM's entry (e0 = SRC | DEST << 32, e1 = COUNT) and GROUP's
    static uint32_t s_src(uint64_t e0) { return uint32_t(e0 & 0x1FFFFF); }
    static uint32_t s_gap(uint64_t e0) { return uint32_t(e0 >> 21 & 7); }
    static uint32_t s_dest(uint64_t e0) { return uint32_t(e0 >> 32); }
    static uint32_t s_count(uint64_t e1) { return uint32_t(e1 & 0x1FFFF); }
    static uint32_t s_size(uint64_t e1) { return uint32_t(e1 >> 17 & 3); }
    static uint32_t s_counter(uint64_t e1) { return uint32_t(e1 >> 19 & 7); }
    static uint32_t s_mode(uint64_t e1) { return uint32_t(e1 >> 22 & 7); }
    static uint32_t s_d(uint64_t e1) { return uint32_t(e1 >> 25 & 0x7F); }
    static uint32_t grp_R(uint64_t e) { return uint32_t(e & 0x3FFFF); }
    static uint32_t grp_start(uint64_t e) { return uint32_t(e >> 32 & 0xFF); }
    static uint32_t grp_T(uint64_t e) { return uint32_t(e >> 40 & 0xF) + 1; }
    static uint32_t grp_gap(uint64_t e) { return uint32_t(e >> 44 & 0xFF); }
    static uint32_t chunks_of(uint32_t size) { return size == lw::SIZE_UNDEF ? 0 : 1u << size; }

    // ================================================================ cycle (AddressGenerator.runCycle)
    sim::Wake step(sim::Ctx& c) {
        if (!wired) return sim::Wake::none();
        const int64_t t = int64_t(c.now());
        const uint32_t mc = c.read(dma, DMA_R_CONFIG);
        const uint32_t knd = kind(mc), rstart = run_start(mc), rcount = run_count(mc);
        const sim::Word s = c.read(start);
        if (s != starts_seen) {                                          // the start pulse of endCycle(t - 1)
            if (s != starts_seen + 1) FATAL("address generator (tile %u): %llu starts in one cycle at %lld", me, (unsigned long long)(s - starts_seen), (long long)t);
            starts_seen = s;
            if (running) FATAL("address generator (tile %u), cycle %lld: a start while a run is running, its lead-in included (8.6; DECISIONS-v1.25 s8e): UB", me, (long long)t);
            load(); begin(t, knd);
            const uint32_t size = knd == RUN_STREAMS ? 256 : 64;
            if (rstart + rcount > size)
                FATAL("address generator (tile %u), cycle %lld: a %s run past the table's end (start %u, count %u > %u; spec 8.5): UB",
                      me, (long long)t, knd == RUN_STREAMS ? "stream" : "group", rstart, rcount, size);
            if (rcount == 0) { running = false; lead_in = false; done = true; done_at = t; finished_issuing = true; }
        }
        table_return(c);
        table_request(c, knd, rstart, rcount);
        if (running) {
            if (c.refused(rd)) {
                c.put(rd, held[0], held[1]); stall_cycles++; refused_now = true; stalled_since_group = true;
                if (knd == RUN_GROUPS) { group_stall_cycles++; drift_events++; c.put(drift); }   // drift (4.13)
            } else {
                refused_now = false;
                if (knd == RUN_STREAMS) stream_issue(c, t, rcount); else group_issue(c, t, rcount);
            }
            if (finished_issuing && !refused_now && !c.held_at_start(rd) && !put_this_cycle) { running = false; done = true; done_at = t; }
            put_this_cycle = false;
        }
        stream_q_peak = std::max(stream_q_peak, sq_buf + sq_out); group_q_peak = std::max(group_q_peak, cq_buf + cq_out);
        const sim::Word st = (running ? sim::Word(ST_RUNNING) : 0) | (done ? sim::Word(ST_DONE) : 0);
        if (st != status_out) { c.put(status, st); status_out = st; }
#ifdef SIM_CHECKS
        if (running_wire && running != running_driven) {
            c.drive(running_out, sim::Word(t) << 2 | sim::Word(running_driven) << 1 | sim::Word(running));
            running_driven = running;
        }
#endif
        return running ? c.next() : sim::Wake::none();
    }

    // ================================================================ start (load, begin)
    void load() {
        done = false; finished_issuing = false; first_request_at = -1;
        cum_requests += requests; cum_stall += stall_cycles; cum_rot_late += rotations_late; cum_grp_late += groups_late_prefetch; runs++;
        cum_zero_groups += zero_groups; cum_zero_groups_with_gap += zero_groups_with_gap; cum_group_stall += group_stall_cycles;
        requests = stall_cycles = rotations = rotations_late = max_rot_late = groups_late_prefetch = max_group_late = 0;
        group_stall_cycles = zero_groups = zero_groups_with_gap = 0;
        underrun_cycles = stream_reads = group_reads = 0; stream_q_peak = group_q_peak = 0; table_wait_cycles = 0;
        for (auto& x : group_start) x = -1;
        for (auto& x : group_late_by) x = 0;
        stream_discard += sq_out; group_discard += cq_out;               // reads of an earlier run, dropped on return
        sq_buf = sq_head = sq_out = 0; cq_buf = cq_head = cq_out = 0;
        s_reads = 0; c_reads = 0; rg = 0; rr = 0; rv = 0;
        sl_run = 0; cur = false; g = 0;
    }
    void begin(int64_t t, uint32_t knd) {
        running = true; started_at = t; lead_in = true; gather_checked = false;
        if (knd == RUN_GROUPS) { g = 0; r = 0; v = 0; in_rot = false; group_s = t; rot_sched = group_s; stalled_since_group = false; }
        else gap_left = 0;
    }

    // ================================================================ the read-ahead queues
    void table_return(sim::Ctx& c) {
        if (c.ready(stream_ret)) {
            const sim::Word* w = c.peek(stream_ret);
            const uint64_t e0 = w[0], e1 = w[1];
            c.take(stream_ret);
            if (stream_discard > 0) stream_discard--;
            else { const uint32_t k = (sq_head + sq_buf) % lookahead; sq[k] = e0; sq1[k] = e1; sq_buf++; sq_out--; }
        }
        if (c.ready(group_ret)) {
            const uint64_t e = c.peek(group_ret)[1];
            c.take(group_ret);
            if (group_discard > 0) group_discard--;
            else { cq[(cq_head + cq_buf) % lookahead] = e; cq_buf++; cq_out--; }
        }
    }
    void read_stream(sim::Ctx& c, uint32_t i) { c.put(stream_req, mem::OP_READ64, mem::IDX_STREAM + (i & 255), 0); sq_out++; stream_reads++; }
    void read_group(sim::Ctx& c, uint32_t i) { c.put(group_req, mem::OP_READ64, mem::IDX_GROUP + 2 * (i & 63), 0); cq_out++; group_reads++; }

    void table_request(sim::Ctx& c, uint32_t knd, uint32_t rstart, uint32_t rcount) {
        if (!running) return;
        if (knd == RUN_STREAMS) {
            if (s_reads < rcount && sq_buf + sq_out < lookahead) { read_stream(c, rstart + s_reads); s_reads++; }
            return;
        }
        if (c_reads < rcount && cq_buf + cq_out < lookahead) { read_group(c, rstart + c_reads); c_reads++; }
        if (rg < g) { rg = g; rr = 0; rv = 0; }
        if (rg >= rcount) return;
        const uint32_t k = rg - g;
        if (k >= cq_buf) return;                                         // the reader's GROUP entry is not in yet
        const uint64_t ce = cq[(cq_head + k) % lookahead];
        const uint32_t R = grp_R(ce), tt = grp_T(ce);
        if (R == 0) { rg++; rr = 0; rv = 0; return; }                    // one group boundary per cycle
        if (sq_buf + sq_out >= lookahead) return;
        read_stream(c, grp_start(ce) + rv);
        if (++rv == tt) { rv = 0; if (++rr == R) { rr = 0; rg++; } }
    }
    uint64_t pop_stream() {
        const uint64_t e = sq[sq_head]; pop_e1 = sq1[sq_head];
        sq_head = (sq_head + 1) % lookahead; sq_buf--;
        return e;
    }

    // ================================================================ stream runs
    void stream_issue(sim::Ctx& c, int64_t t, uint32_t rcount) {
        if (gap_left > 0) { gap_left--; if (gap_left == 0) end_stream(rcount); return; }
        while (!cur) {
            if (sl_run >= rcount) return;
            if (sq_buf == 0) { if (!lead_in) table_wait_cycles++; return; }
            lead_in = false;
            const uint64_t pe0 = pop_stream();
            load_stream(pe0, pop_e1); cur = true; check_entry(c, t, pe0, pop_e1);
            const bool last = sl_run == rcount - 1;
            if (c_count == 0) {                                          // count 0: exactly its gap-after cycles
                if (c_gap == 0 || last) { end_stream(rcount); continue; }
                gap_left = c_gap - 1; if (gap_left == 0) end_stream(rcount);
                return;
            }
        }
        issue_element(c, t, c_j);
        c_j++;
        if (c_j == c_count) { if (c_gap > 0 && sl_run < rcount - 1) gap_left = c_gap; else end_stream(rcount); }
    }
    void end_stream(uint32_t rcount) { cur = false; sl_run++; if (sl_run >= rcount) finished_issuing = true; }
    void load_stream(uint64_t e0, uint64_t e1) {
        c_src = s_src(e0); c_dest = s_dest(e0); c_gap = s_gap(e0); c_d = s_d(e1);
        c_count = s_count(e1); c_mode = s_mode(e1); c_size = s_size(e1); c_ctr = s_counter(e1); c_j = 0;
    }
    void issue_element(sim::Ctx& c, int64_t t, uint32_t e) {
        const uint32_t dst = c_d < 64 ? (me + c_d) & 63 : c_d;          // d: relative 0-63, absolute 64- (4.2)
        const uint32_t n = chunks_of(c_size);
        uint32_t dst_addr = 0;
        if (c_mode == lw::M_WRITE) dst_addr = c_dest + e * (4 * n);     // plain addition, 32 bits (4.2)
        const uint32_t src = (c_src + e * n) & 0x1FFFFF;
        held[0] = mem::req_a(src, c_size, c_mode, c_ctr, dst); held[1] = mem::req_b(dst_addr);
        c.put(rd, held[0], held[1]);
        if (first_request_at < 0) first_request_at = t;
        requests++; put_this_cycle = true;
    }

    // ================================================================ group runs
    void group_issue(sim::Ctx& c, int64_t t, uint32_t rcount) {
        if (g >= rcount) { finished_issuing = true; return; }
        if (!in_rot) {
            if (t < rot_sched) return;
            if (r == 0) {
                if (cq_buf == 0) return;                                 // its GROUP entry is not in: it starts late
                const uint64_t ce = cq[cq_head];
                g_r = grp_R(ce); g_t = grp_T(ce); g_gap = grp_gap(ce);
                if (g_r > 0 && sq_buf == 0) return;                      // nor its first STREAM entry
                int64_t late_by = t - rot_sched;
                if (lead_in) { lead_in = false; group_s = t; rot_sched = t; late_by = 0; }   // S_0 is now
                if (late_by > 0 && !stalled_since_group) {               // a read-ahead wait: the timetable moves
                    groups_late_prefetch++; max_group_late = std::max(max_group_late, uint64_t(late_by)); group_late_by[g] = late_by;
                    group_s = t; rot_sched = t;
                }
                group_start[g] = t - started_at;
                stalled_since_group = false;
                if (g_r == 0) { zero_groups++; if (g_gap > 0) zero_groups_with_gap++; next_group(rcount); return; }
            }
            const int64_t late_by = t - rot_sched;
            if (late_by > 0) {
                rotations_late++; max_rot_late = std::max(max_rot_late, uint64_t(late_by));
                sim::check::record(uint64_t(t), int(me), sim::check::Kind::ROTATION_LATE, runs);   // invariant 3 (reported once a run)
            }
            in_rot = true; v = 0; rotations++;
        }
        if (sq_buf == 0) {                                               // underrun: the visit waits, the timetable moves
            underrun_cycles++; group_s++; group_late_by[g]++; max_group_late = std::max(max_group_late, uint64_t(group_late_by[g]));
            return;
        }
        const uint64_t e0 = pop_stream(), e1 = pop_e1;
        check_entry(c, t, e0, e1);                                       // every visit, idle or not
        if (r < s_count(e1)) { load_stream(e0, e1); issue_element(c, t, r); }
        end_visit(rcount);
    }
    void end_visit(uint32_t rcount) {
        v++;
        if (v < g_t) return;
        in_rot = false; r++;
        const int64_t P = g_t;                                           // period t (v1.1: every visit one cycle)
        if (r < g_r) rot_sched = group_s + int64_t(r) * P; else next_group(rcount);
    }
    void next_group(uint32_t rcount) {
        const int64_t P = g_t;
        group_s = group_s + std::max(int64_t(g_r) * P, int64_t(1)) + g_gap; rot_sched = group_s;   // R = 0: 1 cycle plus its gap
        cq_head = (cq_head + 1) % lookahead; cq_buf--;                   // the group's GROUP entry is freed
        g++; r = 0; in_rot = false;
        if (g >= rcount) finished_issuing = true;
    }

    // ================================================================ checkEntry (4.2, v1.2) and the gather alignment
    void check_entry(sim::Ctx& c, int64_t t, uint64_t e0, uint64_t e1) {
        const uint32_t m = s_mode(e1), z = s_size(e1), d = s_d(e1), ctr = s_counter(e1);
        if (m == lw::M_GATHER && !gather_checked) { gather_checked = true; gather_alignment(c, t); }
        if (m >= lw::M_RSVD6) FATAL("address generator (tile %u), cycle %lld: a stream in the reserved mode %u (3.1: modes 6-7; DECISIONS-v1.25 s8e): UB", me, (long long)t, m);
        namespace ck = sim::check;                                       // each check's fact (checkEntry's texts)
        const uint64_t T = uint64_t(t);
        if (z == lw::SIZE_UNDEF) { size_violations++; ck::record(T, int(me), ck::Kind::AG_SIZE_UNDEF); }
        if (m == lw::M_AUG && z == lw::SIZE_128) { aug128_violations++; ck::record(T, int(me), ck::Kind::AG_AUG128); }
        if (m == lw::M_WRITE && !aligned_bytes(s_dest(e0), z)) { align_violations++; ck::record(T, int(me), ck::Kind::AG_DEST_ALIGN, s_dest(e0), z); }
        if (!aligned_chunks(s_src(e0), z)) { align_violations++; ck::record(T, int(me), ck::Kind::AG_SRC_ALIGN, s_src(e0), z); }
        if (d > 66 && d != 126) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_DEST_ENDPOINT, d); }   // CC3 = 66, BROADCAST = 126
        else if (d == 126 && me < 64) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_BROADCAST_ON_TILE, me); }
        if (m == lw::M_GATHER && d != 0) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_GATHER_DEST, d); }
        if (m == lw::M_COUNT && ctr == 0) { mode_violations++; ck::record(T, int(me), ck::Kind::AG_COUNT_CTR0); }
    }
    static bool aligned_bytes(uint32_t addr, uint32_t size) { return size == lw::SIZE_UNDEF || (addr & ((4u << size) - 1)) == 0; }
    static bool aligned_chunks(uint32_t chunk, uint32_t size) { return size == lw::SIZE_UNDEF || (chunk & ((1u << size) - 1)) == 0; }
    // Tile.checkGatherAlignment (v1.25 s8c): the gather buffers 128-bit aligned, the source (the record base) and the
    // destination (PTR[me]); UB, so meshsim stops.
    void gather_alignment(sim::Ctx& c, int64_t t) {
        const uint32_t b = c.read(dma, DMA_R_RECORD_BASE) & 0x1FFFFF, p = c.read(ptr, me);
        if ((p & 3) != 0 || (b & 3) != 0)
            FATAL("address generator (tile %u), cycle %lld: a run with mode-4 streams starts with the gather buffers not 128-bit "
                  "aligned (DMA_RECORD_BASE_ADDRESS chunk 0x%x, PTR[me] chunk 0x%x; DECISIONS-v1.25 s8c): UB", me, (long long)t, b, p);
    }

    uint64_t digest() const {
        uint64_t h = sim::hash_fields(running, done, lead_in, finished_issuing, gather_checked, sq_out, cq_out, stream_discard,
                                      group_discard, s_reads, c_reads, rg, rr, rv, sl_run, cur, c_src, c_dest, c_count, c_j);
        h = sim::hash_fields(h, sim::hash_fields(c_d, c_mode, c_size, c_gap, c_ctr, gap_left, g, r, v, g_r, g_t, g_gap, group_s,
                                           rot_sched, in_rot, stalled_since_group, sq_buf, sq_head, cq_buf, cq_head));
        for (uint32_t k = 0; k < sq_buf; k++) { const uint32_t j = (sq_head + k) % lookahead; h = sim::hash_fields(h, sim::hash_fields(sq[j], sq1[j])); }
        for (uint32_t k = 0; k < cq_buf; k++) h = sim::hash_fields(h, cq[(cq_head + k) % lookahead]);
        return sim::hash_fields(h, sim::hash_fields(starts_seen, held[0], held[1], status_out));
    }
};

}  // namespace units
