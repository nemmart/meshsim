// The router (WORKER9, stage 1) against ref's Router.java:
//   - its routing decisions for every (tile, src, dst) and (tile, port, src), hashed per tile, equal to ref's
//     (tests/routerconf/tables.txt, from `RouterConf table`);
//   - its behaviour cycle by cycle: tests/ref/RouterConf.java drives one of ref's routers with a seeded script of
//     arrivals and records every output word, every token freed, and the router's canonical stream (rr and every FIFO,
//     AUDIT-M2 6.2) each cycle; here the same script drives the C++ router on the engine and must give the same lines,
//     under the debug options too (shuffle, both orders, step-all).
// The fixtures are regenerated and checked against ref by tests/ref/routerconf.sh --check.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "units/router.h"

using namespace units;
using sim::Ctx;
using sim::Ports;
using sim::Wake;
using sim::Word;

namespace {

const std::string kDir = "tests/routerconf/";

Hops hops() { return load_hops("ref/meshsim/hops.txt"); }

// ---- the script ----------------------------------------------------------------------------------------------------
struct Ev { uint64_t t; uint8_t port; Word w[3]; };   // port: 0-3 a link, 4 the injection, 5 C&C's hold
struct Script { unsigned tile = 0; uint64_t cycles = 0; std::vector<Ev> ev; };

Script parse_script(const std::string& text) {
    Script s;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') { if (line.rfind("# tile ", 0) == 0) s.tile = unsigned(std::atoi(line.c_str() + 7)); continue; }
        char* q = nullptr;
        uint64_t t = std::strtoull(line.c_str(), &q, 10);
        s.cycles = t;
        while (*q == ' ') {
            q++;
            Ev ev{t, 0, {}};
            if (*q == 'L') { ev.port = uint8_t(q[1] - '0'); q += 3; }
            else { ev.port = *q == 'I' ? 4 : 5; q += 2; }
            for (int k = 0; k < 3; k++) { ev.w[k] = std::strtoull(q, &q, 16); if (*q == ',') q++; }
            s.ev.push_back(ev);
        }
    }
    return s;
}

// ---- the stimulus and the observer -----------------------------------------------------------------------------------
struct Stim {
    sim::LinkOut link[4], inj;
    sim::HoldOut cnc;
    bool attached;
    const Ev* ev; uint32_t n, i = 0;
    Stim(Ports& p, bool att, const Ev* e, uint32_t count) : attached(att), ev(e), n(count) {
        for (unsigned d = 0; d < 4; d++) p.out(link[d], "link", 3);
        p.out(inj, "inj", 3);
        if (attached) p.out(cnc, "cnc", 3);
    }
    Wake step(Ctx& c) {
        const uint64_t t = c.now();
        for (; i < n && ev[i].t == t; i++) {
            const Ev& e = ev[i];
            if (e.port < 4) c.put(link[e.port], e.w[0], e.w[1], e.w[2]);
            else if (e.port == 4) c.put(inj, e.w[0], e.w[1], e.w[2]);
            else c.put(cnc, e.w[0], e.w[1], e.w[2]);
        }
        return i < n ? Wake::at_cycle(ev[i].t) : Wake::none();
    }
    uint64_t digest() const { return i; }
};

struct Got { uint64_t at = 0; Word w[3] = {}; };
struct Obs {
    sim::LinkIn link[4], eject, left;
    sim::FifoIn cnc;
    sim::CountIn ovf;
    bool attached;
    Got out[4], ej, x, lf;
    uint64_t ovf_through_last = 0;   // the router's overflow Count as the register block reads it at its shifted end
    Obs(Ports& p, bool att) : attached(att) {
        for (unsigned d = 0; d < 4; d++) p.in(link[d], "link", 3);
        p.in(eject, "eject", 3); p.in(left, "left", 0); p.in(ovf, "ovf");
        if (attached) p.in(cnc, "cnc", 3, 16);
    }
    static void got(Ctx& c, Got& g, const Word* w) { g.at = c.now(); std::memcpy(g.w, w, sizeof g.w); }
    Wake step(Ctx& c) {
        for (unsigned d = 0; d < 4; d++) if (c.ready(link[d])) { got(c, out[d], c.peek(link[d])); c.take(link[d]); }
        if (c.ready(eject)) { got(c, ej, c.peek(eject)); c.take(eject); }
        if (c.ready(left)) { lf.at = c.now(); c.take(left); }
        if (attached && c.ready(cnc)) { got(c, x, c.peek(cnc)); c.take(cnc); }
        ovf_through_last = c.through_last(ovf);
        return c.next();                                    // every cycle: it samples the Count
    }
    uint64_t digest() const { return 0; }   // observations only
};

