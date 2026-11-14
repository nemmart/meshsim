// C&C's model against ref's CnC, Cnc1Peripheral and C&C 1's endpoint, cycle by cycle:
// oracle/ref/CncConf.java runs them in a Chip whose tiles it plays (READY at boot and in answer to every GO, extra
// pulses, error lines up and down, packets into C&C's receive FIFO, tile 0's router stalling on C&C's send register);
// here the same script drives the Cnc unit, the 8 row collectors and 64 stand-in tiles on the engine, and every cycle's
// observations must be equal: the packet tile 0's router took, GO, the calls, and StateFp's "cnc" and "cc1p" streams
// (statefp_g2: C&C's state plus the line words in flight). Also the endpoints' and the request bus's streams once.
// Fixtures: tests/cncconf/, regenerated and checked by oracle/ref/cncconf.sh --check.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "statefp_g2.h"
#include "units/cnc.h"

using namespace units;
using sim::Ctx;
using sim::Ports;
using sim::Wake;
using sim::Word;

namespace {

const std::string kDir = "tests/cncconf/";

uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull; x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull; x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
unsigned delay(uint64_t seed, unsigned k, unsigned n) { return 1 + unsigned(splitmix(seed * 1000003ull + 64ull * n + k) % 40); }

// ---- the script ----------------------------------------------------------------------------------------------------
struct TileEv { uint64_t e; uint8_t kind; uint8_t level; };   // kind 0 READY, 1 error level
struct Script {
    uint64_t seed = 0, cycles = 0;
    CncProgram prog;
    std::vector<TileEv> tile[64];
    std::vector<std::pair<uint64_t, std::vector<Word>>> rx;
    std::vector<uint64_t> stalls;
    uint64_t release_at = 0;                   // 0: none
};

uint32_t hexu(const std::string& s) { return uint32_t(std::strtoull(s.c_str(), nullptr, 16)); }

std::vector<std::string> split(const std::string& l) {
    std::vector<std::string> f;
    size_t p = 0;
    while (p <= l.size()) { size_t e = l.find(' ', p); if (e == std::string::npos) e = l.size(); f.push_back(l.substr(p, e - p)); p = e + 1; }
    return f;
}

void parse(const std::string& text, Script& s) {
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.rfind("# seed ", 0) == 0) { auto f = split(line); s.seed = std::stoull(f[2]); s.cycles = std::stoull(f[4]); continue; }
        if (line.empty() || line[0] == '#') continue;
        auto f = split(line);
        if (f[0] == "P") {
            std::vector<uint32_t> w;
            for (size_t i = 4; i < f.size(); i++) w.push_back(hexu(f[i]));
            if (f[1] == "load") s.prog.load_words(hexu(f[2]), hexu(f[3]), w.data(), w.size());
            else if (f[1] == "cmd") s.prog.command(hexu(f[2]), hexu(f[3]), w.data(), w.size());
            else if (f[1] == "go") s.prog.go();
            else if (f[1] == "run") s.prog.run_when_ready(uint32_t(std::stoul(f[2])));
            else if (f[1] == "wait") s.prog.wait_cycles(uint32_t(std::stoul(f[2])));
            else if (f[1] == "allready") s.prog.at_all_ready(uint32_t(std::stoul(f[2])));
            else if (f[1] == "call") s.prog.call(uint32_t(std::stoul(f[2])));
            else if (f[1] == "release") { s.prog.release(); s.release_at = std::stoull(f[2]); }
        } else if (f[0] == "R") s.tile[std::stoul(f[1])].push_back({std::stoull(f[2]), 0, 0});
        else if (f[0] == "E") s.tile[std::stoul(f[1])].push_back({std::stoull(f[2]), 1, uint8_t(std::stoul(f[3]))});
        else if (f[0] == "X") {
            std::vector<Word> w;
            size_t q = 0;
            for (int k = 0; k < 3; k++) { size_t c = f[2].find(',', q); w.push_back(std::strtoull(f[2].substr(q, c - q).c_str(), nullptr, 16)); q = c + 1; }
            s.rx.push_back({std::stoull(f[1]), w});
        } else if (f[0] == "K") s.stalls.push_back(std::stoull(f[1]));
    }
    s.prog.release();
    for (auto& v : s.tile) std::stable_sort(v.begin(), v.end(), [](const TileEv& a, const TileEv& b) { return a.e < b.e; });
    std::stable_sort(s.rx.begin(), s.rx.end(), [](auto& a, auto& b) { return a.first < b.first; });
}

