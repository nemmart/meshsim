// table_store.h: the table memories (sim/ref/meshsim/src/TableStore.java: TableStore and HistStore; AUDIT-M2 3.5).
// Configuration part by W8 (G1): the memories with their incremental fingerprint sums (memsum.h), written by the
// controller through the table window (Controller.window). Behaviour by W11 (G3): the engines' request Links into
// 16-entry request FIFOs (ReqFifo), one STREAM read, one GROUP read and one PTR read-modify-write a cycle (TableStore),
// HIST's count-on-arrival (HistStore), the RMW pipe with its bypass (Rmw), and the window's figure-12 rule: a
// controller store on a write-back cycle is lost, a HIST load on one returns the engine's value.
//
// ref's controller store at t is a macro port write committed at the end of t (a load at t + 1 sees it); here the core
// puts it on `window`, a Link(1), and the store applies it at its step at t + 1, before any reader of t + 1 (HIST: the
// core reads `hist`, a RegFile, at a later level), unless a write-back was done at t.
//   TableStore: STREAM_SRC, STREAM_DEST, STREAM_COUNT (256 x 32), GROUP (64 x 64 bits: 128 words), PTR (256 x 21).
//   HistStore:  HIST (256 x 17); the only table the controller loads (one-cycle loads, through `hist`).
// The window's word {table << 32 | index, value}; values already masked by the core as ref's window masks them (PTR 21
// bits, HIST 17 bits, the rest 32).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/mem.h"
#include "units/memsum.h"

