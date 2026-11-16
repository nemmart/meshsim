// The gather unit (machine-spec 13; the RTL's rtl/blocks/gather_unit, gather_unit.veryl's header): a bench of stand-ins
// around the unit. The traffic director's side (its puts into the queue; a request for a queue that is full at the
// start of its cycle is lost, with the overflow's pulse: director.h), the tile control unit's `dma` RegFile, the read
// arbiter's client 0 (taking the offered read on a schedule, and saying so in the cycle it does: `taken`), and the
// tile control unit's two inputs (the busy half, the overflow's pulse). These check the rules one at a time; the unit
// on the whole tile is test_mach's.
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/gather.h"
#include "units/net.h"

using namespace sim;
using units::Gather;
namespace lw = units::lw;
namespace mem = units::mem;

namespace {

// The director's side: (cycle, ordinal, counter) puts of mode-4 packets; a request in a cycle the queue is full at the
// start of is lost, and `ovf` pulses in that cycle.
struct Req { Cycle at; uint32_t ord, ctr; };
struct Dir {
    FifoOut out;
    WireIn full;
    LinkOut ovf;
    std::vector<Req> reqs; size_t n = 0;
    std::vector<Cycle> lost_at;
    Dir(Ports& p, std::vector<Req> r) : reqs(std::move(r)) { p.out(out, "out", 3); p.in(full, "full"); p.out(ovf, "ovf", 0); }
    uint64_t digest() const { return hash_fields(n); }
    Wake step(Ctx& c) {
        while (n < reqs.size() && reqs[n].at == c.now()) {
            const Req& q = reqs[n++];
            if (c.read(full) != 0) { lost_at.push_back(c.now()); c.put(ovf); continue; }
            c.put(out, uint64_t(q.ord) << 32 | 0xABCDu, lw::hdr(5, 9, lw::M_GATHER, q.ctr, 0, lw::SIZE_64), 0);
        }
        return n < reqs.size() ? Wake::at_cycle(reqs[n].at) : Wake::none();
    }
};
// The block's `dma` RegFile, written at cycle 1 (and changed at `at2`). The cases hold v1.35's values, a DMA_CONFIG word
// with k - 1 at [26:24] and a record base; the gather unit reads both from GATHER_CONFIG (machine-spec 13: record_base
// [20:0], value_size, k - 1, at [23:21]), so the stand-in writes that word from them.
struct Dma {
    RegFileOut rf;
    uint32_t cfg, base, cfg2; Cycle at2;
    static uint32_t gather_config(uint32_t v135_dma_config, uint32_t record_base) {
        return (record_base & 0x1FFFFF) | (v135_dma_config >> 24 & 7) << 21;
    }
    Dma(Ports& p, uint32_t c, uint32_t b, uint32_t c2 = 0, Cycle a2 = 0) : cfg(c), base(b), cfg2(c2), at2(a2) { p.out(rf, "dma", units::DMA_REGS); }
    Wake step(Ctx& c) {
        if (c.now() == 1) c.write(rf, units::DMA_R_GATHER, gather_config(cfg, base));
        if (at2 && c.now() == at2) c.write(rf, units::DMA_R_GATHER, gather_config(cfg2, base));
        return at2 && c.now() < at2 ? Wake::at_cycle(at2) : Wake::none();
    }
};
// The read arbiter's client 0: takes the offered read on the cycles `take(t)` says, records what it took, and drives
// `taken`, its takes so far, in the cycle of each (inject.h's gather_taken).
struct Srq {
    HoldIn in;
    WireOut taken;
    uint64_t period, phase;                       // takes when t % period >= phase (period 1: every cycle)
    struct Got { Cycle at; Word a, b; };
    std::vector<Got> got;
    Srq(Ports& p, uint64_t per, uint64_t ph) : period(per), phase(ph) { p.in(in, "in", mem::kReadReqWidth); p.out(taken, "taken"); }
    uint64_t digest() const { return hash_fields(uint64_t(got.size())); }
    Wake step(Ctx& c) {
        if (!c.ready(in)) return Wake::none();
        if (c.now() % period >= phase) {
            const Word* w = c.peek(in); got.push_back({c.now(), w[0], w[1]}); c.take(in);
            c.drive(taken, Word(got.size()));
            return Wake::none();
        }
        return c.next();
    }
};
// The block's two inputs from the gather: the busy half (as it stands at each step) and the overflow events.
struct Blk {
    RegIn busy; LinkIn ovf;
    std::vector<std::pair<Cycle, bool>> busy_at;  // (cycle seen, value)
    std::vector<Cycle> ovf_at;
    explicit Blk(Ports& p) { p.in(busy, "busy", 1); p.in(ovf, "ovf", 0); }
    uint64_t digest() const { return hash_fields(uint64_t(busy_at.size()), uint64_t(ovf_at.size())); }
    Wake step(Ctx& c) {
        if (c.ready(ovf)) { ovf_at.push_back(c.now()); c.take(ovf); }
        if (c.valid(busy)) busy_at.push_back({c.now(), c.read(busy)[0] != 0});
        return Wake::none();
    }
};

struct Bench {
    Sim sim;
    static Sim make(std::vector<Req> reqs, uint32_t dma_cfg, uint32_t base, uint64_t period, uint64_t phase, unsigned depth = 16,
                    uint32_t cfg2 = 0, Cycle at2 = 0) {
        Config cfg;
        auto& d = cfg.add<Dir>("dir", std::move(reqs));
        auto& m = cfg.add<Dma>("blk.dma", dma_cfg, base, cfg2, at2);
        auto& g = cfg.add<Gather>("t7.gather", depth, 7u, true);
        auto& s = cfg.add<Srq>("srq", period, phase);
        auto& b = cfg.add<Blk>("blk");
        cfg.connect(d.out, g.in);
        cfg.connect(m.rf, g.dma);
        cfg.connect(g.rd, s.in);
        cfg.connect(s.taken, g.taken);
        cfg.connect(g.full, d.full);
        cfg.connect(g.busy, b.busy);
        cfg.link(d.ovf, b.ovf, 1);
        cfg.missed_wake_after("srq", 0);              // holds on purpose
        return cfg.build();
    }
};

uint32_t mc(uint32_t k) { return (k - 1) << 24; }   // DMA_CONFIG with only k's field

}  // namespace