// ---- the stand-ins -------------------------------------------------------------------------------------------------
// A tile as C&C sees it: its register block's GO input and its line output (READY rises, error-line changes). Its
// line word for the end of cycle e is put at e + 1, as the block does at its shifted end of cycle.
struct TileStub {
    sim::LinkIn go;
    sim::LinkOut line;
    uint8_t k;
    uint64_t seed;
    const TileEv* ev; uint32_t n, i = 0;
    uint64_t dyn[16] = {}; uint32_t ndyn = 0;   // READY edges answering GOs, ascending
    uint32_t gos = 0;
    uint8_t level = 0;
    TileStub(Ports& p, unsigned tile, uint64_t s, const TileEv* e, uint32_t count) : k(uint8_t(tile)), seed(s), ev(e), n(count) {
        p.in(go, "go", 0); p.out(line, "line", 1);
    }
    // the line word due at the end of cycle e (fp::LineEdge)
    fp::LineEdge edge(uint64_t e) const {
        fp::LineEdge le;
        uint8_t lv = level;
        for (uint32_t j = i; j < n && ev[j].e <= e; j++)
            if (ev[j].e == e) { if (ev[j].kind == 0) le.ready = true; else if (ev[j].level != lv) { lv = ev[j].level; le.put = true; } }
        for (uint32_t j = 0; j < ndyn; j++) if (dyn[j] == e) le.ready = true;
        le.put |= le.ready;
        le.level = lv;
        return le;
    }
    Wake step(Ctx& c) {
        const uint64_t u = c.now();
        if (c.ready(go)) {
            c.take(go);
            gos++;
            const uint64_t e = (u - 1) + delay(seed, k, gos);
            uint32_t j = ndyn++;
            while (j > 0 && dyn[j - 1] > e) { dyn[j] = dyn[j - 1]; j--; }
            dyn[j] = e;
        }
        if (u >= 1) {
            const fp::LineEdge le = edge(u - 1);
            while (i < n && ev[i].e <= u - 1) { if (ev[i].kind == 1) level = ev[i].level; i++; }
            uint32_t m = 0;
            for (uint32_t j = 0; j < ndyn; j++) if (dyn[j] > u - 1) dyn[m++] = dyn[j];
            ndyn = m;
            if (le.put) c.put(line, Word(le.ready) | Word(le.level) << 1);
        }
        uint64_t nx = UINT64_MAX;
        if (i < n) nx = ev[i].e;
        if (ndyn && dyn[0] < nx) nx = dyn[0];
        return nx == UINT64_MAX ? Wake::none() : Wake::at_cycle(nx + 1);
    }
    uint64_t digest() const { return sim::hash_fields(k, i, gos, level, ndyn, dyn); }
};

fp::LineEdge stub_edge(const void* ctx, unsigned k, uint64_t c) {
    const TileStub* const* t = static_cast<const TileStub* const*>(ctx);
    return t[k]->edge(c);
}

