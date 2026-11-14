// ag.h: the DMA unit's address generator (machine-spec 8.2-8.5; the RTL's rtl/blocks/dma_unit: address_generator.veryl's
// header and the unit's README). It walks STREAM (and GROUP), reading their entries ahead of their visits, and for each
// element offers one read to the read arbiter.
//
// THE HARDWARE'S CYCLES. E is the cycle DMA_START's store is presented in (a store issued in t: E = t + 1), with
// DMA_CONFIG, which the transfer takes then and never reads again (8.2; the RTL latches it). L is TABLE_LATENCY.
//   - Read-ahead (8.5). Each table takes one read a cycle from E, issued while fewer than READ_AHEAD = 2L + 2 places are
//     held, by the count at the start of the cycle; an entry is in 2L + 1 cycles after its read's issue. A stream
//     transfer reads STREAM[start .. start + count - 1]; a group transfer reads GROUP likewise, and each group's STREAM
//     entries in visit order (its streams, R times over) once its GROUP entry is in, crossing at most one group
//     boundary a cycle: an R = 0 group takes the reader a cycle and no read.
//   - Places. An entry holds its place from its read's issue until its visit is MADE: its read taken by the arbiter,
//     or, when the visit sends nothing, the cycle itself. A stream transfer's entry until its stream begins (its first
//     element taken, or its first gap cycle when its count is 0); a GROUP entry until its group ends.
//   - The lead-in (8.2). The transfer's first cycle is E + 2L + 1 in a stream transfer and E + 4L + 2 in a group
//     transfer (2L + 2 and 4L + 3 after the start store), whatever its first entry holds.
//   - The timetable (8.3, 8.4). One element a cycle; after each stream but the last, DMA_CONFIG.gap idle cycles; a
//     count-0 stream takes the gap alone, and with a gap of 0, or as the last stream, no cycle (a PASS: in one cycle
//     every such stream whose entry is in passes, up to the first other stream). A group makes R rotations of its t
//     visits, one a cycle, a visit idle when its rotation is not below its stream's count, then waits its gap; an
//     R = 0 group takes one cycle and its gap. A cycle that waits for an entry not yet in shifts every later one.
//   - A refused read (offered, not taken) is held until taken, and every later time shifts with it: there is no
//     catching up. In a group transfer each refused cycle sets ERRORS.drift (`drift`).
//   - The end (8.2). The walk is complete in the cycle w the last entry is walked: the last stream's last element
//     taken, or its pass, or the last group's end; the last gap is not waited. EVENTS.dma_done is set in w + 1, at
//     whose end STATUS.dma_busy falls. An empty transfer's w is E: busy for the one cycle E + 1, dma_done at its end.
//
// THE MODEL'S STEPS. The read is offered through `rd`, a Hold: a put of step s is offered to the arbiter in cycle
// s + 1 and taken then or held (inject.h). So the step at s decides the hardware's cycle s + 1, one cycle ahead, as if
// the read offered in cycle s were taken; when it was not (the Hold refuses the next put, or still holds the read),
// what was decided for s + 1 waits for the first cycle it can have, and nothing is decided past it. The readers of
// cycle s are run at step s, when every visit made up to s - 1 is known, so a place is free exactly when the
// hardware's is. The tables are read through the table store's RegFiles (`view`), at the issue of each read: an entry
// the core stores to while the transfer reads it is undefined (8.6).
//
//   port      kind               from -> to                 words                                 when
//   dma       RegFile            tile control -> AG         DMA_CONFIG (write_engine.h)           read at the start
//   start     Wire               tile control -> AG         the count of DMA_STARTs (a change =   driven at E + 1, the
//                                                           a start)                              edge that ends E
//   view      RegFile            table store -> AG          STREAM's three arrays and GROUP       (level)
//   ptr       RegFile            table store -> AG          PTR, for the gather alignment check   (level)
//   rd        Hold, width 2      AG -> read arbiter         mem::req_a, mem::req_b                offered the cycle after
//   status    Reg, width 1       AG -> tile control         running | done << 1                   put at E + 1 and w + 1
//   drift     Link(1), width 0   AG -> tile control         a refused read in a group transfer    the cycle after it
#pragma once

