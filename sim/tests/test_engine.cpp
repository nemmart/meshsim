// The engine, increment 2 (WORKER3): Link, Fifo and Hold on the single-threaded loop; wakes; sizes; the fatal cases.
#include <string>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {

// ---- toy units ---------------------------------------------------------------------------------------------------
// Puts 1, 2, ..., n on a Fifo, one a cycle from cycle 1.
struct FifoSource {
    FifoOut out;
    uint64_t n, sent = 0;
    FifoSource(Ports& p, uint64_t count) : n(count) { p.out(out, "out", 1); }
    Wake step(Ctx& c) {
        if (sent < n) c.put(out, ++sent);
        return sent < n ? c.next() : Wake::none();
    }
};
// Takes one word a cycle while any is visible; sums them, keeps the last cycle it took one.
struct FifoSink {
    FifoIn in;
    uint64_t got = 0, sum = 0, last = 0;
    FifoSink(Ports& p, unsigned depth) { p.in(in, "in", 1, depth); }
    Wake step(Ctx& c) {
        if (c.ready(in)) { sum += c.peek(in)[0]; c.take(in); got++; last = c.now(); }
        return c.ready(in) ? c.next() : Wake::none();
    }
};
// Puts one three-word word on a Link at cycle `at`.
struct LinkSource {
    LinkOut out;
    Cycle at;
    LinkSource(Ports& p, Cycle when) : at(when) { p.out(out, "out", 3); }
    Wake step(Ctx& c) {
        if (c.now() == at) { c.put(out, 7, 8, 9); return Wake::none(); }
        return c.now() < at ? Wake::at_cycle(at) : Wake::none();
    }
};
struct LinkSink {
    LinkIn in;
    Cycle seen = 0; Word w[3] = {};
    bool take = true;
    explicit LinkSink(Ports& p, bool takes = true) : take(takes) { p.in(in, "in", 3); }
    Wake step(Ctx& c) {
        if (c.ready(in) && take) { const Word* v = c.peek(in); w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; seen = c.now(); c.take(in); }
        return Wake::none();
    }
};
// Never fed: stepped once, at the first cycle.
struct Idle {
    LinkIn in;
    explicit Idle(Ports& p) { p.in(in, "in", 1); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct Silent {   // the Idle's producer: never puts
    LinkOut out;
    explicit Silent(Ports& p) { p.out(out, "out", 1); }
    Wake step(Ctx&) { return Wake::none(); }
};
// Asks to be woken once, far away.
struct Sleeper {
    Cycle until, woke = 0;
    Sleeper(Ports&, Cycle u) : until(u) {}
    Wake step(Ctx& c) {
        if (c.now() == 1) return Wake::at_cycle(until);
        woke = c.now();
        return Wake::none();
    }
};
// A hold producer that retries refusals (WordHold's protocol) and a consumer that takes every other cycle.
struct HoldSource {
    HoldOut out;
    uint64_t n, next_v = 1, last = 0;
    HoldSource(Ports& p, uint64_t count) : n(count) { p.out(out, "out", 1); }
    Wake step(Ctx& c) {
        if (c.refused(out)) { c.put(out, last); return c.next(); }
        if (next_v <= n) { last = next_v++; c.put(out, last); return c.next(); }
        return Wake::none();    // a refusal of the last put wakes it
    }
};
struct HoldSink {
    HoldIn in;
    uint64_t got = 0, expect = 1;
    bool in_order = true;
    explicit HoldSink(Ports& p) { p.in(in, "in", 1); }
    Wake step(Ctx& c) {
        if (c.ready(in)) {
            if (c.now() % 2) return c.next();          // takes only on even cycles: the producer gets refused
            if (c.peek(in)[0] != expect) in_order = false;
            expect++; got++; c.take(in);
        }
        return Wake::none();
    }
};
// Puts twice in one cycle (a bug).
struct TwoPuts {
    FifoOut out;
    explicit TwoPuts(Ports& p) { p.out(out, "out", 1); }
    Wake step(Ctx& c) { c.put(out, 1); c.put(out, 2); return Wake::none(); }
};
struct BadWake {
    explicit BadWake(Ports&) {}
    Wake step(Ctx& c) { return Wake::at_cycle(c.now()); }
};

}  // namespace

TEST(link_word_arrives_after_its_latency) {
    Config cfg;
    auto& s = cfg.add<LinkSource>("src", Cycle(5));
    auto& d = cfg.add<LinkSink>("dst");
    cfg.link(s.out, d.in, 3);
    Sim sim = cfg.build();
    sim.run(20);
    auto& dd = sim.unit<LinkSink>("dst");
    CHECK_EQ(dd.seen, Cycle(8));                       // put at 5, visible from 5 + 3
    CHECK_EQ(dd.w[0], 7u); CHECK_EQ(dd.w[1], 8u); CHECK_EQ(dd.w[2], 9u);
    CHECK_EQ(sim.steps("dst"), 2u);                     // the first cycle, and the delivery
}

TEST(fifo_full_is_judged_at_the_start_of_the_put_cycle) {
    // depth 1, a put and a take every cycle: WordFIFO refuses the put of a cycle that started full even though the
    // consumer pops in it, so every other word is dropped (PORTS.md 3).
    Config cfg;
    auto& s = cfg.add<FifoSource>("src", uint64_t(100));
    auto& d = cfg.add<FifoSink>("dst", 1u);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.run(200);
    CHECK_EQ(sim.unit<FifoSink>("dst").got, 50u);
}

TEST(fifo_deep_enough_loses_nothing) {
    Config cfg;
    auto& s = cfg.add<FifoSource>("src", uint64_t(1000));
    auto& d = cfg.add<FifoSink>("dst", 2u);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.run(2000);
    auto& dd = sim.unit<FifoSink>("dst");
    CHECK_EQ(dd.got, 1000u);
    CHECK_EQ(dd.sum, 1000u * 1001u / 2);
    CHECK_EQ(dd.last, Cycle(1001));                    // the last put at 1000, seen and taken at 1001
}

TEST(hold_refuses_and_the_producer_retries) {
    Config cfg;
    auto& s = cfg.add<HoldSource>("src", uint64_t(200));
    auto& d = cfg.add<HoldSink>("dst");
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.run(2000);
    auto& dd = sim.unit<HoldSink>("dst");
    CHECK_EQ(dd.got, 200u);
    CHECK(dd.in_order);
}

TEST(an_idle_unit_is_never_stepped) {
    Config cfg;
    auto& s = cfg.add<Silent>("silent");
    auto& i = cfg.add<Idle>("idle");
    cfg.link(s.out, i.in, 1);
    auto& a = cfg.add<FifoSource>("src", uint64_t(500));   // busy beside it
    auto& b = cfg.add<FifoSink>("dst", 4u);
    cfg.connect(a.out, b.in);
    Sim sim = cfg.build();
    sim.run(1000);
    CHECK_EQ(sim.steps("idle"), 1u);                    // the first cycle only
    CHECK_EQ(sim.steps("silent"), 1u);
}

TEST(a_long_sleep_costs_one_wake) {
    Config cfg;
    cfg.add<Sleeper>("z", Cycle(1000000001));
    Sim sim = cfg.build();
    sim.run(2000000000);
    CHECK_EQ(sim.steps("z"), 2u);
    CHECK_EQ(sim.unit<Sleeper>("z").woke, Cycle(1000000001));
    CHECK_EQ(sim.now(), Cycle(2000000001));
}

TEST(no_allocation_in_the_loop) {
    Config cfg;
    auto& s = cfg.add<FifoSource>("src", uint64_t(5000));
    auto& d = cfg.add<FifoSink>("dst", 4u);
    cfg.connect(s.out, d.in);
    auto& h = cfg.add<HoldSource>("hsrc", uint64_t(100));
    auto& k = cfg.add<HoldSink>("hdst");
    cfg.connect(h.out, k.in);
    auto& l = cfg.add<LinkSource>("lsrc", Cycle(9));
    auto& m = cfg.add<LinkSink>("ldst");
    cfg.link(l.out, m.in, 3);
    Sim sim = cfg.build();
    begin_run();
    sim.run(10000);
    end_run();
    CHECK_EQ(sim.unit<FifoSink>("dst").got, 5000u);
}

TEST(sizes_equal_hand_derived_ones) {
    // one Link of latency 3, width 3: its ring L + 2 = 5 slots of 3 words (15, one line rounding: 16); its wheel 4 buckets
    // (the next power of two above 3) of one entry of 4 words each (8, a line).
    Config cfg;
    auto& s = cfg.add<LinkSource>("src", Cycle(5));
    auto& d = cfg.add<LinkSink>("dst");
    cfg.link(s.out, d.in, 3);
    Sim sim = cfg.build();
    const Sizes& z = sim.sizes();
    uint64_t ring = 0, wheel = 0;
    uint32_t wheel_count = 0;
    for (auto& l : z.by_structure) {
        if (l.what == "ring link w3") ring = l.words;
        if (l.what == "wheel link w3") { wheel = l.words; wheel_count = l.count; }
    }
    CHECK_EQ(ring, 16u);
    CHECK_EQ(wheel, 4u * 8u);
    CHECK_EQ(wheel_count, 4u);
    CHECK_EQ(z.wheel_buckets, 4u);
    uint64_t sum = 0;
    for (auto& l : z.by_structure) sum += l.words;
    uint64_t units = 0;
    for (auto& l : z.by_type) if (l.what.rfind("unit ", 0) == 0) units += l.words;
    CHECK_EQ(z.total_words, sum + units);
}

TEST(the_digest_is_a_function_of_the_run) {
    auto build = [] {
        Config cfg;
        auto& s = cfg.add<FifoSource>("src", uint64_t(300));
        auto& d = cfg.add<FifoSink>("dst", 3u);
        cfg.connect(s.out, d.in);
        return cfg.build();
    };
    Sim a = build(), b = build();
    a.run(150); b.run(150);
    CHECK_EQ(a.digest(), b.digest());
    b.run(1);
    CHECK(a.digest() != b.digest());
}

TEST(the_end_of_run_report_names_a_sleeper) {
    Config cfg;
    auto& l = cfg.add<LinkSource>("lsrc", Cycle(3));
    auto& m = cfg.add<LinkSink>("ldst", false);           // never takes: the word sits in its input
    cfg.link(l.out, m.in, 1);
    Sim sim = cfg.build();
    sim.run(10);
    std::string r = sim.sleepers();
    CHECK(r.find("ldst: asleep with data waiting in ldst.in") != std::string::npos);
}

DEATH_TEST(two_puts_in_a_cycle_are_fatal, "two puts on 'tp.out' in cycle 1") {
    Config cfg;
    auto& t = cfg.add<TwoPuts>("tp");
    auto& d = cfg.add<FifoSink>("dst", 4u);
    cfg.connect(t.out, d.in);
    Sim sim = cfg.build();
    sim.run(3);
}

DEATH_TEST(a_link_whose_consumer_never_takes_fills, "link 'ldst.in' full at cycle .*: capacity 3") {
    struct Flood {
        LinkOut out;
        explicit Flood(Ports& p) { p.out(out, "out", 3); }
        Wake step(Ctx& c) { c.put(out, 1, 2, 3); return c.next(); }
    };
    Config cfg;
    auto& f = cfg.add<Flood>("flood");
    auto& m = cfg.add<LinkSink>("ldst", false);
    cfg.link(f.out, m.in, 1);
    Sim sim = cfg.build();
    sim.run(10);
}

DEATH_TEST(a_wake_in_the_past_is_fatal, "unit 'bw' asked to be woken at cycle 1") {
    Config cfg;
    cfg.add<BadWake>("bw");
    Sim sim = cfg.build();
    sim.run(3);
}

DEATH_TEST(an_unwired_port_is_fatal, "port 'ldst.in' \\(link input\\) is not wired") {
    Config cfg;
    cfg.add<LinkSink>("ldst");
    Sim sim = cfg.build();
}

DEATH_TEST(a_unit_of_the_wrong_type_is_fatal, "unit 'dst' is a .*FifoSink.*, not a .*LinkSink") {
    Config cfg;
    auto& s = cfg.add<FifoSource>("src", uint64_t(3));
    auto& d = cfg.add<FifoSink>("dst", 3u);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.unit<LinkSink>("dst");
}

DEATH_TEST(widths_must_match, "link: 'src.out' \\(width 3\\) to 'idle.in' \\(width 1\\)") {
    Config cfg;
    auto& s = cfg.add<LinkSource>("src", Cycle(5));
    auto& i = cfg.add<Idle>("idle");
    cfg.link(s.out, i.in, 1);
}
