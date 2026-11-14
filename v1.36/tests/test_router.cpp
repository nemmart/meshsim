// The router against the RTL's router (Snarky One, rtl/blocks/router), and what is left of the comparison with ref's
// Router.java (tests/routerconf/README.md says whose each fixture is and how the RTL's are made):
//   - against the RTL: scripts of random many-to-one load on one router (a compute tile's, each attach tile's, each
//     special tile's with its DMA inputs), and what the RTL's router did with each under Verilator, a line a cycle:
//     every output's register, the eject's loopback mark, the overflow pulse, each DMA input offering and not ready.
//     The same script drives the C++ router on the engine and must give the same lines, under the debug options too
//     (shuffle, both orders, step-all). And every router's routes, as a hash of the outputs each (input, src, dst)
//     goes to, equal to the RTL's.
//   - against ref, for the cycles before a script's first packet that v1.36's router routes otherwise:
//     oracle/ref/RouterConf.java drives one of ref's routers with a seeded script of arrivals and records every
//     output word, every token freed, and the router's canonical stream (rr and every FIFO, AUDIT-M2 6.2) each cycle;
//     stage 2: the injection unit behind the router, fed by a core's send holding register that retries a refused
//     put; M3: an attach tile with its attach port as a link.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "units/inject.h"
#include "units/router.h"

using namespace units;
using sim::Ctx;
using sim::Ports;
using sim::Wake;
using sim::Word;

namespace {

const std::string kDir = "tests/routerconf/";

Hops hops() { return load_hops("data/hops.txt"); }

// ---- the script ----------------------------------------------------------------------------------------------------
struct Ev { uint64_t t; uint8_t port; Word w[3]; };   // port: 0-3 a link, 4 the injection, 5 C&C's hold, 6 a core send
struct Script { unsigned tile = 0; uint64_t cycles = 0; bool send = false, link = false, dma = false; std::vector<Ev> ev; };

Script parse_script(const std::string& text) {
    Script s;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("# tile ", 0) == 0) s.tile = unsigned(std::atoi(line.c_str() + 7));
            if (line.rfind("# seed ", 0) == 0 && line.size() > 5 && line.compare(line.size() - 5, 5, " send") == 0) s.send = true;
            if (line == "# fifth link") s.link = true;
            if (line == "# cc dma") s.dma = true;
            continue;
        }
        char* q = nullptr;
        uint64_t t = std::strtoull(line.c_str(), &q, 10);
        s.cycles = t;
        while (*q == ' ') {
            q++;
            Ev ev{t, 0, {}};
            if (*q == 'L') { ev.port = uint8_t(q[1] - '0'); q += 3; }
            else { ev.port = *q == 'I' ? 4 : *q == 'S' ? 6 : 5; q += 2; }
            for (int k = 0; k < 3; k++) { ev.w[k] = std::strtoull(q, &q, 16); if (*q == ',') q++; }
            s.ev.push_back(ev);
        }
    }
    return s;
}

// ---- the stimulus and the observer -----------------------------------------------------------------------------------
struct Stim {
    sim::LinkOut link[4], inj, cc_link;
    sim::HoldOut cnc, send;
    bool attached, core, link5;
    const Ev* ev; uint32_t n, i = 0;
    // the core's send holding register, as the core uses it: a refused put is retried, the script's word dropped
    Word put_w[3] = {}, occupant[3] = {};
    uint64_t put_at = 0, refusals = 0;
    Stim(Ports& p, const Script& s, const Ev* e, uint32_t count)
        : attached(s.tile < 3), core(s.send), link5(s.link), ev(e), n(count) {
        for (unsigned d = 0; d < 4; d++) p.out(link[d], "link", 3);
        if (core) p.out(send, "send", 3); else p.out(inj, "inj", 3);
        if (attached && !link5) p.out(cnc, "cnc", 3);
        if (attached && link5) p.out(cc_link, "cc-link", 3);
    }
    Wake step(Ctx& c) {
        const uint64_t t = c.now();
        bool retried = false;
        if (core && put_at == t - 1) {
            if (c.refused(send)) { refusals++; c.put(send, put_w[0], put_w[1], put_w[2]); put_at = t; retried = true; }
            else std::memcpy(occupant, put_w, sizeof occupant);   // accepted at t: the hold's word
        }
        for (; i < n && ev[i].t == t; i++) {
            const Ev& e = ev[i];
            if (e.port < 4) c.put(link[e.port], e.w[0], e.w[1], e.w[2]);
            else if (e.port == 4) c.put(inj, e.w[0], e.w[1], e.w[2]);
            else if (e.port == 6) {
                if (retried) continue;                            // the core stalls on its retry
                c.put(send, e.w[0], e.w[1], e.w[2]); std::memcpy(put_w, e.w, sizeof put_w); put_at = t;
            }
            else if (link5) c.put(cc_link, e.w[0], e.w[1], e.w[2]);
            else c.put(cnc, e.w[0], e.w[1], e.w[2]);
        }
        return c.next();                                          // every cycle: it plays the core
    }
    uint64_t digest() const { return i; }
};