#include <algorithm>
#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/mem.h"
#include "units/net.h"
#include "units/table_store.h"
#include "units/write_engine.h"

namespace units {

struct AddrGen {
    static constexpr unsigned MAX_L = 7, MAX_PLACES = 2 * MAX_L + 2, MAXGROUPS = 64;
    enum : sim::Word { ST_RUNNING = 1, ST_DONE = 2 };   // `status`'s bits
    enum : uint32_t { RUN_STREAMS = 0, RUN_GROUPS = 1 };

    sim::RegFileIn dma;
    // declared when `wired`
    sim::WireIn start;
    sim::HoldOut rd;
    sim::RegOut status;
    sim::LinkOut drift;
    sim::RegFileIn ptr, view;

    // ---- configuration ----
    uint32_t me = 0, L = 1, places = 4;    // the tile; TABLE_LATENCY; READ_AHEAD = 2L + 2
    bool wired = false;

    // ---- the transfer (DMA_CONFIG as taken at the start) ----
    bool running = false, done = false;
    uint32_t kind = 0, first = 0, count = 0, gap = 0;
    int64_t E = 0, lead = 0;               // the start's cycle; the transfer's first cycle

    // ---- read-ahead: the entries whose places are held, in issue order ----
    struct SEntry { uint64_t e0, e1; int64_t in; uint32_t index, r; bool last; };   // index: the STREAM entry; r, last: a visit's
    struct GEntry { uint64_t e; int64_t in; };
    SEntry sq[MAX_PLACES] = {};
    GEntry gq[MAX_PLACES] = {};
    uint32_t sq_head = 0, sq_n = 0, sq_used = 0;   // sq_used: of the entries held, those the walk has passed (not yet made)
    uint32_t gq_head = 0, gq_n = 0, gq_used = 0;
    uint32_t reads_left = 0, read_index = 0;       // the reader of DMA_CONFIG's entries (GROUP's or STREAM's)
    uint32_t rg = 0, rr = 0, rv = 0;               // the STREAM reader of a group transfer: group, rotation, visit
    uint32_t g_freed = 0;                          // groups whose GROUP entries have left (gq's head is group g_freed)

    // ---- the walk (as decided: ahead of the cycles by the actions not yet made) ----
    bool cur = false;                              // a stream begun
    uint32_t c_src = 0, c_dest = 0, c_count = 0, c_j = 0, c_d = 0, c_mode = 0, c_size = 0, c_ctr = 0, c_index = 0;
    uint32_t gap_left = 0, g = 0;
    bool walked = false;                           // the walk's last entry is decided
    // an action decided and not yet made: `next` waits for its cycle, `offer` is a read offered and not yet taken
    struct Action { bool valid, put, complete; uint32_t s_free, g_free; };
    Action next = {}, offer = {};
    bool put_prev = false;                         // a put (or a refused put again) at the step before
    sim::Word held[2] = {};
    int64_t done_due = -1;                         // the cycle to put `done` in (w + 1), when later than the step
    bool gather_checked = false;
    // ---- the unit's own: the start Wire's count seen, status as last put ----
    sim::Word starts_seen = 0, status_out = 0;

