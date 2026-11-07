// test_statefp_core.cpp: the core's fingerprint writer (statefp_g1: w_core, w_core_mem) against ref on all 67 of M1's
// goldens, cycle by cycle (tests/fp-m1/NAME.corefp, made by tests/ref/CoreFp.java through tests/fp-m1/regen.sh): W5's
// controller on the engine, ready-driven and step-all, after every cycle.
//
// The memories every cycle too: at the cycles where the bench's MEMW is in flight (put at C - 1, applied by the core at
// C) ref's port-B write has landed; the writer overlays it, read through the port observer (W10's Sim::observe).
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

struct RefLine { uint64_t core = 0, mem = 0, ctlx = 0; };

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
        if (u.size() > 5 && u.compare(u.size() - 5, 5, ".core") == 0) out[c].core = v;
        else if (u.size() > 4 && u.compare(u.size() - 4, 4, ".mem") == 0) out[c].mem = v;
        else if (u.size() > 5 && u.compare(u.size() - 5, 5, ".ctlx") == 0) out[c].ctlx = v;
        last = c;
    }
    return true;
}

// One golden on the engine under `opt`; empty if every cycle's core (and mem, where comparable) equals ref's.
std::string check_run(const RunSpec& r, sim::Options opt, uint64_t& overlaid) {
    opt.observe_arrivals = true;
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
    view.tile = r.tile; view.sim = &s;
    for (uint64_t cyc = 0; cyc <= last; cyc++) {
        if (cyc > 0) s.run(1);
        view.m1core = &s.unit<units::Core>("t.core");
        view.core = &view.m1core->rv;
        view.m1ctl = &s.unit<units::RegBlock>("t.ctl");
        fp::FpOut oc; fp::w_core(&view, oc, fp::FpCtx{cyc, nullptr});
        char b[160];
        if (oc.done() != ref[cyc].core) {
            std::snprintf(b, sizeof b, "%s: t%u.core differs at cycle %llu: ref %016llx, ours %016llx", r.name.c_str(), r.tile,
                          (unsigned long long)cyc, (unsigned long long)ref[cyc].core, (unsigned long long)oc.done());
            return b;
        }
        fp::FpOut ox; fp::w_ctl_nogo(&view, ox, fp::FpCtx{cyc, nullptr});
        if (ox.done() != ref[cyc].ctlx) {
            std::snprintf(b, sizeof b, "%s: t%u.ctl differs at cycle %llu (the engine, its endCycle(c) through the observer)",
                          r.name.c_str(), r.tile, (unsigned long long)cyc);
            return b;
        }
        if (memw_in_flight[cyc]) overlaid++;
        fp::FpOut om; fp::w_core_mem(&view, om, fp::FpCtx{cyc, nullptr});
        if (om.done() != ref[cyc].mem) {
            std::snprintf(b, sizeof b, "%s: t%u.mem differs at cycle %llu", r.name.c_str(), r.tile, (unsigned long long)cyc);
            return b;
        }
    }
    return "";
}

// W5's direct driver (golden_run.cpp run_direct: the core and the block as plain code, in ref's order, endCycle(t) at the
// end of t), with the block's stream (without the GO queue) and the core's checked after every cycle.
std::string direct_fp(const RunSpec& r) {
    Golden g; Images im; std::string err;
    if (!read_golden("tests/golden/" + r.name + ".trace", g, err) || !load_images(r, im, err)) return err;
    std::vector<RefLine> ref; uint64_t last = 0;
    if (!read_corefp("tests/fp-m1/" + r.name + ".corefp", ref, last)) return "no tests/fp-m1/" + r.name + ".corefp";
    Tile tile(im, tile_config(r.tile), uint32_t(g.body_lines + 64));
    struct Env {
        units::RegBlockState& b; bool freed; units::RegBlockState::In& in;
        uint32_t reg(unsigned i) const { return b.reg(i); }
        bool wake() const { return b.wake(); }
        bool irq() const { return b.irq(); }
        bool can_push() const { return !b.busy() || freed; }
        void post(uint32_t op, uint32_t arg, uint32_t v) { in.op = op; in.arg = arg; in.v = v; }
    };
    fp::G1Tile view;
    view.tile = r.tile; view.core = &tile.core; view.ctl = &tile.block;
    const auto& ev = g.events;
    size_t i = 0;
    char b[200];
    auto check = [&](uint64_t c) -> bool {
        fp::FpOut oc, ox;
        fp::w_core(&view, oc, fp::FpCtx{c, nullptr});
        fp::w_ctl_nogo(&view, ox, fp::FpCtx{c, nullptr});
        if (oc.done() == ref[c].core && ox.done() == ref[c].ctlx) return true;
        std::snprintf(b, sizeof b, "%s (direct): t%u.%s differs at cycle %llu", r.name.c_str(), r.tile,
                      oc.done() != ref[c].core ? "core" : "ctl", (unsigned long long)c);
        return false;
    };
    if (!check(0)) return b;
    for (uint64_t t = 1; t <= last; t++) {
        units::RegBlockState::In in;
        bool freed = false;
        for (; i < ev.size() && ev[i].t == t - 1 && ev[i].kind != units::T_FREED; i++) {
            const units::BenchEvent& e = ev[i];
            if (e.kind != units::T_MEMW) continue;
            uint32_t* m = units::region(e.addr) == units::R_PMEM ? tile.imem.data() : tile.dmem.data();
            for (uint32_t k = 0; k < e.n; k++)
                m[(((units::offset(e.addr) & (units::MEM_BYTES - 1)) >> 2) + k) & (units::MEM_WORDS - 1)] = e.v[k];
        }
        for (; i < ev.size() && ev[i].t == t && ev[i].kind == units::T_FREED; i++) freed = true;
        Env env{tile.block, freed, in};
        tile.core.step(env, t);
        if (tile.core.stop == units::Stop::ub) break;
        in.freed = freed;
        for (size_t j = i; j < ev.size() && ev[j].t == t; j++) {
            if (ev[j].kind == units::T_GO) in.go = true;
            if (ev[j].kind == units::T_CMD) in.landed = true;
        }
        tile.block.end_cycle(t, in);
        if (!check(t)) return b;
        if (tile.core.stop != units::Stop::running) break;
    }
    return "";
}

TEST(block_fingerprint_equals_ref_every_cycle_on_m1_goldens_direct) {
    for (const RunSpec& r : read_runs()) {
        std::string e = direct_fp(r);
        if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    }
}

TEST(core_fingerprint_equals_ref_every_cycle_on_m1_goldens) {
    uint64_t overlaid = 0;
    sim::Options step_all; step_all.step_all = true;
    for (const sim::Options& opt : {sim::Options{}, step_all})
        for (const RunSpec& r : read_runs()) {
            std::string e = check_run(r, opt, overlaid);
            if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s%s", opt.step_all ? "(step-all) " : "", e.c_str()); return; }
        }
    std::printf("     memories compared every cycle; %llu of them with a MEMW in flight, overlaid through the observer\n", (unsigned long long)overlaid);
}

}  // namespace
