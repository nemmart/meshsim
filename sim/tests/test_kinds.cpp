// The engine, increment 3 (WORKER3): Reg, Wire (levels, loops), RegFile, Count, the credit Fifo.
#include <string>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {

// ---- Reg: puts a value from a schedule; a reader counts its steps and records what it saw ----
struct RegSource {
    RegOut out;
    const Word* vals; uint32_t n, i = 0;
    RegSource(Ports& p, const Word* v, uint32_t count) : vals(v), n(count) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(out, n, i); }
    Wake step(Ctx& c) {
        if (i < n) c.put(out, vals[i++]);
        return i < n ? c.next() : Wake::none();
    }
};
struct RegReader {
    RegIn in;
    uint64_t steps = 0, last = 0;
    Word seen[16] = {}; Cycle at[16] = {};
    explicit RegReader(Ports& p) { p.in(in, "in", 1); }
    uint64_t digest() const { return hash_fields(in, steps, last, seen, at); }
    Wake step(Ctx& c) {
        steps++;
        if (c.valid(in) && c.read(in)[0] != last) { last = c.read(in)[0]; seen[steps % 16] = last; at[steps % 16] = c.now(); }
        return Wake::none();
    }
};

// ---- Wire: a counter drives a wire every 4 cycles; a reader at the next level sees it in the same cycle ----
struct WireDriver {
    WireOut out;
    uint64_t v = 0;
    explicit WireDriver(Ports& p) { p.out(out, "out", 0); }
    uint64_t digest() const { return hash_fields(out, v); }
    Wake step(Ctx& c) {
        if (c.now() % 4 == 0) c.drive(out, ++v);
        return c.next();
    }
};
struct WireReader {
    WireIn in;
    uint64_t steps = 0, sum = 0; Cycle last_change = 0; Word last = 0;
    explicit WireReader(Ports& p) { p.in(in, "in"); }
    uint64_t digest() const { return hash_fields(in, steps, sum, last_change, last); }
    Wake step(Ctx& c) {
        steps++;
        Word v = c.read(in);
        if (v != last) { last = v; last_change = c.now(); sum += v; }
        return Wake::none();
    }
};
// A chain through wires: a -> b -> c; and a loop: x -> y -> x.
struct Relay {
    WireIn in; WireOut out;
    explicit Relay(Ports& p) { p.in(in, "in"); p.out(out, "out"); }
    uint64_t digest() const { return hash_fields(in, out); }
    Wake step(Ctx& c) { c.drive(out, c.read(in) + 1); return Wake::none(); }
};
struct DoubleDriver {
    WireOut out;
    explicit DoubleDriver(Ports& p) { p.out(out, "out"); }
    Wake step(Ctx& c) { c.drive(out, 1); c.drive(out, 2); return Wake::none(); }
};

// ---- RegFile: an owner writes register t % 4 = t each cycle; a reader sums the registers it reads ----
struct RfOwner {
    RegFileOut rf;
    explicit RfOwner(Ports& p) { p.out(rf, "rf", 4); }
    Wake step(Ctx& c) { if (c.now() <= 40) c.write(rf, unsigned(c.now() % 4), uint32_t(c.now())); return c.now() < 40 ? c.next() : Wake::none(); }
};
struct RfReader {
    RegFileIn rf;
    uint64_t steps = 0; uint32_t r[4] = {};
    explicit RfReader(Ports& p) { p.in(rf, "rf"); }
    uint64_t digest() const { return hash_fields(rf, steps, r); }
    Wake step(Ctx& c) { steps++; for (unsigned i = 0; i < 4; i++) r[i] = c.read(rf, i); return Wake::none(); }
};