    // ---- statistics (per transfer and cumulative) and the entries' checks ----
    int64_t started_at = -1, done_at = -1, first_request_at = -1;   // E; w + 1; the first read's offer
    uint64_t requests = 0, stall_cycles = 0, rotations = 0, stream_reads = 0, group_reads = 0, group_stall_cycles = 0,
             zero_groups = 0, table_wait_cycles = 0, passes = 0;
    int64_t group_start[MAXGROUPS] = {};           // each group's first cycle, from E (-1: not begun)
    uint64_t drift_events = 0, runs = 0, cum_requests = 0, cum_stall = 0;
    // (v1.35's "late" counts measured a rotation against a schedule a refusal did not move; v1.36 has no such schedule:
    // a refused read shifts every later time (8.3, 8.4). Kept at 0 for the reports that print them.)
    uint64_t rotations_late = 0, max_rot_late = 0, groups_late_prefetch = 0, max_group_late = 0;
    uint64_t size_violations = 0, mode_violations = 0, aug128_violations = 0, dest_violations = 0, align_violations = 0;
    uint64_t violations() const { return size_violations + mode_violations + aug128_violations + dest_violations + align_violations; }

    AddrGen() = default;
    explicit AddrGen(sim::Ports& p, uint32_t tile = 0, uint32_t table_latency = 1, bool wired_ = false)
        : me(tile), L(table_latency), places(2 * table_latency + 2), wired(wired_) {
        p.in(dma, "dma");
        if (!wired) return;
        if (L < 1 || L > MAX_L) FATAL("address generator: TABLE_LATENCY %u (1-%u)", L, MAX_L);
        p.in(start, "start");
        p.out(rd, "rd", mem::kReadReqWidth);
        p.out(status, "status", 1);
        p.out(drift, "drift", 0);
        p.in(ptr, "ptr"); p.in(view, "view");
    }

    static AddrGen idle() { return AddrGen(); }           // the reset state, outside a machine

    // STREAM's entry (8.3: e0 = STREAM_SRC_ADDR | STREAM_DEST_ADDR << 32, e1 = STREAM_HDR: the header block [7:0], dst
    // [14:8], count [31:15]) and GROUP's (8.4: word 0 rotations [17:0], first_stream [25:18], streams - 1 [29:26]; word 1,
    // here bits [39:32], the gap).
    static uint32_t s_src(uint64_t e0) { return uint32_t(e0 & 0x1FFFFF); }
    static uint32_t s_dest(uint64_t e0) { return uint32_t(e0 >> 32); }
    static uint32_t s_count(uint64_t e1) { return uint32_t(e1 >> 15 & 0x1FFFF); }
    static uint32_t s_size(uint64_t e1) { return uint32_t(e1 & 3); }
    static uint32_t s_counter(uint64_t e1) { return uint32_t(e1 >> 2 & 7); }
    static uint32_t s_mode(uint64_t e1) { return uint32_t(e1 >> 5 & 7); }
    static uint32_t s_d(uint64_t e1) { return uint32_t(e1 >> 8 & 0x7F); }
    static uint32_t grp_R(uint64_t e) { return uint32_t(e & 0x3FFFF); }
    static uint32_t grp_start(uint64_t e) { return uint32_t(e >> 18 & 0xFF); }
    static uint32_t grp_T(uint64_t e) { return uint32_t(e >> 26 & 0xF) + 1; }
    static uint32_t grp_gap(uint64_t e) { return uint32_t(e >> 32 & 0xFF); }
    static uint32_t chunks_of(uint32_t size) { return size == lw::SIZE_UNDEF ? 0 : 1u << size; }

    SEntry& s_at(uint32_t k) { return sq[(sq_head + k) % MAX_PLACES]; }
    GEntry& g_at(uint32_t k) { return gq[(gq_head + k) % MAX_PLACES]; }
    // 8.3: a count-0 stream that takes no cycle: the gap is 0, or it is the transfer's last stream
    bool is_pass(const SEntry& e) const { return s_count(e.e1) == 0 && (gap == 0 || e.index == first + count - 1); }

