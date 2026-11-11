// test_reqbus (WORKER14): the C++ request bus (src/units/reqbus.h) against ref's, cycle by cycle (D34).
//
// tests/reqconf/*.txt.gz come from oracle/ref/ReqConf.java (oracle/ref/reqconf.sh): ref's RequestBus and the 67
// RequestPorts driven by seeded traffic (fan-in, full queues and HOL blocking, a FIFO refusing pushes, drops, calm),
// every call's result and the canonical state after each cycle (StateFp's `reqbus` and the ports' `req` streams,
// hashed). Here the same calls are made on the framework's bus by stand-ins for the cores (a stand per row of tiles,
// one for the controllers' send and receive sides) and every result and every state must be equal:
//   - mode 0 (exact) on every fixture, plain, step-all and shuffled;
//   - mode 3 (the spec, D29) on the calm fixture (no buffer ever full): equal, and its count of cycles that would
//     differ is 0; on the others, the count is reported (and positive where backpressure arises).
// A split machine in mode 0 is refused at build (a death test); mode 3 split over five domains equals one domain.
//
// LOCALISING a mismatch (off by default): REQBUS_DEBUG=1 prints, at the first differing state, our named `reqbus`
// stream (FpOut's dump format), the D29 count, the first call mismatch and the framework's sleepers; ref's side prints
// its named stream after cycle T with `java ReqConf SEED CYCLES FAMILY T` (on stderr). Diff the two by line.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "reqbus_fp.h"
#include "sim.h"
#include "statefp.h"
#include "units/reqbus.h"

using namespace units::reqbus;

namespace {

// ---------------------------------------------------------------- the script
struct Send { unsigned k; uint32_t w[4]; bool ok; size_t line; };
struct Recv { unsigned c; bool pending; uint32_t w[4]; size_t line; };
struct Cycle {
    std::vector<Send> sends;
    std::vector<Recv> recvs;
    bool check = false;
    uint64_t bus_h = 0, ports_h = 0;
    size_t hline = 0;
};
struct Script {
    std::map<uint64_t, Cycle> cyc;
    uint64_t cycles = 0, beats = 0, dropped = 0, underflows = 0;
    bool ok = false;
};

uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }

Script load(const std::string& path) {
    Script s;
    FILE* f = popen(("gzip -dc " + path).c_str(), "r");
    if (!f) return s;
    char* buf = nullptr;
    size_t cap = 0, line = 0;
    uint64_t t = 0;
    while (getline(&buf, &cap, f) >= 0) {
        line++;
        std::istringstream in(buf);
        std::string k;
        in >> k;
        if (k.empty() || k[0] == '#') continue;
        if (k == "t") { in >> t; continue; }
        if (k == "s") {
            Send x; std::string a[4]; int ok;
            in >> x.k >> a[0] >> a[1] >> a[2] >> a[3] >> ok;
            for (int i = 0; i < 4; i++) x.w[i] = uint32_t(hx(a[i]));
            x.ok = ok; x.line = line;
            s.cyc[t].sends.push_back(x);
        } else if (k == "r") {
            Recv x = {}; int p;
            in >> x.c >> p;
            x.pending = p; x.line = line;
            std::string a;
            if (p) for (int i = 0; i < 4; i++) { in >> a; x.w[i] = uint32_t(hx(a)); }
            else { in >> a; x.w[3] = uint32_t(hx(a)); }
            s.cyc[t].recvs.push_back(x);
        } else if (k == "h") {
            std::string a, b; uint64_t tt;
            in >> tt >> a >> b;
            Cycle& c = s.cyc[tt];
            c.check = true; c.bus_h = hx(a); c.ports_h = hx(b); c.hline = line;
        } else if (k == "e") {
            in >> s.cycles >> s.beats >> s.dropped >> s.underflows;
            s.ok = true;
        }
    }
    std::free(buf);
    pclose(f);
    return s;
}

