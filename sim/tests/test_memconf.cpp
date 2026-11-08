// test_memconf.cpp (WORKER11, P2d): the memory path against ref, cycle by cycle (tests/ref/MemConf.java). G3's units
// (the injection unit with its read queue, the big SRAM, the write engine, the table store, HIST) driven by the same
// seeded script as ref's, with a stand playing the GATHER engine's and the AG's read holds, the read port's queue, the
// director's we-in FIFO, the controller's window stores, the AG's STREAM/GROUP requests, the core's send hold and the
// router (it takes every injected packet and frees each loopback token, one a cycle). Every cycle: the packet
// injected, the table returns, the free, and the canonical streams of srq, inj, sram, sram.data, we, ts and hist, each
// line equal to ref's.
//
//   make check         the fixtures in tests/memconf/ (tests/ref/memconf.sh --fixtures), also under the debug options
//   MEMCONF_LONG=DIR   every DIR/*.script.gz against its .obs.gz (memconf.sh CYCLES DIR): the long runs
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "units/big_sram.h"
#include "units/inject.h"
#include "units/table_store.h"
#include "units/write_engine.h"

using namespace units;

namespace {

const std::string kDir = "tests/memconf/";
uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }
uint32_t init(uint32_t c) { uint32_t x = c * 0x9E3779B1u; return x ^ (x >> 15); }

struct Op { char k; uint64_t w[3]; };
struct Script {
    unsigned tile = 0;
    uint32_t dma = 0, base = 0;
    unsigned depth = 8;
    bool comb = false;
    std::vector<std::vector<Op>> at;     // at[t]: the ops of cycle t (1..N)
};
Script parse(const std::string& text) {
    Script s;
    s.at.resize(1);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.rfind("# tile ", 0) == 0) s.tile = unsigned(std::atoi(line.c_str() + 7));
        else if (line.rfind("# cfg ", 0) == 0) {
            char d[32], b[32]; unsigned dep = 0, comb = 0;
            if (std::sscanf(line.c_str() + 6, "%31s %31s %u %u", d, b, &dep, &comb) != 4) FATAL("memconf: bad cfg line");
            s.dma = uint32_t(hx(d)); s.base = uint32_t(hx(b)); s.depth = dep; s.comb = comb != 0;
        }
        if (line.empty() || line[0] == '#') continue;
        std::vector<Op> ops;
        size_t q = line.find(' ');
        const uint64_t t = hx(line.substr(0, q));
        if (t != s.at.size()) FATAL("memconf: cycle %llu out of order", (unsigned long long)t);
        while (q != std::string::npos) {
            size_t r = line.find(' ', q + 1);
            std::string tok = line.substr(q + 1, r == std::string::npos ? std::string::npos : r - q - 1);
            q = r;
            Op o{tok[0], {0, 0, 0}};
            size_t c = tok.find(':') + 1;
            for (unsigned i = 0; i < 3 && c != 0 && c <= tok.size(); i++) {
                size_t comma = tok.find(',', c);
                o.w[i] = hx(tok.substr(c, comma == std::string::npos ? std::string::npos : comma - c));
                if (comma == std::string::npos) break;
                c = comma + 1;
            }
            ops.push_back(o);
        }
        s.at.push_back(std::move(ops));
    }
    return s;
}

// What the stand saw, by the cycle ref's line names (a put at t, seen here at t + L).
struct Seen { bool i = false, sr = false, gr = false, f = false; uint64_t iw[3] = {}, srw[3] = {}, grw[3] = {}; };