namespace units {

enum : uint32_t { TBL_STREAM_SRC = 0, TBL_STREAM_DEST = 1, TBL_STREAM_COUNT = 2, TBL_GROUP = 3, TBL_PTR = 4, TBL_HIST = 5 };

// The table window's decode (Registers.java: 0xFFFF2000 HIST, 2400 PTR, 2800 STREAM_SRC, 2C00 STREAM_DEST, 3000
// STREAM_COUNT, 3400-35FF GROUP): the table and the word index, for an address in [TABLE_BASE, TABLE_END).
struct TableAddr { uint32_t table, index; };
inline TableAddr table_addr(uint32_t a) {
    uint32_t off = a - 0xFFFF2000u;
    if (off < 0x400) return {TBL_HIST, off >> 2};
    if (off < 0x800) return {TBL_PTR, (off - 0x400) >> 2};
    if (off < 0xC00) return {TBL_STREAM_SRC, (off - 0x800) >> 2};
    if (off < 0x1000) return {TBL_STREAM_DEST, (off - 0xC00) >> 2};
    if (off < 0x1400) return {TBL_STREAM_COUNT, (off - 0x1000) >> 2};
    return {TBL_GROUP, (off - 0x1400) >> 2};
}
inline uint32_t table_mask(uint32_t table) { return table == TBL_HIST ? 0x1FFFF : table == TBL_PTR ? 0x1FFFFF : 0xFFFFFFFFu; }

struct TableMems {
    uint32_t stream_src[256] = {}, stream_dest[256] = {}, stream_count[256] = {}, group[128] = {}, ptr[256] = {};
    MemSum s_src{FP_STREAM_SRC}, s_dest{FP_STREAM_DEST}, s_count{FP_STREAM_COUNT}, s_group{FP_GROUP}, s_ptr{FP_PTR};
    void write(uint32_t table, uint32_t i, uint32_t v) {
        uint32_t* m; MemSum* s; uint32_t n;
        switch (table) {
        case TBL_STREAM_SRC: m = stream_src; s = &s_src; n = 256; break;
        case TBL_STREAM_DEST: m = stream_dest; s = &s_dest; n = 256; break;
        case TBL_STREAM_COUNT: m = stream_count; s = &s_count; n = 256; break;
        case TBL_GROUP: m = group; s = &s_group; n = 128; break;
        case TBL_PTR: m = ptr; s = &s_ptr; n = 256; break;
        default: FATAL("table store: table %u is not one of mine", table);
        }
        if (i >= n) FATAL("table store: index %u past table %u's %u words", i, table, n);
        s->write(i, m[i], v);
        m[i] = v;
    }
};

// A request FIFO fed by a Link inside the store (TableStore's qStream, qGroup, qWe, HistStore's q: WordFIFO(16) after a
// delay line). A put at t (an arrival) is judged at the store's next step, t + 1, against the occupancy at the start of
// t (PORTS.md 3: count + a take at t >= 16 drops it); entries visible from t + 1.
struct ReqFifo {
    static constexpr unsigned N = 16;
    uint64_t e[N][3] = {};
    uint8_t head = 0, n = 0;
    bool pend = false;                 // a put not yet judged (made at put_at)
    uint64_t put_at = 0, pw[3] = {};
    uint64_t last_take = ~0ull;
    uint64_t overflows = 0, first_overflow = 0;
    uint32_t peak = 0;                 // W20 (P2): ref's highWater (PhaseLog's ptrReq/histReq/agTable; statistics only)
    // Judge a put made before t; true if it overflowed (dropped).
    bool commit(uint64_t t) {
        if (!pend || put_at >= t) return false;
        pend = false;
        if (n + (last_take == put_at ? 1u : 0u) >= N) { if (!overflows++) first_overflow = put_at; return true; }
        uint64_t* d = e[(head + n) % N];
        d[0] = pw[0]; d[1] = pw[1]; d[2] = pw[2]; n++;
        if (n > peak) peak = n;
        return false;
    }
    void put(uint64_t t, const sim::Word* w) { pend = true; put_at = t; pw[0] = w[0]; pw[1] = w[1]; pw[2] = w[2]; }
    const uint64_t* front() const { return e[head]; }
    void pop(uint64_t t) { head = uint8_t((head + 1) % N); n--; last_take = t; }
    const uint64_t* at(unsigned k) const { return e[(head + k) % N]; }
    uint64_t digest() const {
        uint64_t h = sim::hash_fields(n, pend, put_at, pw, last_take);
        for (unsigned k = 0; k < n; k++) h = sim::hash_mix(h, sim::hash_bytes(at(k), 24));
        return h;
    }
};

// PTR's and HIST's read-modify-write (TableStore.java's RmwPipe; figure 12): read on port A at t, the new value written
// back on port B at t + 1, with a bypass (the RMW at t sees the write-back of t). One RMW a cycle, so at most one write-back
// is pending; the store steps at its due cycle. A controller store on a write-back cycle is lost; a controller load on one
// returns the engine's value (at the engine's index).
struct Rmw {
    bool valid = false;
    uint64_t due = 0;
    uint32_t idx = 0, val = 0;
    uint64_t last_wb = ~0ull;          // the cycle of the last write-back done
    uint64_t ctrl_writes_lost = 0;
    void schedule(uint64_t t, uint32_t i, uint32_t v) {
        if (valid) FATAL("RMW: a write-back already pending at cycle %llu", (unsigned long long)t);
        valid = true; due = t + 1; idx = i; val = v;
    }
    uint64_t digest() const { return sim::hash_fields(valid, due, idx, val, last_wb); }
};

struct TableStore {
    // ---- ports ----
    sim::LinkIn window;        // width 2, from the core: a table-window store of t (Link(1))
    sim::LinkIn req_we;        // width 3, the write engine's PTR RMWs, Link(ts_engine_latency)
    sim::LinkOut ret_we;       // width 3, their returns {i, old, 0}
    sim::LinkIn req_stream, req_group;      // width 3, the AG's (with_ag: G4)
    sim::LinkOut ret_stream, ret_group;     // width 3, to the AG (with_ag)
    sim::LinkOut ovf_ev;       // width 0, Link(1) to the block: a request FIFO overflowed (D29, TABLE_OVF)
    // PTR as the AG reads it at t (with_ag; the gather alignment check, Tile.checkGatherAlignment): ref's AG runs
    // before the table store in the tile's cycle and reads PTR through a host peek, so it sees PTR as committed at the
    // end of t - 1 (the controller's store of t - 1 and the write-back of t - 1), never the write-back of t. A RegFile is
    // read at a later level of the same cycle, so the write-back of t is published at the store's step at t + 1.
    sim::RegFileOut ptr_view;
    bool with_ag = false;
    bool pub = false;          // a write-back of an earlier cycle not yet in ptr_view
    uint32_t pub_idx = 0, pub_val = 0;