    // ================================================================ the step
    sim::Wake step(sim::Ctx& c) {
        if (!wired) return sim::Wake::none();
        const int64_t t = int64_t(c.now());
        const sim::Word s = c.read(start);
        bool started = false;
        if (s != starts_seen) {                                      // DMA_START presented in E = t - 1
            if (s != starts_seen + 1) FATAL("address generator (tile %u): %llu starts in one cycle at %lld", me, (unsigned long long)(s - starts_seen), (long long)t);
            starts_seen = s;
            if (running) FATAL("address generator (tile %u), cycle %lld: a start while a transfer is in progress (machine-spec 8.2): UB", me, (long long)t);
            begin(c, t - 1);
            started = true;
        }
        if (running) {
            // the read offered in cycle t - 1 was not taken then: the Hold refused the put after it, or still holds it
            const bool stalled = c.refused(rd) || (c.held_at_start(rd) && !put_prev);
            put_prev = false;
            if (stalled) {
                stall_cycles++;
                if (kind == RUN_GROUPS) { group_stall_cycles++; drift_events++; c.put(drift); }   // 8.4: drift
            }
            // the read offered in t - 1, taken: its visit was made in t - 1
            if (offer.valid && !stalled) { const Action a = offer; offer.valid = false; made(a, t - 1); }
            // the readers of cycle t (and of E, at the start's step), by the places held at the cycle's start
            if (started) readers(c, t - 1);
            readers(c, t);
            // what was decided for cycle t: it has the cycle unless the read before it is still offered
            if (next.valid) {
                if (!stalled) {
                    const Action a = next; next.valid = false;
                    if (a.put) { offer = a; if (first_request_at < 0) first_request_at = t; }   // offered in t
                    else made(a, t);                                 // made in t: its places are free from t + 1
                } else if (next.put) { c.put(rd, held[0], held[1]); put_prev = true; }          // refused: put again
            }
            // decide cycle t + 1
            if (!next.valid && !walked && t + 1 >= lead) {
                if (kind == RUN_STREAMS) walk_streams(c, t + 1); else walk_groups(c, t + 1);
            }
        }
        if (running && done_due >= 0 && done_due <= t) finish(t);
        const sim::Word st = (running ? sim::Word(ST_RUNNING) : 0) | (done ? sim::Word(ST_DONE) : 0);
        if (st != status_out) { c.put(status, st); status_out = st; }
        return running ? c.next() : sim::Wake::none();
    }

    // ================================================================ the start
    void begin(sim::Ctx& c, int64_t e) {
        const uint32_t mc = c.read(dma, DMA_R_CONFIG);               // DMA_CONFIG, taken at the start (8.2)
        kind = mc_kind(mc); first = mc_start(mc); count = mc_count(mc); gap = mc_gap(mc);
        const uint32_t size = kind == RUN_STREAMS ? 256 : 64;
        if (first + count > size)
            FATAL("address generator (tile %u), cycle %lld: a %s transfer past the table's end (start %u, count %u > %u; machine-spec 8.1): UB",
                  me, (long long)(e + 1), kind == RUN_STREAMS ? "stream" : "group", first, count, size);
        cum_requests += requests; cum_stall += stall_cycles; runs++;
        requests = stall_cycles = rotations = stream_reads = group_reads = group_stall_cycles = zero_groups = table_wait_cycles = passes = 0;
        for (auto& x : group_start) x = -1;
        running = true; done = false; E = e; started_at = e; first_request_at = -1; done_at = -1;
        lead = kind == RUN_STREAMS ? e + 2 * int64_t(L) + 1 : e + 4 * int64_t(L) + 2;
        sq_head = sq_n = sq_used = 0; gq_head = gq_n = gq_used = 0;
        reads_left = count; read_index = first; rg = rr = rv = 0; g_freed = 0;
        cur = false; gap_left = 0; g = 0; walked = false; next = {}; offer = {}; put_prev = false; done_due = -1;
        gather_checked = false;
        if (count == 0) { walked = true; done_due = e + 1; }         // an empty transfer: its walk is complete at E
    }
    void finish(int64_t t) { running = false; done = true; done_at = t; done_due = -1; }

