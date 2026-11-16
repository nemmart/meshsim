// The port observer (the request) and the stop request (the). The observer is checked against the machine
// itself: what it reports at the end of cycle c must be exactly what the consumer sees at c + 1 (its visible words then
// are the ones visible at c, plus those arriving at c + 1, plus the put judged at c + 1 if accepted), in one domain and
// split over two threads, with run lengths that end between barriers; and against the producer's own log for a Link's
// arrival cycles. Then: refused during a run, no allocation, and the stop request's cycle in both forms.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {

uint64_t lcg(uint64_t& s) { s = s * 6364136223846793005ull + 1442695040888963407ull; return s >> 33; }

enum K : unsigned { LINK, FIFO, HOLD, REG };
constexpr unsigned kLat = 3, kDepth = 6, kMaxLog = 4096;

// Puts (i, i ^ 0x55, i + 7) at random cycles: a Link bounded by credits returned on an event line, a Fifo (overflows
// allowed), a Hold (retrying a refused word), a Reg. Logs every put's cycle (a Link's arrival is put + L).
struct Src {
    LinkOut lo; LinkIn cred; FifoOut fo; HoldOut ho; RegOut ro;
    unsigned kind; uint64_t rng; uint32_t i = 0, credits = kDepth;
    Cycle put_at[kMaxLog] = {};
    Word last[3] = {};
    bool retry = false;
    Src(Ports& p, unsigned k, uint64_t seed) : kind(k), rng(seed) {
        if (k == LINK) { p.out(lo, "out", 3); p.in(cred, "cred", 0); }
        if (k == FIFO) p.out(fo, "out", 3);
        if (k == HOLD) p.out(ho, "out", 3);
        if (k == REG) p.out(ro, "out", 3);
    }
    uint64_t digest() const { return hash_fields(kind, rng, i, credits, put_at, last, retry); }
    Wake step(Ctx& c) {
        if (kind == LINK && c.ready(cred)) { c.take(cred); credits++; }
        if (kind == HOLD && c.refused(ho)) { c.put(ho, last[0], last[1], last[2]); return c.next(); }
        if (lcg(rng) % 3 == 0 && i < kMaxLog) {
            const Word a = i, b = i ^ 0x55, d = i + 7;
            if (kind == LINK && credits) { c.put(lo, a, b, d); credits--; put_at[i++] = c.now(); }
            else if (kind == FIFO) { c.put(fo, a, b, d); put_at[i++] = c.now(); }
            else if (kind == HOLD && c.can_put(ho)) { c.put(ho, a, b, d); last[0] = a; last[1] = b; last[2] = d; put_at[i++] = c.now(); }
            else if (kind == REG) { c.put(ro, a, b, d); put_at[i++] = c.now(); }
        }
        return c.next();
    }
};

// Stepped every cycle: first records what it sees (its visible words, in order), then takes at random.
struct Dst {
    LinkIn li; LinkOut cred; FifoIn fi; HoldIn hi; RegIn ri;
    unsigned kind; uint64_t rng;
    uint32_t taken = 0, nseen = 0;
    Cycle seen_at = 0;
    Word seen[16][3] = {};
    Dst(Ports& p, unsigned k, uint64_t seed) : kind(k), rng(seed) {
        if (k == LINK) { p.in(li, "in", 3, kDepth); p.out(cred, "cred", 0); }
        if (k == FIFO) p.in(fi, "in", 3, 4);
        if (k == HOLD) p.in(hi, "in", 3);
        if (k == REG) p.in(ri, "in", 3);
    }
    uint64_t digest() const { return hash_fields(kind, rng, taken, nseen, seen_at, seen); }
    Wake step(Ctx& c) {
        seen_at = c.now();
        nseen = 0;
        auto rec = [&](const Word* w) { for (unsigned k = 0; k < 3; k++) seen[nseen][k] = w[k]; nseen++; };
        if (kind == LINK) for (unsigned k = 0; k < c.size(li); k++) rec(c.peek(li, k));
        if (kind == FIFO) for (unsigned k = 0; k < c.size(fi); k++) rec(c.peek(fi, k));
        if (kind == HOLD && c.ready(hi)) rec(c.peek(hi));
        if (kind == REG && c.valid(ri)) rec(c.read(ri));
        if (lcg(rng) % 2 == 0) {
            if (kind == LINK && c.ready(li)) { c.take(li); c.put(cred); taken++; }
            if (kind == FIFO && c.ready(fi)) { c.take(fi); taken++; }
            if (kind == HOLD && c.ready(hi)) { c.take(hi); taken++; }
        }
        return c.next();
    }
};