struct Stand {
    sim::HoldOut g_rd, a_rd, send;
    sim::FifoOut rp, we_in;
    sim::LinkOut window, hist_w, stream_req, group_req, loop_left;
    sim::LinkIn stream_ret, group_ret, to_router, ovf[3];
    sim::RegIn send_busy, gsb;
    sim::CountIn rp_inj;
    sim::RegFileOut dma;
    sim::RegFileIn ptr, hist;
    const Script* s;
    unsigned L;
    uint64_t gl[2] = {}, al[2] = {}, xl[3] = {};
    std::deque<uint64_t> frees;
    std::vector<Seen>* seen;
    uint64_t overflow_events = 0;
    Stand(sim::Ports& p, const Script* sc, unsigned lat, std::vector<Seen>* sn) : s(sc), L(lat), seen(sn) {
        p.out(g_rd, "g_rd", 2); p.out(a_rd, "a_rd", 2); p.out(send, "send", 3);
        p.out(rp, "rp", 2); p.out(we_in, "we_in", 3);
        p.out(window, "window", 2); p.out(hist_w, "hist_w", 2); p.out(stream_req, "stream_req", 3); p.out(group_req, "group_req", 3);
        p.out(loop_left, "loop_left", 0);
        p.in(stream_ret, "stream_ret", 3); p.in(group_ret, "group_ret", 3); p.in(to_router, "to_router", 3);
        for (auto& o : ovf) p.in(o, "ovf", 0);
        p.in(send_busy, "send_busy", 1); p.in(gsb, "gsb", 1); p.in(rp_inj, "rp_inj");
        p.out(dma, "dma", DMA_REGS); p.in(ptr, "ptr"); p.in(hist, "hist");
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (t == 1) { c.write(dma, DMA_R_CONFIG, s->dma); c.write(dma, DMA_R_RECORD_BASE, s->base); }
        if (c.ready(to_router)) {                                // the injection's put of t - 1
            const sim::Word* w = c.peek(to_router);
            Seen& x = (*seen)[t - 1];
            x.i = true; x.iw[0] = w[0]; x.iw[1] = w[1]; x.iw[2] = w[2];
            if (w[1] & lw::TAG_TOKEN) frees.push_back(t + (w[1] >> 22 & 127));
            c.take(to_router);
        }
        if (c.ready(stream_ret)) { const sim::Word* w = c.peek(stream_ret); Seen& x = (*seen)[t - L]; x.sr = true; for (int k = 0; k < 3; k++) x.srw[k] = w[k]; c.take(stream_ret); }
        if (c.ready(group_ret)) { const sim::Word* w = c.peek(group_ret); Seen& x = (*seen)[t - L]; x.gr = true; for (int k = 0; k < 3; k++) x.grw[k] = w[k]; c.take(group_ret); }
        for (auto& o : ovf) if (c.ready(o)) { overflow_events++; c.take(o); }   // drops show in the streams (the FIFOs)
        if (!frees.empty() && frees.front() <= t) { frees.pop_front(); c.put(loop_left); (*seen)[t].f = true; }
        const Op *g = nullptr, *a = nullptr, *xs = nullptr;
        if (t < s->at.size())
            for (const Op& o : s->at[t]) {
                switch (o.k) {
                case 'G': g = &o; break;
                case 'A': a = &o; break;
                case 'X': xs = &o; break;
                case 'R': c.put(rp, o.w[0], o.w[1]); break;
                case 'W': c.put(we_in, o.w[0], o.w[1], o.w[2]); break;
                case 'P': c.put(window, sim::Word(TBL_PTR) << 32 | o.w[0], o.w[1] & 0x1FFFFF); break;
                case 'H': c.put(hist_w, o.w[0], o.w[1] & 0x1FFFF); break;
                case 'S': c.put(stream_req, 1, mem::IDX_STREAM + o.w[0], 0); break;
                case 'Q': c.put(group_req, 1, mem::IDX_GROUP + 2 * o.w[0], 0); break;
                default: FATAL("memconf: op %c", o.k);
                }
            }
        if (c.refused(g_rd)) c.put(g_rd, gl[0], gl[1]); else if (g) { gl[0] = g->w[0]; gl[1] = g->w[1]; c.put(g_rd, gl[0], gl[1]); }
        if (c.refused(a_rd)) c.put(a_rd, al[0], al[1]); else if (a) { al[0] = a->w[0]; al[1] = a->w[1]; c.put(a_rd, al[0], al[1]); }
        if (c.refused(send)) c.put(send, xl[0], xl[1], xl[2]);
        else if (xs) { xl[0] = xs->w[0]; xl[1] = xs->w[1]; xl[2] = xs->w[2]; c.put(send, xl[0], xl[1], xl[2]); }
        return c.next();
    }
    uint64_t digest() const { return frees.size(); }
};