    // ---- state ----
    TableMems m;
    ReqFifo q_stream, q_group, q_we;
    Rmw pipe;
    uint64_t table_reads = 0, rmws = 0;
    // W20 (harness-only; P2): a harness poke of PTR (ref's TableStore.poke, between cycles) reaches `ptr_view` at the
    // store's next step, as committed before it. `host_poke` is set by the harness only; `host_ptr` marks the indices.
    bool host_poke = false;
    uint64_t host_ptr[4] = {};
    void poke_host(uint32_t table, uint32_t i, uint32_t v) {   // between cycles; the harness wakes the store
        m.write(table, i, v);
        if (table == TBL_PTR) host_ptr[(i & 255) >> 6] |= 1ull << (i & 63);
    }

    uint64_t digest() const {
        return sim::hash_fields(m.s_src.sum, m.s_dest.sum, m.s_count.sum, m.s_group.sum, m.s_ptr.sum) ^
               q_stream.digest() * 3 ^ q_group.digest() * 5 ^ q_we.digest() * 7 ^ pipe.digest() * 11 ^
               sim::hash_fields(pub, pub_idx, pub_val) * 13;
    }

    // W26 (the checker, SIM_CHECKS only): ref's ts.ptrBusy as the core's table-window check reads it (tile_ctl.h,
    // TileCore::table_check): driven at the store's step at t when PTR is busy at t (a write-engine request arriving at
    // t, one queued at the start of t, an RMW scheduled at t or t - 1; the store steps at every such cycle), t + 1, with
    // bit 63 set when it was busy at t - 1 too (a test's store at t sees ref's flag as left by t - 1).
    sim::WireOut busy_ptr;
    bool busy_wire = false;
    uint64_t busy_last = ~0ull;
    explicit TableStore(sim::Ports& p, bool ag = false, bool busy_wire_ = false) : with_ag(ag), busy_wire(busy_wire_) {
#ifdef SIM_CHECKS
        if (busy_wire) p.out(busy_ptr, "busy_ptr");
#endif
        p.in(window, "window", 2);
        p.in(req_we, "req_we", 3);
        p.out(ret_we, "ret_we", 3);
        p.out(ovf_ev, "ovf_ev", 0);
        if (with_ag) {
            p.in(req_stream, "req_stream", 3); p.in(req_group, "req_group", 3);
            p.out(ret_stream, "ret_stream", 3); p.out(ret_group, "ret_group", 3);
            p.out(ptr_view, "ptr", 256);
        }
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        [[maybe_unused]] const bool wb_pending = pipe.valid;   // W26: an RMW scheduled at t - 1
        if (pub) { c.write(ptr_view, pub_idx, pub_val); pub = false; }   // the write-back of t - 1 (with_ag)
        if (host_poke) {                                       // W20 (harness-only): the harness's pokes
            for (unsigned w = 0; w < 4; w++)
                for (uint64_t b = host_ptr[w]; b; b &= b - 1) {
                    const unsigned i = 64 * w + unsigned(__builtin_ctzll(b));
                    if (with_ag) c.write(ptr_view, i, m.ptr[i]);
                }
            host_ptr[0] = host_ptr[1] = host_ptr[2] = host_ptr[3] = 0;
        }
        if (c.ready(window)) {                                 // the controller's store of t - 1 (lost on a write-back)
            const sim::Word* w = c.peek(window);
            const uint32_t table = uint32_t(w[0] >> 32);
            if (table == TBL_PTR && pipe.last_wb == t - 1) pipe.ctrl_writes_lost++;
            else {
                m.write(table, uint32_t(w[0]), uint32_t(w[1]));
                if (with_ag && table == TBL_PTR) c.write(ptr_view, uint32_t(w[0]), m.ptr[uint32_t(w[0]) & 255]);
            }
            c.take(window);
        }
        if (pipe.valid && pipe.due <= t) {                     // the write-back of t (scheduled at t - 1)
            if (pipe.due != t) FATAL("table store: PTR's write-back due at %llu done at %llu", (unsigned long long)pipe.due, (unsigned long long)t);
            m.write(TBL_PTR, pipe.idx, pipe.val);
            pipe.valid = false; pipe.last_wb = t;
            if (with_ag) { pub = true; pub_idx = pipe.idx; pub_val = pipe.val; }
        }
        const bool o1 = q_stream.commit(t), o2 = q_group.commit(t), o3 = q_we.commit(t);
        if (o1 || o2 || o3) c.put(ovf_ev);                    // judged at t, the put's t - 1 (PORTS.md 3)
        if (with_ag) {
            if (c.ready(req_stream)) { q_stream.put(t, c.peek(req_stream)); c.take(req_stream); }
            if (c.ready(req_group)) { q_group.put(t, c.peek(req_group)); c.take(req_group); }
        }
#ifdef SIM_CHECKS
        if (busy_wire && (c.ready(req_we) || q_we.n || wb_pending)) {                          // W26: ptrBusy at t
            c.drive(busy_ptr, (t + 1) | (busy_last + 1 == t ? (uint64_t(1) << 63) : 0));
            busy_last = t;
        }
#endif
        if (c.ready(req_we)) { q_we.put(t, c.peek(req_we)); c.take(req_we); }
        if (q_stream.n) {
            const uint64_t idx = q_stream.front()[1];
            q_stream.pop(t); table_reads++;
            const unsigned i = unsigned(idx - mem::IDX_STREAM) & 255;
            c.put(ret_stream, uint64_t(m.stream_src[i]) | uint64_t(m.stream_dest[i]) << 32, m.stream_count[i], idx);
        }
        if (q_group.n) {
            const uint64_t idx = q_group.front()[1];
            q_group.pop(t); table_reads++;
            const unsigned i = unsigned(idx - mem::IDX_GROUP);
            if (i + 1 >= 128) FATAL("table store: GROUP read at word %u", i);
            c.put(ret_group, idx, uint64_t(m.group[i]) | uint64_t(m.group[i + 1]) << 32, 0);
        }
        if (q_we.n) {
            const uint64_t* r = q_we.front();
            const uint32_t idx = uint32_t(r[1] - mem::IDX_PTR), add = uint32_t(r[2]);
            q_we.pop(t); rmws++;
            if (idx >= 256) FATAL("table store: PTR RMW at index %u", idx);
            const uint32_t old = m.ptr[idx];                     // the write-back of t is in (the bypass)
            pipe.schedule(t, idx, (old + add) & 0x1FFFFF);
            c.put(ret_we, idx, old, 0);
        }
        return pipe.valid || pub || q_stream.n || q_group.n || q_we.n || q_stream.pend || q_group.pend || q_we.pend ? c.next() : sim::Wake::none();
    }
};

struct HistMem {
    uint32_t hist[256] = {};
    MemSum s_hist{FP_HIST};
    void write(uint32_t i, uint32_t v) {
        if (i >= 256) FATAL("HIST: index %u past its 256 words", i);
        s_hist.write(i, hist[i], v);
        hist[i] = v;
    }
};

// HIST's RegFile (the core's one-cycle window loads): HIST[0..255], then the write-back of this cycle: register
// HIST_WB_AT holds its cycle + 1 (low 32 bits; 0: none yet) and HIST_WB_VAL its value, so a load at t returns the
// engine's value when HIST_WB_AT == t + 1 (RmwPipe.ctrlRead), else HIST[i] as committed at the end of t - 1.
enum : unsigned { HIST_WB_AT = 256, HIST_WB_VAL = 257, HIST_REGS = 258 };

struct HistStore {
    sim::LinkIn window;        // width 2, from the core: a HIST store through the window, of t (Link(1))
    sim::LinkIn req;           // width 3, the write engine's count-on-arrival {OP_ADD_RET, i, addend}, Link(ts_engine_latency)
    sim::RegFileOut out;       // HIST as the core's one-cycle loads read it (HIST_REGS registers)
    sim::LinkOut ovf_ev;       // width 0, Link(1) to the block: its request FIFO overflowed (D29, TABLE_OVF)
    HistMem m;
    ReqFifo q;
    Rmw pipe;
    uint64_t rmws = 0;
    bool hist_overflow = false;        // a count past 17 bits (invariant 6)
    // W20 (harness-only; P2): a harness poke of HIST reaches `out` (the core's loads) at the store's next step.
    bool host_poke = false;
    uint64_t host_hist[4] = {};
    void poke_host(uint32_t i, uint32_t v) { m.write(i, v); host_hist[(i & 255) >> 6] |= 1ull << (i & 63); }
    uint64_t digest() const { return m.s_hist.sum ^ q.digest() * 3 ^ pipe.digest() * 5 ^ uint64_t(hist_overflow); }

