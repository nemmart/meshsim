// test_g1_replay.cpp: G1's units of tile 0 against golden-m2 (regs through fp::kRegsCheckedThrough, wfi to its end),
// every unit every cycle, before the C++ network exists: what crosses tile 0's G1 boundary in ref (tests/g1-replay/
// NAME.g1, from tests/ref/G1Trace.java: eject arrivals, GO scheduling, the send hold's takes) is replayed by a stand-in
// for the network; the units' streams are compared with the golden's lines through the fingerprint library, the inputs in
// flight read through the port observer. Under the debug options (ready-driven, step-all, a shuffle, instance-major).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "golden_run.h"
#include "harness.h"
#include "statefp_g1.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

struct Ev { uint64_t t; char kind; uint64_t a, b, c; };   // E: put at t (arrives t + 1); G: put at t, latency a; T: take at t

struct Stand {
    sim::LinkOut eject, go;
    sim::HoldIn send;
    sim::RegOut send_busy;
    sim::LinkIn line;
    sim::CountOut ovf;
    const Ev* ev; unsigned n, i = 0;
    bool busy = false;
    Stand(sim::Ports& p, const Ev* e, unsigned count) : ev(e), n(count) {
        p.out(eject, "eject", 3); p.out(go, "go", 0); p.in(send, "send", 3); p.out(send_busy, "send_busy", 1);
        p.in(line, "line", 1); p.out(ovf, "ovf");
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(line)) c.take(line);
        for (; i < n && ev[i].t == c.now(); i++) {
            if (ev[i].kind == 'E') c.put(eject, ev[i].a, ev[i].b, ev[i].c);
            else if (ev[i].kind == 'G') c.put(go);
            else {
                if (!c.ready(send)) FATAL("replay: ref took the send hold at %llu, here it is empty", (unsigned long long)c.now());
                c.take(send);
            }
        }
        bool b = c.ready(send);                                   // occupied at the end of t (W9's Reg)
        if (b != busy) { c.put(send_busy, sim::Word(b)); busy = b; }
        return i < n ? sim::Wake::at_cycle(ev[i].t) : sim::Wake::none();
    }
    uint64_t digest() const { return i * 2 + busy; }
};

bool read_trace(const std::string& path, std::vector<Ev>& ev, uint64_t& go_latency, uint64_t& end) {
    std::ifstream f(path);
    if (!f) return false;
    std::string s;
    while (std::getline(f, s)) {
        std::istringstream in(s);
        std::string k; in >> k;
        if (k == "E") { uint64_t at; std::string a, b, c; in >> at >> a >> b >> c;
                        ev.push_back({at - 1, 'E', std::stoull(a, nullptr, 16), std::stoull(b, nullptr, 16), std::stoull(c, nullptr, 16)}); }
        else if (k == "G") { uint64_t sc, e; in >> sc >> e; ev.push_back({sc, 'G', 0, 0, 0}); go_latency = e - sc + 1; }
        else if (k == "T") { uint64_t t; in >> t; ev.push_back({t, 'T', 0, 0, 0}); }
        else if (k == "END") in >> end;
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.t < y.t; });
    return true;
}

std::string replay(const char* name, uint64_t through, sim::Options opt) {
    std::vector<Ev> ev; uint64_t go_lat = 0, end = 0;
    if (!read_trace(std::string("tests/g1-replay/") + name + ".g1", ev, go_lat, end)) return std::string("no trace for ") + name;
    fp::FpGolden g = fp::FpGolden::load(std::string("tests/golden-m2/") + name + ".fp");
    if (!g.error.empty()) return g.error;
    if (g.config.at("K") != "8" || g.config.at("cmLatency") != "2" || g.config.at("regLoadLatency") != "2" ||
        g.config.at("doneGate") != "true" || g.config.at("memChunks") != "1048576" || g.config.at("refCycle") != "40")
        return "refused (D12): the golden's configuration is not the one this replay builds";
    test::Images im; std::string err;
    std::vector<test::RunSpec> runs = test::read_runs();
    if (!test::load_images(runs[0], im, err)) return err;                 // the boot ROM (sim/ref/meshsim/sw/bootrom.hex)
    TileConfig tc = test::tile_config(0);
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    G1Ctx gx;
    gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
    gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
    gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data();
    opt.observe_arrivals = true;
    sim::Config cfg(opt);
    auto& st = cfg.add<Stand>("stand", ev.data(), unsigned(ev.size()));
    ControlIO io = add_g1(cfg, 0, &gx);
    cfg.link(st.eject, io.eject, 1);
    cfg.link(st.go, io.go, uint32_t(go_lat));
    cfg.connect(io.send, st.send);
    cfg.connect(st.send_busy, io.send_busy);
    cfg.link(io.line, st.line, 6);
    cfg.connect(st.ovf, io.router_ovf);
    sim::Sim m = cfg.build();
    fp::G1Tile view = fp::g1_tile(m, 0);
    fp::FpRegistry reg;
    fp::register_g1(reg, view);
    static const char* const kUnits[] = {"core", "ctl", "cmem", "dir", "sram", "sram.data", "we", "ag", "ts", "hist",
                                         "gather", "rdport", "req"};
    uint64_t last = std::min(through, end);
    for (uint64_t c = 0; c <= last; c++) {
        if (c > 0) m.run(1);
        auto pts = g.at(c);
        if (pts.empty()) return std::string(name) + ": the golden has no point at cycle " + std::to_string(c);
        for (const char* u : kUnits) {
            std::string un = std::string("t0.") + u;
            uint64_t want = 0;
            for (const fp::FpLine& l : pts[0]->lines) if (l.unit == un) want = l.hash;
            uint64_t got = reg.hash(un, fp::FpCtx{c, &m});
            if (got != want) {
                char b[200];
                std::snprintf(b, sizeof b, "%s: %s differs at cycle %llu: golden %016llx, ours %016llx (fpdump to name the word)",
                              name, un.c_str(), (unsigned long long)c, (unsigned long long)want, (unsigned long long)got);
                if (std::getenv("G1_DUMP")) reg.dump(un, fp::FpCtx{c, &m}, stderr);
                return b;
            }
        }
    }
    return "";
}

std::vector<sim::Options> options() {
    sim::Options a, b, c, d;
    b.step_all = true; c.shuffle_seed = 3; d.order = sim::Options::Order::instance_major;
    return {a, b, c, d};
}

}  // namespace

TEST(g1_replay_wfi_every_unit_every_cycle) {
    for (const sim::Options& o : options()) {
        std::string e = replay("wfi", ~uint64_t(0), o);
        if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    }
}
TEST(g1_replay_regs_every_unit_every_cycle_through_the_limit) {
    for (const sim::Options& o : options()) {
        std::string e = replay("regs", fp::kRegsCheckedThrough, o);
        if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    }
}