std::string h(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%" PRIx64, v); return b; }

std::string run(const Script& s, sim::Options opt) {
    const unsigned L = 1;                                        // tsEngineLatency (ref's default)
    const uint64_t N = s.at.size() - 1;
    std::vector<Seen> seen(N + 2);
    SramBlocks blocks;
    NetConfig nc;
    ReadSideConfig rc; rc.present = true; rc.gather = rc.ag = true;
    SramConfig sc; sc.area_queue_depth = s.depth; sc.write_combining = s.comb;
    opt.observe_arrivals = true;
    sim::Config cfg(opt);
    const std::string t = "t" + std::to_string(s.tile) + ".";
    auto& st = cfg.add<Stand>("stand", &s, L, &seen);
    auto& in = cfg.add<Injection>(t + "inj", s.tile, nc, rc);
    auto& sr = cfg.add<BigSram>(t + "sram", sc, blocks.block(s.tile, sc.mem_chunks), true);
    auto& we = cfg.add<WriteEngine>(t + "we", 4u, L, sc.mem_chunks);
    auto& ts = cfg.add<TableStore>(t + "ts", true);
    auto& hs = cfg.add<HistStore>(t + "hist");
    cfg.connect(st.g_rd, in.rs.client_hold[InjReadSide::GATHER]);
    cfg.connect(st.a_rd, in.rs.client_hold[InjReadSide::AG]);
    cfg.connect(st.rp, in.rs.rp_q);
    cfg.connect(st.send, in.send);
    cfg.link(st.loop_left, in.loop_left, 1);
    cfg.link(in.to_router, st.to_router, 1);
    cfg.connect(in.send_busy, st.send_busy);
    cfg.connect(in.rs.gather_srq_busy, st.gsb);
    cfg.connect(in.rs.rp_injected, st.rp_inj);
    cfg.connect(in.rs.sram_rd, sr.rd);
    for (unsigned a = 0; a < 16; a++) cfg.link(sr.ret[a], in.rs.ret[a], 2);
    cfg.connect(st.we_in, we.in);
    cfg.connect(st.dma, we.dma);
    cfg.link(we.ts_req, ts.req_we, L);
    cfg.link(ts.ret_we, we.ts_ret, L);
    cfg.link(we.hist_req, hs.req, L);
    cfg.connect(we.to_sram, sr.wr);
    cfg.link(st.window, ts.window, 1);
    cfg.link(st.hist_w, hs.window, 1);
    cfg.link(st.stream_req, ts.req_stream, L);
    cfg.link(st.group_req, ts.req_group, L);
    cfg.link(ts.ret_stream, st.stream_ret, L);
    cfg.link(ts.ret_group, st.group_ret, L);
    cfg.connect(ts.ptr_view, st.ptr);
    cfg.connect(hs.out, st.hist);
    cfg.link(we.ovf_ev, st.ovf[0], 1);
    cfg.link(ts.ovf_ev, st.ovf[1], 1);
    cfg.link(hs.ovf_ev, st.ovf[2], 1);
    cfg.missed_wake_after(t + "we", 1u << 10);
    cfg.missed_wake_after(t + "inj", 1u << 20);
    sim::Sim m = cfg.build();
    // the contents before cycle 1, as ref's driver pokes them
    BigSram& bs = m.unit<BigSram>(t + "sram");
    for (uint32_t c = 0; c < 8u << 17; c += 4) if ((c & 0x1FFFF) < 1024) for (uint32_t j = 0; j < 4; j++) bs.poke(c + j, init(c + j));
    TableStore& tsu = m.unit<TableStore>(t + "ts");
    for (uint32_t i = 0; i < 256; i++) {
        tsu.m.write(TBL_STREAM_SRC, i, init(0x40000 + i)); tsu.m.write(TBL_STREAM_DEST, i, init(0x50000 + i));
        tsu.m.write(TBL_STREAM_COUNT, i, init(0x60000 + i));
    }
    for (uint32_t w = 0; w < 128; w++) tsu.m.write(TBL_GROUP, w, init(0x70000 + w));
    // the canonical streams (the writers G1's and G2's chip uses)
    RegBlockState ctl; ctl.dma_config = s.dma; ctl.record_base = s.base;
    fp::G1Tile g1;
    g1.tile = s.tile; g1.sim = &m; g1.ctl = &ctl;
    g1.uwe = &m.unit<WriteEngine>(t + "we"); g1.usram = &bs; g1.uts = &tsu; g1.ts = &tsu.m;
    g1.uhist = &m.unit<HistStore>(t + "hist"); g1.hist = &g1.uhist->m;
    fp::G2Tile g2;
    g2.tile = s.tile; g2.inj = &m.unit<Injection>(t + "inj");
    fp::FpRegistry reg;
    reg.add("srq", fp::w_srq, &g2); reg.add("inj", fp::w_inj, &g2);
    reg.add("sram", fp::w_sram, &g1); reg.add("sram.data", fp::w_sram_data, &g1);
    reg.add("we", fp::w_we, &g1); reg.add("ts", fp::w_ts, &g1); reg.add("hist", fp::w_hist, &g1);
    static const char* const kUnits[] = {"srq", "inj", "sram", "sram.data", "we", "ts", "hist"};
    std::vector<std::string> u(N + 1);
    for (uint64_t c = 1; c <= N; c++) {
        m.run(1);
        std::string x;
        for (unsigned k = 0; k < 7; k++) x += (k ? "," : "") + h(reg.hash(kUnits[k], fp::FpCtx{c, &m}));
        u[c] = x;
    }
    m.run(L > 1 ? L : 1);                                       // the last cycle's outputs reach the stand
    if (std::getenv("MEMCONF_STATS")) {                         // what the script reached (NOTES.md's coverage)
        const Injection& j = m.unit<Injection>(t + "inj");
        const WriteEngine& w = m.unit<WriteEngine>(t + "we");
        const HistStore& hh = m.unit<HistStore>(t + "hist");
        std::fprintf(stderr, "memconf t%u: issued %" PRIu64 "/%" PRIu64 "/%" PRIu64 ", srq stalls %" PRIu64 ", window full %" PRIu64
                     ", out of range %" PRIu64 ", packets %" PRIu64 "; sram reads %" PRIu64 " writes %" PRIu64 " combined %" PRIu64
                     ", read stalls %" PRIu64 ", write stalls %" PRIu64 "; we taken %" PRIu64 " written %" PRIu64 " stalls %" PRIu64
                     " aug %" PRIu64 "; ptr rmws %" PRIu64 " lost %" PRIu64 "; hist rmws %" PRIu64 " lost %" PRIu64 "; overflow events %" PRIu64 "\n",
                     s.tile, j.rs.issued[0], j.rs.issued[1], j.rs.issued[2], j.rs.stall_cycles, j.rs.window_full_cycles, j.rs.out_of_range,
                     j.rs.packets, bs.reads_served, bs.writes_served, bs.combined, bs.read_stall_cycles, bs.write_stall_cycles, w.taken,
                     w.written, w.stall_cycles, w.augmented, tsu.rmws, tsu.pipe.ctrl_writes_lost, hh.rmws, hh.pipe.ctrl_writes_lost,
                     m.unit<Stand>("stand").overflow_events);
    }
    std::string out;
    for (uint64_t c = 1; c <= N; c++) {
        const Seen& x = seen[c];
        out += h(c);
        if (x.i) out += " I:" + h(x.iw[0]) + "," + h(x.iw[1]) + "," + h(x.iw[2]);
        if (x.sr) out += " SR:" + h(x.srw[0]) + "," + h(x.srw[1]) + "," + h(x.srw[2]);
        if (x.gr) out += " GR:" + h(x.grw[0]) + "," + h(x.grw[1]) + "," + h(x.grw[2]);
        if (x.f) out += " F";
        out += " U:" + u[c] + "\n";
    }
    return out;
}

std::string body(const std::string& s) {
    std::string r;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        if (s[p] != '#') r += s.substr(p, e - p) + "\n";
        p = e + 1;
    }
    return r;
}
bool same(const char* name, const std::string& got, const std::string& want) {
    if (got == want) return true;
    size_t a = 0, line = 1;
    while (a < got.size() && a < want.size() && got[a] == want[a]) { if (got[a] == '\n') line++; a++; }
    size_t b = want.rfind('\n', a); b = b == std::string::npos ? 0 : b + 1;
    size_t g = got.rfind('\n', a); g = g == std::string::npos ? 0 : g + 1;
    std::fprintf(stderr, "%s: line %zu differs\n  ref:  %s\n  ours: %s\n", name, line, want.substr(b, want.find('\n', b) - b).c_str(),
                 got.substr(g, got.find('\n', g) - g).c_str());
    return false;
}

