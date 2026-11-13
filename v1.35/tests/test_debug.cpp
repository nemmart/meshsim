// The debug checks (increment 5; sim.h DEBUG CHECKS; D20): the seeded and instance-major orders, step-all, the
// missed-wake detector and its per-unit threshold, run_until's end, and options refused rather than ignored.
#include <string>
#include <vector>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"
#include "synth.h"

using namespace sim;

namespace {

uint64_t synth_digest(Options o, Cycle cycles = 3000) {
    Config cfg(o);
    synth::Params P;
    P.seed = 7;
    synth::build(cfg, P);
    Sim s = cfg.build();
    s.run(cycles);
    return s.digest();
}

// ---- a unit that forgets next(): takes one word per step and returns none, even with more waiting ----
struct Burst {
    FifoOut out;
    uint64_t sent = 0;
    explicit Burst(Ports& p) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(out, sent); }
    Wake step(Ctx& c) {
        if (c.now() >= 10 && sent < 8) { c.put(out, ++sent); return sent < 8 ? c.next() : Wake::none(); }
        return c.now() < 10 ? Wake::at_cycle(10) : Wake::none();
    }
};
struct Forgetful {
    FifoIn in;
    uint64_t got = 0, sum = 0;
    explicit Forgetful(Ports& p) { p.in(in, "in", 1, 8); }
    uint64_t digest() const { return hash_fields(in, got, sum); }
    Wake step(Ctx& c) {
        if (c.now() % 2 == 0 && c.ready(in)) { sum += c.peek(in)[0] * c.now(); c.take(in); got++; }
        return Wake::none();                            // the bug: data may still be waiting
    }
};
struct Careful : Forgetful {
    using Forgetful::Forgetful;
    Wake step(Ctx& c) { Forgetful::step(c); return c.ready(in) ? c.next() : Wake::none(); }
};
template <class Sink>
uint64_t burst_digest(Options o, uint64_t* got = nullptr) {
    Config cfg(o);
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Sink>("sink");
    cfg.connect(b.out, f.in);
    if (o.missed_wake_after) cfg.missed_wake_after("sink", 0);   // here only step-all is under test
    Sim s = cfg.build();
    s.run(100);
    if (got) *got = s.unit<Sink>("sink").got;
    return s.digest();
}

// ---- an illegal coupling: two types share a global, so the result depends on who steps first ----
uint64_t g_shared = 0;
struct Writer { explicit Writer(Ports&) {} Wake step(Ctx& c) { g_shared = g_shared * 31 + c.now(); return c.next(); } };
struct Reader {
    uint64_t seen = 0;
    explicit Reader(Ports&) {}
    Wake step(Ctx& c) { seen = seen * 1000003 + g_shared; return c.now() < 50 ? c.next() : Wake::none(); }
};
uint64_t shared_digest(Options o) {
    g_shared = 0;
    Config cfg(o);
    cfg.add<Reader>("r1"); cfg.add<Writer>("w1"); cfg.add<Reader>("r2"); cfg.add<Writer>("w2");
    Sim s = cfg.build();
    s.run(60);
    return s.digest();
}

// ---- blocked on an output: a credit fifo and a hold, the detector must stay silent ----
struct Source {
    FifoOut out;
    uint64_t n = 0, total;
    explicit Source(Ports& p, uint64_t count = 40) : total(count) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(out, n, total); }
    Wake step(Ctx& c) { if (n < total) c.put(out, ++n); return n < total ? c.next() : Wake::none(); }
};
struct CreditRelay {            // forwards its input only with a credit; with none, sleeps on its data: a return wakes it
    FifoIn in; CreditOut out;
    explicit CreditRelay(Ports& p) { p.in(in, "in", 1, 64); p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(in, out); }
    Wake step(Ctx& c) {
        if (c.ready(in) && c.credits(out)) { c.put(out, c.peek(in)[0]); c.take(in); }
        return c.ready(in) && c.credits(out) > 1 ? c.next() : Wake::none();
    }
};
struct SlowCreditSink {
    FifoIn in;
    uint64_t got = 0;
    explicit SlowCreditSink(Ports& p) { p.in_credit(in, "in", 1, 2); }
    uint64_t digest() const { return hash_fields(in, got); }
    Wake step(Ctx& c) {
        if (c.ready(in) && c.now() % 5 == 0) { c.take(in); got++; }
        return c.ready(in) ? c.next() : Wake::none();
    }
};
struct HoldRelay {              // ref's idiom (WriteEngine): forwards its head with a copy kept; a refusal puts the copy again
    FifoIn in; HoldOut out;
    Word copy = 0;
    explicit HoldRelay(Ports& p) { p.in(in, "in", 1, 64); p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(in, out, copy); }
    Wake step(Ctx& c) {
        if (c.refused(out)) { c.put(out, copy); return c.next(); }          // retry; then look again (an acceptance wakes no one)
        if (c.ready(in)) { copy = c.peek(in)[0]; c.put(out, copy); c.take(in); }
        return c.ready(in) ? c.next() : Wake::none();                        // its last put: a refusal at t + 1 wakes it
    }
};
struct SlowHoldSink {
    HoldIn in;
    uint64_t got = 0;
    explicit SlowHoldSink(Ports& p) { p.in(in, "in", 1); }
    uint64_t digest() const { return hash_fields(in, got); }
    Wake step(Ctx& c) {
        if (c.ready(in) && c.now() % 4 == 0) { c.take(in); got++; }
        return c.ready(in) ? c.next() : Wake::none();
    }
};

