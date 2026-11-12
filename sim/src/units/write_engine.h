// write_engine.h: the write engine (sim/ref/meshsim/src/WriteEngine.java; AUDIT-M2 3.5).
// Configuration part by W8 (G1); the behaviour by W11 (G3): the pointer stage (2 * ts_engine_latency + 2 entries, in
// order), PTR's read-modify-writes and their in-order returns, count-on-arrival into HIST, the SRAM write Hold with its
// refusal and retry (the stage stops, the input fills), modes 0 (region 0x03, at its address), 1 (PTR[src]), 2
// (PTR[bucket digit]) and 3 (the 64-bit augmented record at PTR[src]). A misaligned PTR is undefined (fatal, named); a
// write at or past mem_chunks is dropped and counted (invariant 19).
//
// The is the register block's (TileControl.apply -> Tile.setMasterConfig / setRecordBase at endCycle(t), used
// from t + 1): the block's `dma` RegFile, read at this unit's step (a later level than the block's), so a store at t
// is seen at t + 1 as in ref. The digits and the augment base are derived from it, never copied.
#pragma once

#include <cstdint>
#include <cstdio>

#include "base/fatal.h"
#include "check.h"
#include "sim.h"
#include "units/linkword.h"
#include "units/mem.h"

namespace units {

// The block's `dma` RegFile: the registers as TileControl holds them (dmaConfig; recordBase, already masked to a chunk).
enum : unsigned { DMA_R_CONFIG = 0, DMA_R_RECORD_BASE = 1, DMA_REGS = 2 };

// DMA_CONFIG's fields (Fields.java).
inline uint32_t mc_kind(uint32_t v) { return v & 1; }
inline uint32_t mc_start(uint32_t v) { return v >> 1 & 0xFF; }
inline uint32_t mc_count(uint32_t v) { return v >> 9 & 0x1FF; }
inline uint32_t mc_bucket_digit(uint32_t v) { return v >> 18 & 7; }   // 5-7: UB, stopped by the block
inline uint32_t mc_hist_digit(uint32_t v) { return v >> 21 & 7; }
inline uint32_t mc_k(uint32_t v) { return (v >> 24 & 7) + 1; }

struct WriteEngine {
    static constexpr unsigned MAX_STAGE = 2 * 15 + 2;

    // ---- ports ----
    sim::FifoIn in;            // width 3: link words from the director (mode 0 region 0x03, modes 1-3), Fifo(we_in_depth)
    sim::RegFileIn dma;        // the block's DMA_CONFIG and record base
    sim::LinkOut ts_req;       // width 3: PTR's read-modify-write requests {OP_ADD_RET, PTR + i, addend}, Link(ts_engine_latency)
    sim::LinkIn ts_ret;        // width 3: their returns {i, old pointer, 0}, Link(ts_engine_latency), in order
    sim::LinkOut hist_req;     // width 3: count-on-arrival {OP_ADD_RET, digit, 1}, Link(ts_engine_latency)
    sim::HoldOut to_sram;      // width 3: the big SRAM's write holding register {chunk | n << 21, lo, hi}
    sim::LinkOut ovf_ev;       // width 0, Link(1) to the block: we-in overflowed (judged at this step; D29, WE_IN_OVF)

    // ---- configuration ----
    uint32_t mem_chunks = 1u << 20;
    unsigned cap = 4;          // the pointer stage: 2 * ts_engine_latency + 2 entries

    // ---- state (StateFp.we's order) ----
    uint64_t st_d[MAX_STAGE] = {}, st_h[MAX_STAGE] = {}, st_c[MAX_STAGE] = {};
    uint32_t st_ptr[MAX_STAGE] = {};
    uint8_t st_have[MAX_STAGE] = {};
    uint8_t head = 0, count = 0;
    uint8_t aw[MAX_STAGE] = {};          // stage entries awaiting their PTR return, in order (a ring)
    uint8_t aw_head = 0, aw_n = 0;
    uint64_t last_put[3] = {};           // the write put on to_sram (re-put on a refusal)
    uint64_t ovf_seen = 0;               // we-in's overflows reported so far

