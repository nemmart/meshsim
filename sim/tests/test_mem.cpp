// test_mem.cpp (WORKER11, G3): the memory path's units alone, hand-derived cases the M2 programs don't reach. Exactness
// against ref is MemConf's (tests/ref/MemConf.java); these pin the rules: the overflow events (D29), the request FIFO's
// judgment (PORTS.md 3), the big SRAM's room by the count before the pop (13a), write combining, the write engine's
// stage and the SRAM Hold's refusal.
#include <cstdint>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/big_sram.h"
#include "units/inject.h"
#include "units/regblock.h"
#include "units/table_store.h"
#include "units/write_engine.h"

using namespace units;

static uint32_t init_value(uint32_t i) { uint32_t x = i * 0x9E3779B1u + 1; return x ^ (x >> 15); }

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
        cfg.missed_wake_after("we", 1u << 10);          // as add_g1: the stage waits on returns that may never come here
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

namespace {
// The table store with the AG's ports: a driver puts PTR RMWs and window stores at given cycles; a reader (a later
// level, as W12's AG) records PTR[i] from the view every cycle.
struct TsDriver {
    sim::LinkOut req_we, window, req_stream, req_group;
    sim::LinkIn ret_we, ret_stream, ret_group, ovf;
    struct Op { uint64_t at; bool rmw; uint32_t idx, v; };
    std::vector<Op> ops;
    std::vector<uint64_t> ret_old;
    TsDriver(sim::Ports& p, std::vector<Op> o) : ops(std::move(o)) {
        p.out(req_we, "req_we", 3); p.out(window, "window", 2); p.out(req_stream, "req_stream", 3); p.out(req_group, "req_group", 3);
        p.in(ret_we, "ret_we", 3); p.in(ret_stream, "ret_stream", 3); p.in(ret_group, "ret_group", 3); p.in(ovf, "ovf", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        for (sim::LinkIn* in : {&ret_stream, &ret_group, &ovf}) if (c.ready(*in)) c.take(*in);
        if (c.ready(ret_we)) { ret_old.push_back(c.peek(ret_we)[1]); c.take(ret_we); }
        for (const Op& o : ops)
            if (o.at == c.now()) {
                if (o.rmw) c.put(req_we, mem::OP_ADD_RET, mem::IDX_PTR + o.idx, o.v);
                else c.put(window, sim::Word(TBL_PTR) << 32 | o.idx, o.v);
            }
        return c.next();
    }
    uint64_t digest() const { return 0; }
};
struct PtrReader {
    sim::RegFileIn ptr;
    std::vector<uint32_t> seen[3];         // PTR[0..2] at each cycle (index = cycle)
    explicit PtrReader(sim::Ports& p) { p.in(ptr, "ptr"); }
    sim::Wake step(sim::Ctx& c) {
        for (unsigned i = 0; i < 3; i++) { seen[i].resize(c.now() + 1); seen[i][c.now()] = c.read(ptr, i); }
        return c.next();
    }
    uint64_t digest() const { return 0; }
};
}  // namespace

// ref's tile runs the AG before the table store, and the AG reads PTR by a host peek: at t it sees PTR as committed at
// the end of t - 1, never the write-back of t. An RMW of PTR[0] (+0x10) put at 1 arrives at 2 (L = 1), enters the request
// FIFO (visible at 3), is read at 3 and written back at 4: the AG sees 0 at 4 and 0x10 from 5. A window store of PTR[1]
// at 6 (committed at the end of 6): seen from 7. An RMW of PTR[2] (+0x20) put at 7 is written back at 10, and a window
// store of PTR[2] at 10 is lost (figure 12): the AG sees 0 at 10 and 0x20 from 11.
TEST(table_store_ptr_view_is_ptr_before_this_cycles_write_back) {
    sim::Config cfg;
    auto& d = cfg.add<TsDriver>("drv", std::vector<TsDriver::Op>{{1, true, 0, 0x10}, {6, false, 1, 0x40}, {7, true, 2, 0x20}, {10, false, 2, 0x77}});
    auto& ts = cfg.add<TableStore>("ts", true);
    auto& r = cfg.add<PtrReader>("ag");
    cfg.link(d.req_we, ts.req_we, 1);
    cfg.link(ts.ret_we, d.ret_we, 1);
    cfg.link(d.window, ts.window, 1);
    cfg.link(d.req_stream, ts.req_stream, 1);
    cfg.link(d.req_group, ts.req_group, 1);
    cfg.link(ts.ret_stream, d.ret_stream, 1);
    cfg.link(ts.ret_group, d.ret_group, 1);
    cfg.link(ts.ovf_ev, d.ovf, 1);
    cfg.connect(ts.ptr_view, r.ptr);
    sim::Sim m = cfg.build();
    m.run(14);
    const PtrReader& a = m.unit<PtrReader>("ag");
    CHECK_EQ(a.seen[0][3], 0u);
    CHECK_EQ(a.seen[0][4], 0u);                         // the write-back of 4: not yet
    CHECK_EQ(a.seen[0][5], 0x10u);
    CHECK_EQ(a.seen[1][6], 0u);
    CHECK_EQ(a.seen[1][7], 0x40u);                      // the window store of 6
    CHECK_EQ(a.seen[2][10], 0u);
    CHECK_EQ(a.seen[2][11], 0x20u);                     // the write-back of 10; the store of 10 lost
    CHECK_EQ(a.seen[2][13], 0x20u);
    const TableStore& t = m.unit<TableStore>("ts");
    CHECK_EQ(t.m.ptr[2], 0x20u);
    CHECK_EQ(t.pipe.ctrl_writes_lost, uint64_t(1));
    CHECK_EQ(m.unit<TsDriver>("drv").ret_old.size(), size_t(2));
}

namespace {
// The read queue in the injection unit with a real big SRAM behind it: a stand puts read-port requests (client 2) and
// records what the injection unit puts toward the router.
struct RqStand {
    sim::FifoOut rd;
    sim::HoldOut send;
    sim::LinkOut loop_left;
    sim::LinkIn to_router;
    sim::RegIn send_busy;
    struct Req { uint64_t at; uint32_t chunk, size, dest; };
    std::vector<Req> reqs;
    struct Pkt { uint64_t at, lo, h, hi; };
    std::vector<Pkt> out;
    RqStand(sim::Ports& p, std::vector<Req> r) : reqs(std::move(r)) {
        p.out(rd, "rd", 2); p.out(send, "send", 3); p.out(loop_left, "loop_left", 0);
        p.in(to_router, "to_router", 3); p.in(send_busy, "send_busy", 1);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(to_router)) { const sim::Word* w = c.peek(to_router); out.push_back({c.now(), w[0], w[1], w[2]}); c.take(to_router); }
        for (const Req& r : reqs)
            if (r.at == c.now()) c.put(rd, mem::req_a(r.chunk, r.size, lw::M_WRITE, 2, 5), mem::req_b(r.dest));
        return c.next();
    }
    uint64_t digest() const { return 0; }
};
}  // namespace

