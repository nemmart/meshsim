// The router against the RTL's router (Snarky One's block `router`; tests/routerconf/README.md says what each fixture is
// and how it is made again):
//   - against the RTL: scripts of random many-to-one load on one router (a compute tile's, each attach tile's, each
//     special tile's with its DMA inputs), and what the RTL's router did with each under Verilator, a line a cycle:
//     every output's register, the eject's loopback mark, the overflow pulse, each DMA input offering and not ready.
//     The same script drives the C++ router on the engine and must give the same lines, under the debug options too
//     (shuffle, both orders, step-all). And every router's routes, as a hash of the outputs each (input, src, dst)
//     goes to, equal to the RTL's.
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

Hops hops() { return load_hops("data/hops.txt"); }

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