// Tile 0's router as C&C's endpoint meets it: takes the send register every cycle but the stalls; puts the script's
// packets on C&C's receive FIFO.
struct Router0 {
    sim::HoldIn from_cnc;
    sim::FifoOut to_cnc;
    const uint64_t* stall; uint32_t ns, si = 0;
    const std::pair<uint64_t, std::vector<Word>>* rx; uint32_t nr, ri = 0;
    uint64_t took_at = 0; Word took[3] = {};
    Router0(Ports& p, const uint64_t* s, uint32_t n1, const std::pair<uint64_t, std::vector<Word>>* r, uint32_t n2)
        : stall(s), ns(n1), rx(r), nr(n2) { p.in(from_cnc, "from_cnc", 3); p.out(to_cnc, "to_cnc", 3); }
    Wake step(Ctx& c) {
        const uint64_t t = c.now();
        while (si < ns && stall[si] < t) si++;
        const bool stalled = si < ns && stall[si] == t;
        if (!stalled && c.ready(from_cnc)) { took_at = t; std::memcpy(took, c.peek(from_cnc), sizeof took); c.take(from_cnc); }
        for (; ri < nr && rx[ri].first == t; ri++) c.put(to_cnc, rx[ri].second[0], rx[ri].second[1], rx[ri].second[2]);
        if (stalled && c.ready(from_cnc)) return c.next();
        return ri < nr ? Wake::at_cycle(rx[ri].first) : Wake::none();
    }
    uint64_t digest() const { return sim::hash_fields(si, ri, took_at); }
};

// Tiles 1 and 2's routers, for C&C 2 and 3's endpoints: idle.
struct IdleEnds {
    sim::HoldIn h[2];
    sim::FifoOut f[2];
    IdleEnds(Ports& p) { for (int i = 0; i < 2; i++) { p.in(h[i], "h", 3); p.out(f[i], "f", 3); } }
    Wake step(Ctx&) { return Wake::none(); }
    uint64_t digest() const { return 0; }
};

std::string last_stats;   // the last run's statistics (printed with CNCCONF_DUMP)

std::string run(const Script& s, sim::Options opt, std::string* extra = nullptr) {
    NetConfig nc;
    uint8_t skew[64];
    start_skews(nc, skew);
    opt.observe_arrivals = true;
    sim::Config cfg(opt);
    auto& cc = cfg.add<Cnc>("cnc", nc, s.prog);
    RowCollector* rows[8];
    for (unsigned r = 0; r < 8; r++) { rows[r] = &cfg.add<RowCollector>("row" + std::to_string(r), r); cfg.link(rows[r]->out, cc.rows[r], 1); }
    for (unsigned k = 0; k < 64; k++) {
        auto& t = cfg.add<TileStub>("t" + std::to_string(k), k, s.seed, s.tile[k].data(), uint32_t(s.tile[k].size()));
        cfg.link(cc.go[k], t.go, nc.go_latency + skew[k] + 1);
        cfg.link(t.line, rows[k / 8]->line[k % 8], nc.ready_latency - 2);
    }
    auto& r0 = cfg.add<Router0>("router0", s.stalls.data(), uint32_t(s.stalls.size()), s.rx.data(), uint32_t(s.rx.size()));
    cfg.connect(cc.send, r0.from_cnc);
    cfg.connect(r0.to_cnc, cc.rx);
    auto& e2 = cfg.add<Endpoint>("ep2", 65u, nc);
    auto& e3 = cfg.add<Endpoint>("ep3", 66u, nc);
    auto& idle = cfg.add<IdleEnds>("idle");
    cfg.connect(e2.send, idle.h[0]); cfg.connect(e3.send, idle.h[1]);
    cfg.connect(idle.f[0], e2.rx); cfg.connect(idle.f[1], e3.rx);
    sim::Sim sim = cfg.build();

    const TileStub* stubs[64];
    for (unsigned k = 0; k < 64; k++) stubs[k] = &sim.unit<TileStub>("t" + std::to_string(k));
    fp::G2Chip g;
    g.cnc = &sim.unit<Cnc>("cnc");
    for (unsigned r = 0; r < 8; r++) g.rows[r] = &sim.unit<RowCollector>("row" + std::to_string(r));
    g.ep[0] = &sim.unit<Endpoint>("ep2"); g.ep[1] = &sim.unit<Endpoint>("ep3");
    g.ready_latency = nc.ready_latency;
    g.edge = stub_edge; g.edge_ctx = stubs;

    std::string out;
    for (uint64_t t = 1; t <= s.cycles; t++) {
        sim.run(1);
        const Router0& r = sim.unit<Router0>("router0");
        const Cnc& c = *g.cnc;
        char b[160];
        std::snprintf(b, sizeof b, "%" PRIu64, t);
        std::string line = b;
        if (r.took_at == t) { std::snprintf(b, sizeof b, " T:%" PRIx64 ",%" PRIx64 ",%" PRIx64, r.took[0], r.took[1], r.took[2]); line += b; }
        if (c.last_go_at == int64_t(t)) line += " G";
        for (unsigned id = 0; id < CncProgram::kCalls; id++) if (c.call_at[id] == int64_t(t)) { line += " C:" + std::to_string(id); }
        fp::FpCtx x{t, &sim};
        fp::FpOut a, bb;
        fp::w_cnc(&g, a, x);
        fp::w_cc1p(&g, bb, x);
        std::snprintf(b, sizeof b, " H:%" PRIx64 ",%" PRIx64, a.done(), bb.done());
        out += line + b + "\n";
        if (t == s.release_at) { sim.unit<Cnc>("cnc").release_next(); sim.wake("cnc"); }   // the driver, between cycles
    }
    {
        const Cnc& c = *g.cnc;
        char b[256];
        std::snprintf(b, sizeof b, "GOs %" PRIu64 ", packets %" PRIu64 ", refusals %" PRIu64 ", rx taken %" PRIu64 ", error bit %d, steps left %u, repulses %" PRIu64 ", stepped %" PRIu64 " of %" PRIu64 " cycles",
                      c.gos, c.packets_sent, c.refusals, c.rx_taken, int(c.error_bit), c.steps_queued(), c.ready_repulses, c.stepped, s.cycles);
        last_stats = b;
    }
    if (extra) {
        fp::FpCtx x{s.cycles, &sim};
        fp::FpOut e, q;
        fp::w_ep(&g, e, x); fp::w_reqbus(&g, q, x);
        char b[64];
        std::snprintf(b, sizeof b, "%" PRIx64 " %" PRIx64, e.done(), q.done());
        *extra = b;
    }
    return out;
}