// StateFp.Out: the canonical stream's hash.
struct FpOut {
    uint64_t h = 0x6A09E667F3BCC908ull, n = 0;
    static uint64_t fmix(uint64_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33; return k; }
    void w(const char*, uint64_t v) { h = fmix(h ^ v); n++; }
    uint64_t done() const { return fmix(h ^ n); }
};

void w3(std::string& s, const char* tag, const Word* w) {
    char b[80];
    std::snprintf(b, sizeof b, " %s:%" PRIx64 ",%" PRIx64 ",%" PRIx64, tag, w[0], w[1], w[2]);
    s += b;
}

// The C++ side of RouterConf run: the same observation lines.
std::string run(const Script& s, const Hops& h, sim::Options opt) {
    NetConfig nc;
    sim::Config cfg(opt);
    const bool att = s.tile < 3;
    auto& st = cfg.add<Stim>("stim", att, s.ev.data(), uint32_t(s.ev.size()));
    auto& r = cfg.add<Router>("router", s.tile, nc, &h);
    auto& ob = cfg.add<Obs>("obs", att);
    for (unsigned d = 0; d < 4; d++) { cfg.link(st.link[d], r.in[d], 1); cfg.link(r.out[d], ob.link[d], 1); }
    cfg.link(st.inj, r.inj, 1);
    cfg.link(r.eject, ob.eject, 1);
    cfg.link(r.loop_left, ob.left, 1);
    cfg.connect(r.ovf, ob.ovf);
    if (att) { cfg.connect(st.cnc, r.from_cnc); cfg.connect(r.to_cnc, ob.cnc); }
    sim::Sim sim = cfg.build();
    std::string out, snap;
    uint64_t ovf_prev = 0, ovf_prev2 = 0;                    // the overflows of every put through t - 1, t - 2
    for (uint64_t t = 1; t <= s.cycles + 1; t++) {
        sim.run(1);                                           // cycle t
        if (t > 1) {                                          // the line of t - 1: its outputs arrived at t
            const Obs& o = sim.unit<Obs>("obs");
            char b[32];
            std::snprintf(b, sizeof b, "%" PRIu64, t - 1);
            std::string line = b;
            for (unsigned d = 0; d < 4; d++)
                if (o.out[d].at == t) { const char tag[3] = {'O', char('0' + d), 0}; w3(line, tag, o.out[d].w); }
            if (o.ej.at == t) w3(line, "E", o.ej.w);
            if (o.x.at == t) w3(line, "X", o.x.w);
            if (o.lf.at == t) line += " F";
            out += line + snap + "\n";
            // at t, the Count through t - 1 holds the overflows of the puts through t - 2 (judged at t - 1)
            if (o.ovf_through_last != ovf_prev2) {
                ::test::fail(__FILE__, __LINE__, "cycle %" PRIu64 ": the overflow Count through t - 1 is %" PRIu64 ", want %" PRIu64,
                             t, o.ovf_through_last, ovf_prev2);
                return out;
            }
        }
        if (t <= s.cycles) {                                  // the router's state at the end of t (RouterConf's Q)
            const Router& rp = sim.unit<Router>("router");
            FpOut f;
            rp.fp_own(f, t);
            uint64_t ovf = 0;
            for (const auto& q : rp.q) ovf += q.overflows;
            char b[128];
            std::snprintf(b, sizeof b, " Q:%" PRIx64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64, f.done(), ovf, rp.route_errors,
                          rp.eject_errors);
            snap = b;
            ovf_prev2 = ovf_prev; ovf_prev = ovf;
        }
    }
    (void)st;
    return out;
}

std::string body(const std::string& f) {                     // the observations without the provenance lines
    std::string r;
    size_t p = 0;
    while (p < f.size()) {
        size_t e = f.find('\n', p);
        if (e == std::string::npos) e = f.size();
        if (f[p] != '#') r += f.substr(p, e - p + 1);
        p = e + 1;
    }
    return r;
}

// Compare line by line; report the first difference.
bool same(const std::string& name, const std::string& got, const std::string& want) {
    if (got == want) return true;
    size_t p = 0, line = 1;
    while (p < got.size() && p < want.size() && got[p] == want[p]) { if (got[p] == '\n') line++; p++; }
    size_t a = got.rfind('\n', p), b = want.rfind('\n', p);
    a = a == std::string::npos ? 0 : a + 1; b = b == std::string::npos ? 0 : b + 1;
    std::fprintf(stderr, "%s: line %zu differs\n  ref: %s\n  c++: %s\n", name.c_str(), line,
                 want.substr(b, want.find('\n', b) - b).c_str(), got.substr(a, got.find('\n', a) - a).c_str());
    return false;
}

