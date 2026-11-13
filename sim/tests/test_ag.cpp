// The address generator (AddressGenerator.java): a bench of stand-ins around the unit. The block (the `dma`
// RegFile, the `start` Wire), the table store's STREAM and GROUP paths (TableStore.java:121-137: a request Link(L) into a
// FIFO read the cycle after, one read a cycle per path, the return on a Link(L)) with the PTR view, the read queue's
// client-1 Hold (taking on a schedule), and the block's inputs (status, drift). Exactness against ref is EngConf's (P4);
// these check the timetable's rules on cases worked by hand.
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/ag.h"
#include "units/net.h"

using namespace sim;
using units::AddrGen;
namespace lw = units::lw;
namespace mem = units::mem;

namespace {

constexpr unsigned L = 2;                         // ts_engine_latency

uint32_t stream_count(uint32_t count, uint32_t size, uint32_t counter, uint32_t mode, uint32_t d) {
    return (count & 0x1FFFF) | (size & 3) << 17 | (counter & 7) << 19 | (mode & 7) << 22 | (d & 0x7F) << 25;
}
uint64_t group_entry(uint32_t R, uint32_t start, uint32_t T, uint32_t gap) {
    return uint64_t(R & 0x3FFFF) | uint64_t(start & 0xFF) << 32 | uint64_t((T - 1) & 0xF) << 40 | uint64_t(gap & 0xFF) << 44;
}
uint32_t dma_config(uint32_t kind, uint32_t start, uint32_t count) { return (kind & 1) | (start & 0xFF) << 1 | (count & 0x1FF) << 9; }

struct Tables { uint32_t src[256] = {}, dest[256] = {}, count[256] = {}; uint64_t group[64] = {}; uint32_t ptr[256] = {}; };

// The block: DMA_CONFIG at cycle 1, a start at each cycle in `starts` (driven at level 0, as endCycle(t - 1)).
struct Blk {
    RegFileOut dma; WireOut start;
    uint32_t cfg, base; std::vector<Cycle> starts; size_t n = 0; Word count = 0;
    Blk(Ports& p, uint32_t c, uint32_t b, std::vector<Cycle> s) : cfg(c), base(b), starts(std::move(s)) {
        p.out(dma, "dma", units::DMA_REGS); p.out(start, "start");
    }
    uint64_t digest() const { return hash_fields(n, count); }
    Wake step(Ctx& c) {
        if (c.now() == 1) { c.write(dma, units::DMA_R_CONFIG, cfg); c.write(dma, units::DMA_R_RECORD_BASE, base); }
        while (n < starts.size() && starts[n] == c.now()) { n++; c.drive(start, ++count); }
        return n < starts.size() ? Wake::at_cycle(starts[n]) : Wake::none();
    }
};
// The table store's AG paths (TableStore.runCycle's STREAM and GROUP parts) and the PTR view.
struct Ts {
    LinkIn req_stream, req_group; LinkOut ret_stream, ret_group; RegFileOut ptr;
    Tables tb;
    struct Q { uint32_t idx; Cycle vis; };
    std::vector<Q> qs, qg;
    Ts(Ports& p, const Tables& t) : tb(t) {
        p.in(req_stream, "req_stream", 3); p.in(req_group, "req_group", 3);
        p.out(ret_stream, "ret_stream", 3); p.out(ret_group, "ret_group", 3); p.out(ptr, "ptr", 256);
        qs.reserve(64); qg.reserve(64);
    }
    uint64_t digest() const { return hash_fields(uint64_t(qs.size()), uint64_t(qg.size())); }
    Wake step(Ctx& c) {
        const Cycle t = c.now();
        if (t == 1) for (unsigned i = 0; i < 256; i++) c.write(ptr, i, tb.ptr[i]);
        if (c.ready(req_stream)) { qs.push_back({uint32_t(c.peek(req_stream)[1]), t + 1}); c.take(req_stream); }
        if (c.ready(req_group)) { qg.push_back({uint32_t(c.peek(req_group)[1]), t + 1}); c.take(req_group); }
        if (!qs.empty() && qs.front().vis <= t) {
            const uint32_t i = qs.front().idx - mem::IDX_STREAM; qs.erase(qs.begin());
            c.put(ret_stream, uint64_t(tb.src[i]) | uint64_t(tb.dest[i]) << 32, tb.count[i], mem::IDX_STREAM + i);
        }
        if (!qg.empty() && qg.front().vis <= t) {
            const uint32_t idx = qg.front().idx; qg.erase(qg.begin());
            c.put(ret_group, idx, tb.group[(idx - mem::IDX_GROUP) / 2], 0);
        }
        return qs.empty() && qg.empty() ? Wake::none() : c.next();
    }
};
// The read queue's client 1: takes on cycles where t % period >= phase.
struct Srq {
    HoldIn in; uint64_t period, phase;
    struct Got { Cycle at; Word a, b; };
    std::vector<Got> got;
    Srq(Ports& p, uint64_t per, uint64_t ph) : period(per), phase(ph) { p.in(in, "in", mem::kReadReqWidth); got.reserve(256); }
    uint64_t digest() const { return hash_fields(uint64_t(got.size())); }
    Wake step(Ctx& c) {
        if (!c.ready(in)) return Wake::none();
        if (c.now() % period >= phase) { const Word* w = c.peek(in); got.push_back({c.now(), w[0], w[1]}); c.take(in); return Wake::none(); }
        return c.next();
    }
};
// The block's side of the AG's outputs.
struct Mon {
    RegIn status; LinkIn drift;
    std::vector<std::pair<Cycle, Word>> st; uint64_t drifts = 0;
    explicit Mon(Ports& p) { p.in(status, "status", 1); p.in(drift, "drift", 0); }
    uint64_t digest() const { return hash_fields(uint64_t(st.size()), drifts); }
    Wake step(Ctx& c) {
        if (c.ready(drift)) { drifts++; c.take(drift); }
        if (c.valid(status) && (st.empty() || st.back().second != c.read(status)[0])) st.push_back({c.now(), c.read(status)[0]});
        return Wake::none();
    }
};

Sim bench(const Tables& tb, uint32_t cfg, std::vector<Cycle> starts, uint64_t period = 1, uint64_t phase = 0, uint32_t base = 0) {
    Config c;
    auto& b = c.add<Blk>("blk", cfg, base, std::move(starts));
    auto& a = c.add<AddrGen>("t5.ag", 5u, 2 * L + 2, true);
    auto& t = c.add<Ts>("ts", tb);
    auto& s = c.add<Srq>("srq", period, phase);
    auto& m = c.add<Mon>("mon");
    c.connect(b.dma, a.dma); c.connect(b.start, a.start);
    c.link(a.stream_req, t.req_stream, L); c.link(a.group_req, t.req_group, L);
    c.link(t.ret_stream, a.stream_ret, L); c.link(t.ret_group, a.group_ret, L);
    c.connect(t.ptr, a.ptr);
    c.connect(a.rd, s.in);
    c.connect(a.status, m.status); c.link(a.drift, m.drift, 1);
    c.missed_wake_after("srq", 0);
    return c.build();
}

}  // namespace

