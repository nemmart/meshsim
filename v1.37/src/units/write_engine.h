// write_engine.h: the write engine (machine-spec 12.4; the RTL's rtl/blocks/receive_unit): the scratchpad's only
// writer, a fixed pipeline with no input queue, which never refuses a write and never holds one.
//
// THE PIPELINE (the RTL's receive unit README, "Timing"; a = the cycle the director accepts the packet,
// L = TABLE_LATENCY):
//   a            the director hands the packet over (`in`, a Link(1): here at a + 1), and sends its PTR
//                read-modify-write (APPEND, BUCKET, AUGMENT) to the table store itself (director.h), a Link(L)
//   a + L        PTR is read; a + L + 1 it is written (table_store.h); the old pointer comes back on `ts_ret`,
//                a Link(L + 1): here at a + 2L + 1, the pipeline's last stage
//   a + 2L + 1   the write is put on `to_sram`, with its note to the read arbiter (inject.h)
//   a + 2L + 2   `scratchpad_write`: the scratchpad does the write, which wins its bank (15)
// Every write takes the same 2L + 2 cycles, a WRITE at its address included (it reads no PTR entry), so the writes
// reach the scratchpad in the order the director accepted them, one a cycle (12.4, 15).
//
// Modes: WRITE (region 0x03, at its address), APPEND (PTR[src]), BUCKET (PTR[the bucket digit]), AUGMENT (the record
// (key, ordinal) at PTR[src], the ordinal (the record's word address - GATHER_CONFIG.record_base) / 2). A PTR entry not
// aligned to its write is undefined (fatal, named); a write at or past mem_chunks is dropped and counted (invariant
// 19).
//
// The configuration is the tile control unit's `dma` RegFile (DMA_CONFIG, RECEIVE_CONFIG, GATHER_CONFIG), read where it
// is used, never copied.
#pragma once

#include <cstdint>
#include <cstdio>

#include "base/fatal.h"
#include "units/s1.h"
#include "check.h"
#include "sim.h"
#include "units/linkword.h"
#include "units/mem.h"

namespace units {

// The block's `dma` RegFile: the units' configuration registers as the tile control holds them (v1.36: 8.1, 12.1, 13):
// DMA_CONFIG, RECEIVE_CONFIG and GATHER_CONFIG, each masked to its fields.
enum : unsigned { DMA_R_CONFIG = 0, DMA_R_RECEIVE = 1, DMA_R_GATHER = 2, DMA_REGS = 3 };

// DMA_CONFIG's fields (8.1): kind [0], start [8:1], count [17:9], gap [20:18]. RECEIVE_CONFIG's (12.1): bucket_digit
// [2:0], histogram_digit [5:3]. GATHER_CONFIG's (13): record_base [20:0], value_size (k - 1) [23:21].
inline uint32_t mc_kind(uint32_t v) { return v & 1; }
inline uint32_t mc_start(uint32_t v) { return v >> 1 & 0xFF; }
inline uint32_t mc_count(uint32_t v) { return v >> 9 & 0x1FF; }
inline uint32_t mc_gap(uint32_t v) { return v >> 18 & 7; }
inline uint32_t rc_bucket_digit(uint32_t v) { return v & 7; }          // 5-7: reserved (12.5)
inline uint32_t rc_hist_digit(uint32_t v) { return v >> 3 & 7; }
inline uint32_t gc_record_base(uint32_t v) { return v & 0x1FFFFF; }
inline uint32_t gc_k(uint32_t v) { return (v >> 21 & 7) + 1; }

struct WriteEngine {
    static constexpr unsigned MAX_L = 3;                 // TABLE_LATENCY 1-3: 2L + 2 fits LAND_LATENCY = 8 (12.3)
    static constexpr unsigned MAX_STAGE = 2 * MAX_L + 2;

    // ---- ports ----
    sim::LinkIn in;            // width 3, Link(1): packets from the director (WRITE to region 0x03, APPEND, BUCKET, AUGMENT)
    sim::RegFileIn dma;        // the tile control unit's configuration registers
    sim::LinkIn ts_ret;        // width 3, Link(L + 1): the old pointers {i, old pointer, 0}, in the packets' order
    sim::HoldOut to_sram;      // width 3: the scratchpad's write {word address | words << 21, lo, hi}; never refused
    sim::LinkOut wr_note;      // width 1, Link(1), to the read arbiter (inject.h): the word address of the write put on
                               // to_sram, which the scratchpad does in the next cycle: no read is taken at its bank
                               // then (15). Declared when `note` (a tile with the inject unit's read side)
    bool note = false;

