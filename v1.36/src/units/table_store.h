// table_store.h: the table memories (machine-spec 8.6, 12.5): STREAM and GROUP (TableStore), PTR (TableStore) and HIST
// (HistStore), with their incremental fingerprint sums (memsum.h).
//
// PTR AND HIST (12.5; the RTL's receive_table): a two-port memory each, which its engine's read-modify-write uses, the
// read port in the cycle its request arrives and the write port in the next, with a bypass, so no update to one entry
// is lost at any spacing and no queue holds an update back. The core shares the two ports: in a cycle the engine uses a
// port the engine wins, so a core STORE in the cycle of the engine's write is lost, and a core LOAD in the cycle of
// the engine's read is undefined (the core's side stops the simulator: tile_ctl.h).
//
// The core's store at t is committed at the end of t (a load at t + 1 sees it): the core puts it on `window`, a Link(1),
// and the store applies it at its step at t + 1, before any reader of t + 1 (the core reads the tables' RegFiles at a
// later level), unless the engine wrote the table at t. The engine's write at t is seen by a load at t + 1 too.
//   TableStore: STREAM_SRC, STREAM_DEST, STREAM_HDR (256 x 32), GROUP (64 x 2 words), PTR (256 x 21); the address
//               generator's STREAM and GROUP reads still pass v1.35's request FIFOs (PENDING-v1.36.md).
//   HistStore:  HIST (256 x 17).
// The window's word {table << 32 | index, value}; values already masked by the core (PTR 21 bits, HIST 17 bits, the
// rest 32).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/mem.h"
#include "units/memsum.h"
#include "units/s1.h"

