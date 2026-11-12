// sort_test (WORKER24, P6, rows D1-D4, H4): v1.3's SortTest (sim/ref/meshsim/test/SortTest.java) in C++, on W20's machine
// harness (tests/mach.h), its arguments and its output line for line, so run_tests.sh's sort steps are log-equal:
//
//   sort_test [seed] p1 [full] [only=<set>]                          the local sorts L1, L2
//   sort_test [seed] sort [full] [only=<set>]                        every built-in set at 64 x <= 2,000
//   sort_test [seed] run kind=<builtin>|keys=<file> [n=<per tile>] fmt=k128|kv32 [shaped] [label=..] [ready=N]
//             [cfg.<Config field>=<value> ...] [csv=<file>] [phases=<file>] [force=bt (TEST ONLY)]
//   seed: -1 the fixed order, >= 0 the framework's debug shuffle with that seed (mach.h: SHUFFLING).
//   Options (SortTest's -Dsort.*): SORT_BUILD=<dir> (the image's directory; default the harness's search path),
//   SORT_MAXCYCLES=<n>; SORT_OPT=instance|stepall, a debug option (sim.h DEBUG CHECKS; the shuffle is the seed).
//
// C&C's sequence is queued on C&C 1's model between cycles, as SortTest queues it on CnC: the boot (the image broadcast,
// CMD_JUMP, READY, GO), the input-load stand-in, then every step's command, all READY, GO. The harness's own work
// (the input, p1's L2 stand-in) runs between cycles only (D6).
//
// THE DIFFERENCES FROM v1.3, deliberate (NOTES):
//   - the inputs line's first field is this binary's sha256 (`sort_test=<sha>`) where v1.3 has the jar's
//     (`meshsim.jar=...`): tooling metadata, not the model (owner, Q2); run_tests.sh's --check masks it;
//   - the ORACLE image (SortTest's simulator-only oracle split) isn't ported: no v1.3 build makes it (owner, Q5);
//   - the stack headroom comes from the controller's diagnostic lowest stack pointer (owner, Q3), not a per-cycle scan.
// THE PHASES are W20's PhaseLog (tests/phase_log.h, ref's line for line), attached at the machine's construction; its
// write-engine input and GATHER queue peaks exist in the debug and suite builds only: run_tests.sh and the kit build
// sort_test with MODE=suite.
#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "check.h"
#include "mach.h"
#include "sha256.h"
#include "statefp.h"
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "units/reqbus.h"
#include "sort_input.h"
#include "sort_kit.h"
#include "suite.h"
#include "units/ag.h"
#include "units/regblock.h"
#include "units/tile_ctl.h"

using namespace sortin;
using mach::Mach;
using mach::MachConfig;

namespace {

// Java's printing helpers
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    va_list ap; va_start(ap, f);
    char buf[4096]; vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}
void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fputc('\n', stdout); }
using sortkit::grouped;

// java.lang.String.hashCode, java.util.Random's nextInt() and nextInt(bound)
int32_t java_hash(const std::string& s) { uint32_t h = 0; for (unsigned char c : s) h = 31 * h + c; return int32_t(h); }
struct JRandom {
    uint64_t seed;
    explicit JRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1)) {}
    int32_t next(int bits) { seed = (seed * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1); return int32_t(int64_t(seed) >> (48 - bits)); }
    int32_t next_int() { return next(32); }
    int32_t next_int(int32_t bound) {
        if ((bound & -bound) == bound) return int32_t((int64_t(bound) * int64_t(next(31))) >> 31);
        int32_t bits, val;
        do { bits = next(31); val = bits % bound; } while (int32_t(uint32_t(bits) - uint32_t(val) + uint32_t(bound - 1)) < 0);
        return val;
    }
};

// SortTest.keys: the built-in sets
Keys builtin_keys(const std::string& kind, int n, int64_t seed) {
    JRandom r(seed);
    Keys k(64);
    for (int p = 0; p < 64; p++) {
        int np = n;
        if (kind == "ragged") np = p == 3 ? 0 : p == 5 ? 1 : p == 9 ? 2 * n : r.next_int(2 * n + 1);
        k[p].resize(size_t(np));
        for (int j = 0; j < np; j++) {
            uint32_t v;
            if (kind == "few") v = uint32_t(r.next_int(5)) * 0x01010101u;
            else if (kind == "sorted") v = uint32_t(p * np + j);
            else if (kind == "reverse") v = ~uint32_t(p * np + j);
            else if (kind == "equal") v = 0x5A5A5A5Au;
            else v = uint32_t(r.next_int());
            k[p][size_t(j)] = v;
        }
    }
    return k;
}
int64_t seed_of(const std::string& kind, int base) { return int64_t(int32_t(uint32_t(base) + uint32_t(java_hash(kind)))); }

// SortTest.stableByKey: stable by the low 32 bits, unsigned
std::vector<uint64_t> stable_by_key(std::vector<uint64_t> r) {
    std::stable_sort(r.begin(), r.end(), [](uint64_t x, uint64_t y) { return uint32_t(x) < uint32_t(y); });
    return r;
}

const char* PATHS[] = {"direct", "LB", "TH", "BT", "PLAN"};

// SortTest.phaseName: PhaseLog's namer for the sort
std::string phase_name(int64_t w) {
    if (w < 0) return "-";
    const uint32_t t = uint32_t(w) >> 24, a = uint32_t(w) & 0x7FFFFF;
    switch (t) {
    case CMD_L1: return "L1"; case CMD_SPLIT: case CMD_BARRIER: return "SPLIT"; case CMD_XB: return "XB"; case CMD_RED: return "RED";
    case CMD_XC: return "XC"; case CMD_TH: return "TH"; case CMD_SWEEP: return "SWEEP"; case CMD_DESC: return "DESC";
    case CMD_K: return "K"; case CMD_V: return "V";
    case CMD_K1: return "K1." + std::to_string(a); case CMD_K2: return "K2." + std::to_string(a);
    case CMD_V1: return "V1." + std::to_string(a); case CMD_V2: return "V2." + std::to_string(a);
    case CMD_L2: return "L2"; case CMD_EXIT: return "EXIT";
    default: return fmt("cmd %02X", t);
    }
}
// SortTest.SKEW_CMD: "XC|TH|SWEEP|DESC|K|V|..\\.."
bool skew_cmd(const std::string& n) {
    return n == "XC" || n == "TH" || n == "SWEEP" || n == "DESC" || n == "K" || n == "V" || (n.size() == 4 && n[2] == '.');
}
bool peak_step(const std::string& n) { return n.size() == 4 && n[2] == '.'; }   // "..\\.."