    // W26 (the checker, SIM_CHECKS only): ref's hist.busy as the core's table-window check reads it, as TableStore's
    // busy_ptr: t + 1 driven at the store's step at t when HIST is busy at t (a request arriving at t, one queued at the
    // start of t, an RMW scheduled at t or t - 1), bit 63 set when it was busy at t - 1 too.
    sim::WireOut busy_hist;
    bool busy_wire = false;
    uint64_t busy_last = ~0ull;
    explicit HistStore(sim::Ports& p, bool busy_wire_ = false) : busy_wire(busy_wire_) {
        p.in(window, "window", 2); p.in(req, "req", 3); p.out(out, "hist", HIST_REGS); p.out(ovf_ev, "ovf_ev", 0);
#ifdef SIM_CHECKS
        if (busy_wire) p.out(busy_hist, "busy_hist");
#endif
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        [[maybe_unused]] const bool wb_pending = pipe.valid;   // W26: an RMW scheduled at t - 1
        if (host_poke) {                                       // W20 (harness-only): the harness's pokes
            for (unsigned w = 0; w < 4; w++)
                for (uint64_t b = host_hist[w]; b; b &= b - 1) { const unsigned i = 64 * w + unsigned(__builtin_ctzll(b)); c.write(out, i, m.hist[i]); }
            host_hist[0] = host_hist[1] = host_hist[2] = host_hist[3] = 0;
        }
        if (c.ready(window)) {                                 // the controller's store of t - 1 (lost on a write-back)
            const sim::Word* w = c.peek(window);
            const uint32_t i = uint32_t(w[0]), v = uint32_t(w[1]);
            if (pipe.last_wb == t - 1) pipe.ctrl_writes_lost++;
            else { m.write(i, v); c.write(out, i, v); }
            c.take(window);
        }
        if (pipe.valid && pipe.due <= t) {
            if (pipe.due != t) FATAL("HIST: write-back due at %llu done at %llu", (unsigned long long)pipe.due, (unsigned long long)t);
            m.write(pipe.idx, pipe.val);
            c.write(out, pipe.idx, pipe.val);
            c.write(out, HIST_WB_AT, uint32_t(t + 1));
            c.write(out, HIST_WB_VAL, pipe.val);
            pipe.valid = false; pipe.last_wb = t;
        }
        if (q.commit(t)) c.put(ovf_ev);                        // judged at t (D29)
#ifdef SIM_CHECKS
        if (busy_wire && (c.ready(req) || q.n || wb_pending)) {                        // W26: hist.busy at t
            c.drive(busy_hist, (t + 1) | (busy_last + 1 == t ? (uint64_t(1) << 63) : 0));
            busy_last = t;
        }
#endif
        if (c.ready(req)) { q.put(t, c.peek(req)); c.take(req); }
        if (q.n) {
            const uint64_t* r = q.front();
            const uint32_t idx = uint32_t(r[1]) & 0xFF, add = uint32_t(r[2]);
            q.pop(t); rmws++;
            const uint32_t nv = m.hist[idx] + add;
            if (nv & ~0x1FFFFu) hist_overflow = true;
            pipe.schedule(t, idx, nv & 0x1FFFF);
        }
        return pipe.valid || q.n || q.pend ? c.next() : sim::Wake::none();
    }
};

}  // namespace units
