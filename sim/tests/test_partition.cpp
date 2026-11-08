// Placement by load (WORKER10; Options::Placement::by_load, partition.cpp) and calibration (Options::calibrate,
// Sim::load_profile, LoadProfile's text). A placement can't change results: every machine's digest equals the one-thread
// run's; what it may not split (Wires, a declared domain) stays together; the balance is reported and tight where the
// granules allow it.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"
#include "synth.h"

using namespace sim;

namespace {

// A ring of N stations joined by Links of latency 3 (a word and a credit line, as the mesh); station k does k % 7 + 1
// rounds of hashing per word, so the load is uneven. Optionally each station is a pair joined by a Wire (a granule of two).
struct Station {
    LinkIn in; LinkOut out; LinkIn cred; LinkOut cred_out;
    uint64_t h, n = 0;
    uint32_t work, credits = 4;
    Station(Ports& p, uint32_t k) : h(k), work(k % 7 + 1) {
        p.in(in, "in", 1, 4); p.out(out, "out", 1); p.in(cred, "cred", 0); p.out(cred_out, "cred_out", 0);
    }
    uint64_t digest() const { return hash_fields(h, n, work, credits); }
    Wake step(Ctx& c) {
        if (c.now() == 1) { c.put(out, h); credits--; return Wake::none(); }   // each station starts one word round the ring
        if (c.ready(cred)) { c.take(cred); credits++; }
        if (c.ready(in) && credits) {
            Word w = c.peek(in)[0];
            for (uint32_t r = 0; r < work * 8; r++) w = hash_mix(w, r);
            h = hash_mix(h, w); n++;
            c.take(in); c.put(cred_out);
            c.put(out, w); credits--;
        }
        return c.ready(in) || c.ready(cred) ? c.next() : Wake::none();
    }
};
struct Mate {   // a Wire's reader, the station's twin: must stay in its granule
    WireIn w; uint64_t seen = 0;
    explicit Mate(Ports& p) { p.in(w, "w"); }
    uint64_t digest() const { return hash_fields(seen); }
    Wake step(Ctx& c) { seen = hash_mix(seen, c.read(w)); return Wake::none(); }
};
struct Driver {
    WireOut w; uint64_t t = 0;
    explicit Driver(Ports& p) { p.out(w, "w", 0); }
    uint64_t digest() const { return hash_fields(t); }
    Wake step(Ctx& c) { t = c.now(); c.drive(w, t / 3); return c.in(3); }
};

struct RingSpec { unsigned n = 24; bool mates = false; int domain_every = 0; };

Sim ring(const RingSpec& R, Options o, const std::vector<std::pair<std::string, double>>& weights = {}) {
    Config cfg(o);
    std::vector<Station*> st;
    for (unsigned k = 0; k < R.n; k++) {
        if (R.domain_every) cfg.domain(k / unsigned(R.domain_every));
        st.push_back(&cfg.add<Station>("s" + std::to_string(k), k));
        if (R.mates) {
            auto& d = cfg.add<Driver>("d" + std::to_string(k));
            auto& m = cfg.add<Mate>("m" + std::to_string(k));
            cfg.connect(d.w, m.w);
        }
    }
    for (unsigned k = 0; k < R.n; k++) {
        cfg.link(st[k]->out, st[(k + 1) % R.n]->in, 3);
        cfg.link(st[(k + 1) % R.n]->cred_out, st[k]->cred, 3);
    }
    for (auto& [nm, w] : weights) cfg.weight(nm, w);
    Sim s = cfg.build();
    return s;
}

uint64_t run_digest(Sim& s, Cycle n) { s.run(n); return s.digest(); }

double ratio_of(const std::string& rep) {   // "load max/mean X" from the partition report
    const size_t k = rep.find("max/mean ");
    return k == std::string::npos ? -1 : std::atof(rep.c_str() + k + 9);
}

}  // namespace

TEST(by_load_gives_the_one_thread_digest_on_the_synthetic_chip) {
    synth::Params P;
    P.seed = 5; P.clusters = 8; P.nodes_per_cluster = 4; P.gap = 1;
    Options one;
    Config c1(one);
    synth::build(c1, P);
    Sim a = c1.build();
    a.run(3000);
    const uint64_t want = a.digest();
    for (unsigned t : {2u, 3u, 4u}) {
        Options o; o.threads = t; o.placement = Options::Placement::by_load;
        Config c(o);
        synth::build(c, P);
        Sim s = c.build();
        s.run(1000); s.run(2000);
        CHECK_EQ(s.digest(), want);
        CHECK(s.partition().find("by load:") != std::string::npos);
    }
}

TEST(by_load_keeps_wires_and_declared_domains_together_and_the_digest) {
    RingSpec R; R.n = 24; R.mates = true;
    Sim a = ring(R, Options{});
    const uint64_t want = run_digest(a, 2000);
    for (unsigned t : {2u, 4u, 6u}) {
        Options o; o.threads = t; o.placement = Options::Placement::by_load;
        Sim s = ring(R, o);                       // a Wire split across threads would be fatal at build
        CHECK_EQ(run_digest(s, 2000), want);
        CHECK(s.partition().find("48 granules") != std::string::npos);   // each station; each driver with its mate
    }
    R.domain_every = 6;                           // declared domains of 6 stations (with their pairs): 4 granules
    Options o; o.threads = 4; o.placement = Options::Placement::by_load;
    Sim s = ring(R, o);
    CHECK(s.partition().find(" 4 granules") != std::string::npos);
    CHECK_EQ(run_digest(s, 2000), want);
}