    // ================================================================ read-ahead (8.5): the readers of cycle c
    uint64_t stream_entry0(sim::Ctx& c, uint32_t i) const {
        return uint64_t(c.read(view, TV_STREAM_SRC + (i & 255))) | uint64_t(c.read(view, TV_STREAM_DEST + (i & 255))) << 32;
    }
    void read_stream(sim::Ctx& c, int64_t cyc, uint32_t i, uint32_t r, bool last) {
        SEntry& e = s_at(sq_n++);
        e.e0 = stream_entry0(c, i); e.e1 = c.read(view, TV_STREAM_HDR + (i & 255));
        e.in = cyc + 2 * int64_t(L) + 1; e.index = i & 255; e.r = r; e.last = last;
        stream_reads++;
    }
    void readers(sim::Ctx& c, int64_t cyc) {
        const uint32_t s_places = sq_n, g_places = gq_n;             // the places held at the start of the cycle
        if (kind == RUN_STREAMS) {
            if (reads_left && s_places < places) { read_stream(c, cyc, read_index, 0, false); read_index++; reads_left--; }
            return;
        }
        if (reads_left && g_places < places) {
            GEntry& e = g_at(gq_n++);
            const uint32_t i = read_index & 63;
            e.e = uint64_t(c.read(view, TV_GROUP + 2 * i)) | uint64_t(c.read(view, TV_GROUP + 2 * i + 1)) << 32;
            e.in = cyc + 2 * int64_t(L) + 1;
            read_index++; reads_left--; group_reads++;
        }
        // STREAM, in visit order: the reader's group is at its own position in the GROUP queue
        if (rg >= count) return;
        const uint32_t k = rg - g_freed;
        if (k >= g_places || g_at(k).in > cyc) return;               // its GROUP entry is not in
        const uint64_t ce = g_at(k).e;
        const uint32_t R = grp_R(ce), T = grp_T(ce);
        if (R == 0) { rg++; rr = rv = 0; return; }                   // one group boundary a cycle, and no read
        if (s_places >= places) return;
        read_stream(c, cyc, grp_start(ce) + rv, rr, rr == R - 1 && rv == T - 1);
        if (++rv == T) { rv = 0; if (++rr == R) { rr = 0; rg++; } }
    }

    // ================================================================ an action made in cycle w
    void made(const Action& a, int64_t w) {
        sq_head = (sq_head + a.s_free) % MAX_PLACES; sq_n -= a.s_free; sq_used -= a.s_free;
        gq_head = (gq_head + a.g_free) % MAX_PLACES; gq_n -= a.g_free; gq_used -= a.g_free; g_freed += a.g_free;
        if (a.complete) done_due = w + 1;                            // 8.2: dma_done in w + 1
    }
    void decide(bool put, bool complete, uint32_t s_free, uint32_t g_free) {
        next = Action{true, put, complete, s_free, g_free};
        sq_used += s_free; gq_used += g_free;
        if (complete) walked = true;
    }