TEST(ag_stream_run_lead_in_elements_gap_and_done) {
    Tables tb;
    // stream 7: 3 elements of 64 bits, mode 1, counter 2, d = 1, gap 2; stream 8: 2 of 32 bits, mode 0 to 0x40, d = 66
    tb.src[7] = 0x100 | 2u << 21; tb.dest[7] = 0; tb.count[7] = stream_count(3, lw::SIZE_64, 2, lw::M_SRC, 1);
    tb.src[8] = 0x200; tb.dest[8] = 0x40; tb.count[8] = stream_count(2, lw::SIZE_32, 0, lw::M_WRITE, 66);
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 7, 2), {10});
    sim.run(40);
    auto& s = sim.unit<Srq>("srq");
    auto& a = sim.unit<AddrGen>("t5.ag");
    CHECK_EQ(s.got.size(), size_t(5));
    // the start acts at 10; the first entry is back at 10 + 2L + 1 = 15: elements at 15, 16, 17; the gap at 18, 19;
    // stream 8 at 20, 21 (each put taken the cycle after)
    const Cycle at[] = {16, 17, 18, 21, 22};
    const Word a0[] = {mem::req_a(0x100, lw::SIZE_64, lw::M_SRC, 2, 6), mem::req_a(0x102, lw::SIZE_64, lw::M_SRC, 2, 6),
                       mem::req_a(0x104, lw::SIZE_64, lw::M_SRC, 2, 6), mem::req_a(0x200, lw::SIZE_32, lw::M_WRITE, 0, 66),
                       mem::req_a(0x201, lw::SIZE_32, lw::M_WRITE, 0, 66)};
    const Word b0[] = {0, 0, 0, 0x40, 0x44};
    for (size_t j = 0; j < 5 && j < s.got.size(); j++) { CHECK_EQ(s.got[j].at, at[j]); CHECK_EQ(s.got[j].a, a0[j]); CHECK_EQ(s.got[j].b, b0[j]); }
    CHECK_EQ(a.first_request_at - a.started_at, int64_t(2 * L + 1));
    // done when the last request has left the Hold: put at 21, taken at 22 (held at the start of 22), done at 23
    CHECK_EQ(a.done_at, int64_t(23));
    auto& m = sim.unit<Mon>("mon");
    CHECK_EQ(m.st.size(), size_t(2));
    if (m.st.size() == 2) {
        CHECK_EQ(m.st[0].first, Cycle(11)); CHECK_EQ(m.st[0].second, Word(AddrGen::ST_RUNNING));
        CHECK_EQ(m.st[1].first, Cycle(24)); CHECK_EQ(m.st[1].second, Word(AddrGen::ST_DONE));
    }
    CHECK_EQ(a.violations(), 0u);
}