// ============================================================================================== the run's options
struct Opts {
    int64_t shuffle = -1; int ready_latency = -1;
    std::vector<std::pair<std::string, std::string>> cfg;
    std::string csv, phases_csv, label; bool force_bt = false;
    MachConfig config() const {
        MachConfig k = Mach::cfg(shuffle);
        if (ready_latency >= 0) k.ready_latency = unsigned(ready_latency);
        if (const char* d = std::getenv("SORT_OPT")) {      // the debug options (sim.h DEBUG CHECKS) beside the shuffle
            const std::string o = d;
            if (o == "instance") k.instance_major = true;
            else if (o == "stepall") k.step_all = true;
            else if (!o.empty()) { std::fprintf(stderr, "sort_test: SORT_OPT=%s: instance or stepall\n", d); std::exit(2); }
        }
        for (auto& e : cfg) {
            std::string err;
            if (!k.set(e.first, e.second, err)) { std::fprintf(stderr, "sort_test: %s\n", err.c_str()); std::exit(2); }
        }
        return k;
    }
};

// ============================================================================================== the image
// Its directory: SORT_BUILD (SortTest's -Dsort.build), else the harness's search path (mach.h: IMAGES).
std::string image_dir() {
    if (const char* d = std::getenv("SORT_BUILD")) return d;
    for (const char* d : {"fw/images", "tests/golden-m2/images", "tests/golden/images", "tests/m3-images"})
        if (sortkit::exists(std::string(d) + "/sort.code.bin")) return d;
    return "tests/golden-m2/images";
}
// SortTest.Image's symbols: nm -S lines, "addr [size] type name", the first of a name kept (Mach.Image reads only
// three-field lines)
struct Syms {
    std::map<std::string, uint32_t> sym, sizes;
    std::vector<std::string> order;                // the names in the file's order (SortTest's HashMap is searched by
                                                   //  sort.watch for a clone: the first of a unique prefix here)
    std::map<uint32_t, std::string> funcs;         // program-memory functions (T, t) by address, the first kept
    void load(const std::string& path) {          // nm -S: addr [size] type name
        for (const std::string& l : sortkit::read_lines(path)) {
            std::vector<std::string> f = sortkit::split_ws(l);
            if (f.size() < 3) continue;
            const uint32_t a = uint32_t(std::stoul(f[0], nullptr, 16)), size = f.size() == 4 ? uint32_t(std::stoul(f[1], nullptr, 16)) : 0;
            const std::string& type = f[f.size() - 2]; const std::string& name = f.back();
            if (sym.emplace(name, a).second) order.push_back(name);
            if (size > 0) sizes.emplace(name, size);
            if ((type == "T" || type == "t") && a >= 0x01000000u && a < 0x01008000u) funcs.emplace(a, name);
        }
    }
    // SortTest.Machine.monitor's name for a pc: "bootrom" in the boot ROM, else the function at or below it, "?" if none
    std::string func_at(uint32_t pc) const {
        if (pc >= 0x02000000u && pc < 0x03000000u) return "bootrom";
        auto it = funcs.upper_bound(pc);
        if (it == funcs.begin()) return "?";
        return std::prev(it)->second;
    }
};

// SortTest's profiles (H7), SortTest's -D options as environment variables:
//   SORT_PROFILE (set)         tile 0's cycles per function over the run, awake or in WFI (a WFI's cycles go to the
//                              function waiting in it): "tile 0 cycles per function: {f=n, ...}" (Java's Map.toString)
//   SORT_PROF=lo-hi            tile 0's cycles with pc in [lo, hi) (hex), the boot included (Controller.profCycles)
//   SORT_WATCH=f1,f2,...       every tile's cycles with its pc inside each function (a name, or GCC's clone of it:
//                              name.constprop.0), per step: mean and max over the tiles
// Sampled between cycles, as ref's monitor and runCycle do (run_until checks its condition before every cycle).
struct Profile {
    std::vector<std::pair<std::string, int64_t>> prof;                 // tile 0, over the run, in first-seen order
    std::map<size_t, std::vector<std::pair<std::string, int64_t>>> by_phase;   // tile 0, per phase index
    bool prof_on = false; uint32_t prof_lo = 0, prof_hi = 0; int64_t prof_cycles = 0;
    std::vector<std::string> watch; std::vector<uint32_t> w_lo, w_hi;
    std::map<size_t, std::vector<std::array<int64_t, 64>>> watch_by_phase;   // phase -> [function][tile]
    static void add(std::vector<std::pair<std::string, int64_t>>& v, const std::string& f) {
        for (auto& e : v) if (e.first == f) { e.second++; return; }
        v.emplace_back(f, 1);
    }
    static int64_t get(const std::vector<std::pair<std::string, int64_t>>& v, const std::string& f) {
        for (auto& e : v) if (e.first == f) return e.second;
        return 0;
    }
};