TEST(gather_k_odd_reads_64_bits_in_order) {
    // k = 3: three 64-bit reads at base + 6 ord + 2 i, mode 1, the request's counter, to tile 7, address 0
    Sim sim = Bench::make({{2, 5, 3}, {3, 9, 1}}, mc(3), 0x1000, 1, 0);
    sim.run(20);
    auto& s = sim.unit<Srq>("srq");
    CHECK_EQ(s.got.size(), size_t(6));
    const uint32_t ords[] = {5, 5, 5, 9, 9, 9}, is[] = {0, 1, 2, 0, 1, 2}, ctrs[] = {3, 3, 3, 1, 1, 1};
    for (size_t j = 0; j < 6 && j < s.got.size(); j++) {
        CHECK_EQ(s.got[j].a, mem::req_a(0x1000 + 6 * ords[j] + 2 * is[j], lw::SIZE_64, lw::M_SRC, ctrs[j], 7));
        CHECK_EQ(s.got[j].b, Word(0));
        CHECK_EQ(s.got[j].at, Cycle(4 + j));      // queue put 2, taken 3; reads put 3..8, each taken the cycle after
    }
}

TEST(gather_k_even_reads_128_bits) {
    // k = 4: two 128-bit reads at base + 8 ord + 4 i
    Sim sim = Bench::make({{3, 2, 0}}, mc(4), 0x40, 1, 0);
    sim.run(20);
    auto& s = sim.unit<Srq>("srq");
    CHECK_EQ(s.got.size(), size_t(2));
    for (uint32_t j = 0; j < 2 && j < s.got.size(); j++)
        CHECK_EQ(s.got[j].a, mem::req_a(0x40 + 8 * 2 + 4 * j, lw::SIZE_128, lw::M_SRC, 0, 7));
    CHECK_EQ(sim.unit<Gather>("t7.gather").requests, 1u);
}

// gather_unit.veryl: "The read it offers is a register ... held with every field until the arbiter takes it. The
// register takes its next read at the edge that ends a cycle in which it is empty or its read is taken ... while the
// arbiter takes every read in its cycle, one read is offered in every cycle, from value to value without a gap."
TEST(gather_read_not_taken_is_held_and_the_next_follows_its_take) {
    // six requests of k = 5, in cycles 2 to 7; the arbiter takes in the cycles t with t mod 3 = 2. The first request is
    // at the queue's head in 3 and leaves it then; its first read is offered from 4 and taken in 5, and each later read
    // is offered from the cycle after its predecessor's take: read j is taken in 5 + 3j, across the values too.
    std::vector<Req> r;
    for (uint32_t q = 0; q < 6; q++) r.push_back({Cycle(2 + q), 100 + q, q & 7});
    Sim sim = Bench::make(r, mc(5), 0x200, 3, 2);
    sim.run(200);
    auto& s = sim.unit<Srq>("srq");
    auto& g = sim.unit<Gather>("t7.gather");
    CHECK_EQ(s.got.size(), size_t(30));
    for (size_t j = 0; j < s.got.size(); j++) {
        const uint32_t q = uint32_t(j / 5), i = uint32_t(j % 5);
        CHECK_EQ(s.got[j].a, mem::req_a(0x200 + 10 * (100 + q) + 2 * i, lw::SIZE_64, lw::M_SRC, q & 7, 7));
        CHECK_EQ(s.got[j].at, Cycle(5 + 3 * j));
    }
    CHECK_EQ(g.stall_cycles, uint64_t(1 + 2 * 29));   // the first read waits cycle 4; each other the two after it is offered
    CHECK_EQ(g.reads, 30u);
    CHECK_EQ(g.left, 0u);
    CHECK(sim.unit<Dir>("dir").lost_at.empty());
}


