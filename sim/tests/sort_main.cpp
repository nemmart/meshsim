// sort_test (WORKER24, P6, rows D1-D4, H4): v1.3's SortTest (sim/ref/meshsim/test/SortTest.java) in C++, on W20's machine
// harness (tests/mach.h), its arguments and its output line for line, so run_tests.sh's sort steps are log-equal:
//
//   sort_test [seed] p1 [full] [only=<set>]                          the local sorts L1, L2
//   sort_test [seed] sort [full] [only=<set>]                        every built-in set at 64 x <= 2,000
//   sort_test [seed] run kind=<builtin>|keys=<file> [n=<per tile>] fmt=k128|kv32 [shaped] [label=..] [ready=N]
//             [cfg.<Config field>=<value> ...] [csv=<file>] [phases=<file>] [force=bt (TEST ONLY)]
//   seed: -1 the fixed order, >= 0 the framework's debug shuffle with that seed (mach.h: SHUFFLING).
//   Options (SortTest's -Dsort.*): SORT_BUILD=<dir> (the image's directory; default the harness's search path),
//   SORT_MAXCYCLES=<n>.
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
// PENDING (W20's PhaseLog, H3): the per-phase peaks (the "peaks in" lines, the GATHER queue peak, the CSV's peak
// columns) and the phases CSV; until then the phases are C&C's own log (GO and all READY per GO) and the peaks 0.
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

#include "mach.h"
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
    std::map<std::string, uint32_t> sym;
    void load(const std::string& path) {          // nm -S: addr [size] type name
        for (const std::string& l : sortkit::read_lines(path)) {
            std::vector<std::string> f = sortkit::split_ws(l);
            if (f.size() < 3) continue;
            sym.emplace(f.back(), uint32_t(std::stoul(f[0], nullptr, 16)));
        }
    }
};

// ============================================================================================== a machine running the sort
struct Machine {
    Mach m;
    mach::Chip& c;
    mach::Cc& cc;
    Mach::Image im;
    Syms syms;
    int64_t all_ready_at = -1;
    // C&C's GOs and all-READYs as they happen (ref's PhaseLog.onGo / onAllReady), read between cycles from C&C's
    // last_go_at and last_all_ready_at (W20's PhaseLog, H3, will take this over)
    std::vector<int64_t> gos, readies;
    int64_t seen_go = -1, seen_ready = -1;
    void observe() {
        if (cc.last_go_at() != seen_go) { seen_go = cc.last_go_at(); if (seen_go >= 0) gos.push_back(seen_go); }
        // C&C's last_all_ready_at is ref's c.cycle in C&C's step (t - 1); PhaseLog.onAllReady is called with t
        if (cc.last_all_ready_at() != seen_ready) { seen_ready = cc.last_all_ready_at(); if (seen_ready >= 0) readies.push_back(seen_ready + 1); }
    }
    // phase i: {GO, its end}: the first all READY after its GO (PhaseLog.onAllReady), else the next GO (onGo's close)
    std::vector<std::array<int64_t, 2>> phases() const {
        std::vector<std::array<int64_t, 2>> v;
        for (size_t i = 0; i < gos.size(); i++) {
            int64_t end = i + 1 < gos.size() ? gos[i + 1] : -1;
            // strictly between its GO and the next (C&C's last_all_ready_at also moves at a GO: those aren't phase ends)
            for (int64_t r : readies) if (r > gos[i] && (i + 1 >= gos.size() || r < gos[i + 1])) { end = r; break; }
            v.push_back({gos[i], end});
        }
        return v;
    }
    explicit Machine(const Opts& o) : m(o.config()), c(m.c), cc(m.c.cc()) {
        im = Mach::Image::load_dir(image_dir(), "sort");
        syms.load(image_dir() + "/sort.sym");
        im.syms = syms.sym;
    }
    uint32_t sym(const std::string& s) const { return im.sym(s); }
    // SortTest.Machine.boot (spec 6.2): program memory, data memory from _data_start, CMD_JUMP to _start, GO when all are READY
    void boot() {
        cc.load_words(lw_broadcast(), 0x01000000u, im.code).load_words(lw_broadcast(), sym("_data_start"), im.data)
          .command(lw_broadcast(), 0x4Au << 24, {sym("_start")}).run_when_ready();
        const bool ok = c.run_until([&] { observe(); return !cc.busy() && all_waiting(); }, 100000);
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
            units::Rv32& r = c.tile(k).unit<units::TileCore>("core").rv;
            r.min_sp_watch = true; r.min_sp = r.x[2] != 0 ? int32_t(r.x[2]) : INT32_MAX;
        }
    }
    int32_t min_sp() {
        int32_t m = INT32_MAX;
        for (unsigned k = 0; k < 64; k++) m = std::min(m, c.tile(k).unit<units::TileCore>("core").rv.min_sp);
        return m;
    }
    bool finish(uint64_t max) { arm_sp(); return c.run_until([&] { observe(); return !cc.busy() && c.all_stopped(); }, max); }
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
        out(fmt("    state at cycle %" PRId64 " (C&C at phase %zu), violations %" PRIu64 ":", c.cycle(), gos.size(), c.violations()));
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
    Machine m(op);
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
    const size_t base = m.gos.size();
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
            grouped(l1_ready - m.gos[base]).c_str(), grouped(l2_ready - m.gos[base + 1]).c_str()));
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