struct Got { uint64_t at = 0; Word w[3] = {}; };
struct Obs {
    sim::LinkIn link[4], eject, left, cc_link;
    sim::FifoIn cnc;
    sim::CountIn ovf;
    sim::RegIn busy;
    bool attached, core, link5;
    unsigned nlinks;
    Got out[4], ej, x, lf;
    uint64_t ovf_through_last = 0;   // the router's overflow Count as the register block reads it at its shifted end
    Obs(Ports& p, const Script& s) : attached(s.tile < 3), core(s.send), link5(s.link), nlinks(4) {
        for (unsigned d = 0; d < nlinks; d++) p.in(link[d], "link", 3);
        p.in(eject, "eject", 3); p.in(ovf, "ovf");
        if (core) p.in(busy, "busy", 1); else p.in(left, "left", 0);
        if (attached && !link5) p.in(cnc, "cnc", 3, 16);
        if (attached && link5) p.in(cc_link, "cc-link", 3);
    }
    static void got(Ctx& c, Got& g, const Word* w) { g.at = c.now(); std::memcpy(g.w, w, sizeof g.w); }
    Wake step(Ctx& c) {
        for (unsigned d = 0; d < nlinks; d++) if (c.ready(link[d])) { got(c, out[d], c.peek(link[d])); c.take(link[d]); }
        if (c.ready(eject)) { got(c, ej, c.peek(eject)); c.take(eject); }
        if (!core && c.ready(left)) { lf.at = c.now(); c.take(left); }
        if (attached && !link5 && c.ready(cnc)) { got(c, x, c.peek(cnc)); c.take(cnc); }
        if (attached && link5 && c.ready(cc_link)) { got(c, x, c.peek(cc_link)); c.take(cc_link); }
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

// v1.35's attach tiles, 0, 1 and 2, which the reference observations and tables were made at (v1.36's are 0, 7 and 15:
// machine-spec 2.1; NetConfig::attach). The routing functions are the same; only where the fifth port is differs.
constexpr uint8_t kRefAttach[3] = {0, 1, 2};

// The C++ side of RouterConf run: the same observation lines.
std::string run(const Script& s, const Hops& h, sim::Options opt) {
    NetConfig nc;
    for (unsigned i = 0; i < 3; i++) nc.attach[i] = kRefAttach[i];
    sim::Config cfg(opt);
    const bool att = s.tile < 3;
    if (s.tile >= 64) { ::test::fail(__FILE__, __LINE__, "ref's scripts of a controller's router are not run (tile %u)", s.tile); return ""; }
    auto& st = cfg.add<Stim>("stim", s, s.ev.data(), uint32_t(s.ev.size()));
    auto& r = cfg.add<Router>("router", s.tile, nc, &h, false, s.link ? Router::FIFTH_LINK : Router::FIFTH_HOLD);
    auto& ob = cfg.add<Obs>("obs", s);
    for (unsigned d = 0; d < 4; d++) { cfg.link(st.link[d], r.in[d], 1); cfg.link(r.out[d], ob.link[d], 1); }
    cfg.link(r.eject, ob.eject, 1);
    if (s.send) {
        auto& ij = cfg.add<Injection>("inj", s.tile, nc);
        cfg.connect(st.send, ij.send);
        cfg.link(ij.to_router, r.inj, nc.intra_tile_latency);
        cfg.link(r.loop_left, ij.loop_left, 1);
        cfg.connect(ij.send_busy, ob.busy);
        cfg.missed_wake_after("inj", 1u << 20);                   // a token wait: woken by a free that may not be on its way yet
    } else {
        cfg.link(st.inj, r.inj, 1);
        cfg.link(r.loop_left, ob.left, 1);
    }
    cfg.connect(r.ovf, ob.ovf);
    if (att && !s.link) { cfg.connect(st.cnc, r.from_cnc); cfg.connect(r.to_cnc, ob.cnc); }
    if (att && s.link) { cfg.link(st.cc_link, r.from_cc, 1); cfg.link(r.to_cc, ob.cc_link, 1); }
    sim::Sim sim = cfg.build();
    std::string out, snap, injsnap;
    uint64_t held_end = 0;                                    // the injection unit's loopback tokens at the end of t - 1
    bool freed_end = false;                                   // a token freed (loop_left put) at t - 1
    uint64_t ovf_prev = 0;                                   // the overflows of every put through t - 1
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
            const bool freed = s.send ? freed_end : o.lf.at == t;
            if (freed) line += " F";
            if (s.send) {                                     // tokens held as at the start of t: minus the free in flight
                char b[32];
                std::snprintf(b, sizeof b, "%" PRIu64, held_end - (freed ? 1 : 0));
                const size_t k = injsnap.find('#');
                line += snap + injsnap.substr(0, k) + b + injsnap.substr(k + 1);
            } else line += snap;
            out += line + "\n";
            // at t, the Count through t - 1 holds the overflows of the puts through t - 1: a lost packet is counted in
            // its push's cycle (v1.36: the RTL's `overflow` pulse, router_output.veryl; v1.35 counted it a cycle later)
            if (o.ovf_through_last != ovf_prev) {
                ::test::fail(__FILE__, __LINE__, "cycle %" PRIu64 ": the overflow Count through t - 1 is %" PRIu64 ", want %" PRIu64,
                             t, o.ovf_through_last, ovf_prev);
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
            ovf_prev = ovf;
            if (s.send) {                                     // StateFp's "inj" (read return: empty; the send hold)
                const Injection& ij = sim.unit<Injection>("inj");
                const Stim& sp = sim.unit<Stim>("stim");
                const bool put_now = sp.put_at == t, occ = ij.busy_out != 0;
                FpOut g;
                g.w("readReturn.occ", 0); g.w("readReturn.put", 0);
                g.w("send.occ", occ);
                if (occ) { g.w("a", sp.occupant[0]); g.w("b", sp.occupant[1]); g.w("c", sp.occupant[2]); }
                g.w("send.put", put_now);
                if (put_now) { g.w("pa", sp.put_w[0]); g.w("pb", sp.put_w[1]); g.w("pc", sp.put_w[2]); }
                std::snprintf(b, sizeof b, "%s P:%" PRIx64 ",#,%" PRIu64 ",%" PRIu64 ",%" PRIu64, occ || put_now ? " B" : "",
                              g.done(), ij.token_waits(t), sp.refusals, ij.violations);
                injsnap = b;
                held_end = ij.tok.loop_held;
                freed_end = rp.loop_left_at == t;
            }
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

const char* kFixtures[] = {"t0-s1", "t1-s2", "t5-s3", "t27-s1", "t63-s2", "t0-s4-send", "t5-s5-send", "t63-s6-send",
                           "t0-s7-link", "t2-s8-link"};
// (v1.36: ref's runs of a controller's router, t64-s1, t65-s2 and t66-s3-dma, are no longer run: they drive v1.35's host
// port, which a special tile's router does not have, and a DMA without the flow control of 16.4, from v1.35's source id.
// The fixtures stay in tests/routerconf/; the special tiles' routers are compared with the RTL's below.)

// v1.36 AND REF'S FIXTURES. Two rules of the RTL's router are not ref's: a packet for a reserved tile id, or for none, is
// dropped where it is routed (16.2; ref routes it as tile dst mod 64 and counts it at the eject), and v1.35's DMA source
// ids 72-74, which ref routes as their controllers, do not exist (16.4: a DMA sends with its tile's id). Ref's scripts
// draw ids at random, so each holds such packets (some 3 % of its events), from which on ref's observations are of
// another router; so are they from a packet at a tile its route does not pass (first_divergence says which).
// `first_divergence` is the cycle of a script's first such event; the comparison with ref covers the
// cycles before it, and says how many. The whole of the router's behaviour under load is compared with the RTL's own
// router, below.
uint64_t first_divergence(const Script& s, const Hops& h) {
    for (const Ev& e : s.ev) {
        const unsigned src = lw::src(e.w[1]), dst = lw::dst(e.w[1]);
        const bool stamped = e.port == 4 || e.port == 6;               // the injection: src is the router's to stamp
        if ((dst > lw::CC3 && dst != lw::BROADCAST) || (!stamped && src >= 72 && src <= 74)) return e.t;
        // a packet at a tile its route does not pass, which no link delivers and the scripts' random ids make: the RTL's
        // route functions send two kinds another way than ref's (router.cpp: a unicast with no ring move off its
        // source's column, or with no column move off its destination; a broadcast's ring copy more than a row from
        // its source's)
        if (stamped || dst > lw::CC3) { if (dst != lw::BROADCAST || stamped || e.port > 1) continue; }
        const int st = int(lw::tile_of(src, kRefAttach) & 63), cs = st & 7, cm = int(s.tile) & 7, rs = st >> 3, rm = int(s.tile) >> 3;
        if (dst == lw::BROADCAST) { if (((rm - rs) & 7) == 4 || ((rs - rm) & 7) == 3) return e.t; continue; }
        const int k = (int(lw::tile_of(dst, kRefAttach) & 63) - st) & 63;
        if (h.b[k] == 0 && cm != cs) return e.t;
        const int hb = h.b[k] >= 0 ? (cm - cs) & 7 : -((cs - cm) & 7);
        if (hb == h.b[k] && h.a[k] == 0 && int(s.tile) != ((st + k) & 63)) return e.t;      // no column move, off its destination
    }
    return ~uint64_t(0);
}
// The lines of `text` for the cycles before `cut` (a line starts with its cycle).
std::string before(const std::string& text, uint64_t cut, unsigned* lines = nullptr) {
    size_t p = 0;
    unsigned n = 0;
    while (p < text.size()) {
        if (std::strtoull(text.c_str() + p, nullptr, 10) >= cut) break;
        const size_t e = text.find('\n', p);
        p = e == std::string::npos ? text.size() : e + 1;
        n++;
    }
    if (lines) *lines = n;
    return text.substr(0, p);
}

void conformance(sim::Options opt, bool report = false) {
    const Hops h = hops();
    for (const char* name : kFixtures) {
        bool ok1 = false, ok2 = false;
        std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
        std::string ref = body(portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2));
        if (!ok1 || !ok2 || ref.empty()) { ::test::fail(__FILE__, __LINE__, "fixture %s missing", name); return; }
        Script s = parse_script(script);
        const uint64_t cut = first_divergence(s, h);
        unsigned lines = 0;
        const std::string want = before(ref, cut, &lines);
        const std::string got = run(s, h, opt);
        if (!same(name, before(got, cut), want)) { ::test::fail(__FILE__, __LINE__, "%s differs from ref", name); return; }
        if (report) std::fprintf(stderr, "router conformance: %s: equal to ref for the %u cycles before its first packet v1.36 routes otherwise (of %llu)\n",
                                 name, lines, (unsigned long long)s.cycles);
    }
}

// ---- the RTL's router (Snarky One, rtl/blocks/router) ----------------------------------------------------------------
// tests/routerconf/rtl/ (its README): scripts of arrivals at one router, a line a cycle, and what the RTL's router,
// under Verilator, did with each: every output's register in every cycle, the eject's loopback mark, the overflow
// pulse, and each DMA input that offered and was not ready. The same script drives the simulator's router here, and
// its lines must be the RTL's, character for character. The script's format is router_script_test.rs's header:
//   <t> <P>:<lo>,<h>,<hi> ...    P: I local, W E N S a link input (a special tile's: a DMA input), A the attach port
// A script's cycle t is the simulator's cycle t + 1: the stand puts a link's packet in t on a Link(1), the router has
// it at its input in t + 1, and a packet the router puts in t + 2 reaches the stand in t + 3, which is the RTL's output
// register in the script's cycle t + 2.
const std::string kDirRtl = "tests/routerconf/rtl/";
enum : unsigned { P_LOCAL, P_WEST, P_EAST, P_NORTH, P_SOUTH, P_ATTACH, P_N };
const char kPortName[P_N + 1] = "IWENSA";
struct REv { uint64_t t; unsigned port; Word w[3]; };
struct RScript { unsigned tile = 0; uint64_t cycles = 0; std::vector<REv> ev; };
// The DMA inputs of a special tile's router as the RTL's models are built (rtl/blocks/router/partition.toml): the
// master tile's on all four link inputs, the memory tile's west and north, the tracer tile's east and south.
unsigned rtl_dma_inputs(unsigned tile) { return tile == 64 ? 15u : tile == 65 ? 5u : tile == 66 ? 10u : 0u; }
bool rtl_attached(unsigned tile) { return tile == 0 || tile == 7 || tile == 15 || tile >= 64; }

RScript parse_rtl_script(const std::string& text) {
    RScript s;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("# tile ", 0) == 0) s.tile = unsigned(std::atoi(line.c_str() + 7));
            if (line.rfind("# cycles ", 0) == 0) s.cycles = std::strtoull(line.c_str() + 9, nullptr, 10);
            continue;
        }
        char* q = nullptr;
        const uint64_t t = std::strtoull(line.c_str(), &q, 10);
        while (*q == ' ') {
            q++;
            REv ev{t, unsigned(std::strchr(kPortName, *q) - kPortName), {}};
            q += 2;
            for (int k = 0; k < 3; k++) { ev.w[k] = std::strtoull(q, &q, 16); if (*q == ',') q++; }
            s.ev.push_back(ev);
        }
    }
    return s;
}