std::string body(const std::string& f) {
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

const char* kFixtures[] = {"s1", "s2", "s3"};

void conformance(sim::Options opt) {
    for (const char* name : kFixtures) {
        bool ok1 = false, ok2 = false;
        std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
        std::string ref = body(portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2));
        if (!ok1 || !ok2 || ref.empty()) { ::test::fail(__FILE__, __LINE__, "fixture %s missing", name); return; }
        Script s;
        parse(script, s);
        std::string got = run(s, opt);
        if (const char* d = std::getenv("CNCCONF_DUMP")) {          // the C++ side's lines, for diffing
            FILE* f = std::fopen((std::string(d) + "/" + name + ".cpp.obs").c_str(), "w");
            if (f) { std::fputs(got.c_str(), f); std::fclose(f); }
        }
        if (!same(name, got, ref)) { ::test::fail(__FILE__, __LINE__, "%s differs from ref", name); return; }
        if (std::getenv("CNCCONF_DUMP")) std::fprintf(stderr, "%s: %s\n", name, last_stats.c_str());
    }
}

}  // namespace

TEST(cnc_conformance_with_ref) { conformance({}); }

TEST(cnc_conformance_under_debug_options) {
    sim::Options a; a.shuffle_seed = 3; conformance(a);
    sim::Options b; b.order = sim::Options::Order::instance_major; conformance(b);
    sim::Options c; c.step_all = true; conformance(c);
}

TEST(cnc_idle_endpoints_and_request_bus_streams) {
    // ref's empty receive FIFOs and idle request bus (StateFp "ep", "reqbus"), as hashed by tests/cncconf/idle.txt.
    bool ok1 = false, ok2 = false;
    std::string script = portconf::read_command("gzip -dc " + kDir + "s1.script.gz", &ok1);
    std::string want = portconf::read_command("cat " + kDir + "idle.txt", &ok2);
    CHECK(ok1 && ok2);
    Script s;
    parse(script, s);
    s.cycles = 5;
    std::string got;
    run(s, {}, &got);
    CHECK(got + "\n" == want);
}