// ============================================================================================== the fingerprints
// SORT_FP=<dir> (tests/sort/fp, SortFp's references; RUNS names them): each run against <dir>/<its name>.fpd, the name
// as RUNS gives it (p1-<kind>-<fmt>, <kind>-<fmt>, shaped-<kind>-k128, a key file's run by its label); a run with no
// file there is not checked. At every point the reference took (its GOs and all-READYs,
// the boot's end, the run's end), the canonical state fingerprint's lines (the registry of M2's sort: every tile's 16
// units, C&C, its peripheral, the request bus, the endpoints) digested as sortfp.sh's fp_digest does them: the
// "unit hash" lines sorted bytewise, each ending in \n, their sha256. Read only, between cycles; the log is unchanged.
struct FpCheck {
    struct Pt { int64_t cycle; size_t lines; std::string sha; };
    std::vector<Pt> pts; size_t next = 0; unsigned compared = 0; std::string first_diff;
    std::vector<fp::G1Tile> g1; std::vector<fp::G2Tile> g2; fp::G2Chip gc; units::reqbus::ReqBusUnits rbu; fp::FpRegistry reg;
    std::vector<unsigned> tiles;
    sim::Sim* sim = nullptr;
    static fp::LineEdge edge(const void* ctx, unsigned k, uint64_t c) {
        const fp::G1Tile& g = static_cast<const fp::G1Tile*>(ctx)[k];
        const units::LineEdge e = units::line_edge(*g.sim, *g.uctl, c);
        fp::LineEdge le; le.put = e.put; le.ready = e.word & 1; le.level = e.word >> 1 & 1;
        return le;
    }
    FpCheck(const std::string& path, mach::Chip& c) : g1(64), g2(64) {
        for (const std::string& l : sortkit::read_lines(path)) {
            if (l.empty() || l[0] == '#') continue;
            std::vector<std::string> f = sortkit::split_ws(l);
            if (f.size() == 3) pts.push_back({std::stoll(f[0]), size_t(std::stoull(f[1])), f[2]});
        }
        if (pts.empty()) { std::fprintf(stderr, "sort_test: SORT_FP=%s: no points\n", path.c_str()); std::exit(2); }
        sim = &c.sim();
        for (unsigned k = 0; k < 64; k++) {
            g1[k] = fp::g1_tile(*sim, k); g1[k].uinj = &sim->unit<units::Injection>(units::tile_unit(k, "inj"));
            g2[k] = fp::g2_tile(*sim, k);
        }
        gc.cnc = &sim->unit<units::Cnc>("cnc");
        for (unsigned r = 0; r < 8; r++) gc.rows[r] = &sim->unit<units::RowCollector>("row" + std::to_string(r));
        gc.ep[0] = &sim->unit<units::Endpoint>("ep2"); gc.ep[1] = &sim->unit<units::Endpoint>("ep3");
        gc.ready_latency = c.cfg().ready_latency;
        gc.edge = edge; gc.edge_ctx = g1.data();
        fp::attach_reqbus(*sim, g1.data(), 64, gc, rbu, units::reqbus::ReqBusConfig{});
        for (unsigned t = 0; t < 64; t++) { fp::register_g1(reg, g1[t]); fp::register_g2_tile(reg, g2[t]); tiles.push_back(t); }
        fp::register_g2(reg, gc);
    }
    void at(int64_t cyc) {
        while (next < pts.size() && pts[next].cycle < cyc) next++;     // (a point passed without a check: none expected)
        if (next >= pts.size() || pts[next].cycle != cyc) return;
        std::vector<std::string> lines;
        reg.point(fp::FpCtx{uint64_t(cyc), sim}, tiles, true, lines);
        std::vector<std::string> uh;
        for (const std::string& l : lines) uh.push_back(l.substr(l.find(' ') + 1));
        std::sort(uh.begin(), uh.end());
        std::string all; for (const std::string& x : uh) all += x + "\n";
        const std::string sha = test::sha256_hex(all.data(), all.size());
        if (uh.size() != pts[next].lines || sha != pts[next].sha) {
            if (first_diff.empty()) first_diff = "cycle " + std::to_string(cyc) + ": " + std::to_string(uh.size()) + " lines, sha " + sha.substr(0, 12) + "... (reference " + std::to_string(pts[next].lines) + ", " + pts[next].sha.substr(0, 12) + "...)";
        } else compared++;
        next++;
    }
    bool report(FILE* o) const {
        if (!first_diff.empty()) { std::fprintf(o, "fp: DIFFERS at the first point that differs, %s (%u equal before)\n", first_diff.c_str(), compared); return false; }
        if (compared != pts.size()) { std::fprintf(o, "fp: %u of %zu reference points reached, all equal\n", compared, pts.size()); return false; }
        std::fprintf(o, "fp: all %u reference points equal\n", compared);
        return true;
    }
};
bool g_fp_ok = true;