// What one cycle of the script shows: each output's register (P_LOCAL the eject's), the eject's loopback mark, the
// overflow pulse, each DMA input offering and not ready.
struct RLine { bool v[P_N] = {}; Word w[P_N][3] = {}; bool loopback = false, overflow = false, held[P_N] = {}; };

struct RStand {
    sim::LinkOut to[P_N];
    sim::LinkIn from[P_N], token;
    sim::CountIn ovf;
    sim::WireIn room;
    unsigned tile, dma;
    bool special, attach;
    const RScript* s;
    size_t next = 0;
    std::vector<REv> offered[P_N];           // each DMA input's packets, offered in order
    size_t offer_next[P_N] = {};
    std::vector<RLine> line;                 // by the script's cycle
    uint64_t ovf_seen = 0;
    RStand(Ports& p, const RScript& sc)
        : tile(sc.tile), dma(rtl_dma_inputs(sc.tile)), special(sc.tile >= 64), attach(rtl_attached(sc.tile)), s(&sc), line(sc.cycles + 4) {
        p.out(to[P_LOCAL], "to-local", 3); p.in(from[P_LOCAL], "eject", 3); p.in(token, "token", 0); p.in(ovf, "ovf");
        for (unsigned i = P_WEST; i <= P_SOUTH; i++) {
            if (!special || (dma >> (i - P_WEST) & 1)) p.out(to[i], "to-link", 3);
            if (!special) p.in(from[i], "from-link", 3);
        }
        if (attach) { p.out(to[P_ATTACH], "to-attach", 3); p.in(from[P_ATTACH], "from-attach", 3); }
        if (dma) p.in(room, "room");
    }
    bool is_dma(unsigned port) const { return special && port >= P_WEST && port <= P_SOUTH && (dma >> (port - P_WEST) & 1); }
    Wake step(Ctx& c) {
        const uint64_t now = c.now();                         // the script's cycle now - 1 is at the router's inputs
        // the outputs: a packet here in `now` is in its output's register in the script's cycle now - 1
        RLine* out = now >= 1 && now - 1 < line.size() ? &line[now - 1] : nullptr;
        for (unsigned i = 0; i < P_N; i++) {
            if (i >= P_WEST && i <= P_SOUTH && special) continue;
            if (i == P_ATTACH && !attach) continue;
            if (!c.ready(from[i])) continue;
            const Word* w = c.peek(from[i]);
            if (out) { out->v[i] = true; out->w[i][0] = w[0]; out->w[i][1] = w[1] & lw::HDR_MASK; out->w[i][2] = w[2]; }
            c.take(from[i]);
        }
        if (c.ready(token)) { if (out) out->loopback = true; c.take(token); }
        // the overflows counted through now - 1: those of now - 1's pushes are the script's cycle now - 2
        const uint64_t through = c.through_last(ovf);
        if (through != ovf_seen && now >= 2 && now - 2 < line.size()) line[now - 2].overflow = true;
        ovf_seen = through;
        // the inputs of the script's cycle now: put now, at the router in now + 1; a DMA's joins its input's queue
        for (; next < s->ev.size() && s->ev[next].t == now; next++) {
            const REv& e = s->ev[next];
            if (is_dma(e.port)) { offered[e.port].push_back(e); continue; }
            // every local packet holds a token here, so that the router marks each one leaving the loopback queue
            c.put(to[e.port], e.w[0], e.port == P_LOCAL ? e.w[1] | lw::TAG_TOKEN : e.w[1], e.w[2]);
        }
        // the DMA inputs in the script's cycle now - 1 (the router's `dma_room` of this cycle): each offers its oldest
        // packet of a cycle up to now - 1, which moves if the router is ready for it
        for (unsigned i = P_WEST; i <= P_SOUTH; i++) {
            if (!is_dma(i) || offer_next[i] == offered[i].size()) continue;
            const REv& e = offered[i][offer_next[i]];
            if (e.t > now - 1) continue;
            if (Router::dma_ready(c.read(room), i - P_WEST, tile, lw::dst(e.w[1]))) { c.put(to[i], e.w[0], e.w[1], e.w[2]); offer_next[i]++; }
            else if (now - 1 < line.size()) line[now - 1].held[i] = true;
        }
        return c.next();
    }
    uint64_t digest() const { return next; }
};