// ---- Count: a producer increments every cycle; a reader above it reads now() and through_last() ----
struct Counter {
    CountOut out;
    explicit Counter(Ports& p) { p.out(out, "out"); }
    Wake step(Ctx& c) { c.inc(out, 2); return c.now() < 100 ? c.next() : Wake::none(); }
};
struct CountReader {
    CountIn in; WireIn tick;
    uint64_t now_v = 0, last_v = 0;
    explicit CountReader(Ports& p) { p.in(in, "in"); p.in(tick, "tick"); }
    uint64_t digest() const { return hash_fields(in, tick, now_v, last_v); }
    Wake step(Ctx& c) { now_v = c.now(in); last_v = c.through_last(in); return c.next(); }
};
struct Ticker {   // puts the reader a level above the counter
    WireOut out;
    explicit Ticker(Ports& p) { p.out(out, "out"); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct CounterTicker {   // a counter that also drives the tick wire (so it sits below the reader)
    CountOut out; WireOut tick;
    explicit CounterTicker(Ports& p) { p.out(out, "out"); p.out(tick, "tick"); }
    Wake step(Ctx& c) { c.inc(out, 2); return c.now() < 100 ? c.next() : Wake::none(); }
};
struct FifoFlood {
    FifoOut out;
    explicit FifoFlood(Ports& p) { p.out(out, "out", 1); }
    Wake step(Ctx& c) { c.put(out, c.now()); return c.now() < 50 ? c.next() : Wake::none(); }
};
struct FifoNever {   // never takes: every put after the first `depth` overflows
    FifoIn in;
    explicit FifoNever(Ports& p, unsigned d) { p.in(in, "in", 1, d); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct OvfReader {
    CountIn in;
    uint64_t seen = 0; Cycle at = 0;
    explicit OvfReader(Ports& p) { p.in(in, "ovf"); }
    uint64_t digest() const { return hash_fields(in, seen, at); }
    Wake step(Ctx& c) { if (!at && c.through_last(in)) { seen = c.through_last(in); at = c.now(); } return c.now() < 60 ? c.next() : Wake::none(); }
};

// ---- credit Fifo ----
struct CreditSource {
    CreditOut out;
    uint64_t n, sent = 0; Cycle blocked = 0;
    CreditSource(Ports& p, uint64_t count) : n(count) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(out, n, sent, blocked); }
    Wake step(Ctx& c) {
        if (sent < n) {
            if (c.credits(out)) c.put(out, ++sent); else { blocked++; return Wake::none(); }   // a return wakes it
        }
        return sent < n ? c.next() : Wake::none();
    }
};
struct CreditSink {
    FifoIn in;
    uint64_t got = 0, sum = 0;
    CreditSink(Ports& p, unsigned depth) { p.in_credit(in, "in", 1, depth); }
    uint64_t digest() const { return hash_fields(in, got, sum); }
    Wake step(Ctx& c) {
        if (c.now() % 3 && c.ready(in)) return c.next();      // takes every third cycle: the producer runs out of credit
        if (c.ready(in)) { sum += c.peek(in)[0]; c.take(in); got++; }
        return c.ready(in) ? c.next() : Wake::none();
    }
};
struct Greedy {
    CreditOut out;
    explicit Greedy(Ports& p) { p.out(out, "out", 1); }
    Wake step(Ctx& c) { c.put(out, 1); return c.next(); }
};

}  // namespace

TEST(reg_wakes_its_readers_only_on_a_change) {
    static const Word vals[] = { 5, 5, 5, 6, 6, 7 };
    Config cfg;
    auto& s = cfg.add<RegSource>("src", vals, 6u);
    auto& a = cfg.add<RegReader>("a");
    auto& b = cfg.add<RegReader>("b");
    cfg.connect(s.out, a.in);
    cfg.connect(s.out, b.in);
    Sim sim = cfg.build();
    sim.run(20);
    // put at 1..6: 5 (seen at 2), 5, 5 (no change), 6 (put at 4, seen at 5), 6, 7 (put at 6, seen at 7)
    CHECK_EQ(sim.steps("a"), 4u);                       // cycle 1, then 2, 5, 7
    CHECK_EQ(sim.steps("b"), 4u);
    CHECK_EQ(sim.unit<RegReader>("a").last, 7u);
}

TEST(a_wire_change_steps_its_reader_in_the_same_cycle) {
    Config cfg;
    auto& d = cfg.add<WireDriver>("drv");
    auto& r = cfg.add<WireReader>("rd");
    cfg.connect(d.out, r.in);
    Sim sim = cfg.build();
    sim.run(40);                                       // cycles 1..40: changes at 4, 8, ..., 40
    auto& rr = sim.unit<WireReader>("rd");
    CHECK_EQ(rr.last_change, Cycle(40));
    CHECK_EQ(rr.sum, 55u);                              // 1 + ... + 10
    CHECK_EQ(sim.steps("rd"), 11u);                     // cycle 1, and each of the 10 changes
}

TEST(levels_follow_a_wire_chain) {
    Config cfg;
    auto& d = cfg.add<WireDriver>("drv");
    // Relays of one type form a chain drv -> r1 -> r2: one level per type can't order r1 before r2, so use two types
    struct RelayB { WireIn in; WireOut out; explicit RelayB(Ports& p) { p.in(in, "in"); p.out(out, "out"); }
                    uint64_t digest() const { return hash_fields(in, out); }
                    Wake step(Ctx& c) { c.drive(out, c.read(in) * 10); return Wake::none(); } };
    auto& r1 = cfg.add<Relay>("r1");
    auto& r2 = cfg.add<RelayB>("r2");
    auto& rd = cfg.add<WireReader>("rd");
    cfg.connect(d.out, r1.in);
    cfg.connect(r1.out, r2.in);
    cfg.connect(r2.out, rd.in);
    Sim sim = cfg.build();
    sim.run(8);                                        // the driver's change at 8 crosses three levels in cycle 8
    auto& w = sim.unit<WireReader>("rd");
    CHECK_EQ(w.last, 30u);                             // (2 + 1) * 10
    CHECK_EQ(w.last_change, Cycle(8));
}

DEATH_TEST(a_combinational_loop_is_rejected_with_its_units, "a combinational loop through wires: (x -> y -> x|y -> x -> y)") {
    Config cfg;
    auto& x = cfg.add<Relay>("x");
    auto& y = cfg.add<Relay>("y");
    cfg.connect(x.out, y.in);
    cfg.connect(y.out, x.in);
    Sim sim = cfg.build();
}

DEATH_TEST(one_level_per_type_must_order_the_wires, "levels: 'r1' .* drives a wire read by 'r2'") {
    Config cfg;
    auto& d = cfg.add<WireDriver>("drv");
    auto& r1 = cfg.add<Relay>("r1");
    auto& r2 = cfg.add<Relay>("r2");
    auto& rd = cfg.add<WireReader>("rd");
    cfg.connect(d.out, r1.in);
    cfg.connect(r1.out, r2.in);
    cfg.connect(r2.out, rd.in);
    Sim sim = cfg.build();
}

DEBUG_DEATH_TEST(a_wire_written_twice_in_a_cycle_is_fatal, "wire of dd written twice in one cycle") {
    Config cfg;
    auto& d = cfg.add<DoubleDriver>("dd");
    auto& r = cfg.add<WireReader>("rd");
    cfg.connect(d.out, r.in);
    Sim sim = cfg.build();
    sim.run(2);
}

TEST(a_regfile_change_steps_its_reader) {
    Config cfg;
    auto& o = cfg.add<RfOwner>("own");
    auto& r = cfg.add<RfReader>("rd");
    cfg.connect(o.rf, r.rf);
    Sim sim = cfg.build();
    sim.run(50);
    auto& rr = sim.unit<RfReader>("rd");
    CHECK_EQ(rr.r[0], 40u); CHECK_EQ(rr.r[1], 37u); CHECK_EQ(rr.r[2], 38u); CHECK_EQ(rr.r[3], 39u);
    CHECK_EQ(sim.steps("rd"), 40u);                    // cycle 1 (with its write), then each change at 2..40
}

TEST(a_count_reads_now_and_through_last) {
    Config cfg;
    auto& k = cfg.add<CounterTicker>("ctr");
    auto& r = cfg.add<CountReader>("rd");
    cfg.connect(k.out, r.in);
    cfg.connect(k.tick, r.tick);
    Sim sim = cfg.build();
    sim.run(10);                                       // the counter adds 2 in each of cycles 1..10, before the reader
    auto& rr = sim.unit<CountReader>("rd");
    CHECK_EQ(rr.now_v, 20u);
    CHECK_EQ(rr.last_v, 18u);
}

TEST(fifo_overflows_are_counted_at_the_next_cycle) {
    Config cfg;
    auto& f = cfg.add<FifoFlood>("flood");
    auto& n = cfg.add<FifoNever>("never", 2u);
    auto& o = cfg.add<OvfReader>("rd");
    cfg.connect(f.out, n.in);
    cfg.count_overflows(n.in, o.in);
    cfg.missed_wake_after("never", 0);                    // never takes, on purpose
    Sim sim = cfg.build();
    sim.run(60);
    // puts at 1, 2 fit; the put at 3 overflows at its delivery in 4; through_last sees it at 5
    auto& rr = sim.unit<OvfReader>("rd");
    CHECK_EQ(rr.at, Cycle(5));
    CHECK_EQ(rr.seen, 1u);
}

TEST(credit_fifo_room_as_of_the_start_and_returns_at_the_next_cycle) {
    Config cfg;
    auto& s = cfg.add<CreditSource>("src", uint64_t(300));
    auto& d = cfg.add<CreditSink>("dst", 2u);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    begin_run();
    sim.run(2000);
    end_run();
    auto& dd = sim.unit<CreditSink>("dst");
    CHECK_EQ(dd.got, 300u);
    CHECK_EQ(dd.sum, 300u * 301u / 2);
    CHECK(sim.unit<CreditSource>("src").blocked > 0);
}

DEATH_TEST(a_put_without_a_credit_is_fatal, "put on 'g.out' at cycle 3 without a credit") {
    Config cfg;
    auto& g = cfg.add<Greedy>("g");
    auto& n = cfg.add<FifoNever>("never_unused", 1u);
    (void)n;
    struct Hoard { FifoIn in; explicit Hoard(Ports& p) { p.in_credit(in, "in", 1, 2); } Wake step(Ctx&) { return Wake::none(); } };
    auto& h = cfg.add<Hoard>("h");
    cfg.connect(g.out, h.in);
    struct F { FifoOut out; explicit F(Ports& p) { p.out(out, "o", 1); } Wake step(Ctx&) { return Wake::none(); } };
    auto& f = cfg.add<F>("f");
    cfg.connect(f.out, n.in);
    Sim sim = cfg.build();
    sim.run(5);
}

TEST(reg_wire_regfile_count_allocate_nothing_in_the_loop) {
    static const Word vals[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Config cfg;
    auto& s = cfg.add<RegSource>("src", vals, 8u);
    auto& a = cfg.add<RegReader>("a");
    cfg.connect(s.out, a.in);
    auto& d = cfg.add<WireDriver>("drv");
    auto& r = cfg.add<WireReader>("rd");
    cfg.connect(d.out, r.in);
    auto& o = cfg.add<RfOwner>("own");
    auto& q = cfg.add<RfReader>("rfr");
    cfg.connect(o.rf, q.rf);
    Sim sim = cfg.build();
    begin_run();
    sim.run(500);
    end_run();
    CHECK_EQ(sim.unit<RegReader>("a").last, 8u);
}