// ============================================================================================== a machine running the sort
struct Machine {
    Mach m;
    mach::Chip& c;
    mach::Cc& cc;
    Mach::Image im;
    Syms syms;
    int64_t all_ready_at = -1;
    Profile pf;
    // sort.prof (Controller.runCycle): before each cycle the core runs, tile 0 in RUN with pc in [lo, hi)
    bool prof_sample() {
        mach::TileRef p = c.tile(0);
        if (!pf.prof_on || p.halt() != units::Rv32::RUN) return false;
        const uint32_t pc = p.pc();
        if (pc >= pf.prof_lo && pc < pf.prof_hi) { pf.prof_cycles++; return true; }
        return false;
    }
    // SortTest.Machine.monitor's profiles (its stack watch is the core's own, Q3)
    void monitor() {
        const size_t ph = c.phases().phases.size() - 1;                 // PhaseLog's phases.size() - 1 (-1: none yet)
        if (!pf.watch.empty()) {
            auto& w = pf.watch_by_phase[ph];
            if (w.empty()) w.assign(pf.watch.size(), std::array<int64_t, 64>{});
            for (unsigned i = 0; i < 64; i++) {
                const uint32_t pc = c.tile(i).pc();
                for (size_t f = 0; f < pf.watch.size(); f++) if (pc >= pf.w_lo[f] && pc < pf.w_hi[f]) w[f][i]++;
            }
        }
        mach::TileRef p = c.tile(0);
        if (p.halt() == units::Rv32::RUN) {
            const std::string f = syms.func_at(p.pc());
            Profile::add(pf.prof, f); Profile::add(pf.by_phase[ph], f);
        }
    }
    std::unique_ptr<FpCheck> fpc;
    Machine(const Opts& o, const std::string& fp_name) : m(o.config()), c(m.c), cc(m.c.cc()) {
        if (const char* d = std::getenv("SORT_FP")) {
            const std::string f = std::string(d) + "/" + fp_name + ".fpd";
            if (sortkit::is_file(f)) { fpc = std::make_unique<FpCheck>(f, c); std::fprintf(stderr, "fp: %s against %s\n", fp_name.c_str(), f.c_str()); }
            else std::fprintf(stderr, "fp: %s: no reference %s, not checked\n", fp_name.c_str(), f.c_str());
        }
        c.phases();                                   // ref's Chip.phases (W20's PhaseLog), there from the start
        im = Mach::Image::load_dir(image_dir(), "sort");
        syms.load(image_dir() + "/sort.sym");
        im.syms = syms.sym;
        if (const char* pr = std::getenv("SORT_PROF")) {
            const std::string v = pr; const size_t d = v.find('-');
            pf.prof_on = true; pf.prof_lo = uint32_t(std::stoul(v.substr(0, d), nullptr, 16)); pf.prof_hi = uint32_t(std::stoul(v.substr(d + 1), nullptr, 16));
        }
        if (const char* w = std::getenv("SORT_WATCH"); w && *w) {
            for (const std::string& n0 : sortkit::split_all(w, ',')) {
                std::string nm = n0;
                if (!syms.sym.count(nm)) for (const std::string& k : syms.order) if (k.rfind(n0 + ".", 0) == 0) { nm = k; break; }
                pf.watch.push_back(n0);
                pf.w_lo.push_back(sym(nm));
                pf.w_hi.push_back(pf.w_lo.back() + (syms.sizes.count(nm) ? syms.sizes.at(nm) : 0));
            }
        }
    }
    uint32_t sym(const std::string& s) const { return im.sym(s); }
    // tile k's unit (through a named TileRef: gcc 13's -Wdangling-reference otherwise flags the chip's own unit)
    template <class T> T& tunit(unsigned k, const char* n) { mach::TileRef p = c.tile(k); return p.unit<T>(n); }
    // SortTest.Machine.boot (spec 6.2): program memory, data memory from _data_start, CMD_JUMP to _start, GO when all are READY
    void boot() {
        cc.load_words(lw_broadcast(), 0x01000000u, im.code).load_words(lw_broadcast(), sym("_data_start"), im.data)
          .command(lw_broadcast(), 0x4Au << 24, {sym("_start")}).run_when_ready();
        const bool ok = c.run_until([&] {
            const bool counted = prof_sample();
            const bool done = !cc.busy() && all_waiting();
            if (done && counted) pf.prof_cycles--;     // the last check: no cycle follows it
            return done;
        }, 100000);
        suite::check(ok, "boot: every tile waits for its first command");
    }
    static unsigned lw_broadcast() { return 126; }   // LinkWord.BROADCAST (units/net.h)
    bool all_waiting() {
        for (unsigned k = 0; k < 64; k++) { mach::TileRef p = c.tile(k); if (!p.in_wfi() || p.halt() != units::Rv32::RUN) return false; }
        return true;
    }
    void poke_s(unsigned tile, unsigned field, uint32_t v) { c.tile(tile).poke_local(sym("S") + 4 * field, v); }
    uint32_t peek_s(unsigned tile, unsigned field) { return c.tile(tile).peek_local(sym("S") + 4 * field); }
    // SortTest's atAllReady: wait for all READY (recording when), then run `r` (between READY and GO). SortTest records
    // `c.cycle` from inside C&C's step at t, where ref's Chip.cycle is still t - 1 (the last cycle completed): so t - 1.
    mach::Cc& at_all_ready(std::function<void()> r) {
        return cc.at_all_ready([this, r](int64_t t) { all_ready_at = t - 1; if (r) r(); });
    }
    // the stack watch armed with every core's x2 now (ref's monitor starts with the sort's run)
    void arm_sp() {
        for (unsigned k = 0; k < 64; k++) {
            units::Rv32& r = tunit<units::TileCore>(k, "core").rv;
            r.min_sp_watch = true; r.min_sp = r.x[2] != 0 ? int32_t(r.x[2]) : INT32_MAX;
        }
    }
    int32_t min_sp() {
        int32_t m = INT32_MAX;
        for (unsigned k = 0; k < 64; k++) m = std::min(m, tunit<units::TileCore>(k, "core").rv.min_sp);
        return m;
    }
    bool finish(uint64_t max) {
        arm_sp();
        if (fpc) fpc->at(c.cycle());                  // the state after the input and the queue (FpHooks' first point)
        const bool ok = c.run_until([&] {
            monitor(); const bool counted = prof_sample(); if (fpc) fpc->at(c.cycle());
            const bool done = !cc.busy() && c.all_stopped();
            if (done && counted) pf.prof_cycles--;     // the last check: no cycle follows it
            return done;
        }, max);
        if (fpc) { fpc->at(c.cycle()); g_fp_ok = fpc->report(stderr) && g_fp_ok; }
        return ok;
    }
    std::string halt_summary() {
        std::string sb;
        for (unsigned k = 0; k < 64; k++) {
            mach::TileRef p = c.tile(k);
            if (p.halt() != units::Rv32::HALT_EXIT || p.exit_value() != 0) {
                sb += p.halt_report() + fmt(" exit=0x%x pc=0x%x; ", unsigned(uint32_t(p.exit_value())), p.pc());
                if (sb.size() > 400) break;
            }
        }
        return sb;
    }
    void check_end(const std::string& what) {
        suite::check(c.all_exited(), "%s: every core exited through SIM_EXIT", what.c_str());
        const std::string hs = halt_summary();
        suite::check(hs.empty(), "%s: every core exited with 0: %s", what.c_str(), hs.c_str());
        const uint64_t v = c.violations();
        suite::check(v == 0, "%s: no invariant violations%s", what.c_str(), v == 0 ? "" : ("\n" + c.violation_report()).c_str());
        for (unsigned k = 0; k < 64; k++) {
            const uint32_t cond = c.tile(k).block().conditions;
            if (cond != 0) { suite::check(false, "%s: tile %u ERRORS 0x%x", what.c_str(), k, cond); break; }
        }
    }
    void hist_clear(const std::string& what) {
        for (unsigned k = 0; k < 64; k++)
            for (unsigned t = 0; t < 256; t++)
                if (c.tile(k).hist_peek(t) != 0) { suite::check(false, "%s: HIST clear at the end (tile %u)", what.c_str(), k); t = 256; break; }
    }
    void dump_state() {                             // SortTest.Machine.dumpState (in part: a failed run's state)
        out(fmt("    state at cycle %" PRId64 " (C&C at phase %zu), violations %" PRIu64 ":", c.cycle(), cc.phases_logged(), c.violations()));
        std::map<std::string, std::vector<unsigned>> st;
        for (unsigned k = 0; k < 64; k++) {
            mach::TileRef p = c.tile(k);
            st[fmt("pc 0x%x wfi %s counters 1-3: sram %u cm %u reply %u", p.pc(), p.in_wfi() ? "true" : "false", p.remaining(1), p.remaining(2), p.remaining(3))].push_back(k);
        }
        for (auto& e : st) {
            std::string l = "      " + e.first + ": tiles [";
            for (size_t i = 0; i < e.second.size(); i++) l += (i ? ", " : "") + std::to_string(e.second[i]);
            out(l + "]");
        }
    }
};

int mismatches(Machine& m, unsigned tile, uint32_t base, const std::vector<uint64_t>& expect) {
    int bad = 0;
    for (size_t i = 0; i < expect.size(); i++) if (m.c.tile(tile).sram_peek64(base + 2 * uint32_t(i)) != expect[i]) bad++;
    return bad;
}