struct Pair { Sim sim; };

Sim build_pair(unsigned kind, bool split, bool arrivals = true) {
    Options o;
    o.threads = split ? 2 : 1;
    o.observe_arrivals = arrivals;
    o.missed_wake_after = 0;
    Config cfg(o);
    cfg.domain(0);
    auto& s = cfg.add<Src>("src", kind, uint64_t(7 + kind));
    cfg.domain(split ? 1 : 0);
    auto& d = cfg.add<Dst>("dst", kind, uint64_t(99 + kind));
    if (kind == LINK) { cfg.link(s.lo, d.li, kLat); cfg.link(d.cred, s.cred, kLat); }
    if (kind == FIFO) cfg.connect(s.fo, d.fi);
    if (kind == HOLD) cfg.connect(s.ho, d.hi);
    if (kind == REG) cfg.connect(s.ro, d.ri);
    return cfg.build();
}

PortView view_of(const Sim& sim, const Dst& d, unsigned kind, PortEntry* out, uint32_t max) {
    if (kind == LINK) return sim.observe(d.li, out, max);
    if (kind == FIFO) return sim.observe(d.fi, out, max);
    if (kind == HOLD) return sim.observe(d.hi, out, max);
    return sim.observe(d.ri, out, max);
}

// Observes after runs of varied lengths and checks each view against the next cycle's sight; returns the checks made.
void check_kind(unsigned kind, bool split, unsigned& checks) {
    Sim sim = build_pair(kind, split);
    sim.run(5);
    PortEntry buf[32];
    checks = 0;
    const unsigned steps[] = {1, 2, 4, 1, 7, 3, 1, 1, 5};
    for (unsigned r = 0; r < 120; r++) {
        sim.run(steps[r % 9]);
        const Cycle c = sim.now() - 1;
        const Dst& d = sim.unit<Dst>("dst");
        const Src& s = sim.unit<Src>("src");
        begin_run();
        const PortView v = view_of(sim, d, kind, buf, 32);
        end_run();
        // Link: every entry is a put not yet taken, in order, arriving at put + L
        if (kind == LINK)
            for (uint32_t k = 0; k < v.n; k++) {
                const uint32_t i = d.taken + k;
                CHECK_EQ(buf[k].w[0], Word(i));
                CHECK_EQ(buf[k].at, s.put_at[i] + kLat);
                CHECK(k + 1 == v.n || buf[k].at < buf[k + 1].at);
            }
        // what the consumer will see at c + 1
        std::vector<std::vector<Word>> want;
        for (uint32_t k = 0; k < v.n; k++)
            if (kind != LINK || buf[k].at <= c + 1) want.push_back({buf[k].w[0], buf[k].w[1], buf[k].w[2]});
        if (kind == REG && v.put) { want.clear(); want.push_back({v.put_w[0], v.put_w[1], v.put_w[2]}); }
        if (kind == HOLD && v.put && !v.valid) want.push_back({v.put_w[0], v.put_w[1], v.put_w[2]});
        const size_t base = want.size();
        if (kind == FIFO && v.put) want.push_back({v.put_w[0], v.put_w[1], v.put_w[2]});
        CHECK(!(kind == LINK && v.put));
        sim.run(1);
        const Dst& n = sim.unit<Dst>("dst");
        CHECK_EQ(n.seen_at, c + 1);
        if (kind == FIFO && v.put && n.nseen == base) want.pop_back();   // the put judged at c + 1 overflowed: dropped
        CHECK_EQ(size_t(n.nseen), want.size());
        for (uint32_t k = 0; k < n.nseen && k < want.size(); k++)
            for (unsigned j = 0; j < 3; j++) CHECK_EQ(n.seen[k][j], want[k][j]);
        checks++;
    }
}

}  // namespace