// ---- waiting on a Wire with data in an input: the detector's blind spot, so a per-unit threshold ----
struct Gate {
    WireOut open;
    explicit Gate(Ports& p) { p.out(open, "open", 0); }
    Wake step(Ctx& c) { if (c.now() == 30) c.drive(open, 1); return c.now() < 30 ? Wake::at_cycle(30) : Wake::none(); }
};
struct GatedSink {
    FifoIn in; WireIn open;
    uint64_t got = 0;
    explicit GatedSink(Ports& p) { p.in(in, "in", 1, 64); p.in(open, "open"); }
    uint64_t digest() const { return hash_fields(in, open, got); }
    Wake step(Ctx& c) {
        if (c.read(open) && c.ready(in)) { c.take(in); got++; }
        return c.read(open) && c.ready(in) ? c.next() : Wake::none();     // waits for the gate's change
    }
};
uint64_t gated(bool own_threshold) {
    Config cfg;
    auto& s = cfg.add<Source>("src", uint64_t(10));        // words at 1..10, seen 2..11; the gate opens at 30
    auto& g = cfg.add<Gate>("gate");
    auto& k = cfg.add<GatedSink>("sink");
    cfg.connect(s.out, k.in);
    cfg.connect(g.open, k.open);
    if (own_threshold) cfg.missed_wake_after("sink", 25);   // asleep on its data from 11 to 29: within its own threshold
    Sim sim = cfg.build();
    sim.run(200);
    return sim.unit<GatedSink>("sink").got;
}

// ---- run_until's end ----
struct Halt {
    Cycle at, halted = 0;
    uint64_t steps = 0;
    Halt(Ports&, Cycle t) : at(t) {}
    Wake step(Ctx& c) { steps++; if (c.now() == at) { halted = c.now(); return Wake::none(); } return c.next(); }
};

}  // namespace

TEST(the_synthetic_chip_digests_the_same_under_every_order) {
    std::vector<Options> opts(7);
    opts[1].shuffle_seed = 1; opts[2].shuffle_seed = 2; opts[3].shuffle_seed = 3;
    opts[4].order = Options::Order::instance_major;
    opts[5].order = Options::Order::instance_major; opts[5].shuffle_seed = 9;
    opts[6].step_all = true;
    const uint64_t want = synth_digest(opts[0]);
    for (size_t k = 1; k < opts.size(); k++) CHECK_EQ(synth_digest(opts[k]), want);
}

TEST(step_all_catches_a_forgotten_next) {
    Options ready, all;
    all.step_all = true;
    uint64_t got_ready = 0, got_all = 0;
    const uint64_t a = burst_digest<Forgetful>(ready, &got_ready), b = burst_digest<Forgetful>(all, &got_all);
    CHECK(a != b);                                      // the digests differ: the missed wake is caught
    CHECK(got_ready < got_all);
    CHECK_EQ(burst_digest<Careful>(ready), burst_digest<Careful>(all));   // the corrected unit agrees
}

DEBUG_DEATH_TEST(the_detector_names_a_unit_that_forgets_next, "missed wake at cycle .*: sink is asleep with data waiting in sink.in") {
    Config cfg;
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    Sim s = cfg.build();
    s.run(100);
}

TEST(the_seeded_and_instance_major_orders_catch_an_order_dependence) {
    Options type_major, inst, s1, s2, s3;
    inst.order = Options::Order::instance_major;
    s1.shuffle_seed = 1; s2.shuffle_seed = 2; s3.shuffle_seed = 3;
    const uint64_t t = shared_digest(type_major);
    CHECK_EQ(shared_digest(type_major), t);             // deterministic
    CHECK(shared_digest(inst) != t);
    CHECK(shared_digest(s1) != t || shared_digest(s2) != t || shared_digest(s3) != t);
}