std::vector<RLine> run_rtl_script(const RScript& s, const Hops& h, sim::Options opt = {}) {
    const NetConfig nc;
    sim::Config cfg(opt);
    const bool special = s.tile >= 64;
    auto& r = cfg.add<Router>("router", s.tile, nc, &h, false, Router::FIFTH_LINK, rtl_dma_inputs(s.tile));
    auto& st = cfg.add<RStand>("stand", s);
    cfg.link(st.to[P_LOCAL], r.inj, 1);
    cfg.link(r.eject, st.from[P_LOCAL], 1);
    cfg.link(r.loop_left, st.token, 1);
    cfg.connect(r.ovf, st.ovf);
    static const unsigned in_of[4] = {Router::FROM_LEFT, Router::FROM_RIGHT, Router::FROM_UP, Router::FROM_DOWN};
    static const unsigned out_of[4] = {Router::TO_LEFT, Router::TO_RIGHT, Router::TO_UP, Router::TO_DOWN};
    for (unsigned i = 0; i < 4; i++) {
        if (!special || (st.dma >> i & 1)) cfg.link(st.to[P_WEST + i], r.in[in_of[i]], 1);
        if (!special) cfg.link(r.out[out_of[i]], st.from[P_WEST + i], 1);
    }
    if (st.attach) { cfg.link(st.to[P_ATTACH], r.from_cc, 1); cfg.link(r.to_cc, st.from[P_ATTACH], 1); }
    if (st.dma) cfg.connect(r.dma_room, st.room);
    sim::Sim m = cfg.build();
    m.run(s.cycles + 3);
    return m.unit<RStand>("stand").line;
}

