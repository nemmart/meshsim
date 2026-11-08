// test_mem.cpp (WORKER11, G3): the memory path's units alone, hand-derived cases the M2 programs don't reach. Exactness
// against ref is MemConf's (tests/ref/MemConf.java); these pin the rules: the overflow events (D29), the request FIFO's
// judgment (PORTS.md 3), the big SRAM's room by the count before the pop (13a), write combining, the write engine's
// stage and the SRAM Hold's refusal.
#include <cstdint>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/big_sram.h"
#include "units/regblock.h"
#include "units/table_store.h"
#include "units/write_engine.h"

using namespace units;

namespace {

// The write engine's surroundings: a director stand-in putting one mode-1 packet a cycle into we-in from cycle 1 to
// `until`; PTR returns given only when `ptr_back`; a big-SRAM stand-in taking a write only when `take_sram`.
struct WeStand {
    sim::FifoOut we_in;
    sim::RegFileOut dma;
    sim::LinkIn ts_req, hist_req, ovf;
    sim::LinkOut ts_ret;
    sim::HoldIn sram;
    uint64_t until = 0;
    bool ptr_back = false, take_sram = true;
    std::vector<uint64_t> ovf_at, sram_at;
    uint32_t ptr = 0x100;
    WeStand(sim::Ports& p, uint64_t n) : until(n) {
        p.out(we_in, "we_in", 3); p.out(dma, "dma", DMA_REGS);
        p.in(ts_req, "ts_req", 3); p.in(hist_req, "hist_req", 3); p.in(ovf, "ovf", 0);
        p.out(ts_ret, "ts_ret", 3); p.in(sram, "sram", 3);
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (t == 1) { c.write(dma, DMA_R_CONFIG, 0); c.write(dma, DMA_R_RECORD_BASE, 0); }
        if (c.ready(ovf)) { ovf_at.push_back(t); c.take(ovf); }
        if (c.ready(hist_req)) c.take(hist_req);
        if (c.ready(ts_req)) {
            const sim::Word* r = c.peek(ts_req);
            if (ptr_back) { c.put(ts_ret, r[1] - mem::IDX_PTR, ptr, 0); ptr += uint32_t(r[2]); }
            c.take(ts_req);
        }
        if (take_sram && c.ready(sram)) { sram_at.push_back(t); c.take(sram); }
        if (t >= 1 && t <= until) c.put(we_in, 0x1234, lw::hdr(5, 0, lw::M_SRC, 0, 0, lw::SIZE_32), 0);
        return c.next();
    }
    uint64_t digest() const { return 0; }
};

struct WeRig {
    static sim::Sim build(uint64_t until, bool ptr_back, bool take_sram) {
        sim::Config cfg;
        auto& s = cfg.add<WeStand>("stand", until);
        s.ptr_back = ptr_back; s.take_sram = take_sram;
        auto& w = cfg.add<WriteEngine>("we", 4u, 1u, 1u << 20);
        cfg.connect(s.we_in, w.in);
        cfg.connect(s.dma, w.dma);
        cfg.link(w.ts_req, s.ts_req, 1);
        cfg.link(s.ts_ret, w.ts_ret, 1);
        cfg.link(w.hist_req, s.hist_req, 1);
        cfg.connect(w.to_sram, s.sram);
        cfg.link(w.ovf_ev, s.ovf, 1);
        return cfg.build();
    }
};

}  // namespace

// Cycles start at 1. No PTR ever returns: the stage (2L + 2 = 4) takes the puts of 1-4 (at 2-5), we-in (4) holds the
// puts of 5-8; the put of 9 meets a full FIFO, judged at 10 (the write engine stepped then, W10's hook), its event at
// 11. One event per overflow: the puts of 9-12.
TEST(write_engine_input_overflow_is_an_event) {
    sim::Sim m = WeRig::build(12, false, true);
    m.run(20);
    const WeStand& s = m.unit<WeStand>("stand");
    const WriteEngine& w = m.unit<WriteEngine>("we");
    CHECK_EQ(w.count, 4u);
    CHECK_EQ(w.ovf_seen, uint64_t(4));
    CHECK_EQ(s.ovf_at.size(), size_t(4));
    CHECK_EQ(s.ovf_at[0], uint64_t(11));
    CHECK_EQ(s.ovf_at[3], uint64_t(14));
}

// PTR returns (L = 1 each way): a packet taken at t asks at t, the store's stand-in answers at t + 1, the return lands
// at t + 2 and the write goes into the SRAM Hold at t + 2, taken at t + 3. With the SRAM refusing, the engine retries
// and its stage fills; nothing is lost.
TEST(write_engine_ptr_path_and_sram_refusal) {
    {
        sim::Sim m = WeRig::build(3, true, true);
        m.run(12);
        const WeStand& s = m.unit<WeStand>("stand");
        CHECK_EQ(s.sram_at.size(), size_t(3));
        CHECK_EQ(s.sram_at[0], uint64_t(2 + 1 + 1 + 1));   // taken 2, asked 2, answered 3, returned 4, put 4, taken 5
        CHECK_EQ(m.unit<WriteEngine>("we").written, uint64_t(3));
    }
    {
        sim::Sim m = WeRig::build(6, true, false);
        m.run(30);
        const WriteEngine& w = m.unit<WriteEngine>("we");
        CHECK_EQ(w.written, uint64_t(2));               // the first sits in the Hold; the second is put, refused ever after
        CHECK(w.stall_cycles > 20);
        CHECK_EQ(w.count, 4u);
    }
}

