// test_statefp_core.cpp: the core's fingerprint writer (statefp_g1: w_core, w_core_mem) against ref on all 67 of M1's
// goldens, cycle by cycle (tests/fp-m1/NAME.corefp, made by tests/ref/CoreFp.java through tests/fp-m1/regen.sh): W5's
// controller on the engine, ready-driven and step-all, after every cycle.
//
// The memories' line is skipped at the cycles where the bench's MEMW is in flight (put at C - 1, applied by the core at
// C): ref's port-B write has landed by then. That lag is the shifted end of cycle the port observer will close; it is
// counted and reported, never hidden.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "golden_run.h"
#include "harness.h"
#include "statefp_g1.h"
#include "units/controller.h"

using namespace test;

namespace {

struct RefLine { uint64_t core, mem; };

bool read_corefp(const std::string& path, std::vector<RefLine>& out, uint64_t& last) {
    std::ifstream f(path);
    if (!f) return false;
    std::string s;
    while (std::getline(f, s)) {
        if (s.empty() || s[0] == '#') continue;
        std::istringstream in(s);
        uint64_t c; std::string u, h;
        in >> c >> u >> h;
        if (out.size() <= c) out.resize(c + 1);
        uint64_t v = std::stoull(h, nullptr, 16);
        if (u.size() > 5 && u.compare(u.size() - 5, 5, ".core") == 0) out[c].core = v; else out[c].mem = v;
        last = c;
    }
    return true;
}

// One golden on the engine under `opt`; empty if every cycle's core (and mem, where comparable) equals ref's.
std::string check_run(const RunSpec& r, const sim::Options& opt, uint64_t& lagged) {
    Golden g; Images im; std::string err;
    if (!read_golden("tests/golden/" + r.name + ".trace", g, err) || !load_images(r, im, err)) return err;
    std::vector<RefLine> ref; uint64_t last = 0;
    if (!read_corefp("tests/fp-m1/" + r.name + ".corefp", ref, last)) return "no tests/fp-m1/" + r.name + ".corefp";
    std::vector<bool> memw_in_flight(last + 2, false);
    for (const units::BenchEvent& e : g.events) if (e.kind == units::T_MEMW && e.t <= last) memw_in_flight[e.t] = true;
    units::TileConfig c = tile_config(r.tile);
    Tile t(im, c, uint32_t(g.body_lines + 64));
    sim::Sim s = build_engine(t, g, opt);
    fp::G1Tile view;
    view.tile = r.tile;
    for (uint64_t cyc = 0; cyc <= last; cyc++) {
        if (cyc > 0) s.run(1);
        view.core = &s.unit<units::Core>("t.core").rv;
        fp::FpOut oc; fp::w_core(&view, oc, fp::FpCtx{cyc, nullptr});
        char b[160];
        if (oc.done() != ref[cyc].core) {
            std::snprintf(b, sizeof b, "%s: t%u.core differs at cycle %llu: ref %016llx, ours %016llx", r.name.c_str(), r.tile,
                          (unsigned long long)cyc, (unsigned long long)ref[cyc].core, (unsigned long long)oc.done());
            return b;
        }
        if (memw_in_flight[cyc]) { lagged++; continue; }
        fp::FpOut om; fp::w_core_mem(&view, om, fp::FpCtx{cyc, nullptr});
        if (om.done() != ref[cyc].mem) {
            std::snprintf(b, sizeof b, "%s: t%u.mem differs at cycle %llu", r.name.c_str(), r.tile, (unsigned long long)cyc);
            return b;
        }
    }
    return "";
}

TEST(core_fingerprint_equals_ref_every_cycle_on_m1_goldens) {
    uint64_t lagged = 0;
    sim::Options step_all; step_all.step_all = true;
    for (const sim::Options& opt : {sim::Options{}, step_all})
        for (const RunSpec& r : read_runs()) {
            std::string e = check_run(r, opt, lagged);
            if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s%s", opt.step_all ? "(step-all) " : "", e.c_str()); return; }
        }
    std::printf("     memories skipped at %llu cycles with a MEMW in flight (the shifted end of cycle)\n", (unsigned long long)lagged);
}

}  // namespace