// The lines as router_script_test.rs writes them.
std::string rtl_lines(const RScript& s, const std::vector<RLine>& line) {
    std::string out;
    char b[96];
    for (uint64_t t = 1; t <= s.cycles; t++) {
        const RLine& l = line[t];
        std::snprintf(b, sizeof b, "%" PRIu64, t);
        out += b;
        static const unsigned order[P_N] = {P_WEST, P_EAST, P_NORTH, P_SOUTH, P_ATTACH, P_LOCAL};
        for (unsigned i : order) {
            if (!l.v[i]) continue;
            std::snprintf(b, sizeof b, " %c:%" PRIx64 ",%" PRIx64 ",%" PRIx64, i == P_LOCAL ? 'J' : kPortName[i], l.w[i][0], l.w[i][1], l.w[i][2]);
            out += b;
            if (i == P_LOCAL && l.loopback) out += '*';
        }
        if (l.overflow) out += " V";
        for (unsigned i = P_WEST; i <= P_SOUTH; i++) if (l.held[i]) { out += " !"; out += kPortName[i]; }
        out += '\n';
    }
    return out;
}

// The scripts (README: made once, by ROUTERCONF_GEN=DIR; the RTL's observations of them are the references).
const char* kRtlFixtures[] = {"t27-s1", "t27-s2", "t8-s3", "t63-s4", "t0-s5", "t7-s6", "t15-s7", "t64-s8", "t65-s9", "t66-s10"};

