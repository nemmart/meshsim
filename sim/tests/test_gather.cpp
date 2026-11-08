// The GATHER engine (W12, G4; GatherEngine.java): a bench of stand-ins around the unit. The director's queue puts, the
// block's `dma` RegFile, the read queue's client-0 Hold (taking on a schedule, so puts are refused and retried), and
// the block's two inputs (the busy half, the overflow event). Exactness against ref is EngConf's (P4); these check
// the rules one at a time.
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

// The director's side: (cycle, ordinal, counter) puts of mode-4 packets.
struct Req { Cycle at; uint32_t ord, ctr; };
struct Dir {
    FifoOut out;
    std::vector<Req> reqs; size_t n = 0;
    Dir(Ports& p, std::vector<Req> r) : reqs(std::move(r)) { p.out(out, "out", 3); }
    uint64_t digest() const { return hash_fields(n); }
    Wake step(Ctx& c) {
        while (n < reqs.size() && reqs[n].at == c.now()) {
            const Req& q = reqs[n++];
            c.put(out, uint64_t(q.ord) << 32 | 0xABCDu, lw::hdr(5, 9, lw::M_GATHER, q.ctr, 0, lw::SIZE_64), 0);
        }
        return n < reqs.size() ? Wake::at_cycle(reqs[n].at) : Wake::none();
    }
};
// The block's `dma` RegFile: DMA_CONFIG and the record base, written at cycle 1 (and changed at `at2`).
struct Dma {
    RegFileOut rf;
    uint32_t cfg, base, cfg2; Cycle at2;
    Dma(Ports& p, uint32_t c, uint32_t b, uint32_t c2 = 0, Cycle a2 = 0) : cfg(c), base(b), cfg2(c2), at2(a2) { p.out(rf, "dma", units::DMA_REGS); }
    Wake step(Ctx& c) {
        if (c.now() == 1) { c.write(rf, units::DMA_R_CONFIG, cfg); c.write(rf, units::DMA_R_RECORD_BASE, base); }
        if (at2 && c.now() == at2) c.write(rf, units::DMA_R_CONFIG, cfg2);
        return at2 && c.now() < at2 ? Wake::at_cycle(at2) : Wake::none();
    }
};
// The read queue's client 0: takes the held word on the cycles `take(t)` says, records what it took.
struct Srq {
    HoldIn in;
    uint64_t period, phase;                       // takes when t % period >= phase (period 1: every cycle)
    struct Got { Cycle at; Word a, b; };
    std::vector<Got> got;
    Srq(Ports& p, uint64_t per, uint64_t ph) : period(per), phase(ph) { p.in(in, "in", mem::kReadReqWidth); }
    uint64_t digest() const { return hash_fields(uint64_t(got.size())); }
    Wake step(Ctx& c) {
        if (!c.ready(in)) return Wake::none();
        if (c.now() % period >= phase) { const Word* w = c.peek(in); got.push_back({c.now(), w[0], w[1]}); c.take(in); return Wake::none(); }
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
        cfg.connect(g.busy, b.busy);
        cfg.link(g.ovf, b.ovf, 1);
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

TEST(gather_refusals_retry_the_same_words_and_lose_nothing) {
    // the read queue takes one cycle in three: most puts are refused; every read arrives once, in order
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
    }
    CHECK(g.stall_cycles > 0);
    CHECK_EQ(g.reads, 30u);
    CHECK_EQ(g.left, 0u);
}

TEST(gather_busy_half_rises_with_the_first_put_and_falls_the_cycle_after_the_last) {
    Sim sim = Bench::make({{4, 1, 0}}, mc(1), 0, 1, 0);
    sim.run(20);
    auto& b = sim.unit<Blk>("blk");
    // the request is delivered at 5: taken, one read put at 5 (busy put at 5, seen 6); at 6 nothing: busy 0 (seen 7)
    bool rose = false, fell = false;
    for (auto& [at, v] : b.busy_at) { if (v && at == 6) rose = true; if (!v && at == 7) fell = true; }
    CHECK(rose);
    CHECK(fell);
}

TEST(gather_k_follows_dma_config_live_wide_fixed_at_the_take) {
    // the request is taken at 3 with k = 3 (left 3, 64 bits); k becomes 5 at 4: ref reads k each cycle for the address
    // (2k * ord), while `left` and `wide` were fixed at the take
    Sim sim = Bench::make({{2, 1, 0}}, mc(3), 0, 1, 0, 16, mc(5), 4);
    sim.run(20);
    auto& s = sim.unit<Srq>("srq");
    CHECK_EQ(s.got.size(), size_t(3));
    const uint32_t want[] = {6, 12, 14};
    for (size_t j = 0; j < 3 && j < s.got.size(); j++) CHECK_EQ(mem::a_addr(s.got[j].a), want[j]);
}

TEST(gather_queue_overflow_reaches_the_block_as_an_event) {
    // depth 2 and the read queue never takes: the queue fills; the third and later puts overflow, each an event
    std::vector<Req> r;
    for (uint32_t q = 0; q < 6; q++) r.push_back({Cycle(2 + q), q, 0});
    Sim sim = Bench::make(r, mc(8), 0, 1000000, 999999, 2);
    sim.run(30);
    auto& g = sim.unit<Gather>("t7.gather");
    auto& b = sim.unit<Blk>("blk");
    CHECK_EQ(g.overflows, 3u);                    // req 0 taken at 3; 1 and 2 fill the queue; 3, 4, 5 overflow
    CHECK_EQ(b.ovf_at.size(), size_t(g.overflows));
}
