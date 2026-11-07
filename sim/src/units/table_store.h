// table_store.h: the table memories (sim/ref/meshsim/src/TableStore.java: TableStore and HistStore; AUDIT-M2 3.5).
// CONFIGURATION PART BY W8 (G1); G3 EXTENDS THIS FILE with the behaviour (the request FIFOs and returns for the AG and
// the write engine, PTR's and HIST's read-modify-write pipes with their bypass, a controller access losing port B to a
// write-back), in these same units.
//
// What exists now: the memories with their incremental fingerprint sums (memsum.h), written by the controller through
// the table window (Controller.window). ref's controller store at t is a macro port write committed at the end of t (a
// load at t + 1 sees it); here the core puts it on `window`, a Link(1), and the store applies it at its step at t + 1,
// before any reader of t + 1 (HIST: the core reads `hist`, a RegFile, at a later level).
//   TableStore: STREAM_SRC, STREAM_DEST, STREAM_COUNT (256 x 32), GROUP (64 x 64 bits: 128 words), PTR (256 x 21).
//   HistStore:  HIST (256 x 17); the only table the controller loads (one-cycle loads, through `hist`).
// The window's word {table << 32 | index, value}; values already masked by the core as ref's window masks them (PTR 21
// bits, HIST 17 bits, the rest 32).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
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

struct TableStore {
    sim::LinkIn window;        // width 2, from the core: a table-window store of t (Link(1))
    TableMems m;
    uint64_t digest() const { return m.s_src.sum ^ m.s_dest.sum * 3 ^ m.s_count.sum * 5 ^ m.s_group.sum * 7 ^ m.s_ptr.sum * 11; }

    explicit TableStore(sim::Ports& p) { p.in(window, "window", 2); }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(window)) {
            const sim::Word* w = c.peek(window);
            m.write(uint32_t(w[0] >> 32), uint32_t(w[0]), uint32_t(w[1]));
            c.take(window);
        }
        return sim::Wake::none();
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

struct HistStore {
    sim::LinkIn window;        // width 2, from the core: a HIST store through the window, of t (Link(1))
    sim::RegFileOut out;       // HIST as the core's one-cycle loads read it (256 registers)
    HistMem m;
    uint64_t digest() const { return m.s_hist.sum; }

    explicit HistStore(sim::Ports& p) { p.in(window, "window", 2); p.out(out, "hist", 256); }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(window)) {
            const sim::Word* w = c.peek(window);
            uint32_t i = uint32_t(w[0]), v = uint32_t(w[1]);
            m.write(i, v);
            c.write(out, i, v);
            c.take(window);
        }
        return sim::Wake::none();
    }
};

}  // namespace units