namespace units {

enum : uint32_t { TBL_STREAM_SRC = 0, TBL_STREAM_DEST = 1, TBL_STREAM_HDR = 2, TBL_GROUP = 3, TBL_PTR = 4, TBL_HIST = 5 };

// The tables' windows (5.2; regmap.h): 0xFFFF_E000 HIST, E400 PTR, E800 STREAM_SRC_ADDR, EC00 STREAM_DEST_ADDR, F000
// STREAM_HDR, F400-F5FF GROUP (word w at +4w: GROUP[g]'s two words at 2g and 2g + 1): the table and the word index, for
// an address in [REGS_BASE, TABLE_END).
struct TableAddr { uint32_t table, index; };
inline TableAddr table_addr(uint32_t a) {
    uint32_t off = a - 0xFFFFE000u;
    if (off < 0x400) return {TBL_HIST, off >> 2};
    if (off < 0x800) return {TBL_PTR, (off - 0x400) >> 2};
    if (off < 0xC00) return {TBL_STREAM_SRC, (off - 0x800) >> 2};
    if (off < 0x1000) return {TBL_STREAM_DEST, (off - 0xC00) >> 2};
    if (off < 0x1400) return {TBL_STREAM_HDR, (off - 0x1000) >> 2};
    return {TBL_GROUP, (off - 0x1400) >> 2};
}
// A table stores only its fields' bits (5.2): a store ignores the others, a load reads them 0.
inline uint32_t table_mask(uint32_t table, uint32_t index) {
    switch (table) {
    case TBL_HIST: return 0x1FFFF;
    case TBL_PTR: case TBL_STREAM_SRC: return 0x1FFFFF;
    case TBL_GROUP: return index & 1 ? 0xFFu : 0x3FFFFFFFu;        // 8.4: word 1 the gap; word 0 30 bits
    default: return 0xFFFFFFFFu;                                   // STREAM_DEST_ADDR; STREAM_HDR (25 + TILE_ID_BITS = 32)
    }
}
// The store's `view` RegFile, for the core's loads (5.2: every table is loadable): STREAM_SRC_ADDR, STREAM_DEST_ADDR,
// STREAM_HDR (256 each) and GROUP's 128 words, as the core's stores left them (nothing else writes them).
enum : unsigned { TV_STREAM_SRC = 0, TV_STREAM_DEST = 256, TV_STREAM_HDR = 512, TV_GROUP = 768, TV_REGS = 896 };
// The store's `ptr` RegFile: PTR[0..255], then PTR_RMW_AT (TableStore::ptr_view).
enum : unsigned { PTR_RMW_AT = 256, PTR_REGS = 257 };
inline unsigned table_view_index(uint32_t table, uint32_t i) {
    return table == TBL_STREAM_SRC ? TV_STREAM_SRC + (i & 255) : table == TBL_STREAM_DEST ? TV_STREAM_DEST + (i & 255)
           : table == TBL_STREAM_HDR ? TV_STREAM_HDR + (i & 255) : TV_GROUP + (i & 127);
}

struct TableMems {
    uint32_t stream_src[256] = {}, stream_dest[256] = {}, stream_count[256] = {}, group[128] = {}, ptr[256] = {};
    MemSum s_src{FP_STREAM_SRC}, s_dest{FP_STREAM_DEST}, s_count{FP_STREAM_COUNT}, s_group{FP_GROUP}, s_ptr{FP_PTR};
    void write(uint32_t table, uint32_t i, uint32_t v) {
        uint32_t* m; MemSum* s; uint32_t n;
        switch (table) {
        case TBL_STREAM_SRC: m = stream_src; s = &s_src; n = 256; break;
        case TBL_STREAM_DEST: m = stream_dest; s = &s_dest; n = 256; break;
        case TBL_STREAM_HDR: m = stream_count; s = &s_count; n = 256; break;
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
    uint32_t peak = 0;                 // ref's highWater (PhaseLog's ptrReq/histReq/agTable; statistics only)
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

// PTR's and HIST's read-modify-write (12.5): read on the read port at t, the new value written on the write port at
// t + 1, with a bypass (the read at t sees the write of t). One a cycle, so at most one write is pending; the store
// steps at its due cycle. A core store in the cycle of a write is lost.
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
    sim::LinkIn req_we;        // width 3, from the director: PTR's read-modify-writes, Link(TABLE_LATENCY)
    sim::LinkOut ret_we;       // width 3, to the write engine: the old pointers {i, old, 0}, Link(TABLE_LATENCY + 1)
    sim::LinkIn req_stream, req_group;      // width 3, the AG's (with_ag: G4)
    sim::LinkOut ret_stream, ret_group;     // width 3, to the AG (with_ag)
    sim::LinkOut ovf_ev;       // width 0, Link(1) to the block: a STREAM or GROUP request FIFO overflowed (D29)
    // PTR as the core's loads and the AG read it (with_ag): entry i as committed at the end of t - 1, the core's store
    // of t - 1 and the engine's write of t - 1 included; and PTR_RMW_AT, t + 1 in a cycle t the engine reads PTR in
    // (12.5: a core load of that cycle is undefined).
    sim::RegFileOut ptr_view;
    sim::RegFileOut view;      // (v1.36, 5.2) STREAM and GROUP for the core's loads (TV_*), when with_ag
    bool with_ag = false;
    bool pub = false;          // the engine's write of the cycle before, not yet in ptr_view
    uint32_t pub_idx = 0, pub_val = 0;

    // ---- state ----
    TableMems m;
    ReqFifo q_stream, q_group;
    Rmw pipe;
    uint64_t table_reads = 0, rmws = 0;
    // (harness-only; P2): a harness poke of PTR (between cycles) reaches `ptr_view` at the store's next step, as
    // committed before it. `host_poke` is set by the harness only; `host_ptr` marks the indices.
    bool host_poke = false;
    uint64_t host_ptr[4] = {};
    void poke_host(uint32_t table, uint32_t i, uint32_t v) {   // between cycles; the harness wakes the store
        m.write(table, i, v);
        if (table == TBL_PTR) host_ptr[(i & 255) >> 6] |= 1ull << (i & 63);
    }

    uint64_t digest() const {
        return sim::hash_fields(m.s_src.sum, m.s_dest.sum, m.s_count.sum, m.s_group.sum, m.s_ptr.sum) ^
               q_stream.digest() * 3 ^ q_group.digest() * 5 ^ pipe.digest() * 11 ^
               sim::hash_fields(pub, pub_idx, pub_val) * 13;
    }

    S1Tile* s1 = nullptr;              // S1 (s1.h), SIM_CHECKS only (add_g1)
    explicit TableStore(sim::Ports& p, bool ag = false) : with_ag(ag) {
        p.in(window, "window", 2);
        p.in(req_we, "req_we", 3);
        p.out(ret_we, "ret_we", 3);
        p.out(ovf_ev, "ovf_ev", 0);
        if (with_ag) {
            p.in(req_stream, "req_stream", 3); p.in(req_group, "req_group", 3);
            p.out(ret_stream, "ret_stream", 3); p.out(ret_group, "ret_group", 3);
            p.out(ptr_view, "ptr", PTR_REGS);
            p.out(view, "view", TV_REGS);
        }
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (pub) { c.write(ptr_view, pub_idx, pub_val); pub = false; }   // the engine's write of t - 1 (with_ag)
        if (host_poke) {                                       // (harness-only): the harness's pokes
            for (unsigned w = 0; w < 4; w++)
                for (uint64_t b = host_ptr[w]; b; b &= b - 1) {
                    const unsigned i = 64 * w + unsigned(__builtin_ctzll(b));
                    if (with_ag) c.write(ptr_view, i, m.ptr[i]);
                }
            host_ptr[0] = host_ptr[1] = host_ptr[2] = host_ptr[3] = 0;
        }
        if (c.ready(window)) {                                 // the core's store of t - 1: lost if the engine wrote PTR then
            const sim::Word* w = c.peek(window);
            const uint32_t table = uint32_t(w[0] >> 32);
            if (table == TBL_PTR && pipe.last_wb == t - 1) pipe.ctrl_writes_lost++;
            else {
                m.write(table, uint32_t(w[0]), uint32_t(w[1]));
                if (with_ag && table == TBL_PTR) c.write(ptr_view, uint32_t(w[0]), m.ptr[uint32_t(w[0]) & 255]);
                else if (with_ag) c.write(view, table_view_index(table, uint32_t(w[0])), uint32_t(w[1]));
            }
            c.take(window);
        }
        if (pipe.valid && pipe.due <= t) {                     // the engine's write of t (its read was at t - 1)
            if (pipe.due != t) FATAL("table store: PTR's write due at %llu done at %llu", (unsigned long long)pipe.due, (unsigned long long)t);
            m.write(TBL_PTR, pipe.idx, pipe.val);
            pipe.valid = false; pipe.last_wb = t;
#ifdef SIM_CHECKS
            if (s1 && s1->on) s1->written_back(0, t, pipe.idx);   // S1
#endif
            if (with_ag) { pub = true; pub_idx = pipe.idx; pub_val = pipe.val; }
        }
        const bool o1 = q_stream.commit(t), o2 = q_group.commit(t);
        if (o1 || o2) c.put(ovf_ev);                           // judged at t, the put's t - 1 (PORTS.md 3)
        if (with_ag) {
            if (c.ready(req_stream)) { q_stream.put(t, c.peek(req_stream)); c.take(req_stream); }
            if (c.ready(req_group)) { q_group.put(t, c.peek(req_group)); c.take(req_group); }
        }
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
        // the engine's read-modify-write (12.5): the entry read in the cycle its request arrives, t, with the write of
        // t in (the bypass); written in t + 1. No queue: one request a cycle at most (the director's one packet).
        if (c.ready(req_we)) {
            const sim::Word* r = c.peek(req_we);
            const uint32_t idx = uint32_t(r[1] - mem::IDX_PTR), add = uint32_t(r[2]);
            c.take(req_we); rmws++;
            if (idx >= 256) FATAL("table store: PTR read-modify-write at index %u", idx);
            const uint32_t old = m.ptr[idx];
            pipe.schedule(t, idx, (old + add) & 0x1FFFFF);
            c.put(ret_we, idx, old, 0);
            if (with_ag) c.write(ptr_view, PTR_RMW_AT, uint32_t(t + 1));
        }
        return pipe.valid || pub || q_stream.n || q_group.n || q_stream.pend || q_group.pend ? c.next() : sim::Wake::none();
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

// HIST's RegFile (the core's loads): HIST[0..255] as committed at the end of t - 1, then HIST_RMW_AT, t + 1 in a cycle
// t count-on-arrival reads HIST in (12.5: a core load of that cycle is undefined).
enum : unsigned { HIST_RMW_AT = 256, HIST_REGS = 257 };

struct HistStore {
    sim::LinkIn window;        // width 2, from the core: a HIST store through the window, of t (Link(1))
    sim::LinkIn req;           // width 3, from the director: count-on-arrival {OP_ADD_RET, i, addend}, Link(TABLE_LATENCY)
    sim::RegFileOut out;       // HIST as the core's loads read it (HIST_REGS registers)
    HistMem m;
    Rmw pipe;
    bool pub = false;          // the engine's write of the cycle before, not yet in `out`
    uint32_t pub_idx = 0, pub_val = 0;
    uint64_t rmws = 0;
    bool hist_overflow = false;        // a count past 17 bits (invariant 6)
    int ck_tile = -1;                  // the checker's tile (add_g1); -1 records nothing
    // (harness-only; P2): a harness poke of HIST reaches `out` (the core's loads) at the store's next step.
    bool host_poke = false;
    uint64_t host_hist[4] = {};
    void poke_host(uint32_t i, uint32_t v) { m.write(i, v); host_hist[(i & 255) >> 6] |= 1ull << (i & 63); }
    uint64_t digest() const { return m.s_hist.sum ^ pipe.digest() * 5 ^ uint64_t(hist_overflow) ^ sim::hash_fields(pub, pub_idx, pub_val) * 7; }

    S1Tile* s1 = nullptr;              // S1 (s1.h), SIM_CHECKS only (add_g1)
    explicit HistStore(sim::Ports& p) {
        p.in(window, "window", 2); p.in(req, "req", 3); p.out(out, "hist", HIST_REGS);
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (pub) { c.write(out, pub_idx, pub_val); pub = false; }   // the engine's write of t - 1
        if (host_poke) {                                       // (harness-only): the harness's pokes
            for (unsigned w = 0; w < 4; w++)
                for (uint64_t b = host_hist[w]; b; b &= b - 1) { const unsigned i = 64 * w + unsigned(__builtin_ctzll(b)); c.write(out, i, m.hist[i]); }
            host_hist[0] = host_hist[1] = host_hist[2] = host_hist[3] = 0;
        }
        if (c.ready(window)) {                                 // the core's store of t - 1: lost if the engine wrote HIST then
            const sim::Word* w = c.peek(window);
            const uint32_t i = uint32_t(w[0]), v = uint32_t(w[1]);
            if (pipe.last_wb == t - 1) pipe.ctrl_writes_lost++;
            else { m.write(i, v); c.write(out, i, v); }
            c.take(window);
        }
        if (pipe.valid && pipe.due <= t) {                     // the engine's write of t (its read was at t - 1)
            if (pipe.due != t) FATAL("HIST: write due at %llu done at %llu", (unsigned long long)pipe.due, (unsigned long long)t);
            m.write(pipe.idx, pipe.val);
            pub = true; pub_idx = pipe.idx; pub_val = pipe.val;
            pipe.valid = false; pipe.last_wb = t;
#ifdef SIM_CHECKS
            if (s1 && s1->on) s1->written_back(1, t, pipe.idx);   // S1
#endif
        }
        if (c.ready(req)) {                                    // count-on-arrival: read in t (the bypass), written in t + 1
            const sim::Word* r = c.peek(req);
            const uint32_t idx = uint32_t(r[1]) & 0xFF, add = uint32_t(r[2]);
            c.take(req); rmws++;
            const uint32_t nv = m.hist[idx] + add;
            if (nv & ~0x1FFFFu) {
                hist_overflow = true;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::HIST_OVERFLOW);   // invariant 6 (once a tile)
            }
            pipe.schedule(t, idx, nv & 0x1FFFF);
            c.write(out, HIST_RMW_AT, uint32_t(t + 1));
        }
        return pipe.valid || pub ? c.next() : sim::Wake::none();
    }
};

}  // namespace units