    // ---- statistics and ref's checks ----
    // W20 (harness-only; W22-16, AgTest's figure 12): ref's weTsReq.put / weHistReq.put from a test, made in this
    // step at t: [0] PTR's line, [1] HIST's; {t, op, idx, add}, valid when t != 0
    uint64_t host_req[2][4] = {};
    uint32_t host_rets = 0;         //   PTR returns of the test's requests still to come (dropped: ref's writer awaits none)
    int ck_tile = -1;               // W20: the checker's facts under this tile (src/check.h); -1 none
    uint64_t (*log)[4] = nullptr;   // W20 (harness-only): ref's writer.record()/log: {t, payload lo, header, payload hi}
    uint32_t log_cap = 0, log_n = 0;
    uint64_t taken = 0, written = 0, stall_cycles = 0, augmented = 0, explicit_writes = 0, out_of_range = 0;
#ifdef SIM_CHECKS
    uint64_t written_by_size[3] = {};   // W22 (P4; approved 2026-11-12; SIM_CHECKS only): ref's writtenBySize[log2 chunks], a
                                        // write put to big SRAM (WriteEngine.java:81), for ReadPortGatherTest. A statistic.
    uint32_t stage_peak = 0;        // W22 (P4; approved 2026-11-12; SIM_CHECKS only): ref's stageHighWater, the stage's count
                                    // after a take (WriteEngine.java:109), for EngineTest. A statistic.
#endif

    WriteEngine(sim::Ports& p, unsigned we_in_depth, unsigned ts_engine_latency, uint32_t chunks) : mem_chunks(chunks) {
        if (ts_engine_latency < 1 || 2 * ts_engine_latency + 2 > MAX_STAGE) FATAL("write engine: ts_engine_latency %u", ts_engine_latency);
        cap = 2 * ts_engine_latency + 2;
        p.in(in, "in", 3, we_in_depth);
        p.in(dma, "dma");
        p.out(ts_req, "ts_req", 3);
        p.in(ts_ret, "ts_ret", 3);
        p.out(hist_req, "hist_req", 3);
        p.out(to_sram, "to_sram", 3);
        p.out(ovf_ev, "ovf_ev", 0);
        p.overflow_steps(in);                                  // W10: stepped in the cycle an overflow is judged
    }