TEST(ag_group_run_lead_in_rotations_and_period) {
    Tables tb;
    // group 3: R = 2 rotations of T = 2 streams (10, 11), gap 0; each stream has 2 elements of 64 bits, mode 1, d = 0
    tb.group[3] = group_entry(2, 10, 2, 0);
    for (uint32_t i : {10u, 11u}) { tb.src[i] = 0x1000 * i; tb.count[i] = stream_count(2, lw::SIZE_64, 1, lw::M_SRC, 0); }
    Sim sim = bench(tb, dma_config(AddrGen::RUN_GROUPS, 3, 1), {10});
    sim.run(40);
    auto& s = sim.unit<Srq>("srq");
    auto& a = sim.unit<AddrGen>("t5.ag");
    // GROUP read at 10, back at 15; the STREAM reader reads at 15, back at 20 = 10 + 4L + 2: S_0 = 20; rotation 1 at 22
    CHECK_EQ(a.first_request_at - a.started_at, int64_t(4 * L + 2));
    CHECK_EQ(s.got.size(), size_t(4));
    const uint32_t src[] = {0xA000, 0xB000, 0xA002, 0xB002};
    for (size_t j = 0; j < 4 && j < s.got.size(); j++) {
        CHECK_EQ(s.got[j].at, Cycle(21 + j));
        CHECK_EQ(s.got[j].a, mem::req_a(src[j], lw::SIZE_64, lw::M_SRC, 1, 5));
    }
    CHECK_EQ(a.rotations, 2u); CHECK_EQ(a.rotations_late, 0u); CHECK_EQ(a.groups_late_prefetch, 0u);
    CHECK_EQ(a.done_at, int64_t(25));
}

TEST(ag_refusals_in_a_group_run_are_drift_events_and_lose_nothing) {
    Tables tb;
    tb.group[0] = group_entry(6, 0, 3, 1);
    for (uint32_t i = 0; i < 3; i++) { tb.src[i] = 0x400 * (i + 1); tb.count[i] = stream_count(6, lw::SIZE_128, 0, lw::M_SRC, 0); }
    Sim sim = bench(tb, dma_config(AddrGen::RUN_GROUPS, 0, 1), {5}, 4, 2);   // the read queue takes 2 cycles in 4
    sim.run(300);
    auto& s = sim.unit<Srq>("srq");
    auto& a = sim.unit<AddrGen>("t5.ag");
    auto& m = sim.unit<Mon>("mon");
    CHECK_EQ(s.got.size(), size_t(18));
    for (size_t j = 0; j < s.got.size(); j++) {
        const uint32_t r = uint32_t(j / 3), i = uint32_t(j % 3);
        CHECK_EQ(s.got[j].a, mem::req_a(0x400 * (i + 1) + 4 * r, lw::SIZE_128, lw::M_SRC, 0, 5));
    }
    CHECK(a.drift_events > 0);
    CHECK_EQ(m.drifts, a.drift_events);
    CHECK_EQ(a.group_stall_cycles, a.drift_events);
    CHECK(a.rotations_late > 0);                  // drift delays rotations (invariant 3)
    CHECK(!a.running);
    CHECK(a.done);
}