// ============================================================================================== P1: the local sorts
void p1(const std::string& kind, int n, int fmt_, const Opts& op) {
    const std::string what = "P1 " + kind + " n=" + std::to_string(n) + (fmt_ == FMT_K128 ? " k=2 (augmented)" : " 32-bit values (copy)");
    suite::group(what);
    Machine m(op, "p1-" + kind + (fmt_ == FMT_K128 ? "-k128" : "-kv32"));
    m.boot();
    Keys k = builtin_keys(kind, n, seed_of(kind, 11));
    std::vector<std::vector<uint64_t>> l1_expect(64), l2_expect(64);
    for (unsigned i = 0; i < 64; i++) {
        mach::TileRef p = m.c.tile(i);
        std::vector<uint64_t> rec(k[i].size());
        for (uint32_t j = 0; j < k[i].size(); j++) {
            if (fmt_ == FMT_K128) { p.sram_poke(BUF_IN + j, k[i][j]); rec[j] = uint64_t(k[i][j]) | uint64_t(j) << 32; }
            else { rec[j] = uint64_t(k[i][j]) | value32(i, j) << 32; p.sram_poke64(BUF_IN + 2 * j, rec[j]); }
        }
        l1_expect[i] = stable_by_key(rec);
        m.poke_s(i, S_NIN, uint32_t(k[i].size()));
    }
    Keys k2 = builtin_keys(kind, n, seed_of(kind, 77));
    int l1_bad = 0;
    const size_t base = m.cc.phases_logged();
    m.cc.command(Machine::lw_broadcast(), CMD_L1 << 24 | uint32_t(fmt_)).go().command(Machine::lw_broadcast(), CMD_L2 << 24);
    int64_t l1_ready = 0, l2_ready = 0;
    m.at_all_ready([&] {
        l1_ready = m.all_ready_at;
        for (unsigned i = 0; i < 64; i++) {
            mach::TileRef p = m.c.tile(i);
            l1_bad += mismatches(m, i, BUF_X, l1_expect[i]);
            std::vector<uint64_t> rec(k2[i].size()); uint32_t cnt[256] = {};
            for (uint32_t j = 0; j < rec.size(); j++) { rec[j] = uint64_t(k2[i][j]) | uint64_t(j) << 32; p.sram_poke64(BUF_Y + 2 * j, rec[j]); cnt[k2[i][j] & 0xFF]++; }
            for (unsigned t = 0; t < 256; t++) p.hist_poke(t, cnt[t]);
            l2_expect[i] = stable_by_key(rec);
            m.poke_s(i, S_NRECV, uint32_t(rec.size()));
        }
    }).go().command(Machine::lw_broadcast(), CMD_EXIT << 24);
    m.at_all_ready([&] { l2_ready = m.all_ready_at; }).go();
    const bool ok = m.finish(30000000);
    suite::check(ok, "%s: the sequence ran to its end", what.c_str());
    suite::eq(0, l1_bad, "%s: L1: every tile's BUF_X holds its input, stably sorted by key", what.c_str());
    int l2_bad = 0;
    for (unsigned i = 0; i < 64; i++) l2_bad += mismatches(m, i, BUF_Y, l2_expect[i]);
    suite::eq(0, l2_bad, "%s: L2: every tile's BUF_Y holds its records, stably sorted by key", what.c_str());
    m.hist_clear(what);
    m.check_end(what);
    out(fmt("%s: L1 GO -> all READY %s; L2 GO -> all READY %s  [meas: SortTest p1]", what.c_str(),
            grouped(l1_ready - m.cc.phase_log()[base][1]).c_str(), grouped(l2_ready - m.cc.phase_log()[base + 1][1]).c_str()));
}

// ============================================================================================== the sort, end to end
void load_input(Machine& m, const Keys& k, int fmt_) {
    for (unsigned i = 0; i < 64; i++) {
        mach::TileRef p = m.c.tile(i);
        for (uint32_t j = 0; j < k[i].size(); j++) {
            if (fmt_ == FMT_K128) {
                p.sram_poke(BUF_IN + j, k[i][j]);
                p.sram_poke64(BUF_VIN + 4 * j, value_half(i, j, 0)); p.sram_poke64(BUF_VIN + 4 * j + 2, value_half(i, j, 1));
            } else p.sram_poke64(BUF_IN + 2 * j, uint64_t(k[i][j]) | value32(i, j) << 32);
        }
        m.poke_s(i, S_NIN, uint32_t(k[i].size()));
    }
}

struct Step {
    std::string name; int64_t cycles = 0, go_ready = 0, ov_load = 0; int phases = 0;
    std::map<std::string, int> peak;               // PhaseLog's peaks, the maximum over the step's phases
    int pk(const char* k) const { auto it = peak.find(k); return it == peak.end() ? 0 : it->second; }
};
struct Result { std::string path; bool stage2 = false, bt_wanted = false; int64_t total = 0, oracle = 0; std::vector<Step> steps; int checks_failed = 0; };

void write_csv(const std::string& f, const std::string& label, int fmt_, int n, const Opts& op, unsigned ready, const Result& r) {
    std::string sb = "label,fmt,n,seed,ready_latency,params,path,stage2,bt_wanted,step,cycles,go_ready,phases,overlay_load,peak_link,peak_eject,peak_weIn,peak_gatherQ,peak_area,checks_failed\n";
    std::string params;
    if (op.cfg.empty()) params = "default";
    else for (size_t i = 0; i < op.cfg.size(); i++) params += (i ? " " : "") + op.cfg[i].first + "=" + op.cfg[i].second;
    const std::string head = label + "," + (fmt_ == FMT_K128 ? "k128" : "kv32") + "," + std::to_string(n) + "," + std::to_string(op.shuffle) + ","
        + std::to_string(ready) + "," + params + "," + r.path + "," + (r.stage2 ? "true" : "false") + "," + (r.bt_wanted ? "true" : "false");
    for (const Step& s : r.steps)
        sb += head + "," + s.name + "," + std::to_string(s.cycles) + "," + std::to_string(s.go_ready) + "," + std::to_string(s.phases) + ","
            + std::to_string(s.ov_load) + "," + std::to_string(s.pk("link")) + "," + std::to_string(s.pk("eject")) + "," + std::to_string(s.pk("weIn"))
            + "," + std::to_string(s.pk("gatherQ")) + "," + std::to_string(s.pk("area")) + "," + std::to_string(r.checks_failed) + "\n";
    sb += head + ",TOTAL," + std::to_string(r.total) + ",,,,,,,,," + std::to_string(r.checks_failed) + "\n";
    if (!sortkit::write_file(f, sb)) { std::fprintf(stderr, "sort_test: cannot write %s\n", f.c_str()); std::exit(1); }
}