TEST(gather_busy_half_rises_with_the_first_put_and_falls_the_cycle_after_the_last) {
    Sim sim = Bench::make({{4, 1, 0}}, mc(1), 0, 1, 0);
    sim.run(20);
    auto& b = sim.unit<Blk>("blk");
    // its half of STATUS.gather_busy is "a read put this cycle", which is offered in the next (gather.h): the request is
    // delivered at 5: taken, one read put at 5 (seen by the tile control unit at 6); at 6 nothing put: 0 (seen 7). The whole
    // level, with the director's and the read arbiter's halves, is test_mach's mach_gather_busy_is_the_queue_or_a_read_not_yet_taken
    bool rose = false, fell = false;
    for (auto& [at, v] : b.busy_at) { if (v && at == 6) rose = true; if (!v && at == 7) fell = true; }
    CHECK(rose);
    CHECK(fell);
}

// 13 and the RTL (rtl/blocks/gather_unit/README.md: "A request's value, its start, its k and so its reads, is computed
// from GATHER_CONFIG as it stands in the cycle the request leaves the queue ... No store moves a read of a request that
// has left the queue"). A store while the unit is busy is UB (13); the RTL's choice is the model's.
TEST(gather_config_is_taken_when_the_request_leaves_the_queue) {
    // the request (ordinal 1) is taken at 3 with k = 3: three 64-bit reads at record_base + 2 k ordinal + 2 i = 6, 8, 10;
    // k becomes 5 at 4 and moves none of them
    Sim sim = Bench::make({{2, 1, 0}}, mc(3), 0, 1, 0, 16, mc(5), 4);
    sim.run(20);
    auto& s = sim.unit<Srq>("srq");
    CHECK_EQ(s.got.size(), size_t(3));
    const uint32_t want[] = {6, 8, 10};
    for (size_t j = 0; j < 3 && j < s.got.size(); j++) CHECK_EQ(mem::a_addr(s.got[j].a), want[j]);
}

// 13 and gather_unit.veryl: "the first read of the request at the queue's head, which leaves the queue in that cycle"
// (a cycle in which the offer register is empty or its read is taken), and "the queue is full when it holds [its
// depth] at the start of a cycle, the count before the cycle's pop: a request then is lost, even if the head leaves in
// that cycle, and error_gather_overflow is 1 in that cycle."
TEST(gather_request_behind_a_read_not_taken_stays_in_the_queue_and_a_full_queue_loses_the_next) {
    // a queue of 2, values of one read (k = 1), the arbiter never takes; requests in cycles 2 to 7. Request 0 is at the
    // head in 3 and leaves then (the register is empty); its read is offered from 4 and never taken, so request 1 (in
    // the queue from 4) and request 2 (from 5) stay: the queue holds 2 at the start of 5, 6 and 7, and the requests of
    // those cycles are lost, each with its pulse in its cycle (the tile control unit has it the cycle after).
    std::vector<Req> r;
    for (uint32_t q = 0; q < 6; q++) r.push_back({Cycle(2 + q), q, 0});
    Sim sim = Bench::make(r, mc(1), 0, 1000000, 999999, 2);
    sim.run(30);
    auto& g = sim.unit<Gather>("t7.gather");
    auto& b = sim.unit<Blk>("blk");
    const std::vector<Cycle> lost = {5, 6, 7}, seen = {6, 7, 8};
    CHECK(sim.unit<Dir>("dir").lost_at == lost);
    CHECK(b.ovf_at == seen);
    CHECK_EQ(g.requests, 1u);                     // only request 0 left the queue
    CHECK_EQ(g.reads, 1u);
}

// The same with the arbiter taking every read: a request leaves the queue the cycle after it enters, the queue never
// holds 2 at the start of a cycle, and none is lost.
TEST(gather_queue_that_drains_loses_nothing) {
    std::vector<Req> r;
    for (uint32_t q = 0; q < 12; q++) r.push_back({Cycle(2 + q), q, 0});
    Sim sim = Bench::make(r, mc(1), 0, 1, 0, 2);
    sim.run(40);
    CHECK(sim.unit<Dir>("dir").lost_at.empty());
    auto& s = sim.unit<Srq>("srq");
    CHECK_EQ(s.got.size(), size_t(12));
    for (size_t j = 0; j < s.got.size(); j++) CHECK_EQ(s.got[j].at, Cycle(4 + j));   // offered in a + 2
}