TEST(observe_a_link_one_domain) { unsigned n = 0; check_kind(LINK, false, n); CHECK_EQ(n, 120u); }
TEST(observe_a_link_split) { unsigned n = 0; check_kind(LINK, true, n); CHECK_EQ(n, 120u); }
TEST(observe_a_fifo_one_domain) { unsigned n = 0; check_kind(FIFO, false, n); CHECK_EQ(n, 120u); }
TEST(observe_a_fifo_split) { unsigned n = 0; check_kind(FIFO, true, n); CHECK_EQ(n, 120u); }
TEST(observe_a_hold_one_domain) { unsigned n = 0; check_kind(HOLD, false, n); CHECK_EQ(n, 120u); }
TEST(observe_a_hold_split) { unsigned n = 0; check_kind(HOLD, true, n); CHECK_EQ(n, 120u); }
TEST(observe_a_reg_one_domain) { unsigned n = 0; check_kind(REG, false, n); CHECK_EQ(n, 120u); }
TEST(observe_a_reg_split) { unsigned n = 0; check_kind(REG, true, n); CHECK_EQ(n, 120u); }

TEST(observe_the_same_view_in_one_domain_and_split) {
    for (unsigned kind : {LINK, FIFO, HOLD, REG}) {
        Sim a = build_pair(kind, false), b = build_pair(kind, true);
        PortEntry x[32], y[32];
        for (Cycle n : {7u, 1u, 2u, 11u}) {
            a.run(n); b.run(n);
            const PortView va = view_of(a, a.unit<Dst>("dst"), kind, x, 32), vb = view_of(b, b.unit<Dst>("dst"), kind, y, 32);
            CHECK_EQ(va.n, vb.n); CHECK_EQ(va.put, vb.put); CHECK_EQ(va.valid, vb.valid);
            CHECK(std::memcmp(x, y, sizeof(PortEntry) * va.n) == 0);
            CHECK(std::memcmp(va.put_w, vb.put_w, sizeof va.put_w) == 0);
        }
    }
}

DEATH_TEST(observe_a_link_without_arrivals_is_fatal_while_a_word_is_visible, "no arrival cycles kept") {
    Sim sim = build_pair(LINK, false, false);
    PortEntry buf[32];
    for (int r = 0; r < 200; r++) { sim.run(1); sim.observe(sim.unit<Dst>("dst").li, buf, 32); }
}

namespace {
struct Peeker {   // observes from inside a step: refused
    LinkIn in; const Sim* sim = nullptr;
    explicit Peeker(Ports& p) { p.in(in, "in", 0); }
    uint64_t digest() const { return 0; }
    Wake step(Ctx& c) {
        PortEntry b[4];
        if (sim) sim->observe(in, b, 4);
        if (c.ready(in)) c.take(in);
        return c.now() < 3 ? c.next() : Wake::none();
    }
};
struct Ticker {
    LinkOut out; Cycle stop_at = 0; Cycle stop2 = 0;
    explicit Ticker(Ports& p, Cycle at = 0, Cycle at2 = 0) : stop_at(at), stop2(at2) { p.out(out, "out", 0); }
    uint64_t digest() const { return hash_fields(stop_at, stop2); }
    Wake step(Ctx& c) {
        if (c.now() == stop_at || c.now() == stop2) c.stop();
        if (c.now() % 5 == 0) c.put(out);
        return c.next();
    }
};
}  // namespace

DEATH_TEST(observe_during_a_run_is_fatal, "during a run") {
    Config cfg;
    auto& t = cfg.add<Ticker>("t");
    auto& p = cfg.add<Peeker>("p");
    cfg.link(t.out, p.in, 1);
    Sim sim = cfg.build();
    sim.unit<Peeker>("p").sim = &sim;
    sim.run(10);
}

TEST(stop_ends_a_run_after_its_cycle_in_one_domain) {
    Config cfg;
    auto& t = cfg.add<Ticker>("t", Cycle(37), Cycle(40));
    auto& p = cfg.add<Peeker>("p");
    cfg.link(t.out, p.in, 1);
    Sim sim = cfg.build();
    begin_run();
    sim.run(1000);
    end_run();
    CHECK_EQ(sim.stop_cycle(), Cycle(37));
    CHECK_EQ(sim.now(), Cycle(38));
    sim.run(1000);                      // a new run: the request is cleared; the second stop is at 40
    CHECK_EQ(sim.stop_cycle(), Cycle(40));
    CHECK_EQ(sim.now(), Cycle(41));
    sim.run(100);
    CHECK_EQ(sim.stop_cycle(), kNoCycle);
    CHECK_EQ(sim.now(), Cycle(141));
}