    // ---- configuration ----
    uint32_t mem_chunks = 1u << 20;
    unsigned L = 1;            // TABLE_LATENCY
    unsigned cap = 4;          // the pipeline's stages: 2L + 2

    // ---- state: the pipeline, in acceptance order ----
    uint64_t st_d[MAX_STAGE] = {}, st_h[MAX_STAGE] = {}, st_c[MAX_STAGE] = {}, st_due[MAX_STAGE] = {};
    uint32_t st_ptr[MAX_STAGE] = {};
    uint8_t st_have[MAX_STAGE] = {};
    uint8_t head = 0, count = 0;
    uint8_t aw[MAX_STAGE] = {};          // entries awaiting their PTR entry, in order (a ring)
    uint8_t aw_head = 0, aw_n = 0;

    // ---- statistics and checks ----
    uint32_t host_rets = 0;         // (harness-only): old pointers of a test's own requests (director.h) still to come: dropped
    int ck_tile = -1;               // the checker's facts under this tile (src/check.h); -1 none
    uint64_t (*log)[4] = nullptr;   // (harness-only): {t, payload lo, header, payload hi} of every packet taken
    uint32_t log_cap = 0, log_n = 0;
    uint64_t taken = 0, written = 0, augmented = 0, explicit_writes = 0, out_of_range = 0;
    uint64_t written_by_size[3] = {};   // writes put to the scratchpad, by log2 of their words
    uint32_t stage_peak = 0;            // the most packets in the pipeline at once (at most 2L + 1)
#ifdef SIM_CHECKS
    S1Tile* s1 = nullptr;           // S1 (s1.h): each entry's acceptance, -1 none
    int64_t st_acc[MAX_STAGE] = {};
#endif

    WriteEngine(sim::Ports& p, unsigned ts_engine_latency, uint32_t chunks, bool note_ = false)
        : note(note_), mem_chunks(chunks), L(ts_engine_latency) {
        if (L < 1 || L > MAX_L)
            FATAL("write engine: TABLE_LATENCY %u: 1 to %u (2L + 2 cycles to the scratchpad's write within LAND_LATENCY: machine-spec 12.3)", L, MAX_L);
        cap = 2 * L + 2;
        p.in(in, "in", 3);
        p.in(dma, "dma");
        p.in(ts_ret, "ts_ret", 3);
        p.out(to_sram, "to_sram", 3);
        if (note) p.out(wr_note, "wr_note", 1);
    }