// PhaseLog's phases, from C&C's own log until W20's PhaseLog lands: phase i is GO i (its command word, its GO), ending at
// the first all READY C&C sees after it (the next GO's wait), as PhaseLog.onGo / onAllReady close it.
struct Phase { std::string name; uint32_t word = 0; int64_t go_at = 0, end_at = -1; int64_t cycles() const { return end_at - go_at; } };
struct Step {
    std::string name; int64_t cycles = 0, go_ready = 0, ov_load = 0; int phases = 0;
    std::map<std::string, int> peak;               // PENDING (W20's PhaseLog): link, eject, weIn, gatherQ, area
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
bool sort_run(const std::string& label, const Keys& k, int fmt_, bool shaped, const Opts& op) {
    int n = 0; for (auto& x : k) n = std::max(n, int(x.size()));
    const std::string what = "sort " + label + (fmt_ == FMT_K128 ? " 128-bit values" : " 32-bit values") + (shaped ? " shaped" : "");
    suite::group(what);
    const int failed0 = int(suite::failed);
    Oracle o(k, fmt_, shaped);
    Machine m(op);
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
    std::vector<uint32_t> words = {0x4Au << 24};   // every GO's command word, in order (the boot's first)
    m.cc.command(Machine::lw_broadcast(), cmd[0]).go(); words.push_back(cmd[0]);
    for (size_t i = 1; i <= cmd.size(); i++) {
        if (cmd[i - 1] >> 24 == CMD_SPLIT)
            for (int b = 0; b < barriers; b++) { m.cc.command(Machine::lw_broadcast(), CMD_BARRIER << 24); m.at_all_ready(nullptr).go(); words.push_back(CMD_BARRIER << 24); }
        const uint32_t w = i < cmd.size() ? cmd[i] : CMD_EXIT << 24;
        m.cc.command(Machine::lw_broadcast(), w);
        m.at_all_ready(nullptr).go(); words.push_back(w);
    }
    uint64_t maxc = 400000000ull;
    if (const char* e = std::getenv("SORT_MAXCYCLES")) maxc = std::strtoull(e, nullptr, 10);
    const bool ok = m.finish(maxc);
    suite::check(ok, "%s: the sequence ran to its end", what.c_str());
    if (!ok) { m.dump_state(); return false; }

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

    // ---- measurement: the phases (C&C's log; PhaseLog's when W20's lands), steps in sequence order
    const auto pl = m.phases();
    std::vector<Phase> ph(pl.size());
    for (size_t i = 0; i < pl.size(); i++) {
        ph[i].word = i < words.size() ? words[i] : 0;
        ph[i].name = phase_name(int64_t(ph[i].word));
        ph[i].go_at = pl[i][0];
        ph[i].end_at = pl[i][1];
    }
    std::vector<Step> steps;
    int last_sort = int(ph.size()) - 1;
    while (last_sort >= 0 && ph[size_t(last_sort)].name != "L2") last_sort--;
    int first_sort = 0;
    while (first_sort < last_sort && ph[size_t(first_sort)].name != "L1") first_sort++;
    for (int i = first_sort; i <= last_sort; i++) {
        const Phase& p = ph[size_t(i)];
        if (steps.empty() || steps.back().name != p.name) { steps.push_back(Step{}); steps.back().name = p.name; }
        Step& cur = steps.back();
        const int64_t next = i < last_sort ? ph[size_t(i) + 1].go_at : p.end_at;
        cur.cycles += next - p.go_at; cur.go_ready += p.cycles(); cur.phases++;
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
            const units::AddrGen& a = m.c.tile(t).unit<units::AddrGen>("ag");
            rot_late += a.rotations_late; max_rot = std::max(max_rot, a.max_rot_late); grp_late += a.groups_late_prefetch;
            max_grp = std::max(max_grp, a.max_group_late); stalls += a.stall_cycles; if (a.rotations_late > 0) late_tiles++;
            z += a.cum_zero_groups + a.zero_groups; zg += a.cum_zero_groups_with_gap + a.zero_groups_with_gap; gs += a.cum_group_stall + a.group_stall_cycles;
        }
        for (unsigned g = 0; g < units::AddrGen::MAXGROUPS; g++) {
            int64_t lo = INT64_MAX, hi = INT64_MIN;
            for (unsigned t = 0; t < 64; t++) { const int64_t v = m.c.tile(t).unit<units::AddrGen>("ag").group_start[g]; if (v < 0) continue; lo = std::min(lo, v); hi = std::max(hi, v); }
            if (hi >= lo && hi - lo > max_drift) { max_drift = int(hi - lo); drift_group = int(g); }
        }
        out(fmt("    address generator: %" PRIu64 " request stall cycles; rotations late %" PRIu64 " on %d tiles (max %" PRIu64 " cycles); group starts waiting for read-ahead %" PRIu64 " (max %" PRIu64 ", timetable); drift between tiles at a group start %d%s",
                stalls, rot_late, late_tiles, max_rot, grp_late, max_grp, max_drift, drift_group >= 0 ? fmt(" (group %d)", drift_group).c_str() : ""));
        out(fmt("    M14: zero-length groups %" PRIu64 " (summed over tiles), of which with a gap %" PRIu64 " (1 + gap since 10 October); group-run stall cycles %" PRIu64 " (drift)", z, zg, gs));
    }
    Result res;
    res.path = PATHS[path0 < 5 ? path0 : 0]; res.stage2 = plan0 != 0; res.bt_wanted = bt0 != 0; res.total = total - oracle_cyc; res.oracle = oracle_cyc;
    res.steps = steps; res.checks_failed = int(suite::failed) - failed0;
    if (!op.phases_csv.empty()) std::fprintf(stderr, "sort_test: phases=%s: PENDING (W20's PhaseLog, H3); not written\n", op.phases_csv.c_str());
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
                sort_run(std::string(x.kind) + " n=" + std::to_string(x.n), builtin_keys(x.kind, x.n, seed_of(x.kind, 11)), x.fmt, false, op);
        }
        if (only.empty() || only == "shaped") { const int sn = full ? 32768 : 1000; sort_run("ragged n=" + std::to_string(sn), builtin_keys("ragged", sn, seed_of("ragged", 11)), FMT_K128, true, op); }
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
        sort_run(label, k, fmt_, has("shaped"), op);
    }
    return suite::done(std::string("SortTest") + (op.shuffle >= 0 ? " (shuffled, seed " + std::to_string(op.shuffle) + ")" : ""));
}