TEST(stop_ends_a_split_run_at_the_barrier_on_every_thread) {
    for (unsigned where = 0; where < 2; where++) {
        Options o; o.threads = 2;
        Config cfg(o);
        cfg.domain(0);
        auto& t = cfg.add<Ticker>("t", where ? Cycle(0) : Cycle(37));
        cfg.domain(1);
        auto& u = cfg.add<Ticker>("u", where ? Cycle(37) : Cycle(0));
        auto& p = cfg.add<Peeker>("p");
        auto& q = cfg.add<Peeker>("q");
        cfg.link(t.out, p.in, 3);       // crossing: a barrier every 3 cycles
        cfg.link(u.out, q.in, 3);
        Sim sim = cfg.build();
        sim.run(1000);
        CHECK_EQ(sim.stop_cycle(), Cycle(37));
        CHECK(sim.now() - 1 >= 37 && sim.now() - 1 < 37 + 3);
        CHECK_EQ(sim.steps("p") + sim.steps("q") > 0, true);
        const Cycle at = sim.now();
        sim.run(10);
        CHECK_EQ(sim.stop_cycle(), kNoCycle);
        CHECK_EQ(sim.now(), at + 10);
    }
}

// ---- the between-cycles wake (the driver): Sim::wake(name) steps the unit at now(), exactly, in its own domain
namespace {
struct Sleeper {   // never asks to be woken; records the cycles it was stepped at
    LinkIn in; Cycle seen[8] = {}; uint32_t n = 0;
    explicit Sleeper(Ports& p) { p.in(in, "in", 0); }
    uint64_t digest() const { return hash_fields(seen, n); }
    Wake step(Ctx& c) {
        if (n < 8) seen[n++] = c.now();
        if (c.ready(in)) c.take(in);
        return Wake::none();
    }
};
struct Quiet {     // puts one event at cycle 1 and then sleeps: the machine goes idle (the run jumps to its end)
    LinkOut out;
    explicit Quiet(Ports& p) { p.out(out, "out", 0); }
    uint64_t digest() const { return 0; }
    Wake step(Ctx& c) { if (c.now() == 1) c.put(out); return Wake::none(); }
};
void wake_case(bool split) {
    Options o; o.threads = split ? 2 : 1;
    Config cfg(o);
    cfg.domain(0);
    auto& q = cfg.add<Quiet>("q");
    auto& q2 = cfg.add<Quiet>("q2");
    cfg.domain(split ? 1 : 0);
    auto& z = cfg.add<Sleeper>("z");
    auto& z2 = cfg.add<Sleeper>("z2");
    cfg.link(q.out, z.in, 3);
    cfg.link(q2.out, z2.in, 3);
    Sim s = cfg.build();
    s.run(100);                                   // z stepped at 1 (its first cycle) and 4 (the event); then idle
    const Sleeper& a = s.unit<Sleeper>("z");
    CHECK_EQ(a.n, 2u);
    CHECK_EQ(a.seen[1], Cycle(4));
    CHECK_EQ(s.now(), Cycle(101));
    s.wake("z");
    s.run(50);
    CHECK_EQ(a.n, 3u);
    CHECK_EQ(a.seen[2], Cycle(101));              // exactly the next cycle
    CHECK_EQ(s.unit<Sleeper>("z2").n, 2u);        // nothing else stepped
    s.wake("z"); s.wake("z");                     // twice before one run: one step
    s.run(1);
    CHECK_EQ(a.n, 4u);
    CHECK_EQ(a.seen[3], Cycle(151));
    s.run(7);
    s.wake("z2");
    s.run(1);
    CHECK_EQ(s.unit<Sleeper>("z2").n, 3u);
    CHECK_EQ(s.unit<Sleeper>("z2").seen[2], Cycle(159));
    CHECK_EQ(a.n, 4u);
}
}  // namespace

TEST(wake_steps_the_unit_at_the_next_cycle_in_one_domain) { wake_case(false); }
TEST(wake_steps_the_unit_at_the_next_cycle_split) { wake_case(true); }

TEST(wake_gives_the_same_digest_in_one_domain_and_split) {
    uint64_t d[2];
    for (int split = 0; split < 2; split++) {
        Options o; o.threads = split ? 2 : 1;
        Config cfg(o);
        cfg.domain(0);
        auto& q = cfg.add<Quiet>("q");
        cfg.domain(split ? 1 : 0);
        auto& z = cfg.add<Sleeper>("z");
        cfg.link(q.out, z.in, 3);
        Sim s = cfg.build();
        for (Cycle n : {5u, 9u, 1u, 30u}) { s.run(n); s.wake("z"); }
        s.run(10);
        d[split] = s.digest();
    }
    CHECK_EQ(d[0], d[1]);
}