    // The configuration as the engine uses it (WriteEngine's fields), from the block's registers.
    static uint32_t bucket_digit(uint32_t dma_config) { return mc_bucket_digit(dma_config); }
    static uint32_t hist_digit(uint32_t dma_config) { return mc_hist_digit(dma_config); }
    static uint32_t augment_base(uint32_t record_base) { return record_base & 0x1FFFFF; }
    // LinkWord.digit: byte sel - 1 of the payload (sel 0: 0).
    static uint32_t digit(uint64_t payload, uint32_t sel) { return sel == 0 ? 0 : uint32_t(payload >> (8 * (sel - 1)) & 0xFF); }

    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const {
        uint64_t h = sim::hash_fields(head, count, aw_head, aw_n, last_put[0], last_put[1], last_put[2], ovf_seen);
        for (unsigned k = 0; k < count; k++) {
            const unsigned j = (head + k) % cap;
            h = sim::hash_mix(h, sim::hash_fields(st_d[j], st_h[j], st_c[j], st_ptr[j], st_have[j]));
        }
        for (unsigned k = 0; k < aw_n; k++) h = sim::hash_mix(h, aw[(aw_head + k) % cap]);
        return h;
    }
};

// WriteEngine.runCycle(t): a PTR return; the write side (a refusal retried, else the stage's head if its PTR is in);
// one packet in while the stage has room.
inline sim::Wake WriteEngine::step(sim::Ctx& c) {
    for (unsigned l = 0; l < 2; l++)                           // W20 (harness-only): a test's request on a line at t
        if (host_req[l][0] && host_req[l][0] == c.now()) {
            c.put(l == 0 ? ts_req : hist_req, host_req[l][1], host_req[l][2], host_req[l][3]);
            host_req[l][0] = 0;
            if (l == 0) host_rets++;                           // PTR's ADD_RET returns the old value here
        }
    const uint64_t t = c.now();
    if (const uint64_t o = c.overflows(in); o != ovf_seen) { ovf_seen = o; c.put(ovf_ev); }   // D29: an event
    if (c.ready(ts_ret)) {                                     // pointer returns, in order
        const uint32_t old = uint32_t(c.peek(ts_ret)[1]);
        c.take(ts_ret);
        if (aw_n == 0 && host_rets) host_rets--;               // W20 (harness-only): a test's request's return, dropped
        else {
            if (aw_n == 0) FATAL("write engine: a PTR return at cycle %llu with nothing awaiting it", (unsigned long long)t);
            const unsigned i = aw[aw_head];
            aw_head = uint8_t((aw_head + 1) % cap); aw_n--;
            st_ptr[i] = old & 0x1FFFFF; st_have[i] = 1;
        }
    }
    if (c.refused(to_sram)) { c.put(to_sram, last_put[0], last_put[1], last_put[2]); stall_cycles++; }
    else if (count > 0 && st_have[head]) {
        uint64_t lo = st_d[head], hi = st_c[head];
        const uint64_t h = st_h[head];
        const uint32_t a = st_ptr[head];
        const unsigned mode = lw::mode(h);
        unsigned n;
        if (mode == lw::M_AUG) {
            const uint32_t base = augment_base(c.read(dma, DMA_R_RECORD_BASE));
            lo = (lo & 0xFFFFFFFFull) | uint64_t(((a - base) & 0x1FFFFF) >> 1) << 32; hi = 0; n = 2; augmented++;
        } else n = 1u << lw::size(h);
        if (a & (n - 1)) {   // W22 (F3, approved 2026-11-12): the PTR named as ref names it (PTR[digit] for mode 2, PTR[src N] otherwise)
            char which[24];
            if (mode == lw::M_BUCKET) std::snprintf(which, sizeof which, "digit");
            else std::snprintf(which, sizeof which, "src %u", lw::src(h));
            FATAL("write engine: tile %u, cycle %llu: a write through PTR[%s] = chunk 0x%x, not aligned to its %u-bit write "
                  "(undefined behaviour, v1.25 s8b): header 0x%llx", lw::dst(h), (unsigned long long)t, which, a, 32 * n, (unsigned long long)h);
        }
        if (a + n > mem_chunks) {                               // undefined (4.1, invariant 19): dropped
            out_of_range++;
            if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::WE_PAST_SRAM, a, st_d[head], h, st_c[head]);   // W20
        }
        else {
            last_put[0] = uint64_t(a & 0x1FFFFF) | uint64_t(n) << 21; last_put[1] = lo; last_put[2] = hi;
            c.put(to_sram, last_put[0], lo, hi);
            written++;
#ifdef SIM_CHECKS
            written_by_size[__builtin_ctz(n)]++;                   // W22: WriteEngine.writtenBySize
#endif
        }
        st_have[head] = 0; head = uint8_t((head + 1) % cap); count--;
    }
    if (count < cap && c.ready(in)) {                          // one packet a cycle while the stage has room
        const sim::Word* w = c.peek(in);
        const uint64_t d = w[0], h = w[1], cw = w[2];
        c.take(in);
        taken++;
        if (log) {                                             // W20 (harness-only): ref's writer.log
            if (log_n == log_cap) FATAL("write engine: the harness's log is full (%u entries)", log_cap);
            uint64_t* e = log[log_n++];
            e[0] = c.now(); e[1] = d; e[2] = h; e[3] = cw;
        }
        const unsigned i = (head + count) % cap, mode = lw::mode(h);
        st_d[i] = d; st_h[i] = h; st_c[i] = cw; count++;
        if (mode == lw::M_WRITE) { st_ptr[i] = lw::offset(lw::addr(h)) >> 2; st_have[i] = 1; explicit_writes++; }
        else {
            const uint32_t cfgw = c.read(dma, DMA_R_CONFIG);
            const uint32_t idx = mode == lw::M_BUCKET ? digit(d, bucket_digit(cfgw)) : lw::src(h);
            const uint32_t stp = mode == lw::M_AUG ? 2 : 1u << lw::size(h);
            c.put(ts_req, mem::OP_ADD_RET, mem::IDX_PTR + idx, stp);
            st_have[i] = 0; aw[(aw_head + aw_n) % cap] = uint8_t(i); aw_n++;
            if (const uint32_t hd = hist_digit(cfgw)) c.put(hist_req, mem::OP_ADD_RET, digit(d, hd), 1);
        }
#ifdef SIM_CHECKS
        if (count > stage_peak) stage_peak = count;         // W22: WriteEngine.stageHighWater
#endif
    }
    // Stepped again while the head can be written or more input can be taken; a head awaiting its PTR is woken by the
    // return, a refusal by its edge.
    if ((count > 0 && st_have[head]) || (count < cap && c.ready(in))) return c.next();
    if (host_req[0][0] || host_req[1][0])                     // W20 (harness-only): a test's request still to make
        return sim::Wake::at_cycle(host_req[0][0] && (!host_req[1][0] || host_req[0][0] < host_req[1][0]) ? host_req[0][0] : host_req[1][0]);
    return sim::Wake::none();
}

}  // namespace units