struct Rng {
    uint64_t x;
    uint64_t next() { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; }
    unsigned below(unsigned n) { return unsigned((next() >> 16) % n); }
    bool chance(unsigned percent) { return below(100) < percent; }
};

// A script of random many-to-one load on tile `tile`'s router: phases of a packet at each input in 20 %, 50 % and
// 95 % of the cycles, a lull, then every input sending to the tile itself in every cycle, and cycles to drain. A
// destination is the tile itself in 45 % of the packets (the eject's fan-in), a broadcast, a reserved id or none, a
// special tile, or any compute tile; a source any tile id (the local input's and a DMA's are the router's to stamp:
// the script gives none, 127).
std::string gen_script(unsigned tile, unsigned seed) {
    Rng rng{0x9E3779B97F4A7C15ull * (seed + 1) ^ tile};
    const unsigned dma = rtl_dma_inputs(tile);
    const bool special = tile >= 64;
    const uint64_t cycles = 3200;
    std::string out = "# routerconf-rtl v1 (tests/routerconf/README.md)\n# tile " + std::to_string(tile) + "\n# seed " + std::to_string(seed) +
                      "\n# cycles " + std::to_string(cycles) + "\n";
    uint64_t serial = 0;
    char b[96];
    for (uint64_t t = 1; t <= 3000; t++) {
        const unsigned rate = t < 700 ? 20 : t < 1400 ? 50 : t < 2200 ? 95 : t < 2600 ? 10 : t < 2900 ? 100 : 0;
        const bool fan_in = t >= 2600;
        std::string line = std::to_string(t);
        for (unsigned port = 0; port < P_N; port++) {
            const bool is_dma = special && port >= P_WEST && port <= P_SOUTH && (dma >> (port - P_WEST) & 1);
            if (special && port >= P_WEST && port <= P_SOUTH && !is_dma) continue;
            if (port == P_ATTACH && !rtl_attached(tile)) continue;
            if (!rng.chance(rate)) continue;
            const unsigned k = rng.below(100);
            unsigned dst = fan_in || k < 45 ? tile : k < 55 ? unsigned(lw::BROADCAST) : k < 58 ? 67 + rng.below(59) : k < 60 ? unsigned(lw::NONE)
                         : k < 68 ? 64 + rng.below(3) : rng.below(64);
            unsigned src = rng.below(67);
            if (port == P_LOCAL || is_dma) src = lw::NONE;
            else if (port == P_ATTACH && !special && rng.chance(80)) src = 64 + unsigned(lw::attached_special(tile));
            serial++;
            const uint64_t h = lw::hdr(src, dst, rng.below(8), rng.below(8), uint32_t(serial * 16), rng.below(4));
            std::snprintf(b, sizeof b, " %c:%" PRIx64 ",%" PRIx64 ",%" PRIx64, kPortName[port], serial, h, serial ^ 0xA5A5);
            line += b;
        }
        out += line + "\n";
    }
    return out;
}
}  // namespace

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

