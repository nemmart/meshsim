// One machine over several threads (WORKER4, form 2): crossings arrive exactly when they would in one domain; the same
// machine on 1, 2, 4 and 8 threads over several partitions gives identical digests; the fatal kinds. Run under TSan by
// `make tsan` (src/threads.mk).
#include <string>
#include <vector>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"
#include "synth.h"

using namespace sim;

namespace {


// ---- a Link across domains arrives at t + L, as in one domain ------------------------------------------------------
struct Pulse {
    LinkOut out;
    Cycle at;
    Pulse(Ports& p, Cycle when) : at(when) { p.out(out, "out", 3); }
    uint64_t digest() const { return hash_fields(at); }
    Wake step(Ctx& c) {
        if (c.now() == at) { c.put(out, 7, 8, at); return Wake::none(); }
        return c.now() < at ? Wake::at_cycle(at) : Wake::none();
    }
};
struct Catch {
    LinkIn in;
    Cycle seen = 0, first_step_after = 0;
    Catch(Ports& p) { p.in(in, "in", 3); }
    uint64_t digest() const { return hash_fields(seen, first_step_after); }
    Wake step(Ctx& c) {
        if (c.now() > 1 && !first_step_after) first_step_after = c.now();   // the ready mark: stepped at arrival, not later
        if (c.ready(in)) { seen = c.now(); c.take(in); }
        return Wake::none();
    }
};
struct Arrival { Cycle seen, stepped; uint64_t steps; };
Arrival pulse(unsigned latency, bool split, Cycle at) {
    Options o;
    o.threads = split ? 2 : 1;
    Config cfg(o);
    cfg.domain(0);
    Pulse& p = cfg.add<Pulse>("p", at);
    cfg.domain(split ? 1 : 0);
    Catch& c = cfg.add<Catch>("c");
    cfg.link(p.out, c.in, latency);
    Sim s = cfg.build();
    s.run(at + latency + 20);
    return {s.unit<Catch>("c").seen, s.unit<Catch>("c").first_step_after, s.steps("c")};
}

TEST(a_link_across_domains_arrives_at_t_plus_l) {
    for (unsigned L : {1u, 2u, 3u, 5u, 8u})
        for (Cycle at : {Cycle(2), Cycle(5), Cycle(13)}) {
            const Arrival one = pulse(L, false, at), two = pulse(L, true, at);
            CHECK_EQ(one.seen, at + L);
            CHECK_EQ(two.seen, at + L);
            CHECK_EQ(two.stepped, at + L);                 // its ready mark: stepped the cycle it arrived
            CHECK_EQ(two.steps, one.steps);                // and no other step: the boundary is never a hop
        }
}

TEST(the_partition_reports_its_interval_and_why) {
    Options o;
    o.threads = 2;
    Config cfg(o);
    cfg.domain(0);
    Pulse& p = cfg.add<Pulse>("p", 3);
    Pulse& p2 = cfg.add<Pulse>("p2", 3);
    cfg.domain(1);
    Catch& c = cfg.add<Catch>("c");
    Catch& c2 = cfg.add<Catch>("c2");
    cfg.link(p.out, c.in, 8);
    cfg.link(p2.out, c2.in, 3);
    Sim s = cfg.build();
    const std::string r = s.partition();
    CHECK(r.find("2 domains on 2 threads") != std::string::npos);
    CHECK(r.find("2 link") != std::string::npos);
    CHECK(r.find("barrier every 3 cycles, set by link 'p2.out' -> 'c2.in' (latency 3)") != std::string::npos);
}

TEST(a_partition_with_no_crossing_needs_no_barrier) {
    Options o;
    o.threads = 2;
    Config cfg(o);
    for (unsigned k = 0; k < 2; k++) {
        synth::Params P;
        P.seed = 50 + k;
        P.local_counts = true;
        synth::build(cfg, P, [k](unsigned) { return k; }, "chip" + std::to_string(k) + ".");
    }
    Sim s = cfg.build();
    CHECK(s.partition().find("crossings: none; no barrier") != std::string::npos);
    s.run(500);
    for (unsigned k = 0; k < 2; k++) {               // each chip as it runs alone
        Config one;
        synth::Params P;
        P.seed = 50 + k;
        P.local_counts = true;
        synth::build(one, P, "chip" + std::to_string(k) + ".");
        Sim a = one.build();
        a.run(500);
        CHECK_EQ(a.unit<synth::Node>("chip" + std::to_string(k) + ".n3").h, s.unit<synth::Node>("chip" + std::to_string(k) + ".n3").h);
    }
}

// ---- the synthetic chip, every kind crossing: identical digests over partitions and thread counts --------------------
synth::Params sparams(uint64_t seed) {
    synth::Params P;
    P.seed = seed;
    P.clusters = 8;
    P.nodes_per_cluster = 4;
    P.gap = 2;
    P.min_latency = 1;
    P.max_latency = 9;
    return P;
}
// The digests at checkpoints (each run ends at a barrier, after the merge).
std::vector<uint64_t> synth_digests(uint64_t seed, unsigned threads, unsigned domains, uint64_t pseed, Options o = {}) {
    o.threads = threads;
    Config cfg(o);
    test::Rng r(pseed);
    std::vector<unsigned> dom(8);
    for (auto& d : dom) d = domains <= 1 ? 0 : unsigned(r.below(domains));
    if (domains > 1) dom[0] = 0, dom[1] = domains - 1;   // at least two domains
    synth::build(cfg, sparams(seed), [&](unsigned k) { return dom[k]; });
    Sim s = cfg.build();
    std::vector<uint64_t> d;
    for (Cycle c : {Cycle(1), Cycle(7), Cycle(50), Cycle(300), Cycle(1000)}) { s.run(c); d.push_back(s.digest()); }
    return d;
}

TEST(the_synthetic_chip_split_any_way_matches_one_domain) {
    for (uint64_t seed : {1ull, 2ull, 3ull}) {
        const auto want = synth_digests(seed, 1, 1, 0);
        for (unsigned domains : {2u, 4u, 8u})
            for (uint64_t pseed : {11ull, 12ull})
                for (unsigned threads : {1u, 2u, 4u, 8u}) {
                    if (threads > domains) continue;
                    const auto got = synth_digests(seed, threads, domains, pseed);
                    for (size_t k = 0; k < want.size(); k++) CHECK_EQ(got[k], want[k]);
                }
    }
}

TEST(one_domain_per_cluster_on_three_threads) {
    const auto want = synth_digests(4, 1, 1, 0);
    Options o;
    o.threads = 3;
    Config cfg(o);
    synth::Params P = sparams(4);
    synth::build(cfg, P, [](unsigned k) { return k; });
    Sim s = cfg.build();
    std::vector<uint64_t> d;
    for (Cycle c : {Cycle(1), Cycle(7), Cycle(50), Cycle(300), Cycle(1000)}) { s.run(c); d.push_back(s.digest()); }
    for (size_t k = 0; k < want.size(); k++) CHECK_EQ(d[k], want[k]);
}

// threads == domains (the case that works): the synthetic chip split 2, 4 and 8 ways, two partitions each
TEST(the_synthetic_chip_split_one_thread_per_domain_matches_one_domain) {
    for (uint64_t seed : {1ull, 2ull}) {
        const auto want = synth_digests(seed, 1, 1, 0);
        for (unsigned domains : {2u, 4u, 8u})
            for (uint64_t pseed : {11ull, 12ull}) {
                const auto got = synth_digests(seed, domains, domains, pseed);
                for (size_t k = 0; k < want.size(); k++) CHECK_EQ(got[k], want[k]);
            }
    }
}

TEST(debug_options_per_domain_change_nothing) {
    const auto want = synth_digests(5, 1, 1, 0);
    Options o;
    o.shuffle_seed = 99;
    const auto a = synth_digests(5, 4, 4, 21, o);
    o.shuffle_seed = 0;
    o.order = Options::Order::instance_major;
    const auto b = synth_digests(5, 2, 4, 21, o);
    for (size_t k = 0; k < want.size(); k++) { CHECK_EQ(a[k], want[k]); CHECK_EQ(b[k], want[k]); }
}

TEST(nothing_allocated_in_the_loop_with_threads) {
    Options o;
    o.threads = 4;
    Config cfg(o);
    synth::build(cfg, sparams(6), [](unsigned k) { return k % 4; });
    Sim s = cfg.build();
    s.run(10);
    begin_run();
    s.run(2000);                                       // every worker runs its loop under the hook too
    end_run();
    CHECK(s.now() == 2011);
}

TEST(barrier_block_gives_the_same) {
    const auto want = synth_digests(7, 1, 1, 0);
    Options o;
    o.barrier_block = true;
    const auto got = synth_digests(7, 3, 3, 5, o);
    for (size_t k = 0; k < want.size(); k++) CHECK_EQ(got[k], want[k]);
}

// ---- a quiet machine jumps whole intervals ---------------------------------------------------------------------------
TEST(a_long_sleep_across_domains_costs_nothing) {
    Options o;
    o.threads = 2;
    Config cfg(o);
    cfg.domain(0);
    Pulse& p = cfg.add<Pulse>("p", 1000000000);
    cfg.domain(1);
    Catch& c = cfg.add<Catch>("c");
    cfg.link(p.out, c.in, 3);
    Sim s = cfg.build();
    s.run(1000000100);
    CHECK_EQ(s.unit<Catch>("c").seen, Cycle(1000000003));
    CHECK_EQ(s.steps("c"), uint64_t(2));
    CHECK_EQ(s.steps("p"), uint64_t(2));
}

// ---- wide payloads across: copied into the destination's pool, freed at both ends ------------------------------------
constexpr unsigned kLine = 8;
struct LineSrc {
    FifoOut out;
    uint64_t sent = 0;
    LineSrc(Ports& p) { p.out(out, "out", 1); p.by_ref(out, kLine); }
    uint64_t digest() const { return hash_fields(sent); }
    Wake step(Ctx& c) {
        uint32_t i = c.alloc(out);
        for (unsigned k = 0; k < kLine; k++) c.slot(out, i)[k] = c.now() * 10 + k;
        c.put(out, i);
        sent++;
        return c.next();
    }
};
struct LineDst {
    FifoIn in;
    uint64_t got = 0, sum = 0, bad = 0;
    LineDst(Ports& p) { p.in(in, "in", 1, 3); p.by_ref(in, kLine); }
    uint64_t digest() const { return hash_fields(got, sum, bad); }
    Wake step(Ctx& c) {
        if (c.ready(in) && c.now() % 3 != 0) {
            const uint32_t i = uint32_t(c.peek(in)[0]);
            const Word* s = c.slot(in, i);
            for (unsigned k = 1; k < kLine; k++) if (s[k] != s[0] + k) bad++;
            sum += s[0];
            c.free(in, i);
            c.take(in);
            got++;
        }
        return c.ready(in) ? c.next() : Wake::none();
    }
};
uint64_t lines(bool split, unsigned threads) {
    Options o;
    o.threads = split ? threads : 1;
    Config cfg(o);
    cfg.domain(0);
    LineSrc& a = cfg.add<LineSrc>("a");
    cfg.domain(split ? 1 : 0);
    LineDst& b = cfg.add<LineDst>("b");
    cfg.connect(a.out, b.in);
    Sim s = cfg.build();
    s.run(301);
    if (s.unit<LineDst>("b").bad || s.unit<LineDst>("b").got < 150) return 0;   // a damaged line, or too few: the test fails
    return s.digest();
}
TEST(wide_payloads_cross_by_copy) {
    const uint64_t one = lines(false, 1);
    CHECK(one != 0);
    CHECK_EQ(lines(true, 1), one);
    CHECK_EQ(lines(true, 2), one);
}

// ---- E6: the missed-wake detector across domains ----------------------------------------------------------------------
// A producer putting every other cycle; a consumer leaving a single word waiting and sleeping until the next arrives (it
// takes when two are queued, or the last). Legitimate, with a threshold of 2: the next word is in the wheel by the end of
// the following cycle. Across domains that word sits in an outbox until the drain: the detector must not flag it.
struct Every2 {
    FifoOut out;
    uint64_t n = 0;
    Every2(Ports& p) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(n); }
    Wake step(Ctx& c) {
        if (n < 20 && c.now() % 2 == 1) c.put(out, ++n);
        return c.next();                    // ticks on, so the machine never idles (an idle jump skips the detector)
    }
};
struct Lazy {
    FifoIn in;
    uint64_t got = 0, last;                 // last: 20, or 0 to forget the last word (a real missed wake)
    Lazy(Ports& p, uint64_t l) : last(l) { p.in(in, "in", 1, 4); }
    uint64_t digest() const { return hash_fields(got, last); }
    bool wants(Ctx& c) const { return c.size(in) >= 2 || (c.ready(in) && c.peek(in)[0] == last); }
    Wake step(Ctx& c) {
        if (wants(c)) { c.take(in); got++; }
        return wants(c) ? c.next() : Wake::none();
    }
};
uint64_t lazy(bool split, uint64_t last = 20) {
    Options o;
    o.threads = split ? 2 : 1;
    Config cfg(o);
    cfg.domain(0);
    Every2& a = cfg.add<Every2>("a");
    cfg.domain(split ? 1 : 0);
    Lazy& b = cfg.add<Lazy>("b", last);
    cfg.connect(a.out, b.in);
    cfg.missed_wake_after("b", 2);
    Sim s = cfg.build();
    s.run(60);
    return s.unit<Lazy>("b").got;
}
TEST(the_missed_wake_detector_sees_words_on_their_way_across_domains) {
    CHECK_EQ(lazy(false), uint64_t(20));
    CHECK_EQ(lazy(true), uint64_t(20));
}
DEBUG_DEATH_TEST(a_real_missed_wake_across_domains_is_still_caught, "missed wake at cycle [0-9]+: b is asleep with data waiting in b.in") {
    lazy(true, 0);
}