// ---------------------------------------------------------------- the stand-ins for the cores
// A stand for some senders (a row's tiles, or the controllers) and, for the controllers, the receive sides: it tracks
// each port's FIFO count as the register block would (a free at t seen at t through `freed`), makes the script's pushes
// and pops, and checks each call's result.
struct Stand {
    static constexpr unsigned MAXS = 8;
    unsigned first = 0, n = 0;                  // senders first .. first + n - 1
    bool recv = false;
    sim::LinkOut push[MAXS];
    sim::WireIn freed[MAXS];
    sim::RegFileIn head[3];
    sim::LinkOut pop[3], qcredit[3];
    const Script* sc = nullptr;
    unsigned depth = 128;
    unsigned count[MAXS] = {};
    uint32_t set_w[MAXS][3] = {};
    uint64_t pushes = 0, refusals = 0, pops = 0, mismatches = 0;
    char* err = nullptr;                        // the first mismatch (a fixed buffer: no allocation in a step)

    Stand(sim::Ports& p, unsigned first_, unsigned n_, bool recv_, const Script* s, char* e)
        : first(first_), n(n_), recv(recv_), sc(s), err(e) {
        static const char* kP[MAXS] = {"push0", "push1", "push2", "push3", "push4", "push5", "push6", "push7"};
        static const char* kF[MAXS] = {"freed0", "freed1", "freed2", "freed3", "freed4", "freed5", "freed6", "freed7"};
        for (unsigned i = 0; i < n; i++) { p.out(push[i], kP[i], 2); p.in(freed[i], kF[i]); }
        if (recv) {
            static const char* kH[3] = {"head0", "head1", "head2"};
            static const char* kO[3] = {"pop0", "pop1", "pop2"};
            static const char* kC[3] = {"qcredit0", "qcredit1", "qcredit2"};
            for (int c = 0; c < 3; c++) { p.in(head[c], kH[c]); p.out(pop[c], kO[c], 0); p.out(qcredit[c], kC[c], 1); }
        }
    }
    void bad(const char* what, size_t line, uint64_t t) {
        if (!err[0]) std::snprintf(err, 200, "line %zu, cycle %llu: %s", line, (unsigned long long)t, what);
        mismatches++;
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        for (unsigned i = 0; i < n; i++) if (c.read(freed[i])) count[i]--;
        auto it = sc->cyc.find(t);
        if (it != sc->cyc.end()) {
            const Cycle& cy = it->second;
            if (recv)
                for (const Recv& r : cy.recvs) {
                    const bool p = c.read(head[r.c], ReqRecv::R_PENDING) != 0;
                    if (p != r.pending) { bad("pending differs", r.line, t); continue; }
                    if (!p) { if (r.w[3] != 0) bad("an underflow reads 0", r.line, t); continue; }
                    for (int i = 0; i < 4; i++)
                        if (c.read(head[r.c], ReqRecv::R_W0 + unsigned(i)) != r.w[i]) bad("a GET / RECEIVE word differs", r.line, t);
                    c.put(pop[r.c]);
                    c.put(qcredit[r.c], sim::Word(t));
                    pops++;
                }
            for (const Send& s : cy.sends) {
                if (s.k < first || s.k >= first + n) continue;
                const unsigned i = s.k - first;
                for (int j = 0; j < 3; j++) set_w[i][j] = s.w[j];
                const bool can = count[i] < depth;
                if (can != s.ok) { bad(s.ok ? "a push refused here, accepted in ref" : "a push accepted here, refused in ref", s.line, t); continue; }
                if (!can) { refusals++; continue; }
                c.put(push[i], sim::Word(s.w[0]) | sim::Word(s.w[1]) << 32, sim::Word(s.w[2]) | sim::Word(s.w[3]) << 32);
                count[i]++;
                set_w[i][0] = set_w[i][1] = set_w[i][2] = 0;
                pushes++;
            }
            auto nx = std::next(it);
            if (nx != sc->cyc.end()) return sim::Wake::at_cycle(nx->first);
        } else {
            auto nx = sc->cyc.upper_bound(t);
            if (nx != sc->cyc.end()) return sim::Wake::at_cycle(nx->first);
        }
        return sim::Wake::none();
    }
    uint64_t digest() const { return pushes * 131 + refusals * 7 + pops; }
};