const char* kFixtures[] = {"t0-s1", "t1-s2", "t5-s3", "t27-s1", "t63-s2"};

void conformance(sim::Options opt) {
    const Hops h = hops();
    for (const char* name : kFixtures) {
        bool ok1 = false, ok2 = false;
        std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
        std::string ref = body(portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2));
        if (!ok1 || !ok2 || ref.empty()) { ::test::fail(__FILE__, __LINE__, "fixture %s missing", name); return; }
        Script s = parse_script(script);
        if (!same(name, run(s, h, opt), ref)) { ::test::fail(__FILE__, __LINE__, "%s differs from ref", name); return; }
    }
}

}  // namespace

TEST(routing_tables_equal_refs) {
    const Hops h = hops();
    bool ok = false;
    std::string ref = portconf::read_command("cat " + kDir + "tables.txt", &ok);
    CHECK(ok && !ref.empty());
    std::string got;
    for (unsigned me = 0; me < 64; me++) {
        uint64_t x = 0xcbf29ce484222325ull;
        for (unsigned s = 0; s < 128; s++) for (unsigned d = 0; d < 128; d++) { x ^= route_unicast(h, me, s, d); x *= 0x100000001b3ull; }
        for (unsigned p = 0; p < 6; p++) for (unsigned s = 0; s < 128; s++) { x ^= route_broadcast(me, p, s); x *= 0x100000001b3ull; }
        char b[48];
        std::snprintf(b, sizeof b, "%u %016" PRIx64 "\n", me, x);
        got += b;
    }
    CHECK(same("tables", got, body(ref)));
}

TEST(broadcast_tree_reaches_every_tile_once) {
    // From every source tile: follow the broadcast's copies hop by hop (each link's arrival port), count ejects per tile.
    for (unsigned src = 0; src < 64; src++) {
        unsigned ej[64] = {};
        struct Hop { unsigned tile, port; };
        std::vector<Hop> todo{{src, Router::LOOPBACK}};
        unsigned steps = 0;
        while (!todo.empty()) {
            Hop x = todo.back(); todo.pop_back();
            CHECK(++steps < 10000);
            unsigned m = route_broadcast(x.tile, x.port, src);
            if (m & Router::M_EJ) ej[x.tile]++;
            CHECK(!(m & Router::M_CNC));                       // never delivered to a controller
            const unsigned bit[4] = {Router::M_RIGHT, Router::M_LEFT, Router::M_DOWN, Router::M_UP};
            for (unsigned d = 0; d < 4; d++) if (m & bit[d]) todo.push_back({neighbour(x.tile, d), arrival_port(d)});
        }
        for (unsigned k = 0; k < 64; k++) CHECK_EQ(ej[k], 1u);
    }
}

TEST(router_conformance_with_ref) { conformance({}); }

// The long runs (tests/ref/routerconf.sh CYCLES DIR, plain files), when ROUTERCONF_LONG=DIR is set: every configuration,
// seeds 1-3. Not in make check (minutes); run before a merge that touches the router.
TEST(router_conformance_long_runs_when_asked) {
    const char* dir = std::getenv("ROUTERCONF_LONG");
    if (!dir) return;
    const Hops h = hops();
    const char* tiles[] = {"0", "1", "5", "27", "63"};
    unsigned n = 0;
    for (const char* t : tiles)
        for (int seed = 1; seed <= 3; seed++) {
            std::string name = std::string(dir) + "/t" + t + "-s" + std::to_string(seed);
            bool ok1 = false, ok2 = false;
            std::string script = portconf::read_command("gzip -dc " + name + ".script.gz", &ok1);
            std::string ref = body(portconf::read_command("gzip -dc " + name + ".obs.gz", &ok2));
            CHECK(ok1 && ok2 && !ref.empty());
            CHECK(same(name, run(parse_script(script), h, {}), ref));
            n++;
        }
    std::fprintf(stderr, "router conformance: %u long runs equal to ref\n", n);
}

TEST(router_conformance_under_debug_options) {
    sim::Options a; a.shuffle_seed = 7; conformance(a);
    sim::Options b; b.order = sim::Options::Order::instance_major; conformance(b);
    sim::Options c; c.step_all = true; conformance(c);
}