// SortTest.sortRun; false if the sequence didn't finish
bool sort_run(const std::string& label, const Keys& k, int fmt_, bool shaped, const Opts& op, const std::string& fp_name) {
    int n = 0; for (auto& x : k) n = std::max(n, int(x.size()));
    const std::string what = "sort " + label + (fmt_ == FMT_K128 ? " 128-bit values" : " 32-bit values") + (shaped ? " shaped" : "");
    suite::group(what);
    const int failed0 = int(suite::failed);
    Oracle o(k, fmt_, shaped);
    Machine m(op, fp_name);
    m.c.phases().namer = [](int32_t w) { return phase_name(w); };
    m.boot();
    load_input(m, k, fmt_);
    if (m.im.syms.count("ORACLE")) { std::fprintf(stderr, "sort_test: an image with SortTest's oracle split is not supported (NOTES, Q5)\n"); std::exit(2); }
    // the fixed sequence (skew-v2.md 5): L1 SPLIT XB RED XC TH SWEEP DESC K K1.0-4 K2.0-4 [V V1.0-4 V2.0-4] L2
    std::vector<uint32_t> cmd = {CMD_L1 << 24 | uint32_t(fmt_), CMD_SPLIT << 24 | (shaped ? 1u : 0u), CMD_XB << 24, CMD_RED << 24,
                                 CMD_XC << 24, CMD_TH << 24 | (op.force_bt ? 1u : 0u), CMD_SWEEP << 24, CMD_DESC << 24, CMD_K << 24};
    for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K1 << 24 | l);
    for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K2 << 24 | l);
    if (fmt_ == FMT_K128) {
        cmd.push_back(CMD_V << 24);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V1 << 24 | l);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V2 << 24 | l);
    }
    cmd.push_back(CMD_L2 << 24);
    const int barriers = shaped ? 65 : 66;         // the split's own barriers (BINDING.md)
    m.cc.command(Machine::lw_broadcast(), cmd[0]).go();
    for (size_t i = 1; i <= cmd.size(); i++) {
        if (cmd[i - 1] >> 24 == CMD_SPLIT)
            for (int b = 0; b < barriers; b++) { m.cc.command(Machine::lw_broadcast(), CMD_BARRIER << 24); m.at_all_ready(nullptr).go(); }
        const uint32_t w = i < cmd.size() ? cmd[i] : CMD_EXIT << 24;
        m.cc.command(Machine::lw_broadcast(), w);
        m.at_all_ready(nullptr).go();
    }
    uint64_t maxc = 400000000ull;
    if (const char* e = std::getenv("SORT_MAXCYCLES")) maxc = std::strtoull(e, nullptr, 10);
    const bool ok = m.finish(maxc);
    suite::check(ok, "%s: the sequence ran to its end", what.c_str());
    if (!ok) { m.dump_state(); return false; }
    m.c.phases().close();

    // ---- verification
    int bad = 0, size_bad = 0, r_bad = 0, v_bad = 0;
    for (unsigned q = 0; q < 64; q++) {
        mach::TileRef p = m.c.tile(q);
        if (m.peek_s(q, S_NRECV) != o.expect[q].size()) size_bad++;
        bad += mismatches(m, q, BUF_Y, o.expect[q]);
        if (fmt_ == FMT_K128)
            for (uint32_t r = 0; r < o.source[q].size(); r++) {
                const uint32_t sp = uint32_t(o.source[q][r] >> 32), sj = uint32_t(o.source[q][r]);
                if (p.sram_peek64(BUF_VS + 4 * r) != value_half(sp, sj, 0) || p.sram_peek64(BUF_VS + 4 * r + 2) != value_half(sp, sj, 1)) v_bad++;
            }
    }
    const uint32_t path0 = m.peek_s(0, S_PATH), plan0 = m.peek_s(0, S_PLAN), bt0 = m.peek_s(0, S_BT);
    int path_bad = 0;
    for (unsigned q = 0; q < 64; q++) if (m.peek_s(q, S_PATH) != path0 || m.peek_s(q, S_PLAN) != plan0) path_bad++;
    constexpr uint32_t GROUP = 0x400;              // TableStore.GROUP
    if (path0 <= 1)
        for (unsigned q = 0; q < 64; q++) for (unsigned g = 0; g < 15; g++) if ((m.c.tile(q).ts_peek(GROUP + 2 * g) & 0x3FFFF) != o.R[g]) r_bad++;
    suite::eq(0, path_bad, "%s: every tile took the same path", what.c_str());
    const int64_t headroom = int64_t(m.min_sp()) - int64_t(int32_t(m.sym("_end")));
    suite::check(headroom > 0, "%s: the stack stayed above .bss (headroom %" PRId64 " B)", what.c_str(), headroom);
    if (headroom < 100) out(fmt("    WARNING (lead, 3 Oct): stack headroom %" PRId64 " B, within 100 B of .bss", headroom));
    suite::eq(0, size_bad, "%s: every owner received R_(q+1) - R_q records", what.c_str());
    if (path0 <= 1) suite::eq(0, r_bad, "%s: every tile's GROUP R = the global group maxima (groups 0-14)", what.c_str());
    suite::eq(0, bad, "%s: the keys are in global (key, tile, position) order on every owner, %s", what.c_str(),
              fmt_ == FMT_KV32 ? "every value with its key" : "every ordinal naming its key's receive position");
    if (fmt_ == FMT_K128) suite::eq(0, v_bad, "%s: the final values (A5-A6) are in the same order as the final keys, every value its key's", what.c_str());
    m.hist_clear(what);
    m.check_end(what);

    // ---- measurement (PhaseLog): steps in sequence order, the split's barrier phases inside SPLIT
    const std::vector<mach::PhaseLog::Phase>& ph = m.c.phases().phases;
    std::vector<Step> steps;
    int last_sort = int(ph.size()) - 1;
    while (last_sort >= 0 && ph[size_t(last_sort)].name != "L2") last_sort--;
    int first_sort = 0;
    while (first_sort < last_sort && ph[size_t(first_sort)].name != "L1") first_sort++;
    for (int i = first_sort; i <= last_sort; i++) {
        const mach::PhaseLog::Phase& p = ph[size_t(i)];
        if (steps.empty() || steps.back().name != p.name) { steps.push_back(Step{}); steps.back().name = p.name; }
        Step& cur = steps.back();
        const int64_t next = i < last_sort ? ph[size_t(i) + 1].go_at : p.end_at;
        cur.cycles += next - p.go_at; cur.go_ready += p.cycles(); cur.phases++;
        for (int q = 0; q < mach::PhaseLog::NP; q++) {
            auto it = cur.peak.find(mach::PhaseLog::PEAKS[q]);
            if (it == cur.peak.end()) cur.peak[mach::PhaseLog::PEAKS[q]] = int(p.peak[q]);
            else it->second = std::max(it->second, int(p.peak[q]));
        }
        auto bp = m.pf.by_phase.find(size_t(i));
        if (bp != m.pf.by_phase.end()) cur.ov_load += Profile::get(bp->second, "ov_load");
    }
    const int64_t total = ph[size_t(last_sort)].end_at - ph[size_t(first_sort)].go_at;
    const int64_t oracle_cyc = 0;                  // the oracle split isn't ported (Q5)
    const std::string path = std::string(PATHS[path0 < 5 ? path0 : 0]) + (plan0 != 0 ? std::string(" (stage 2 ran") + (bt0 != 0 && path0 != 3 ? "; BT wanted)" : ")") : "");
    if (op.force_bt) suite::check(path0 == 3, "%s: forced BT took the BT path (%s)", label.c_str(), PATHS[path0 < 5 ? path0 : 0]);

    std::string sb = what + " [meas: SortTest]: path " + path + "; steps (GO -> next GO):";
    int64_t empty = 0, go_ready_sum = 0; int empty_n = 0, gq = 0;
    for (const Step& s : steps) {
        gq = std::max(gq, s.pk("gatherQ"));
        go_ready_sum += s.go_ready;
        if (skew_cmd(s.name) && s.cycles <= 150) { empty += s.cycles; empty_n++; continue; }
        sb += " " + s.name + " " + grouped(s.cycles) + (s.ov_load > 0 ? " (overlay load " + std::to_string(s.ov_load) + ")" : "") + ";";
    }
    sb += " " + std::to_string(empty_n) + " empty commands " + grouped(empty);
    out(sb);
    int64_t sum_r = 0; for (uint32_t r : o.R) sum_r += r;
    out("    TOTAL " + grouped(total - oracle_cyc) + " cycles (L1 GO -> L2 all READY; real split), of which C&C turnaround "
        + grouped(total - go_ready_sum) + "; ready_latency " + std::to_string(m.c.cfg().ready_latency) + "; direct-path bound 4*sum(R) = "
        + grouped(4 * sum_r) + (fmt_ == FMT_K128 ? ", V 8*sum(R) = " + grouped(8 * sum_r) : std::string()) + "; GATHER queue peak "
        + std::to_string(gq) + " of 16; stack headroom " + std::to_string(headroom) + " B");
    for (const Step& s : steps)
        if (peak_step(s.name) && s.cycles > 150)
            out(fmt("    peaks in %s: link %d, eject %d, write-engine input %d, area %d", s.name.c_str(), s.pk("link"), s.pk("eject"), s.pk("weIn"), s.pk("area")));
    {   // Chip.agReport, summed over the tiles
        uint64_t rot_late = 0, max_rot = 0, grp_late = 0, max_grp = 0, stalls = 0; int late_tiles = 0, max_drift = 0, drift_group = -1;
        uint64_t z = 0, zg = 0, gs = 0;
        for (unsigned t = 0; t < 64; t++) {
            const units::AddrGen& a = m.tunit<units::AddrGen>(t, "ag");
            rot_late += a.rotations_late; max_rot = std::max(max_rot, a.max_rot_late); grp_late += a.groups_late_prefetch;
            max_grp = std::max(max_grp, a.max_group_late); stalls += a.stall_cycles; if (a.rotations_late > 0) late_tiles++;
            z += a.cum_zero_groups + a.zero_groups; zg += a.cum_zero_groups_with_gap + a.zero_groups_with_gap; gs += a.cum_group_stall + a.group_stall_cycles;
        }
        for (unsigned g = 0; g < units::AddrGen::MAXGROUPS; g++) {
            int64_t lo = INT64_MAX, hi = INT64_MIN;
            for (unsigned t = 0; t < 64; t++) { const int64_t v = m.tunit<units::AddrGen>(t, "ag").group_start[g]; if (v < 0) continue; lo = std::min(lo, v); hi = std::max(hi, v); }
            if (hi >= lo && hi - lo > max_drift) { max_drift = int(hi - lo); drift_group = int(g); }
        }
        out(fmt("    address generator: %" PRIu64 " request stall cycles; rotations late %" PRIu64 " on %d tiles (max %" PRIu64 " cycles); group starts waiting for read-ahead %" PRIu64 " (max %" PRIu64 ", timetable); drift between tiles at a group start %d%s",
                stalls, rot_late, late_tiles, max_rot, grp_late, max_grp, max_drift, drift_group >= 0 ? fmt(" (group %d)", drift_group).c_str() : ""));
        out(fmt("    M14: zero-length groups %" PRIu64 " (summed over tiles), of which with a gap %" PRIu64 " (1 + gap since 10 October); group-run stall cycles %" PRIu64 " (drift)", z, zg, gs));
    }
    if (std::getenv("SORT_PROFILE")) {             // Java's Map.toString
        std::string l = "    tile 0 cycles per function: {";
        for (size_t i = 0; i < m.pf.prof.size(); i++) l += (i ? ", " : "") + m.pf.prof[i].first + "=" + std::to_string(m.pf.prof[i].second);
        out(l + "}");
    }
    if (!m.pf.watch.empty()) {                     // per step: the phases of one name together, in first-seen order
        std::vector<std::pair<std::string, std::vector<std::array<int64_t, 64>>>> by_step;
        for (int i = 0; i <= last_sort; i++) {
            auto w = m.pf.watch_by_phase.find(size_t(i));
            if (w == m.pf.watch_by_phase.end()) continue;
            const std::string& nm = ph[size_t(i)].name;
            size_t at = by_step.size();
            for (size_t j = 0; j < by_step.size(); j++) if (by_step[j].first == nm) at = j;
            if (at == by_step.size()) by_step.emplace_back(nm, std::vector<std::array<int64_t, 64>>(m.pf.watch.size(), std::array<int64_t, 64>{}));
            for (size_t f = 0; f < w->second.size(); f++) for (unsigned t = 0; t < 64; t++) by_step[at].second[f][t] += w->second[f][t];
        }
        for (auto& e : by_step) {
            std::string b = "    watch in " + e.first + " (cycles per tile: mean / max):";
            for (size_t f = 0; f < m.pf.watch.size(); f++) {
                int64_t sum = 0, mx = 0; for (int64_t v : e.second[f]) { sum += v; mx = std::max(mx, v); }
                if (mx > 0) b += " " + m.pf.watch[f] + " " + grouped(sum / 64) + " / " + grouped(mx) + ";";
            }
            out(b);
        }
    }
    if (std::getenv("SORT_PHASES")) out(m.c.phases().report());
    if (const char* pr = std::getenv("SORT_PROF")) out(std::string("    sort.prof ") + pr + ": tile 0 cycles " + std::to_string(m.pf.prof_cycles));
    Result res;
    res.path = PATHS[path0 < 5 ? path0 : 0]; res.stage2 = plan0 != 0; res.bt_wanted = bt0 != 0; res.total = total - oracle_cyc; res.oracle = oracle_cyc;
    res.steps = steps; res.checks_failed = int(suite::failed) - failed0;
    if (!op.phases_csv.empty()) {
        std::string err;
        if (!m.c.phases().csv(op.phases_csv, err)) { std::fprintf(stderr, "sort_test: %s\n", err.c_str()); std::exit(1); }
    }
    if (!op.csv.empty()) write_csv(op.csv, label, fmt_, n, op, m.c.cfg().ready_latency, res);
    return true;
}