struct Rig {
    ReqBusConfig rc;
    char err[200] = {};
    sim::Sim m;
    ReqBusUnits u;
    Rig(const Script& s, unsigned credit_latency, const sim::Options& o) : rc(cfg_of(credit_latency)), m(build(s, o)) {
        u = resolve(m, ~uint64_t(0), 7, rc);
    }
    static ReqBusConfig cfg_of(unsigned cl) { ReqBusConfig r; r.credit_latency = cl; return r; }
    sim::Sim build(const Script& s, const sim::Options& o) {
        sim::Config cfg(o);
        ReqBusIO io = add_reqbus(cfg, rc, ~uint64_t(0), 7);
        static const char* kSt[9] = {"stand.row0", "stand.row1", "stand.row2", "stand.row3", "stand.row4",
                                     "stand.row5", "stand.row6", "stand.row7", "stand.cc"};
        for (unsigned r = 0; r < 9; r++) {
            const bool cc = r == 8;
            Stand& st = cfg.add<Stand>(kSt[r], cc ? 64u : 8 * r, cc ? 3u : 8u, cc, &s, err);
            for (unsigned i = 0; i < st.n; i++) {
                ReqLeaf& l = *io.leaf[st.first + i];
                cfg.link(st.push[i], l.push, 1);
                cfg.connect(l.freed, st.freed[i]);
            }
            if (cc)
                for (unsigned c = 0; c < 3; c++) {
                    cfg.connect(io.recv[c]->head, st.head[c]);
                    cfg.link(st.pop[c], io.recv[c]->pop, 1);
                    cfg.link(st.qcredit[c], io.root->qcredit[c], io.queue_credit_latency());
                }
        }
        // freed_ev goes to the register blocks in a chip; here nobody listens (the stands count through `freed`)
        for (unsigned k = 0; k < SENDERS; k++) cfg.add<Sink>(sink_name(k), &io.leaf[k]->freed_ev, &cfg);
        return cfg.build();
    }
    // a unit taking the leaves' freed_ev (a Link must have a consumer)
    struct Sink {
        sim::LinkIn ev;
        Sink(sim::Ports& p, sim::LinkOut* from, sim::Config* cfg) { p.in(ev, "ev", 0); cfg->link(*from, ev, 1); }
        sim::Wake step(sim::Ctx& c) { while (c.ready(ev)) c.take(ev); return sim::Wake::none(); }
        uint64_t digest() const { return 0; }
    };
    static const char* sink_name(unsigned k) {
        static char names[SENDERS][20];
        std::snprintf(names[k], sizeof names[k], "sink%u", k);
        return names[k];
    }
    const Stand& stand_of(unsigned k) {
        static const char* kSt[9] = {"stand.row0", "stand.row1", "stand.row2", "stand.row3", "stand.row4",
                                     "stand.row5", "stand.row6", "stand.row7", "stand.cc"};
        return m.unit<Stand>(kSt[k < 64 ? k / 8 : 8]);
    }
    // the two hashes at the end of cycle c
    void hashes(uint64_t c, uint64_t& bus, uint64_t& ports) {
        fp::FpOut o;
        reqfp::reqbus(o, m, u, c);
        bus = o.done();
        uint64_t all = fp::kSeed;
        for (unsigned k = 0; k < SENDERS; k++) {
            const Stand& st = stand_of(k);
            fp::FpOut q;
            reqfp::port(q, m, u.leaf[k], st.set_w[k - st.first], c);
            all = fp::fmix(all ^ q.done());
        }
        ports = fp::fmix(all ^ uint64_t(SENDERS));
    }
};

struct Result { bool ok = true; uint64_t checks = 0, lag = 0, pushes = 0, refusals = 0, pops = 0; };