TEST(by_load_balances_declared_weights) {
    RingSpec R; R.n = 24;
    std::vector<std::pair<std::string, double>> w;
    for (unsigned k = 0; k < R.n; k++) w.push_back({"s" + std::to_string(k), k % 6 == 0 ? 10.0 : 1.0});   // four heavy
    Options o; o.threads = 4; o.placement = Options::Placement::by_load;
    Sim s = ring(R, o, w);
    const double r = ratio_of(s.partition());
    CHECK(r > 0 && r <= 1.05);                    // total 60: 15 per thread, each heavy one with its 5 light neighbours
}

TEST(by_load_on_one_thread_is_one_engine_and_never_more_groups_than_granules) {
    RingSpec R; R.n = 6;
    Options o; o.threads = 1; o.placement = Options::Placement::by_load;
    CHECK(ring(R, o).partition() == std::string("one domain"));
    R.domain_every = 3;                           // two declared domains: two granules, so two groups on 4 threads
    Options p; p.threads = 4; p.placement = Options::Placement::by_load;
    const std::string rep = ring(R, p).partition();
    CHECK(rep.find(" 2 granules") != std::string::npos);
    CHECK(rep.find("2 engines on 2 threads") != std::string::npos);
}

TEST(by_load_runs_allocation_free) {
    RingSpec R; R.n = 24;
    Options o; o.threads = 3; o.placement = Options::Placement::by_load;
    Sim s = ring(R, o);
    s.run(1);
    begin_run();
    s.run(5000);
    end_run();
    CHECK(s.now() == 5002);
}

TEST(calibration_measures_uneven_loads_and_round_trips_as_text) {
    synth::Params P;
    P.seed = 9; P.clusters = 4; P.nodes_per_cluster = 4; P.gap = 1;
    Options o; o.calibrate = true;
    Config c(o);
    synth::build(c, P);
    Sim s = c.build();
    s.run(2000);
    const LoadProfile p = s.load_profile();
    CHECK_EQ(p.cycles, Cycle(2000));
    CHECK(!p.unit.empty());
    double sum = 0;
    for (double v : p.load) { CHECK(v >= 0); sum += v; }
    CHECK(sum > 0);
    const LoadProfile q = LoadProfile::from_text(p.to_text());
    CHECK(q.unit == p.unit);
    CHECK(q.load == p.load);
    CHECK(q.link == p.link);
    CHECK(q.words == p.words);
    CHECK(!p.link.empty());                       // the Links' traffic, counted while calibrating
    CHECK(q.ticks_per_ns == p.ticks_per_ns && p.ticks_per_ns > 0);
    CHECK_EQ(q.cycles, p.cycles);
    const LoadProfile v1 = LoadProfile::from_text("loadprofile v1\ncycles 7\na.b 3\n");   // the first format, still read
    CHECK(v1.unit.size() == 1 && v1.unit[0] == "a.b" && v1.load[0] == 3 && v1.cycles == 7);
    // a profile from a split machine names the machine's units only (no boundary units)
    Options so; so.calibrate = true; so.threads = 2; so.placement = Options::Placement::by_load;
    Config sc(so);
    synth::build(sc, P);
    Sim ss = sc.build();
    ss.run(500);
    const LoadProfile sp = ss.load_profile();
    CHECK_EQ(sp.unit.size(), p.unit.size());
    for (auto& nm : sp.unit) CHECK(nm[0] != '~');
    // and placing by it gives the same digest
    Options po; po.threads = 3; po.placement = Options::Placement::by_load; po.load = &p;
    Config pc(po);
    synth::build(pc, P);
    Sim ps = pc.build();
    Options plain;
    Config qc(plain);
    synth::build(qc, P);
    Sim qs = qc.build();
    ps.run(1500); qs.run(1500);
    CHECK_EQ(ps.digest(), qs.digest());
}

DEATH_TEST(a_malformed_profile_is_fatal_naming_its_line, "load profile line 3: 'r0 x'") {
    LoadProfile::from_text("loadprofile v1\ncycles 10\nr0 x\n");
}

// W10: the barrier on per-thread epochs (Options::barrier_flags) gives the same machine: the synthetic chip over 2-4
// threads, spinning and yielding, in several runs, against one thread.
TEST(the_flags_barrier_gives_the_one_thread_digest) {
    synth::Params P;
    P.seed = 21; P.clusters = 8; P.nodes_per_cluster = 4; P.gap = 1;
    Options one;
    Config c1(one);
    synth::build(c1, P, [](unsigned c) { return c; });
    Sim a = c1.build();
    a.run(1500);
    const uint64_t want = a.digest();
    for (unsigned t : {2u, 3u, 4u})
        for (unsigned spins : {2000u, ~0u}) {
            Options o; o.threads = t; o.barrier_flags = true; o.barrier_spins = spins;
            Config c(o);
            synth::build(c, P, [](unsigned cl) { return cl; });
            Sim s = c.build();
            s.run(1); s.run(499); s.run(1000);
            CHECK_EQ(s.digest(), want);
        }
}