TEST(the_detector_is_silent_on_units_blocked_on_a_credit_or_a_refusal) {
    Config cfg;
    auto& a = cfg.add<Source>("a");
    auto& r = cfg.add<CreditRelay>("crelay");
    auto& k = cfg.add<SlowCreditSink>("csink");
    cfg.connect(a.out, r.in);
    cfg.connect(r.out, k.in);
    auto& b = cfg.add<Source>("b");
    auto& h = cfg.add<HoldRelay>("hrelay");
    auto& j = cfg.add<SlowHoldSink>("hsink");
    cfg.connect(b.out, h.in);
    cfg.connect(h.out, j.in);
    Sim s = cfg.build();
    s.run(400);                                         // fatal on a report (the default)
    CHECK_EQ(s.unit<SlowCreditSink>("csink").got, 40u);
    CHECK_EQ(s.unit<SlowHoldSink>("hsink").got, 40u);
}

TEST(a_units_own_threshold_covers_a_wait_on_a_wire) {
    CHECK_EQ(gated(true), 10u);
}

DEBUG_DEATH_TEST(a_wait_on_a_wire_is_flagged_without_its_own_threshold, "missed wake at cycle 11: sink is asleep with data waiting in sink.in") {
    gated(false);
}

TEST(the_detector_can_report_and_go_on) {
    Options o;
    o.missed_wake_fatal = false;
    Config cfg(o);
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    Sim s = cfg.build();
    s.run(100);
    CHECK(s.unit<Forgetful>("sink").got < 8u);
    CHECK(s.sleepers().find("sink: asleep with data waiting in sink.in") != std::string::npos);
}

TEST(run_until_runs_through_its_cycle) {
    Config cfg;
    cfg.add<Halt>("h", Cycle(25));
    Sim s = cfg.build();
    s.run_until(25);
    CHECK_EQ(s.unit<Halt>("h").halted, Cycle(25));      // the halt at 25 is reached
    CHECK_EQ(s.now(), Cycle(26));
    CHECK_EQ(s.unit<Halt>("h").steps, 25u);             // cycles 1 .. 25
    Config c2;
    c2.add<Halt>("h", Cycle(1000));
    Sim t = c2.build();
    t.run(10);                                          // run(n): the next n cycles
    CHECK_EQ(t.unit<Halt>("h").steps, 10u);
    CHECK_EQ(t.now(), Cycle(11));
    t.run_until(11);
    CHECK_EQ(t.unit<Halt>("h").steps, 11u);
}

DEATH_TEST(an_option_not_implemented_is_refused, "option threads = 4 not implemented") {
    Options o;
    o.threads = 4;
    Config cfg(o);
    cfg.add<Halt>("h", Cycle(3));
    Sim s = cfg.build();
}

// ---- D24: every diagnostic of the engine fires under the allocation hook (its message, not the hook's) ----
namespace {
template <class F>
void under_hook(F f) { begin_run(); f(); end_run(); }
struct Flood3 { LinkOut out; explicit Flood3(Ports& p) { p.out(out, "out", 3); } Wake step(Ctx& c) { c.put(out, 1, 2, 3); return c.next(); } };
struct Deaf { LinkIn in; explicit Deaf(Ports& p) { p.in(in, "in", 3); } Wake step(Ctx&) { return Wake::none(); } };
struct Twice { FifoOut out; explicit Twice(Ports& p) { p.out(out, "out", 1); } Wake step(Ctx& c) { c.put(out, 1); c.put(out, 2); return Wake::none(); } };
struct Hoarder { FifoIn in; explicit Hoarder(Ports& p) { p.in_credit(in, "in", 1, 1); } Wake step(Ctx&) { return Wake::none(); } };
struct Spender { CreditOut out; explicit Spender(Ports& p) { p.out(out, "out", 1); } Wake step(Ctx& c) { c.put(out, 1); return c.next(); } };
struct FifoNeverTake { FifoIn in; explicit FifoNeverTake(Ports& p) { p.in(in, "in", 1, 4); } Wake step(Ctx&) { return Wake::none(); } };
struct Past { explicit Past(Ports&) {} Wake step(Ctx& c) { return Wake::at_cycle(c.now()); } };
struct Wide { FifoOut out; explicit Wide(Ports& p) { p.out(out, "out", 1); p.by_ref(out, 16); } Wake step(Ctx& c) { c.alloc(out); return Wake::none(); } };
struct WideIn { FifoIn in; explicit WideIn(Ports& p) { p.in(in, "in", 1, 2); p.by_ref(in, 16); } Wake step(Ctx&) { return Wake::none(); } };
struct Twice2 { WireOut out; explicit Twice2(Ports& p) { p.out(out, "out"); } Wake step(Ctx& c) { c.drive(out, 1); c.drive(out, 2); return Wake::none(); } };
struct WireSink { WireIn in; explicit WireSink(Ports& p) { p.in(in, "in"); } Wake step(Ctx&) { return Wake::none(); } };
}  // namespace