Result replay(const Script& s, unsigned credit_latency, int variant, const char* name, bool strict) {
    sim::Options o;
    o.observe_arrivals = true;
    if (variant == 1) o.step_all = true;
    if (variant == 2) o.shuffle_seed = 11;
    Rig r(s, credit_latency, o);
    Result res;
    for (const auto& [t, cy] : s.cyc) {
        if (!cy.check) continue;
        if (r.m.now() <= t) r.m.run(t + 1 - r.m.now());
        if (r.err[0]) break;
        uint64_t bh, ph;
        r.hashes(t, bh, ph);
        res.checks++;
        if (strict && (bh != cy.bus_h || ph != cy.ports_h)) {
            if (std::getenv("REQBUS_DEBUG")) {   // the localising dump (header)
                fp::FpOut d(stdout, "reqbus");
                reqfp::reqbus(d, r.m, r.u, t);
                std::printf("lag %llu err %s\nsleepers %s\n", (unsigned long long)lag_cycles(r.u), r.err, r.m.sleepers().c_str());
            }
            ::test::fail(__FILE__, __LINE__, "%s mode %u variant %d: line %zu, after cycle %llu: reqbus %016llx (ref %016llx), ports %016llx (ref %016llx)",
                         name, credit_latency, variant, cy.hline, (unsigned long long)t, (unsigned long long)bh,
                         (unsigned long long)cy.bus_h, (unsigned long long)ph, (unsigned long long)cy.ports_h);
            res.ok = false;
            return res;
        }
    }
    if (r.m.now() < s.cycles) r.m.run(s.cycles - r.m.now());
    if (r.err[0] && strict) {
        ::test::fail(__FILE__, __LINE__, "%s mode %u variant %d: %s", name, credit_latency, variant, r.err);
        res.ok = false;
    }
    res.lag = lag_cycles(r.u);
    uint64_t beats = r.u.root->beats_at_root, dropped = r.u.root->dropped;
    if (strict && (beats != s.beats || dropped != s.dropped)) {
        ::test::fail(__FILE__, __LINE__, "%s mode %u: beats at the root %llu (ref %llu), dropped %llu (ref %llu)", name, credit_latency,
                     (unsigned long long)beats, (unsigned long long)s.beats, (unsigned long long)dropped, (unsigned long long)s.dropped);
        res.ok = false;
    }
    for (unsigned k = 0; k < 9; k++) {
        static const char* kSt[9] = {"stand.row0", "stand.row1", "stand.row2", "stand.row3", "stand.row4",
                                     "stand.row5", "stand.row6", "stand.row7", "stand.cc"};
        const Stand& st = r.m.unit<Stand>(kSt[k]);
        res.pushes += st.pushes; res.refusals += st.refusals; res.pops += st.pops;
    }
    return res;
}

void run_fixture(const char* file, bool calm) {
    const std::string path = std::string("tests/reqconf/") + file;
    Script s = load(path);
    CHECK(s.ok);
    for (int v = 0; v < 3; v++) {
        Result r = replay(s, 0, v, file, true);
        CHECK(r.ok);
        CHECK_EQ(r.lag, uint64_t(0));
        if (v == 0)
            std::printf("  %s mode 0: %llu cycles, %llu states equal to ref, %llu pushes (%llu refused), %llu pops, %llu beats at the root, %llu dropped\n",
                        file, (unsigned long long)s.cycles, (unsigned long long)r.checks, (unsigned long long)r.pushes,
                        (unsigned long long)r.refusals, (unsigned long long)r.pops, (unsigned long long)s.beats, (unsigned long long)s.dropped);
    }
    Result r3 = replay(s, 3, 0, file, calm);
    if (calm) { CHECK(r3.ok); CHECK_EQ(r3.lag, uint64_t(0)); }
    std::printf("  %s mode 3: %s, lag_cycles %llu (cycles that would differ from ref)\n", file,
                calm ? (r3.ok ? "equal to ref" : "DIFFERS") : "not compared (backpressure)", (unsigned long long)r3.lag);
    if (!calm) CHECK(r3.lag > 0);
}

}  // namespace

TEST(reqbus_mix_seed1) { run_fixture("mix-s1.txt.gz", false); }
TEST(reqbus_calm_seed2_mode3_equals_ref) { run_fixture("calm-s2.txt.gz", true); }
TEST(reqbus_full_seed3) { run_fixture("full-s3.txt.gz", false); }
TEST(reqbus_fanin_seed4) { run_fixture("fanin-s4.txt.gz", false); }
TEST(reqbus_drop_seed5) { run_fixture("drop-s5.txt.gz", false); }

// D34: mode 0 needs every sender in the bus's domain; a split machine is refused at build.
DEATH_TEST(reqbus_mode0_split_is_refused_at_build, "a regfile can't cross thread domains") {
    sim::Options o;
    o.threads = 2;
    sim::Config cfg(o);
    ReqBusConfig rc;
    rc.credit_latency = 0;
    ReqBusPlacement pl;
    pl.leaf[5] = 1;                  // tile 5's leaf in another domain than its row node
    add_reqbus(cfg, rc, uint64_t(1) << 5, 0, &pl);
    sim::Sim m = cfg.build();
}