// PORTS.md 3 on the table store's request FIFOs: a put at t is judged at t + 1 against the occupancy at the start of t.
TEST(request_fifo_judges_a_put_by_the_start_of_its_cycle) {
    ReqFifo f;
    const sim::Word w[3] = {mem::OP_ADD_RET, mem::IDX_PTR + 1, 1};
    uint64_t t = 1;
    for (unsigned i = 0; i < ReqFifo::N; i++, t++) { CHECK(!f.commit(t)); f.put(t, w); }
    CHECK(!f.commit(t));                                // the 16th lands
    CHECK_EQ(unsigned(f.n), ReqFifo::N);
    f.pop(t); f.put(t, w);                              // full at the start of t, popped in t: refused
    CHECK(f.commit(t + 1));
    CHECK_EQ(f.overflows, uint64_t(1));
    CHECK_EQ(f.first_overflow, t);
    f.put(t + 1, w);                                    // 15 at the start of t + 1: lands
    CHECK(!f.commit(t + 2));
    CHECK_EQ(unsigned(f.n), ReqFifo::N);
}

// The register block: the overflow events set ERRORS [4] and [5] at their endCycle.
TEST(block_sets_we_in_and_table_overflow_conditions_from_events) {
    RegBlockState b;
    RegBlockState::In in;
    in.we_in_ovf_ev = true;
    b.end_cycle(10, in);
    CHECK(b.conditions >> RegBlockState::COND_WE_IN_OVF & 1);
    CHECK(!(b.conditions >> RegBlockState::COND_TABLE_OVF & 1));
    RegBlockState::In in2;
    in2.table_ovf_ev = true;
    b.end_cycle(11, in2);
    CHECK(b.conditions >> RegBlockState::COND_TABLE_OVF & 1);
    CHECK(b.error_line);
}

namespace {
// The big SRAM fed directly: writes `w` (chunk, n, lo) at cycles; records its write Hold's refusals.
struct SramFeed {
    struct W { uint64_t at; uint32_t chunk, n; uint64_t lo, hi; };   // n: chunks (1, 2, 4), as the write engine puts it
    sim::HoldOut wr;
    std::vector<W> ws;
    size_t next = 0;
    uint64_t refusals = 0;
    W last{};
    SramFeed(sim::Ports& p, std::vector<W> v) : ws(std::move(v)) { p.out(wr, "wr", 3); }
    sim::Wake step(sim::Ctx& c) {
        if (c.refused(wr)) { refusals++; c.put(wr, last.chunk | uint64_t(last.n) << 21, last.lo, last.hi); return c.next(); }
        if (next < ws.size() && ws[next].at <= c.now()) { last = ws[next++]; c.put(wr, last.chunk | uint64_t(last.n) << 21, last.lo, last.hi); }
        return next < ws.size() ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};
sim::Sim sram_rig(SramBlocks& blocks, std::vector<SramFeed::W> ws, bool combining, unsigned depth) {
    sim::Config cfg;
    auto& f = cfg.add<SramFeed>("feed", std::move(ws));
    SramConfig sc; sc.write_combining = combining; sc.area_queue_depth = depth;
    auto& s = cfg.add<BigSram>("sram", sc, blocks.block(0, sc.mem_chunks), false);
    cfg.connect(f.wr, s.wr);
    return cfg.build();
}
}  // namespace

// Writes land at the queue's head the cycle after they enter (a put at t, taken at t + 1, served at t + 2); the sum is
// kept as the stores land; a write past mem_chunks is dropped.
TEST(big_sram_writes_and_the_sum) {
    SramBlocks blocks;
    sim::Sim m = sram_rig(blocks, {{1, 8, 1, 0xAAAA0001, 0}, {2, 12, 2, 0x0000000200000003ull, 0}, {3, (1u << 20) + 4, 1, 7, 0}}, false, 8);
    const BigSram& s = m.unit<BigSram>("sram");
    m.run(2);                                           // cycles 1-2: put at 1, taken at 2
    CHECK_EQ(s.peek(8), 0u);
    m.run(1);                                           // cycle 3: the first served
    CHECK_EQ(s.peek(8), 0xAAAA0001u);
    m.run(4);
    CHECK_EQ(s.peek(12), 3u);
    CHECK_EQ(s.peek(13), 2u);
    MemSum check(FP_SRAM);
    check.write(8, 0, 0xAAAA0001); check.write(12, 0, 3); check.write(13, 0, 2);
    CHECK_EQ(s.sum.sum, check.sum);
    CHECK_EQ(s.writes_served, uint64_t(3));            // the third (out of range: chunk 2^20) served, nothing stored
}

// Room by the count before the pop (13a): with depth 1, a queue holding one write at the start of t refuses t's arrival
// even though it pops at t; one write enters every other cycle. Puts at 1, 2, 3, 4: taken 2, 4, 6, 8; the feed sees two
// refusals (its puts of 3 and 5, behind an occupied Hold).
TEST(big_sram_room_by_count_before_pop) {
    SramBlocks blocks;
    std::vector<SramFeed::W> ws;
    for (unsigned i = 0; i < 4; i++) ws.push_back({1 + i, 4 * i, 1, i + 1, 0});   // all in area 0
    sim::Sim m = sram_rig(blocks, ws, false, 1);
    m.run(20);
    CHECK_EQ(m.unit<SramFeed>("feed").refusals, uint64_t(2));
    CHECK_EQ(m.unit<BigSram>("sram").writes_served, uint64_t(4));
    for (unsigned i = 0; i < 4; i++) CHECK_EQ(m.unit<BigSram>("sram").peek(4 * i), i + 1);
}
// Write combining needs a write behind a read in its area (one arrival of each a cycle): MemConf covers it with P2.