DEBUG_DEATH_TEST(d24_missed_wake_fatal_under_the_hook, "missed wake at cycle [0-9]+: sink is asleep with data waiting in sink.in") {
    Config cfg;
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    Sim s = cfg.build();
    under_hook([&] { s.run(100); });
}
DEBUG_DEATH_TEST(d24_missed_wake_report_under_the_hook, "missed wake at cycle [0-9]+: sink is asleep with data waiting in sink.in.*after the run") {
    Options o;
    o.missed_wake_fatal = false;
    Config cfg(o);
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    Sim s = cfg.build();
    under_hook([&] { s.run(100); });                     // reported and gone on, without allocating
    FATAL("after the run");
}
DEBUG_DEATH_TEST(d24_leak_under_the_hook, "slot pool 'w16' leaks at cycle") {
    Config cfg;
    auto& w = cfg.add<Wide>("w");
    auto& i = cfg.add<WideIn>("i");
    cfg.connect(w.out, i.in);
    Sim s = cfg.build();
    under_hook([&] { s.run(5); });
}
DEBUG_DEATH_TEST(d24_wire_twice_under_the_hook, "wire of tw written twice in one cycle") {
    Config cfg;
    auto& t = cfg.add<Twice2>("tw");
    auto& r = cfg.add<WireSink>("ws");
    cfg.connect(t.out, r.in);
    Sim s = cfg.build();
    under_hook([&] { s.run(3); });
}
DEATH_TEST(d24_two_puts_under_the_hook, "two puts on 'two.out' in cycle 1") {
    Config cfg;
    auto& t = cfg.add<Twice>("two");
    auto& h = cfg.add<FifoNeverTake>("nt");
    cfg.connect(t.out, h.in);
    cfg.missed_wake_after("nt", 0);
    Sim s = cfg.build();
    under_hook([&] { s.run(3); });
}
DEATH_TEST(d24_link_full_under_the_hook, "link 'deaf.in' full at cycle") {
    Config cfg;
    auto& f = cfg.add<Flood3>("flood");
    auto& d = cfg.add<Deaf>("deaf");
    cfg.link(f.out, d.in, 1);
    Sim s = cfg.build();
    under_hook([&] { s.run(10); });
}
DEATH_TEST(d24_no_credit_under_the_hook, "put on 'sp.out' at cycle 2 without a credit") {
    Config cfg;
    auto& p = cfg.add<Spender>("sp");
    auto& h = cfg.add<Hoarder>("ho");
    cfg.connect(p.out, h.in);
    Sim s = cfg.build();
    under_hook([&] { s.run(5); });
}
DEATH_TEST(d24_wake_in_the_past_under_the_hook, "unit 'past' asked to be woken at cycle 1") {
    Config cfg;
    cfg.add<Past>("past");
    Sim s = cfg.build();
    under_hook([&] { s.run(3); });
}

// the finding: when the machine goes idle the engine jumps ahead; the jump must not skip the judgement of a suspect whose
// threshold is above 1. The burst ends at 17, the forgetful sink's last step leaves data at 18, and then nothing runs: the
// sink, with a threshold of 5, must still be reported at the end of cycle 22 (18 + 5 - 1), not skipped by the jump.
DEBUG_DEATH_TEST(an_idle_jump_does_not_skip_a_suspects_judgement, "missed wake at cycle 22: sink is asleep with data waiting in sink.in") {
    Config cfg;
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    cfg.missed_wake_after("sink", 5);
    Sim s = cfg.build();
    s.run(1000000);
}

TEST(an_idle_jump_still_jumps_when_no_suspect_is_due) {
    // the same machine with the sink's detector off: the run of 10^9 cycles still takes the jump (a few steps in all)
    Config cfg;
    auto& b = cfg.add<Burst>("burst");
    auto& f = cfg.add<Forgetful>("sink");
    cfg.connect(b.out, f.in);
    cfg.missed_wake_after("sink", 0);
    Sim s = cfg.build();
    s.run(1000000000);
    CHECK(s.steps("sink") < 20u);
    CHECK_EQ(s.now(), Cycle(1000000001));
}