// ---- the kinds that can't cross ---------------------------------------------------------------------------------------
struct WireSrc {
    WireOut w;
    WireSrc(Ports& p) { p.out(w, "w"); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct WireDst {
    WireIn w;
    WireDst(Ports& p) { p.in(w, "w"); }
    Wake step(Ctx&) { return Wake::none(); }
};
DEATH_TEST(a_wire_across_domains_is_fatal, "a wire can't cross thread domains: 'a.w' \\(unit 'a', domain 0\\) drives 'b.w' \\(unit 'b', domain 1\\)") {
    Config cfg;
    cfg.domain(0);
    WireSrc& a = cfg.add<WireSrc>("a");
    cfg.domain(1);
    WireDst& b = cfg.add<WireDst>("b");
    cfg.connect(a.w, b.w);
    cfg.build();
}
struct RfSrc {
    RegFileOut r;
    RfSrc(Ports& p) { p.out(r, "r", 4); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct RfDst {
    RegFileIn r;
    RfDst(Ports& p) { p.in(r, "r"); }
    Wake step(Ctx&) { return Wake::none(); }
};
DEATH_TEST(a_regfile_across_domains_is_fatal, "a regfile can't cross thread domains: 'a.r'") {
    Config cfg;
    cfg.domain(3);
    RfSrc& a = cfg.add<RfSrc>("a");
    cfg.domain(4);
    RfDst& b = cfg.add<RfDst>("b");
    cfg.connect(a.r, b.r);
    cfg.build();
}
struct Inc {
    CountOut k;
    Inc(Ports& p) { p.out(k, "k"); }
    Wake step(Ctx& c) { c.inc(k); return c.next(); }
};
struct ReadNow {
    CountIn k;
    uint64_t v = 0;
    ReadNow(Ports& p) { p.in(k, "k"); }
    uint64_t digest() const { return hash_fields(v); }
    Wake step(Ctx& c) { v = c.now(k); return c.next(); }
};
DEBUG_DEATH_TEST(a_count_read_now_across_domains_is_fatal_in_debug, "read through_last") {
    Config cfg;
    cfg.domain(0);
    Inc& a = cfg.add<Inc>("a");
    cfg.domain(1);
    ReadNow& b = cfg.add<ReadNow>("b");
    cfg.connect(a.k, b.k);
    Sim s = cfg.build();
    s.run(5);
}

// ---- the barrier's watchdog (debug): a thread stuck in a step is named, with every thread's state ------------------
struct Stuck {
    LinkOut out;
    Stuck(Ports& p) { p.out(out, "out", 3); }
    Wake step(Ctx& c) {
        if (c.now() == 3) { volatile uint64_t x = 0; while (true) x = x + 1; }   // never returns
        return c.next();
    }
};
DEBUG_DEATH_TEST(a_thread_stuck_past_the_watchdog_is_named,
                 "thread 0: interval from cycle 1, domain 0, running\n"
                 "thread 1: interval from cycle 1, domain [0-9]+, at the barrier after its interval\n"
                 "FATAL .*barrier: waited more than 10 s") {
    Options o;
    o.threads = 2;
    Config cfg(o);
    cfg.domain(0);
    Stuck& a = cfg.add<Stuck>("a");
    cfg.domain(1);
    Catch& b = cfg.add<Catch>("b");
    cfg.link(a.out, b.in, 3);
    Sim s = cfg.build();
    s.run(10);
}

}  // namespace