TEST(router_conformance_with_ref) { conformance({}, true); }

// The long runs (oracle/ref/routerconf.sh CYCLES DIR, plain files), when ROUTERCONF_LONG=DIR is set: every configuration,
// seeds 1-3. Not in make check (minutes); run before a merge that touches the router.
TEST(router_conformance_long_runs_when_asked) {
    const char* dir = std::getenv("ROUTERCONF_LONG");
    if (!dir) return;
    const Hops h = hops();
    const char* confs[] = {"0", "1", "5", "27", "63", "0|-send", "5|-send", "63|-send", "0|-link", "2|-link"};
    unsigned n = 0;
    for (const char* cf : confs)
        for (int seed = 1; seed <= 3; seed++) {
            std::string c = cf, tile = c.substr(0, c.find('|')), mode = c.find('|') == std::string::npos ? "" : c.substr(c.find('|') + 1);
            std::string name = std::string(dir) + "/t" + tile + "-s" + std::to_string(seed) + mode;
            bool ok1 = false, ok2 = false;
            std::string script = portconf::read_command("gzip -dc " + name + ".script.gz", &ok1);
            std::string ref = body(portconf::read_command("gzip -dc " + name + ".obs.gz", &ok2));
            CHECK(ok1 && ok2 && !ref.empty());
            const Script sc = parse_script(script);
            const uint64_t cut = first_divergence(sc, h);
            CHECK(same(name, before(run(sc, h, {}), cut), before(ref, cut)));
            n++;
        }
    std::fprintf(stderr, "router conformance: %u long runs equal to ref\n", n);
}

TEST(router_conformance_under_debug_options) {
    sim::Options a; a.shuffle_seed = 7; conformance(a);
    sim::Options b; b.order = sim::Options::Order::instance_major; conformance(b);
    sim::Options c; c.step_all = true; conformance(c);
}

// ---- against the RTL's router ---------------------------------------------------------------------------------------

// ROUTERCONF_GEN=DIR writes the scripts, DIR/<name>.script, for the RTL to run (tests/routerconf/README.md); a
// generator, not a test of anything.
TEST(router_scripts_written_when_asked) {
    const char* dir = std::getenv("ROUTERCONF_GEN");
    if (!dir) return;
    unsigned seed = 0;
    for (const char* name : kRtlFixtures) {
        seed++;
        const unsigned tile = unsigned(std::atoi(name + 1));
        const std::string text = gen_script(tile, seed), path = std::string(dir) + "/" + name + ".script";
        FILE* o = std::fopen(path.c_str(), "w");
        CHECK(o && std::fwrite(text.data(), 1, text.size(), o) == text.size() && std::fclose(o) == 0);
    }
    std::fprintf(stderr, "router conformance: %u scripts written to %s\n", seed, dir);
}