    // ================================================================ a stream transfer's cycle `cyc` (8.3)
    void walk_streams(sim::Ctx& c, int64_t cyc) {
        if (gap_left > 0) { gap_left--; decide(false, false, 0, 0); return; }          // a gap cycle
        if (cur) { element(c, 0); return; }                                             // the stream begun: its next element
        // the next stream: the passes before it that are in, then the first other stream if it is in
        uint32_t p = 0;
        bool last_passed = false;
        while (sq_used + p < sq_n && s_at(sq_used + p).in <= cyc && is_pass(s_at(sq_used + p))) {
            if (s_at(sq_used + p).index == first + count - 1) last_passed = true;
            p++;
        }
        const bool have = sq_used + p < sq_n && s_at(sq_used + p).in <= cyc;
        if (!have) {
            if (p == 0) { table_wait_cycles++; return; }                               // a wait for an entry not yet in
            for (uint32_t i = 0; i < p; i++) check_entry(c, cyc, s_at(sq_used + i).e0, s_at(sq_used + i).e1);
            passes += p;
            decide(false, last_passed, p, 0);                                          // with no entry held, the passes pass
            return;
        }
        for (uint32_t i = 0; i < p; i++) check_entry(c, cyc, s_at(sq_used + i).e0, s_at(sq_used + i).e1);
        passes += p;
        const SEntry e = s_at(sq_used + p);
        check_entry(c, cyc, e.e0, e.e1);
        load_stream(e.e0, e.e1, e.index);
        if (c_count == 0) {                                                            // the gap alone: this is its first cycle
            gap_left = gap - 1;
            decide(false, false, p + 1, 0);
            return;
        }
        cur = true;
        element(c, p + 1);                                                             // its first element: the places leave with it
    }
    // The stream's element c_j: offered, and, as decided, taken.
    void element(sim::Ctx& c, uint32_t s_free) {
        put_element(c, c_j);
        c_j++;
        bool complete = false;
        if (c_j == c_count) {
            cur = false;
            if (c_index == first + count - 1) complete = true;                         // the last stream's last element
            else gap_left = gap;
        }
        decide(true, complete, s_free, 0);
    }
    void load_stream(uint64_t e0, uint64_t e1, uint32_t index) {
        c_src = s_src(e0); c_dest = s_dest(e0); c_d = s_d(e1); c_index = index;
        c_count = s_count(e1); c_mode = s_mode(e1); c_size = s_size(e1); c_ctr = s_counter(e1); c_j = 0;
    }
    void put_element(sim::Ctx& c, uint32_t e) {
        const uint32_t dst = c_d < 64 && me < 64 ? (me + c_d) & 63 : c_d;   // 8.3: relative on a compute tile, absolute otherwise
        const uint32_t n = chunks_of(c_size);
        uint32_t dst_addr = 0;
        if (c_mode == lw::M_WRITE) dst_addr = c_dest + e * (4 * n);         // plain addition, 32 bits
        const uint32_t src = (c_src + e * n) & 0x1FFFFF;
        held[0] = mem::req_a(src, c_size, c_mode, c_ctr, dst); held[1] = mem::req_b(dst_addr);
        c.put(rd, held[0], held[1]);
        put_prev = true;
        requests++;
    }

    // ================================================================ a group transfer's cycle `cyc` (8.4)
    void walk_groups(sim::Ctx& c, int64_t cyc) {
        if (gap_left > 0) { gap_left--; decide(false, false, 0, 0); return; }          // a gap cycle
        if (gq_used >= gq_n || g_at(gq_used).in > cyc) { table_wait_cycles++; return; }   // its GROUP entry is not in
        const uint64_t ce = g_at(gq_used).e;
        const uint32_t R = grp_R(ce);
        const bool last_group = g == count - 1;
        if (group_start[g] < 0) group_start[g] = cyc - started_at;
        if (R == 0) {                                                                  // one cycle, then its gap
            zero_groups++;
            if (!last_group) gap_left = grp_gap(ce);
            g++;
            decide(false, last_group, 0, 1);
            return;
        }
        if (sq_used >= sq_n || s_at(sq_used).in > cyc) { table_wait_cycles++; return; }   // nor its visit's STREAM entry
        const SEntry e = s_at(sq_used);
        check_entry(c, cyc, e.e0, e.e1);                                               // every visit, idle or not
        const bool sends = e.r < s_count(e.e1);
        if (e.last) { rotations += R; if (!last_group) gap_left = grp_gap(ce); g++; }  // the group ends with this visit
        if (sends) { load_stream(e.e0, e.e1, e.index); put_element(c, e.r); }
        decide(sends, e.last && last_group, 1, e.last ? 1 : 0);
    }

