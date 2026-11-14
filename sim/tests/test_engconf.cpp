// The engines' conformance, the C++ side (ref's side is oracle/ref/EngConf.java, the fixtures tests/engconf/
// made by oracle/ref/engconf.sh). The same script drives the AG (ag.h) and the gather engine (gather.h) among stand-ins:
// the block (the `dma` RegFile, the `start` Wire, endCycle's stores applied as ref's), the table store's STREAM and
// GROUP paths (TableStore.java:121-137) with the PTR view, the read queue's two client Holds (one take a cycle, the
// gather first), and the block's inputs. One observation line per cycle, in EngConf's format; equal to ref's, line for
// line. Under the debug options too (shuffle, instance-major, step-all).
//
//   ENGCONF_LONG=DIR test_engconf long     every DIR/*.script.gz against its .obs.gz (engconf.sh CYCLES DIR)
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "units/ag.h"
#include "units/gather.h"
#include "units/table_store.h"
#include "units/net.h"

using namespace sim;
using units::AddrGen;
using units::Gather;
namespace mem = units::mem;

namespace {

const std::string kDir = "tests/engconf/";
const char* kFixtures[] = {"t5-s1", "t0-s2", "t27-s3", "t63-s4"};

// ---------------------------------------------------------------------------------------------------- the script
struct Line { uint32_t rcfg = 0, base = 0; bool r = false, b = false, g = false, take = false; uint64_t gd = 0, gh = 0; };
struct Script {
    unsigned tile = 0, L = 1; uint64_t cycles = 0;
    uint32_t src[256] = {}, dest[256] = {}, count[256] = {}; uint64_t group[64] = {}; uint32_t ptr = 0, base = 0;
    std::vector<Line> lines;                      // lines[t - 1]
};
uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }
std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> v; size_t a = 0;
    for (size_t b; (b = s.find(c, a)) != std::string::npos; a = b + 1) v.push_back(s.substr(a, b - a));
    v.push_back(s.substr(a));
    return v;
}
Script parse(const std::string& text) {
    Script s;
    for (const std::string& ln : split(text, '\n')) {
        if (ln.empty()) continue;
        std::vector<std::string> f = split(ln, ' ');
        if (ln[0] == '#') {
            if (f.size() < 2) continue;
            if (f[1] == "tile") s.tile = unsigned(std::stoul(f[2]));
            else if (f[1] == "seed") { s.cycles = std::stoull(f[4]); s.L = unsigned(std::stoul(f[6])); }
            else if (f[1] == "s") { unsigned i = unsigned(std::stoul(f[2])); s.src[i] = uint32_t(hx(f[3])); s.dest[i] = uint32_t(hx(f[4])); s.count[i] = uint32_t(hx(f[5])); }
            else if (f[1] == "g") s.group[std::stoul(f[2])] = hx(f[3]);
            else if (f[1] == "ptr") s.ptr = uint32_t(hx(f[2]));
            else if (f[1] == "base") s.base = uint32_t(hx(f[2]));
            continue;
        }
        Line l;
        for (size_t i = 1; i < f.size(); i++) {
            const std::string& x = f[i];
            if (x[0] == 'R') { l.r = true; l.rcfg = uint32_t(hx(x.substr(2))); }
            else if (x[0] == 'B') { l.b = true; l.base = uint32_t(hx(x.substr(2))); }
            else if (x[0] == 'G') { auto w = split(x.substr(2), ','); l.g = true; l.gd = hx(w[0]); l.gh = hx(w[1]); }
            else if (x[0] == 'T') l.take = true;
        }
        s.lines.push_back(l);
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------- the stand-ins
// What the harness sets between cycles for the next one (endCycle(t - 1)'s effects, and cycle t's script).
struct Plan {
    bool write_cfg = false, write_base = false, pulse = false; uint32_t cfg = 0, base = 0;
    bool put_g = false; uint64_t gd = 0, gh = 0;
    bool take = false;
};
// The block (level 0: the shifted endCycle) and the director's gather queue.
struct Drv {
    RegFileOut dma; WireOut start; FifoOut gq;
    const Plan* plan; Word starts = 0; uint32_t base0;
    Drv(Ports& p, const Plan* pl, uint32_t b) : plan(pl), base0(b) { p.out(dma, "dma", units::DMA_REGS); p.out(start, "start"); p.out(gq, "gq", 3); }
    uint64_t digest() const { return hash_fields(starts); }
    Wake step(Ctx& c) {
        if (c.now() == 1) c.write(dma, units::DMA_R_RECORD_BASE, base0);
        if (plan->write_cfg) c.write(dma, units::DMA_R_CONFIG, plan->cfg);
        if (plan->write_base) c.write(dma, units::DMA_R_RECORD_BASE, plan->base);
        if (plan->pulse) c.drive(start, ++starts);
        if (plan->put_g) c.put(gq, plan->gd, plan->gh, 0);
        return c.next();
    }
};
// The table store is the (units::TableStore with the AG's ports and the PTR view). Its other ends: the core's table
// window (a store of PTR[me] at cycle 1, so the PTR view holds it from cycle 2, as ref's poke before cycle 1 does for
// every read the AG makes), the write engine's RMW Links (idle), the overflow event (a sink).
struct TsEnds {
    LinkOut window, req_we; LinkIn ret_we, ovf;
    const Script* s;
    TsEnds(Ports& p, const Script* sc) : s(sc) { p.out(window, "window", 2); p.out(req_we, "req_we", 3); p.in(ret_we, "ret_we", 3); p.in(ovf, "ovf", 0); }
    uint64_t digest() const { return 0; }
    Wake step(Ctx& c) {
        if (c.now() == 1) c.put(window, uint64_t(units::TBL_PTR) << 32 | s->tile, s->ptr);
        if (c.ready(ovf)) FATAL("engconf: a table store request FIFO overflowed at %llu", (unsigned long long)c.now());
        return Wake::none();
    }
};
// The read queue's clients 0 (gather) and 1 (AG): one take a cycle when the plan allows, client 0 first.
struct Srq {
    HoldIn ga, ag; const Plan* plan;
    bool ga_occupied = false;                     // the gather's Hold occupied at the end of t (its Reg to the block)
    Srq(Ports& p, const Plan* pl) : plan(pl) { p.in(ga, "ga", mem::kReadReqWidth); p.in(ag, "ag", mem::kReadReqWidth); }
    uint64_t digest() const { return hash_fields(ga_occupied); }
    Wake step(Ctx& c) {
        if (plan->take) { if (c.ready(ga)) c.take(ga); else if (c.ready(ag)) c.take(ag); }
        ga_occupied = c.ready(ga);
        return c.next();
    }
};
// The block's inputs from the engines (sinks).
struct Mon {
    RegIn status, gbusy; LinkIn drift, govf;
    uint64_t drifts = 0;
    explicit Mon(Ports& p) { p.in(status, "status", 1); p.in(gbusy, "gbusy", 1); p.in(drift, "drift", 0); p.in(govf, "govf", 0); }
    uint64_t digest() const { return hash_fields(drifts); }
    Wake step(Ctx& c) {
        if (c.ready(drift)) { drifts++; c.take(drift); }
        if (c.ready(govf)) c.take(govf);
        return Wake::none();
    }
};

// StateFp.Out
struct Out {
    uint64_t h = 0x6A09E667F3BCC908ull, n = 0;
    static uint64_t fmix(uint64_t k) { k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33; return k; }
    void w(uint64_t v) { h = fmix(h ^ v); n++; }
    void i(uint32_t v) { w(uint64_t(int64_t(int32_t(v)))); }   // a Java int, sign-extended
    uint64_t done() const { return fmix(h ^ n); }
};

std::string run(const Script& s, Options opt) {
    Plan plan;
    Config cfg(opt);
    auto& d = cfg.add<Drv>("blk", &plan, s.base);
    auto& a = cfg.add<AddrGen>("ag", s.tile, 2 * s.L + 2, true);
    auto& t = cfg.add<units::TableStore>("ts", true);
    auto& te = cfg.add<TsEnds>("ts.ends", &s);
    auto& g = cfg.add<Gather>("gather", 16u, s.tile, true);
    auto& q = cfg.add<Srq>("srq", &plan);
    auto& m = cfg.add<Mon>("mon");
    cfg.connect(d.dma, a.dma); cfg.connect(d.dma, g.dma); cfg.connect(d.start, a.start);
    cfg.connect(d.gq, g.in);
    cfg.link(a.stream_req, t.req_stream, s.L); cfg.link(a.group_req, t.req_group, s.L);
    cfg.link(t.ret_stream, a.stream_ret, s.L); cfg.link(t.ret_group, a.group_ret, s.L);
    cfg.connect(t.ptr_view, a.ptr);
    cfg.link(te.window, t.window, 1); cfg.link(te.req_we, t.req_we, s.L);
    cfg.link(t.ret_we, te.ret_we, s.L); cfg.link(t.ovf_ev, te.ovf, 1);
    cfg.connect(a.rd, q.ag); cfg.connect(g.rd, q.ga);
    cfg.connect(a.status, m.status); cfg.link(a.drift, m.drift, 1);
    cfg.connect(g.busy, m.gbusy); cfg.link(g.ovf, m.govf, 1);
    Sim sim = cfg.build();
    {                                                     // the tables, as ref's pokes before cycle 1
        units::TableMems& tm = sim.unit<units::TableStore>("ts").m;
        for (uint32_t i = 0; i < 256; i++) {
            tm.write(units::TBL_STREAM_SRC, i, s.src[i]); tm.write(units::TBL_STREAM_DEST, i, s.dest[i]);
            tm.write(units::TBL_STREAM_COUNT, i, s.count[i]);
        }
        for (uint32_t i = 0; i < 64; i++) { tm.write(units::TBL_GROUP, 2 * i, uint32_t(s.group[i])); tm.write(units::TBL_GROUP, 2 * i + 1, uint32_t(s.group[i] >> 32)); }
        tm.write(units::TBL_PTR, s.tile, s.ptr);
    }
    const AddrGen& A = sim.unit<AddrGen>("ag");
    const Gather& G = sim.unit<Gather>("gather");
    const Srq& Q = sim.unit<Srq>("srq");

    std::string out;
    out.reserve(s.lines.size() * 80);
    uint32_t cur_cfg = 0, cur_base = s.base;
    bool pulse_next = false;
    uint64_t last_a = 0, last_g = 0;
    char b[256];
    for (uint64_t c = 1; c <= s.lines.size(); c++) {
        const Line& l = s.lines[c - 1];
        plan.put_g = l.g; plan.gd = l.gd; plan.gh = l.gh; plan.take = l.take;
        sim.run(1);
        // endCycle(c): a pending start, then this cycle's stores; the plan for c + 1
        plan.write_cfg = plan.write_base = plan.pulse = false;
        bool pulse = false;
        const bool was_running = A.running;
        if (pulse_next) { pulse = true; pulse_next = false; plan.pulse = true; }
        else if (l.r && !was_running) { cur_cfg = l.rcfg; plan.write_cfg = true; plan.cfg = cur_cfg; pulse_next = true; }
        if (l.b) { cur_base = l.base & 0x1FFFFF; plan.write_base = true; plan.base = cur_base; }
        // the observation
        int n = std::snprintf(b, sizeof b, "%" PRIu64, c);
        const uint64_t na = A.requests + A.stall_cycles + A.cum_requests + A.cum_stall;
        if (na != last_a) { n += std::snprintf(b + n, sizeof b - n, " A:%" PRIx64 ",%" PRIx64, A.held[0], A.held[1]); last_a = na; }
        const uint64_t ng = G.reads + G.stall_cycles;
        if (ng != last_g) { n += std::snprintf(b + n, sizeof b - n, " g:%" PRIx64 ",%" PRIx64, G.held[0], G.held[1]); last_g = ng; }
        n += std::snprintf(b + n, sizeof b - n, " S:%d,%d", int(A.running || pulse), int(A.done && !pulse));
        Out o;
        o.i(AddrGen::kind(cur_cfg)); o.i(AddrGen::run_start(cur_cfg)); o.i(AddrGen::run_count(cur_cfg)); o.w(pulse);
        o.w(A.running); o.w(A.done); o.w(A.lead_in); o.w(A.finished_issuing); o.w(A.gather_checked);
        for (uint32_t v : {A.sq_out, A.cq_out, A.stream_discard, A.group_discard, A.s_reads, A.c_reads, A.rg, A.rr, A.rv, A.sl_run}) o.i(v);
        o.w(A.cur);
        for (uint32_t v : {A.c_src, A.c_dest, A.c_count, A.c_j, A.c_d, A.c_mode, A.c_size, A.c_gap, A.c_ctr, A.gap_left,
                           A.g, A.r, A.v, A.g_r, A.g_t, A.g_gap}) o.i(v);
        o.w(uint64_t(A.group_s)); o.w(uint64_t(A.rot_sched)); o.w(A.in_rot); o.w(A.stalled_since_group);
        o.i(A.sq_buf); for (uint32_t k = 0; k < A.sq_buf; k++) { const uint32_t j = (A.sq_head + k) % A.lookahead; o.w(A.sq[j]); o.w(A.sq1[j]); }
        o.i(A.cq_buf); for (uint32_t k = 0; k < A.cq_buf; k++) o.w(A.cq[(A.cq_head + k) % A.lookahead]);
        Out gq;
        gq.i(Gather::k(cur_cfg)); gq.i(Gather::base(cur_base)); gq.i(G.left); gq.i(G.ord); gq.i(G.i); gq.i(G.ctr); gq.w(G.wide);
        const bool busy = l.g || G.busy_out || Q.ga_occupied;
        n += std::snprintf(b + n, sizeof b - n, " a:%" PRIx64 " q:%" PRIx64 " b:%d d:%" PRIu64 " v:%" PRIu64 "\n", o.done(), gq.done(),
                           int(busy), A.drift_events, A.violations());
        out.append(b, size_t(n));
    }
    return out;
}

std::string body(const std::string& obs) {             // without the provenance lines
    size_t a = 0;
    while (a < obs.size() && obs[a] == '#') a = obs.find('\n', a) + 1;
    return obs.substr(a);
}
bool same(const std::string& name, const std::string& got, const std::string& want) {
    if (got == want) return true;
    size_t i = 0, line = 1;
    while (i < got.size() && i < want.size() && got[i] == want[i]) { if (got[i] == '\n') line++; i++; }
    const size_t a = got.rfind('\n', i == 0 ? 0 : i - 1), w = want.rfind('\n', i == 0 ? 0 : i - 1);
    const size_t ga = a == std::string::npos ? 0 : a + 1, wa = w == std::string::npos ? 0 : w + 1;
    std::fprintf(stderr, "%s: first difference at line %zu\n  ref: %s\n  ours: %s\n", name.c_str(), line,
                 want.substr(wa, want.find('\n', wa) - wa).c_str(), got.substr(ga, got.find('\n', ga) - ga).c_str());
    return false;
}
bool one(const std::string& path, Options opt) {
    bool ok1 = false, ok2 = false;
    std::string script = portconf::read_command("gzip -dc " + path + ".script.gz", &ok1);
    std::string ref = body(portconf::read_command("gzip -dc " + path + ".obs.gz", &ok2));
    if (!ok1 || !ok2 || ref.empty()) { std::fprintf(stderr, "engconf: %s missing\n", path.c_str()); return false; }
    return same(path, run(parse(script), opt), ref);
}

}  // namespace

TEST(engconf_equal_to_ref) {
    for (const char* f : kFixtures) CHECK(one(kDir + f, {}));
}

TEST(engconf_under_debug_options) {
    Options a; a.shuffle_seed = 11;
    Options b; b.order = Options::Order::instance_major;
    Options c; c.step_all = true;
    for (const char* f : kFixtures) { CHECK(one(kDir + f, a)); CHECK(one(kDir + f, b)); CHECK(one(kDir + f, c)); }
}

TEST(engconf_long) {
    const char* dir = std::getenv("ENGCONF_LONG");
    if (!dir) return;                                     // on demand: ENGCONF_LONG=DIR (engconf.sh CYCLES DIR)
    std::vector<std::string> names;
    if (DIR* d = opendir(dir)) {
        for (dirent* e; (e = readdir(d)) != nullptr; ) {
            std::string f = e->d_name;
            if (f.size() > 10 && f.compare(f.size() - 10, 10, ".script.gz") == 0) names.push_back(f.substr(0, f.size() - 10));
        }
        closedir(d);
    }
    CHECK(!names.empty());
    unsigned n = 0;
    for (const std::string& f : names) { CHECK(one(std::string(dir) + "/" + f, {})); n++; }
    std::fprintf(stderr, "engconf: %u long runs equal to ref\n", n);
}