// Tile 5, area queues of depth 1 (room by the count before the pop), sram_read_latency 2. N0 (chunk 0x40, area 0) put
// at 1 issues at 2 on token 0, the SRAM takes it at 3 and serves it at 4, its row lands at 6: released into the
// read-return hold at 6, injected at 7. N1 (chunk 0x44, area 0) issues at 3 (token 1); the SRAM leaves it in its Hold at
// 4 (area 0 held N0 at the start of 4), takes it at 5, serves it at 6: ready at 8. Z (chunk 0x100000, past mem_chunks
// 2^20: zero-width, payload 0) issues at 4 on token 2 without touching the SRAM: ready at 4 + 2 + 2 = 8. Two read-port
// returns ready at 8: token order, N1 released at 8 (injected 9), Z at 9 (injected 10).
TEST(read_queue_read_port_returns_and_a_same_cycle_tie) {
    SramBlocks blocks;
    NetConfig nc;
    ReadSideConfig rc; rc.present = true;
    sim::Config cfg;
    auto& st = cfg.add<RqStand>("stand", std::vector<RqStand::Req>{{1, 0x40, lw::SIZE_32, 0x100}, {2, 0x44, lw::SIZE_32, 0x104}, {3, 0x100000, lw::SIZE_32, 0x108}});
    auto& in = cfg.add<Injection>("inj", 5u, nc, rc);
    SramConfig sc; sc.area_queue_depth = 1;
    auto& sr = cfg.add<BigSram>("sram", sc, blocks.block(5, sc.mem_chunks), true);
    cfg.connect(st.rd, in.rs.rp_q);
    cfg.connect(st.send, in.send);
    cfg.link(st.loop_left, in.loop_left, 1);
    cfg.link(in.to_router, st.to_router, 1);
    cfg.connect(in.send_busy, st.send_busy);
    cfg.connect(in.rs.sram_rd, sr.rd);
    for (unsigned a = 0; a < 16; a++) cfg.link(sr.ret[a], in.rs.ret[a], 2);
    auto& wsink = cfg.add<SramFeed>("feed", std::vector<SramFeed::W>{});
    cfg.connect(wsink.wr, sr.wr);
    sim::Sim m = cfg.build();
    m.unit<BigSram>("sram").poke(0x40, 0xCAFE0040);
    m.unit<BigSram>("sram").poke(0x44, 0xCAFE0044);
    m.run(14);
    const RqStand& s = m.unit<RqStand>("stand");
    CHECK_EQ(s.out.size(), size_t(3));
    const uint64_t tags = lw::TAG_FROM_SRQ | lw::TAG_READPORT | lw::TAG_TOKEN;     // dst = me: the token loops
    const uint64_t at[3] = {7 + 1, 9 + 1, 10 + 1}, lo[3] = {0xCAFE0040, 0xCAFE0044, 0};
    const uint32_t dest[3] = {0x100, 0x104, 0x108}, tok[3] = {0, 1, 2};
    for (unsigned i = 0; i < 3 && i < s.out.size(); i++) {
        CHECK_EQ(s.out[i].at, at[i]);                   // put at 7, 9, 10; Link(1)
        CHECK_EQ(s.out[i].lo, lo[i]);
        CHECK_EQ(s.out[i].h, lw::hdr(0, 5, lw::M_WRITE, 2, dest[i], lw::SIZE_32) | uint64_t(tok[i]) << lw::TOKEN_SHIFT | tags);
    }
    const Injection& j = m.unit<Injection>("inj");
    CHECK_EQ(j.tok.reads, 0u);
    CHECK_EQ(j.tok.loop_held, 3u);
    CHECK_EQ(j.rs.out_of_range, uint64_t(1));
    CHECK_EQ(j.rs.free_n, uint8_t(64));
    CHECK_EQ(unsigned(j.rs.free_q[(j.rs.free_head + 61) % 64]), 0u);   // freed tokens at the tail, in injection order
    CHECK_EQ(unsigned(j.rs.free_q[(j.rs.free_head + 63) % 64]), 2u);
}