TEST(ag_stream_run_refusals_are_not_drift) {
    Tables tb;
    tb.src[0] = 0x10; tb.count[0] = stream_count(20, lw::SIZE_32, 0, lw::M_SRC, 3);
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), {3}, 3, 1);
    sim.run(200);
    auto& s = sim.unit<Srq>("srq");
    auto& a = sim.unit<AddrGen>("t5.ag");
    CHECK_EQ(s.got.size(), size_t(20));
    for (size_t j = 0; j < s.got.size(); j++) CHECK_EQ(mem::a_addr(s.got[j].a), 0x10u + uint32_t(j));
    CHECK(a.stall_cycles > 0);
    CHECK_EQ(a.drift_events, 0u);
    CHECK_EQ(sim.unit<Mon>("mon").drifts, 0u);
}

TEST(ag_count_zero_run_is_done_at_once) {
    Tables tb;
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 4, 0), {6});
    sim.run(20);
    auto& a = sim.unit<AddrGen>("t5.ag");
    CHECK(!a.running); CHECK(a.done); CHECK_EQ(a.done_at, int64_t(6));
    CHECK_EQ(sim.unit<Srq>("srq").got.size(), size_t(0));
    auto& m = sim.unit<Mon>("mon");
    CHECK_EQ(m.st.size(), size_t(1));
    if (!m.st.empty()) CHECK_EQ(m.st[0].second, Word(AddrGen::ST_DONE));
}

TEST(ag_two_runs_the_second_start_resets_per_run_counters) {
    Tables tb;
    tb.src[1] = 0x20; tb.count[1] = stream_count(4, lw::SIZE_32, 0, lw::M_SRC, 0);
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 1, 1), {3, 30});
    sim.run(60);
    auto& a = sim.unit<AddrGen>("t5.ag");
    CHECK_EQ(sim.unit<Srq>("srq").got.size(), size_t(8));
    CHECK_EQ(a.runs, 2u); CHECK_EQ(a.requests, 4u); CHECK_EQ(a.cum_requests, 4u);
}

DEATH_TEST(ag_a_start_while_running_is_ub, "a start while a run is running") {
    Tables tb;
    tb.src[0] = 0; tb.count[0] = stream_count(50, lw::SIZE_32, 0, lw::M_SRC, 0);
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), {3, 10});
    sim.run(30);
}

DEATH_TEST(ag_a_run_past_the_table_is_ub, "a stream run past the table's end") {
    Tables tb;
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 250, 10), {3});
    sim.run(10);
}

DEATH_TEST(ag_gather_buffers_misaligned_is_ub, "gather buffers not 128-bit") {
    Tables tb;
    tb.count[0] = stream_count(1, lw::SIZE_64, 0, lw::M_GATHER, 0);
    tb.ptr[5] = 0x102;                            // PTR[me] (me = 5) not 128-bit aligned
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), {3});
    sim.run(20);
}

TEST(ag_check_entry_counts_violations) {
    Tables tb;
    tb.src[0] = 0x1; tb.count[0] = stream_count(1, lw::SIZE_64, 0, lw::M_SRC, 0);        // source not aligned to 64 bits
    tb.src[1] = 0; tb.count[1] = stream_count(1, lw::SIZE_32, 0, lw::M_COUNT, 0);        // count-only on counter 0
    tb.src[2] = 0; tb.count[2] = stream_count(1, lw::SIZE_32, 0, lw::M_SRC, 126);        // broadcast from a mesh tile
    Sim sim = bench(tb, dma_config(AddrGen::RUN_STREAMS, 0, 3), {3});
    sim.run(40);
    auto& a = sim.unit<AddrGen>("t5.ag");
    CHECK_EQ(a.align_violations, 1u); CHECK_EQ(a.mode_violations, 1u); CHECK_EQ(a.dest_violations, 1u);
    CHECK_EQ(sim.unit<Srq>("srq").got.size(), size_t(3));   // every element still issues as written
}