DEATH_TEST(wake_of_an_unknown_unit_is_fatal, "no unit named 'nope'") {
    Config cfg;
    auto& q = cfg.add<Quiet>("q");
    auto& z = cfg.add<Sleeper>("z");
    cfg.link(q.out, z.in, 1);
    Sim s = cfg.build();
    s.wake("nope");
}

// ---- Options::time_threads (a measurement option): the phase shares of a split run add up, and change nothing
TEST(time_threads_reports_phase_shares_and_changes_nothing) {
    uint64_t d[2];
    for (int timed = 0; timed < 2; timed++) {
        Options o; o.threads = 2; o.time_threads = timed != 0;
        Config cfg(o);
        cfg.domain(0);
        auto& t = cfg.add<Ticker>("t");
        auto& p = cfg.add<Peeker>("p");
        cfg.domain(1);
        auto& u = cfg.add<Ticker>("u");
        auto& q = cfg.add<Peeker>("q");
        cfg.link(t.out, q.in, 3);
        cfg.link(u.out, p.in, 3);
        Sim s = cfg.build();
        s.run(1);
        begin_run();
        s.run(3000);
        end_run();
        d[timed] = s.digest();
        const std::string r = s.thread_times();
        if (!timed) { CHECK(r == std::string("time_threads off")); continue; }
        double sum = 0;
        for (const char* f : {"compute=", "imbalance=", "barrier_wait=", "drain=", "publish=", "clear=", "end=", "other="}) {
            const size_t k = r.find(f);
            CHECK(k != std::string::npos);
            if (k != std::string::npos) sum += std::atof(r.c_str() + k + std::strlen(f));
        }
        CHECK(sum > 0.99 && sum < 1.01);
        CHECK(r.find("intervals=") == 0);
    }
    CHECK_EQ(d[0], d[1]);
}

// Crossing Links of different latencies into one domain: their outboxes have windows of different sizes (direct delivery,
// W >= latency + interval); the observer reads each over its own window, so no word is found twice (the eject link).
namespace {
struct Pacer {   // puts its count every other cycle
    LinkOut out; uint32_t i = 0;
    explicit Pacer(Ports& p) { p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(i); }
    Wake step(Ctx& c) { if (c.now() % 2 == 0) c.put(out, i++); return c.next(); }
};
struct Sink2 {   // takes whatever arrives
    LinkIn a, b; uint64_t h = 0;
    explicit Sink2(Ports& p) { p.in(a, "a", 1); p.in(b, "b", 1); }
    uint64_t digest() const { return hash_fields(h); }
    Wake step(Ctx& c) {
        if (c.ready(a)) { h = hash_mix(h, c.peek(a)[0]); c.take(a); }
        if (c.ready(b)) { h = hash_mix(h, c.peek(b)[0] + 1); c.take(b); }
        return Wake::none();
    }
};
}  // namespace

TEST(observe_links_of_different_latencies_split_as_in_one_domain) {
    PortEntry x[64], y[64];
    auto make = [](bool split) {
        Options o; o.threads = split ? 2 : 1; o.observe_arrivals = true;
        Config cfg(o);
        cfg.domain(0);
        auto& p = cfg.add<Pacer>("p");
        auto& q = cfg.add<Pacer>("q");
        cfg.domain(split ? 1 : 0);
        auto& k = cfg.add<Sink2>("k");
        cfg.link(p.out, k.a, 3);
        cfg.link(q.out, k.b, 13);                   // window 16 against the other's 8
        return cfg.build();
    };
    Sim s[2] = {make(false), make(true)};
    for (Cycle n : {1u, 2u, 3u, 5u, 7u, 11u, 1u, 1u, 20u}) {
        for (auto& m : s) m.run(n);
        for (int port = 0; port < 2; port++) {
            const Sink2& a = s[0].unit<Sink2>("k");
            const Sink2& b = s[1].unit<Sink2>("k");
            const PortView va = s[0].observe(port ? a.b : a.a, x, 64), vb = s[1].observe(port ? b.b : b.a, y, 64);
            CHECK_EQ(va.n, vb.n);
            CHECK(std::memcmp(x, y, sizeof(PortEntry) * va.n) == 0);
            if (s[0].now() > 15)                        // steady: a put every other cycle, latency 13 or 3
                CHECK(port ? (va.n == 6 || va.n == 7) : (va.n == 1 || va.n == 2));
        }
    }
}