    static uint32_t augment_base(uint32_t gather_config) { return gc_record_base(gather_config); }
    // 12.5: byte sel - 1 of the payload's low 32 bits (sel 0: none, 0).
    static uint32_t digit(uint64_t payload, uint32_t sel) { return sel == 0 ? 0 : uint32_t(payload >> (8 * (sel - 1)) & 0xFF); }

    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const {
        uint64_t h = sim::hash_fields(head, count, aw_head, aw_n);
        for (unsigned k = 0; k < count; k++) {
            const unsigned j = (head + k) % cap;
            h = sim::hash_mix(h, sim::hash_fields(st_d[j], st_h[j], st_c[j], st_due[j], st_ptr[j], st_have[j]));
        }
        for (unsigned k = 0; k < aw_n; k++) h = sim::hash_mix(h, aw[(aw_head + k) % cap]);
        return h;
    }
};

inline sim::Wake WriteEngine::step(sim::Ctx& c) {
    const uint64_t t = c.now();
    if (c.refused(to_sram))
        FATAL("write engine: cycle %llu: the scratchpad refused a write: it never does (machine-spec 12.4, 15)", (unsigned long long)t);
    // the packet the director accepted the cycle before: into the pipeline's first stage
    if (c.ready(in)) {
        const sim::Word* w = c.peek(in);
        const uint64_t d = w[0], h = w[1], cw = w[2];
        c.take(in);
        taken++;
        if (log) {                                             // (harness-only)
            if (log_n == log_cap) FATAL("write engine: the harness's log is full (%u entries)", log_cap);
            uint64_t* e = log[log_n++];
            e[0] = t; e[1] = d; e[2] = h; e[3] = cw;
        }
        if (count == cap) FATAL("write engine: cycle %llu: the pipeline is full: a write was held (machine-spec 12.4)", (unsigned long long)t);
        const unsigned i = (head + count) % cap;
        st_d[i] = d; st_h[i] = h; st_c[i] = cw; st_due[i] = t + 2 * L; count++;
#ifdef SIM_CHECKS
        st_acc[i] = s1 && s1->on ? s1->take_accept() : -1;     // S1: the director's acceptance
#endif
        if (lw::mode(h) == lw::M_WRITE) { st_ptr[i] = lw::offset(lw::addr(h)) >> 2; st_have[i] = 1; explicit_writes++; }
        else { st_have[i] = 0; aw[(aw_head + aw_n) % cap] = uint8_t(i); aw_n++; }
        if (count > stage_peak) stage_peak = count;
    }
    // the old pointer of the packet now in the last stage
    if (c.ready(ts_ret)) {
        const uint32_t old = uint32_t(c.peek(ts_ret)[1]);
        c.take(ts_ret);
        if (aw_n == 0 && host_rets) host_rets--;               // (harness-only): a test's request's return, dropped
        else {
            if (aw_n == 0) FATAL("write engine: a PTR entry at cycle %llu with nothing awaiting it", (unsigned long long)t);
            const unsigned i = aw[aw_head];
            aw_head = uint8_t((aw_head + 1) % cap); aw_n--;
            st_ptr[i] = old & 0x1FFFFF; st_have[i] = 1;
        }
    }
    // the last stage: the write goes to the scratchpad
    if (count > 0 && st_due[head] <= t) {
        if (st_due[head] != t || !st_have[head])
            FATAL("write engine: cycle %llu: the write due at %llu is %s: the pipeline is fixed (machine-spec 12.4)",
                  (unsigned long long)t, (unsigned long long)st_due[head], st_have[head] ? "late" : "without its PTR entry");
        uint64_t lo = st_d[head], hi = st_c[head];
        const uint64_t h = st_h[head];
        const uint32_t a = st_ptr[head];
        const unsigned mode = lw::mode(h);
        unsigned n;
        if (mode == lw::M_AUG) {
            const uint32_t base = augment_base(c.read(dma, DMA_R_GATHER));
            lo = (lo & 0xFFFFFFFFull) | uint64_t(((a - base) & 0x1FFFFF) >> 1) << 32; hi = 0; n = 2; augmented++;
        } else n = 1u << lw::size(h);
        if (a & (n - 1)) {
            char which[24];
            if (mode == lw::M_BUCKET) std::snprintf(which, sizeof which, "digit");
            else std::snprintf(which, sizeof which, "src %u", lw::src(h));
            FATAL("write engine: tile %u, cycle %llu: a write through PTR[%s] = word 0x%x, not aligned to its %u-bit write "
                  "(undefined behaviour: machine-spec 12.4): header 0x%llx", lw::dst(h), (unsigned long long)t, which, a, 32 * n, (unsigned long long)h);
        }
        if (a + n > mem_chunks) {                               // undefined (15, invariant 19): dropped
            out_of_range++;
            if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::WE_PAST_SRAM, a, st_d[head], h, st_c[head]);
        }
        else {
            c.put(to_sram, uint64_t(a & 0x1FFFFF) | uint64_t(n) << 21, lo, hi);
            if (note) c.put(wr_note, a & 0x1FFFFF);
            written++;
            written_by_size[__builtin_ctz(n)]++;
#ifdef SIM_CHECKS
            if (s1 && s1->on && st_acc[head] >= 0) for (unsigned k = 0; k < n; k++) s1->sram_put(t, a + k, uint64_t(st_acc[head]));   // S1
#endif
        }
        st_have[head] = 0; head = uint8_t((head + 1) % cap); count--;
    }
    // Stepped at the head's last stage; a packet or a PTR entry arriving steps it by itself.
    if (count == 0) return sim::Wake::none();
    return st_due[head] <= t + 1 ? c.next() : sim::Wake::at_cycle(st_due[head]);
}

}  // namespace units