// D31: without the running sum the store is plain and data_sum() scans the block; both give the same sum, also after
// a value is overwritten and a word goes back to zero.
TEST(big_sram_sum_running_or_scanned_agree) {
    SramBlocks blocks;
    sim::Config cfg;
    auto& f = cfg.add<SramFeed>("feed", std::vector<SramFeed::W>{});
    auto& g = cfg.add<SramFeed>("feed2", std::vector<SramFeed::W>{});
    SramConfig on, off; off.running_sum = false;
    auto& a = cfg.add<BigSram>("on", on, blocks.block(0, on.mem_chunks), false);
    auto& b = cfg.add<BigSram>("off", off, blocks.block(1, off.mem_chunks), false);
    cfg.connect(f.wr, a.wr);
    cfg.connect(g.wr, b.wr);
    sim::Sim m = cfg.build();
    BigSram& x = m.unit<BigSram>("on");
    BigSram& y = m.unit<BigSram>("off");
    for (uint32_t i = 0; i < 1000; i++) {
        const uint32_t c = (i * 7919u) % (1u << 20), v = i % 5 == 0 ? 0 : init_value(i);
        x.poke(c, v); y.poke(c, v);
    }
    x.poke(7919, 0); y.poke(7919, 0);
    CHECK(x.data_sum() != 0);
    CHECK_EQ(x.data_sum(), y.data_sum());
    CHECK_EQ(y.sum.sum, uint64_t(0));                  // nothing kept per store
}