void rtl_conformance(sim::Options opt, bool report = false) {
    const Hops h = hops();
    for (const char* name : kRtlFixtures) {
        bool ok1 = false, ok2 = false;
        const std::string script = portconf::read_command("gzip -dc " + kDirRtl + name + ".script.gz", &ok1);
        const std::string rtl = body(portconf::read_command("gzip -dc " + kDirRtl + name + ".obs.gz", &ok2));
        if (!ok1 || !ok2 || rtl.empty()) { ::test::fail(__FILE__, __LINE__, "the RTL's fixture %s missing", name); return; }
        const RScript s = parse_rtl_script(script);
        const std::vector<RLine> line = run_rtl_script(s, h, opt);
        if (!same(std::string(name) + " (the RTL's)", rtl_lines(s, line), rtl)) { ::test::fail(__FILE__, __LINE__, "%s differs from the RTL's router", name); return; }
        if (!report) continue;
        // what the script reached, so that a script that loads nothing is seen: the lines must hold each kind
        unsigned packets = 0, overflows = 0, held = 0, loopbacks = 0;
        for (uint64_t t = 1; t <= s.cycles; t++) {
            for (unsigned i = 0; i < P_N; i++) { packets += line[t].v[i]; held += line[t].held[i]; }
            overflows += line[t].overflow; loopbacks += line[t].loopback;
        }
        CHECK(packets > 1000); CHECK(overflows > 0); CHECK(loopbacks > 0);
        if (rtl_dma_inputs(s.tile)) CHECK(held > 0);
        std::fprintf(stderr, "router conformance: %s: equal to the RTL's router in each of %llu cycles: %zu arrivals, %u packets out, "
                     "%u cycles with a lost packet, %u loopback packets, %u cycles a DMA input was held back\n", name,
                     (unsigned long long)s.cycles, s.ev.size(), packets, overflows, loopbacks, held);
    }
}

TEST(router_conformance_with_the_rtl) { rtl_conformance({}, true); }

TEST(router_conformance_with_the_rtl_under_debug_options) {
    sim::Options a; a.shuffle_seed = 7; rtl_conformance(a);
    sim::Options b; b.order = sim::Options::Order::instance_major; rtl_conformance(b);
    sim::Options c; c.step_all = true; rtl_conformance(c);
}

// The routes of every router, as the RTL's router makes them (tests/routerconf/rtl/tables.txt, written by
// router_script_test.rs's `router_tables`): for tile 0 to 66, each input the router has, in the order local, the four
// links (a special tile's: its DMA inputs), the attach port; each src 0 to 66 and each dst 0 to 127: one packet a cycle
// at that input, and the outputs that hold it two cycles later, a bit each (the eject 1, west 2, east 4, north 8,
// south 16, the attach port 32), folded into an FNV-1a hash. The same packets go through the simulator's router here.
TEST(routes_equal_the_rtls) {
    const Hops h = hops();
    bool ok = false;
    const std::string rtl = body(portconf::read_command("cat " + kDirRtl + "tables.txt", &ok));
    CHECK(ok && !rtl.empty());
    std::string got;
    for (unsigned tile = 0; tile < 67; tile++) {
        RScript s;
        s.tile = tile;
        const unsigned dma = rtl_dma_inputs(tile);
        uint64_t t = 1;
        for (unsigned port = 0; port < P_N; port++) {
            if (port >= P_WEST && port <= P_SOUTH && tile >= 64 && !(dma >> (port - P_WEST) & 1)) continue;
            if (port == P_ATTACH && !rtl_attached(tile)) continue;
            for (unsigned src = 0; src < 67; src++)
                for (unsigned dst = 0; dst < 128; dst++, t++) s.ev.push_back({t, port, {t, lw::hdr(src, dst, 0, 0, 0, lw::SIZE_128), 0}});
        }
        s.cycles = t + 2;
        const std::vector<RLine> line = run_rtl_script(s, h);
        uint64_t x = 0xcbf29ce484222325ull;
        for (const REv& e : s.ev) {
            const RLine& l = line[e.t + 2];
            unsigned mask = 0;
            for (unsigned i = 0; i < P_N; i++) {
                if (!l.v[i]) continue;
                if (l.w[i][0] != e.t) { ::test::fail(__FILE__, __LINE__, "tile %u: output %u holds packet %llu in cycle %llu", tile, i, (unsigned long long)l.w[i][0], (unsigned long long)e.t + 2); return; }
                mask |= 1u << i;
            }
            for (unsigned i = 0; i < P_N; i++) if (l.held[i] || l.overflow) { ::test::fail(__FILE__, __LINE__, "tile %u: a loaded router", tile); return; }
            x ^= mask; x *= 0x100000001b3ull;
        }
        char b[48];
        std::snprintf(b, sizeof b, "%u %016" PRIx64 "\n", tile, x);
        got += b;
    }
    CHECK(same("the routes", got, rtl));
}