// ============================================================================================== Inputs (D5)
std::string inputs_line(const std::string& keys) {
    std::vector<std::pair<std::string, std::string>> m;
    auto hash = [](const std::string& p) { return sortkit::is_file(p) ? sortkit::sha256_file(p) : std::string("missing"); };
    char self[4096]; const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    m.emplace_back("sort_test", n > 0 ? hash(std::string(self, size_t(n))) : "missing");   // Q2: v1.3's meshsim.jar=...
    m.emplace_back("sw/bootrom.hex", hash("fw/boot/bootrom.hex"));
    const std::string dir = image_dir();
    for (const char* f : {"sort.code.bin", "sort.data.bin", "sort.sym"}) m.emplace_back(std::string("image/") + f, hash(dir + "/" + f));
    m.emplace_back("hops.txt", hash("data/hops.txt"));
    if (!keys.empty()) {                            // <the key file's directory>/<file>
        char abs[4096];
        std::string a = realpath(keys.c_str(), abs) ? abs : keys;
        const size_t s = a.rfind('/');
        const std::string file = s == std::string::npos ? a : a.substr(s + 1), parent = s == std::string::npos ? "" : a.substr(0, s);
        const size_t s2 = parent.rfind('/');
        m.emplace_back((parent.empty() ? "" : (s2 == std::string::npos ? parent : parent.substr(s2 + 1)) + "/") + file, hash(keys));
    }
    std::string l = "inputs:";
    for (auto& e : m) l += " " + e.first + "=" + e.second;
    return l;
}

}  // namespace