// ---- Ports::overflow_steps (the and the request, D29): a Fifo overflow steps its owner in the cycle it is judged
namespace {
struct Flood {   // puts every cycle from 1 to 10
    FifoOut out;
    explicit Flood(Ports& p) { p.out(out, "out", 1); }
    uint64_t digest() const { return 0; }
    Wake step(Ctx& c) { c.put(out, c.now()); return c.now() < 10 ? c.next() : Wake::none(); }
};
struct Deaf {    // never takes, never asks to be woken: steps only on its first cycle and on overflows
    FifoIn in; bool steps_on_ovf;
    uint64_t seen[16] = {}; Cycle at[16] = {}; uint32_t n = 0;
    Deaf(Ports& p, bool s) : steps_on_ovf(s) { p.in(in, "in", 1, 3); if (s) p.overflow_steps(in); }
    uint64_t digest() const { return hash_fields(seen, at, n); }
    Wake step(Ctx& c) { if (n < 16) { seen[n] = c.overflows(in); at[n] = c.now(); n++; } return Wake::none(); }
};
void ovf_case(bool split, bool steps) {
    Options o; o.threads = split ? 2 : 1; o.missed_wake_after = 0;
    Config cfg(o);
    cfg.domain(0);
    auto& f = cfg.add<Flood>("f");
    cfg.domain(split ? 1 : 0);
    auto& d = cfg.add<Deaf>("d", steps);
    cfg.connect(f.out, d.in);
    Sim s = cfg.build();
    s.run(30);
    const Deaf& r = s.unit<Deaf>("d");
    // puts at 1..10, judged at 2..11; depth 3, never taken: arrivals at 2, 3, 4 step it (the level's edge), the puts at
    // 4..10 overflow, judged at 5..11
    const uint32_t base = 1 + 3;                              // its first cycle and the three arrivals
    if (!steps) { CHECK_EQ(r.n, base); CHECK_EQ(r.seen[base - 1], 0u); return; }
    CHECK_EQ(r.n, base + 7u);
    for (uint32_t k = 0; k < 7 && base + k < r.n; k++) {
        CHECK_EQ(r.at[base + k], Cycle(5 + k));
        CHECK_EQ(r.seen[base + k], uint64_t(k + 1));
    }
}
}  // namespace

TEST(overflow_steps_the_owner_when_judged_in_one_domain) { ovf_case(false, true); }
TEST(overflow_steps_the_owner_when_judged_split) { ovf_case(true, true); }
TEST(overflow_without_the_declaration_stays_hidden) { ovf_case(false, false); }

// ---- Internals::regions (footprints): the machine's memory by structure, each with its owning domain; no two
// regions overlap, and together they cover the engine blocks and arenas Sizes counts (padding aside)
#include "internals.h"
TEST(regions_cover_the_machine_without_overlap) {
    for (int split = 0; split < 2; split++) {
        Options o; o.threads = split ? 2 : 1;
        Config cfg(o);
        cfg.domain(0);
        auto& q = cfg.add<Quiet>("q");
        cfg.domain(split ? 1 : 0);
        auto& z = cfg.add<Sleeper>("z");
        cfg.link(q.out, z.in, 3);
        Sim s = cfg.build();
        auto r = detail::Internals::regions(s);
        CHECK(!r.empty());
        std::sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a.base < b.base; });
        uint64_t total = 0;
        for (size_t k = 0; k < r.size(); k++) {
            CHECK(r[k].bytes > 0);
            CHECK(r[k].domain >= 0 && r[k].domain < (split ? 2 : 1));
            if (k + 1 < r.size()) CHECK(static_cast<const char*>(r[k].base) + r[k].bytes <= static_cast<const char*>(r[k + 1].base));
            if (r[k].what.rfind("outbox", 0) != 0) total += r[k].bytes;
        }
        CHECK(total <= s.sizes().total_words * 8 * (split ? 2 : 1));
        CHECK(total * 2 >= s.sizes().total_words * 8);   // most of it: Sizes adds padding to whole lines
    }
}