    // ================================================================ the entries' checks and the gather alignment
    void check_entry(sim::Ctx& c, int64_t t, uint64_t e0, uint64_t e1) {
        const uint32_t m = s_mode(e1), z = s_size(e1), d = s_d(e1), ctr = s_counter(e1);
        if (m == lw::M_GATHER && !gather_checked) { gather_checked = true; gather_alignment(c, t); }
        if (m >= lw::M_RSVD6) FATAL("address generator (tile %u), cycle %lld: a stream in the reserved mode %u (machine-spec 4.2: modes 6-7): UB", me, (long long)t, m);
        namespace ck = sim::check;
        const uint64_t T = uint64_t(t);
        if (z == lw::SIZE_UNDEF) { size_violations++; ck::record(T, int(me), ck::Kind::AG_SIZE_UNDEF); }
        if (m == lw::M_AUG && z == lw::SIZE_128) { aug128_violations++; ck::record(T, int(me), ck::Kind::AG_AUG128); }
        if (m == lw::M_WRITE && !aligned_bytes(s_dest(e0), z)) { align_violations++; ck::record(T, int(me), ck::Kind::AG_DEST_ALIGN, s_dest(e0), z); }
        if (!aligned_chunks(s_src(e0), z)) { align_violations++; ck::record(T, int(me), ck::Kind::AG_SRC_ALIGN, s_src(e0), z); }
        if (d > 66 && d != 126) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_DEST_ENDPOINT, d); }   // the tracer tile is 66, broadcast 126
        else if (d == 126 && me < 64) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_BROADCAST_ON_TILE, me); }
        if (m == lw::M_GATHER && d != 0) { dest_violations++; ck::record(T, int(me), ck::Kind::AG_GATHER_DEST, d); }
        if (m == lw::M_COUNT && ctr == 0) { mode_violations++; ck::record(T, int(me), ck::Kind::AG_COUNT_CTR0); }
    }
    static bool aligned_bytes(uint32_t addr, uint32_t size) { return size == lw::SIZE_UNDEF || (addr & ((4u << size) - 1)) == 0; }
    static bool aligned_chunks(uint32_t chunk, uint32_t size) { return size == lw::SIZE_UNDEF || (chunk & ((1u << size) - 1)) == 0; }
    // 13: the gather buffers aligned to a row (128 bits), the source (record_base) and the destination (PTR[me]); UB, so
    // meshsim stops.
    void gather_alignment(sim::Ctx& c, int64_t t) {
        const uint32_t b = gc_record_base(c.read(dma, DMA_R_GATHER)), p = c.read(ptr, me);
        if ((p & 3) != 0 || (b & 3) != 0)
            FATAL("address generator (tile %u), cycle %lld: a transfer with GATHER streams starts with the gather buffers not 128-bit "
                  "aligned (GATHER_CONFIG.record_base word 0x%x, PTR[me] word 0x%x; machine-spec 13): UB", me, (long long)t, b, p);
    }

    uint64_t digest() const {
        uint64_t h = sim::hash_fields(running, done, kind, first, count, gap, E, lead, sq_n, sq_used, gq_n, gq_used, reads_left,
                                      read_index, rg, rr, rv, g_freed, cur, c_src, c_dest, c_count, c_j, c_d, c_mode, c_size, c_ctr);
        h = sim::hash_fields(h, sim::hash_fields(c_index, gap_left, g, walked, next.valid, next.put, next.complete, next.s_free, next.g_free,
                                           offer.valid, offer.complete, offer.s_free, offer.g_free, put_prev, done_due));
        for (uint32_t k = 0; k < sq_n; k++) { const SEntry& e = sq[(sq_head + k) % MAX_PLACES]; h = sim::hash_fields(h, sim::hash_fields(e.e0, e.e1, e.in, e.index, e.r, e.last)); }
        for (uint32_t k = 0; k < gq_n; k++) { const GEntry& e = gq[(gq_head + k) % MAX_PLACES]; h = sim::hash_fields(h, sim::hash_fields(e.e, e.in)); }
        return sim::hash_fields(h, sim::hash_fields(starts_seen, held[0], held[1], status_out));
    }
};

}  // namespace units