int main(int argc, char** argv) {
    if (!sim::check::kOn) {                         // PhaseLog (the measurement) is kept in these builds only
        std::fprintf(stderr, "sort_test: build it with MODE=suite (or debug): W20's PhaseLog runs only there\n");
        return 2;
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    Opts op;
    auto is_int = [](const std::string& s) { size_t i = s[0] == '-' ? 1 : 0; if (i == s.size()) return false; for (; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') return false; return true; };
    if (!args.empty() && is_int(args[0])) { op.shuffle = std::stoll(args[0]); args.erase(args.begin()); }
    bool full = false;
    for (auto it = args.begin(); it != args.end(); ) if (*it == "full") { full = true; args.erase(it); break; } else ++it;
    std::vector<std::pair<std::string, std::string>> kv;
    auto put = [&](const std::string& k, const std::string& v) { for (auto& e : kv) if (e.first == k) { e.second = v; return; } kv.emplace_back(k, v); };
    for (const std::string& s : args) { const size_t e = s.find('='); if (e != std::string::npos) put(s.substr(0, e), s.substr(e + 1)); }
    auto get = [&](const std::string& k) -> const std::string* { for (auto& e : kv) if (e.first == k) return &e.second; return nullptr; };
    auto has = [&](const char* w) { return std::find(args.begin(), args.end(), w) != args.end(); };
    for (auto& e : kv) if (e.first.rfind("cfg.", 0) == 0) op.cfg.emplace_back(e.first.substr(4), e.second);
    if (get("ready")) op.ready_latency = std::atoi(get("ready")->c_str());
    if (get("csv")) op.csv = *get("csv");
    if (get("phases")) op.phases_csv = *get("phases");
    op.force_bt = get("force") && *get("force") == "bt";
    const std::string only = get("only") ? *get("only") : "";
    out(inputs_line(has("run") && get("keys") ? *get("keys") : ""));
    if (has("p1")) {
        const int n = full ? 65536 : 2000;
        struct S { const char* kind; int n; int fmt; } sets[] = {{"uniform", n, FMT_K128}, {"uniform", n, FMT_KV32}, {"few", full ? 65536 : 1000, FMT_K128},
            {"ragged", full ? 32768 : 1000, FMT_KV32}, {"sorted", full ? 4096 : 500, FMT_K128}, {"equal", full ? 4096 : 500, FMT_KV32}};
        for (auto& s : sets) if (only.empty() || only == s.kind) p1(s.kind, s.n, s.fmt, op);
    }
    if (has("sort")) {
        const int n = full ? 65536 : 2000;
        struct S { const char* kind; int n; int fmt; } sets[] = {{"uniform", n, FMT_K128}, {"uniform", n, FMT_KV32}, {"few", full ? 65536 : 1000, FMT_K128},
            {"ragged", full ? 32768 : 1000, FMT_KV32}, {"sorted", n, FMT_K128}, {"equal", n, FMT_KV32}, {"equal", n, FMT_K128},
            {"reverse", n, FMT_K128}, {"reverse", n, FMT_KV32}};
        for (auto& x : sets) {
            const std::string nm = std::string(x.kind) + (x.fmt == FMT_KV32 ? "-kv32" : "-k128");
            if (only.empty() || only == nm)
                sort_run(std::string(x.kind) + " n=" + std::to_string(x.n), builtin_keys(x.kind, x.n, seed_of(x.kind, 11)), x.fmt, false, op, nm);
        }
        if (only.empty() || only == "shaped") { const int sn = full ? 32768 : 1000; sort_run("ragged n=" + std::to_string(sn), builtin_keys("ragged", sn, seed_of("ragged", 11)), FMT_K128, true, op, "shaped-ragged-k128"); }
    }
    if (has("run")) {
        const int fmt_ = get("fmt") && *get("fmt") == "kv32" ? FMT_KV32 : FMT_K128;
        Keys k; std::string label;
        if (get("keys")) {
            std::string err; k = read_keys(*get("keys"), err);
            if (!err.empty()) { std::fprintf(stderr, "sort_test: %s\n", err.c_str()); return 2; }
            std::string f = *get("keys"); const size_t s = f.rfind('/'); if (s != std::string::npos) f = f.substr(s + 1);
            label = get("label") ? *get("label") : sortkit::remove_all(f, ".keys");
        } else {
            const std::string kind = get("kind") ? *get("kind") : "uniform";
            const int n = get("n") ? std::atoi(get("n")->c_str()) : full ? 65536 : 2000;
            k = builtin_keys(kind, n, seed_of(kind, 11)); label = get("label") ? *get("label") : kind + " n=" + std::to_string(n);
        }
        op.label = label;
        sort_run(label, k, fmt_, has("shaped"), op, label);
    }
    const int rc = suite::done(std::string("SortTest") + (op.shuffle >= 0 ? " (shuffled, seed " + std::to_string(op.shuffle) + ")" : ""));
    return rc != 0 ? rc : g_fp_ok ? 0 : 1;
}