namespace {
// Mode 3 on a script, in one domain (threads 1) or split over five (the nodes, the controllers' leaves and queues in
// domain 0; the tiles' leaves and their stands by pairs of rows in domains 1-4): every check's two hashes, the results'
// mismatch text, and the D29 count.
struct Mode3Run { std::vector<uint64_t> h; std::string err, partition; uint64_t lag = 0; };
Mode3Run mode3(const Script& s, unsigned threads) {
    Mode3Run out;
    sim::Options o;
    o.observe_arrivals = true;
    o.threads = threads;
    ReqBusConfig rc;
    rc.credit_latency = 3;
    ReqBusPlacement pl;
    const bool split = threads > 1;
    for (unsigned k = 0; k < SENDERS; k++) pl.leaf[k] = split && k < 64 ? 1 + k / 16 : 0;
    char err[200] = {};
    sim::Config cfg(o);
    ReqBusIO io = add_reqbus(cfg, rc, ~uint64_t(0), 7, &pl);
    static const char* kSt[9] = {"stand.row0", "stand.row1", "stand.row2", "stand.row3", "stand.row4",
                                 "stand.row5", "stand.row6", "stand.row7", "stand.cc"};
    for (unsigned r = 0; r < 9; r++) {
        const bool cc = r == 8;
        cfg.domain(split && !cc ? 1 + r / 2 : 0);
        Stand& st = cfg.add<Stand>(kSt[r], cc ? 64u : 8 * r, cc ? 3u : 8u, cc, &s, err);
        for (unsigned i = 0; i < st.n; i++) {
            ReqLeaf& l = *io.leaf[st.first + i];
            cfg.link(st.push[i], l.push, 1);
            cfg.connect(l.freed, st.freed[i]);
            cfg.add<Rig::Sink>(Rig::sink_name(st.first + i), &l.freed_ev, &cfg);
        }
        if (cc)
            for (unsigned c = 0; c < 3; c++) {
                cfg.connect(io.recv[c]->head, st.head[c]);
                cfg.link(st.pop[c], io.recv[c]->pop, 1);
                cfg.link(st.qcredit[c], io.root->qcredit[c], io.queue_credit_latency());
            }
    }
    sim::Sim m = cfg.build();
    out.partition = m.partition();
    ReqBusUnits u = resolve(m, ~uint64_t(0), 7, rc);
    for (const auto& [t, cy] : s.cyc) {
        if (!cy.check) continue;
        if (m.now() <= t) m.run(t + 1 - m.now());
        fp::FpOut b;
        reqfp::reqbus(b, m, u, t);
        out.h.push_back(b.done());
        uint64_t all = fp::kSeed;
        for (unsigned k = 0; k < SENDERS; k++) {
            const Stand& st = m.unit<Stand>(kSt[k < 64 ? k / 8 : 8]);
            fp::FpOut q;
            reqfp::port(q, m, u.leaf[k], st.set_w[k - st.first], t);
            all = fp::fmix(all ^ q.done());
        }
        out.h.push_back(fp::fmix(all ^ uint64_t(SENDERS)));
    }
    if (m.now() < s.cycles) m.run(s.cycles - m.now());
    out.err = err;
    out.lag = lag_cycles(u);
    return out;
}
}  // namespace

// Mode 3 split over five thread domains gives one domain's results exactly (W4's guarantee): on the calm script equal
// to ref as well; on the full script (backpressure) the same differences from ref, and the same count of them.
TEST(reqbus_mode3_split_equals_one_domain) {
    for (const char* f : {"calm-s2.txt.gz", "full-s3.txt.gz"}) {
        Script s = load(std::string("tests/reqconf/") + f);
        CHECK(s.ok);
        const Mode3Run one = mode3(s, 1), five = mode3(s, 5);
        std::printf("  %s: one domain lag %llu; split: %s, lag %llu\n", f, (unsigned long long)one.lag, five.partition.c_str(),
                    (unsigned long long)five.lag);
        CHECK(five.partition != "one domain");
        CHECK(one.h == five.h);
        CHECK(one.err == five.err);
        CHECK_EQ(one.lag, five.lag);
        if (std::string(f) == "calm-s2.txt.gz") {
            CHECK(one.err.empty());
            CHECK_EQ(one.lag, uint64_t(0));
            size_t i = 0;
            for (const auto& [t, cy] : s.cyc) {
                if (!cy.check) continue;
                if (one.h[i] != cy.bus_h || one.h[i + 1] != cy.ports_h) {
                    ::test::fail(__FILE__, __LINE__, "calm mode 3: after cycle %llu: differs from ref", (unsigned long long)t);
                    break;
                }
                i += 2;
            }
        } else {
            CHECK(one.lag > 0);
        }
    }
}