bool check_file(const std::string& stem, sim::Options opt) {
    bool ok1 = false, ok2 = false;
    std::string script = portconf::read_command("gzip -dc " + stem + ".script.gz", &ok1);
    std::string ref = body(portconf::read_command("gzip -dc " + stem + ".obs.gz", &ok2));
    if (!ok1 || !ok2 || ref.empty()) { std::fprintf(stderr, "memconf: %s missing\n", stem.c_str()); return false; }
    return same(stem.c_str(), run(parse(script), opt), ref);
}

const char* kFixtures[] = {"t5-s1", "t0-s2", "t63-s3", "t27-s4-comb", "t9-s5"};

}  // namespace

TEST(memconf_fixtures_equal_ref) {
    for (const char* f : kFixtures) if (!check_file(kDir + f, {})) { test::fail(__FILE__, __LINE__, "%s differs from ref", f); return; }
}

TEST(memconf_fixtures_under_debug_options) {
    sim::Options a; a.step_all = true;
    sim::Options b; b.shuffle_seed = 7;
    sim::Options c; c.order = sim::Options::Order::instance_major;
    for (const sim::Options& o : {a, b, c})
        for (const char* f : kFixtures) if (!check_file(kDir + f, o)) { test::fail(__FILE__, __LINE__, "%s differs from ref", f); return; }
}

TEST(memconf_long_runs_when_asked) {
    const char* dir = std::getenv("MEMCONF_LONG");
    if (!dir) return;
    bool ok = false;
    std::string list = portconf::read_command(std::string("ls ") + dir + "/*.script.gz", &ok);
    unsigned n = 0;
    for (size_t p = 0; p < list.size();) {
        size_t e = list.find('\n', p);
        std::string f = list.substr(p, e - p);
        p = e == std::string::npos ? list.size() : e + 1;
        if (f.empty()) continue;
        if (!check_file(f.substr(0, f.size() - 10), {})) { test::fail(__FILE__, __LINE__, "%s differs from ref", f.c_str()); return; }
        n++;
    }
    std::fprintf(stderr, "memconf: %u long runs equal ref's\n", n);
}
