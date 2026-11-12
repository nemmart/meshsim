// sweep: v1.3's Sweep (sim/ref/meshsim/test/Sweep.java) in C++, on the machine harness (tests/mach.h),
// its output line for line and its CSV byte for byte: the sensitivity of four workloads' main phase (GO -> all READY,
// PhaseLog) to the machine's uncertain parameters, one at a time around the defaults, and an all-pessimistic corner.
//
//   sweep [quick|full]          writes results/sweep-<mode>.csv (run from sim/, as run_tests.sh runs it)
//
// The workloads run from C on the machine as specified: the 16-group all-to-all (a2a.c; uniform, and in full mode the
// zipfraw stream matrix), gather 1 (gather.c, k = 2), 63 searches through the read port (search.c), a one-command phase's
// round trip (cmd.c). The disjoint-buffer check is off, as in ref. PhaseLog's peaks exist in the debug and suite
// builds only: build it with MODE=suite.
#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "check.h"
#include "mach.h"
#include "sort_kit.h"

using mach::Mach;
using mach::MachConfig;

namespace {

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    va_list ap; va_start(ap, f);
    char buf[4096]; vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}
// Java's %,Nd: grouped, right-aligned in N
std::string grouped_w(int64_t v, int w) { std::string s = sortkit::grouped(v); return s.size() < size_t(w) ? std::string(size_t(w) - s.size(), ' ') + s : s; }

// java.util.Random: nextInt() and nextInt(bound)
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

constexpr unsigned BROADCAST = 126;              // LinkWord.BROADCAST (units/net.h)

struct Res { int64_t cycles; uint64_t violations; int64_t link, eject, area, weIn; std::string first_violation; };
struct Workload { std::string name; std::function<Res(const MachConfig&)> run; };
struct Point { std::string param, value; std::function<void(MachConfig&)> set; };

// Sweep.base: Config.machine() without the disjoint-buffer check
MachConfig base() { MachConfig k = MachConfig::machine(); k.check_disjoint = false; return k; }
void set(MachConfig& k, const char* f, const std::string& v) {
    std::string err;
    if (!k.set(f, v, err)) { std::fprintf(stderr, "sweep: %s\n", err.c_str()); std::exit(2); }
}

// Sweep.result: the phase's GO -> all READY, the run's violations, the phase's peaks
Res result(Mach& m, const mach::PhaseLog::Phase* p) {
    auto pk = [&](const char* n) -> int64_t {
        if (!p) return 0;
        for (int i = 0; i < mach::PhaseLog::NP; i++) if (std::string(mach::PhaseLog::PEAKS[i]) == n) return p->peak[i];
        return 0;
    };
    const uint64_t v = m.c.violations();
    return Res{p ? p->cycles() : -1, v, pk("link"), pk("eject"), pk("area"), pk("weIn"), v > 0 ? m.c.violation_messages().at(0) : ""};
}
const mach::PhaseLog::Phase* phase(Mach& m, size_t i) { auto& ph = m.c.phases().phases; return ph.size() > i ? &ph[i] : nullptr; }

// The 16-group all-to-all from C (a2a.c): configure, start (measured), exit
Workload a2a(const std::string& name, std::shared_ptr<mach::A2A> a) {
    return {name, [a](const MachConfig& k) {
        Mach m(k); m.c.phases();
        for (unsigned s = 0; s < 64; s++) {
            mach::TileRef p = m.c.tile(s);
            for (size_t i = 0; i < a->rec[s].size(); i++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(i), uint64_t(a->rec[s][i]));
            for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(a->cnt[s][d]));
            for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(a->cnt[src][s]));
            for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(a->R[g]));
            p.sram_poke(0xF0000 + 144, 0);
        }
        m.boot("a2a");
        m.c.cc().command(BROADCAST, 1u << 24).go().command(BROADCAST, 2u << 24).go().command(BROADCAST, 3u << 24).go();
        m.run([&] { return m.c.all_stopped(); }, 10000000);
        m.c.phases().close();
        Res r = result(m, phase(m, 2));
        if (a->verify(m.c) != 0) return Res{r.cycles, r.violations + 1, r.link, r.eject, r.area, r.weIn, "records did not all land"};
        return r;
    }};
}
// Gather 1 from C (gather.c), k = 2
Workload gather(int n) {
    return {"gather " + std::to_string(n) + " k=2", [n](const MachConfig& k) {
        Mach m(k); m.c.phases();
        for (unsigned t = 0; t < 64; t++) {
            JRandom r(100 + int64_t(t));
            std::vector<int32_t> pm(static_cast<size_t>(n)); for (int i = 0; i < n; i++) pm[size_t(i)] = i;
            for (int i = n - 1; i > 0; i--) { const int j = r.next_int(i + 1); std::swap(pm[size_t(i)], pm[size_t(j)]); }
            mach::TileRef p = m.c.tile(t);
            for (int i = 0; i < n; i++) p.sram_poke64(0x60000 + 2 * uint32_t(i), uint64_t(uint32_t(pm[size_t(i)])) << 32 | uint32_t(i));
        }
        m.boot("gather");
        m.c.cc().command(BROADCAST, 1u << 24 | uint32_t(n)).go().command(BROADCAST, 2u << 24).go();
        m.run([&] { return m.c.all_stopped(); }, 10000000);
        m.c.phases().close();
        return result(m, phase(m, 1));
    }};
}
// 63 searches through the read port (search.c, packed, N = 65,536): GO -> all READY
Workload search() {
    return {"63 searches (read port)", [](const MachConfig& k) {
        Mach m(k); m.c.phases();
        JRandom r(65536);
        std::vector<int64_t> key(65536);
        for (unsigned t = 0; t < 64; t++) {
            for (auto& x : key) x = int64_t(uint32_t(r.next_int()));
            std::sort(key.begin(), key.end());
            mach::TileRef p = m.c.tile(t);
            for (uint32_t i = 0; i < key.size(); i++) p.sram_poke(i, uint32_t(key[i]));
            p.sram_poke(0xF0000, 65536);
            for (uint32_t i = 0; i < 64; i++) p.sram_poke(0xF0002 + i, uint32_t(r.next_int()));
        }
        m.boot("search");
        m.c.cc().command(BROADCAST, 1u << 24 | 1u << 8 | 63).go().command(BROADCAST, 2u << 24).go();
        m.run([&] { return m.c.all_stopped(); }, 10000000);
        m.c.phases().close();
        return result(m, phase(m, 1));
    }};
}
// A one-command phase's round trip (cmd.c, command 1): GO -> all READY
Workload round_trip() {
    return {"one-command round trip", [](const MachConfig& k) {
        Mach m(k); m.c.phases();
        m.boot("cmd");
        m.c.cc().command(BROADCAST, 1u << 24 | 1, {1}).go().command(BROADCAST, 1u << 24 | 2, {2}).go().command(BROADCAST, 4u << 24).go();
        m.run([&] { return m.c.all_stopped(); }, 100000);
        m.c.phases().close();
        return result(m, phase(m, 2));
    }};
}

}  // namespace

int main(int argc, char** argv) {
    if (!sim::check::kOn) { std::fprintf(stderr, "sweep: build it with MODE=suite (or debug): PhaseLog runs only there\n"); return 2; }
    const bool full = argc > 1 && std::string(argv[1]) == "full";
    std::vector<Workload> ws;
    ws.push_back(a2a("a2a uniform " + std::to_string(full ? 65536 : 4096), std::make_shared<mach::A2A>(7, full ? 65536 : 4096)));
    if (full) ws.push_back(a2a("a2a zipfraw (full)", std::make_shared<mach::A2A>(std::string("data/zipfraw.txt"))));
    ws.push_back(gather(full ? 65536 : 4096));
    ws.push_back(search());
    ws.push_back(round_trip());
    std::vector<Point> pts;
    pts.push_back({"default", "", [](MachConfig&) {}});
    for (int v : {1, 3, 4}) pts.push_back({"array read latency", std::to_string(v), [v](MachConfig& k) { set(k, "sramReadLatency", std::to_string(v)); }});
    for (int v : {4, 16}) pts.push_back({"area queue depth", std::to_string(v), [v](MachConfig& k) { set(k, "areaQueueDepth", std::to_string(v)); }});
    for (int v : {2, 3, 4}) pts.push_back({"ts_engine_latency", std::to_string(v), [v](MachConfig& k) { k.with_ts_latency(unsigned(v)); }});
    for (int v : {4, 8}) pts.push_back({"ready_latency", std::to_string(v), [v](MachConfig& k) { set(k, "readyLatency", std::to_string(v)); }});
    for (int v : {4, 8}) pts.push_back({"go/run latency", std::to_string(v), [v](MachConfig& k) { set(k, "goLatency", std::to_string(v)); set(k, "runLatency", std::to_string(v)); }});
    for (int v : {2, 4}) pts.push_back({"link latency", std::to_string(v), [v](MachConfig& k) { set(k, "linkLatency", std::to_string(v)); }});
    pts.push_back({"start skew", "off", [](MachConfig& k) { set(k, "skew", "false"); }});
    for (int s : {2, 3, 4, 5, 6}) pts.push_back({"start skew", "seed " + std::to_string(s), [s](MachConfig& k) { set(k, "skewSeed", std::to_string(s)); }});
    pts.push_back({"cm_latency", "4", [](MachConfig& k) { set(k, "cmLatency", "4"); }});
    pts.push_back({"corner (all pessimistic)", "", [](MachConfig& k) {
        set(k, "sramReadLatency", "4"); set(k, "areaQueueDepth", "4"); k.with_ts_latency(4); set(k, "readyLatency", "8");
        set(k, "goLatency", "8"); set(k, "runLatency", "8"); set(k, "linkLatency", "4"); set(k, "cmLatency", "4"); }});

    std::string sb = std::string("sensitivity sweep (") + (full ? "full" : "quick") + "), main-phase cycles GO -> all READY; delta vs default; peaks link/eject/area/weIn [meas: Sweep]\n";
    std::string csv = "param,value,workload,cycles,delta,violations,peak_link,peak_eject,peak_area,peak_weIn\n";
    std::vector<int64_t> def(ws.size()), lo(ws.size(), INT64_MAX), hi(ws.size(), INT64_MIN);
    for (const Point& pt : pts) {
        sb += fmt("  %-26s %-8s", pt.param.c_str(), pt.value.c_str());
        for (size_t w = 0; w < ws.size(); w++) {
            MachConfig k = base(); pt.set(k);
            const Res r = ws[w].run(k);
            if (pt.param == "default") def[w] = r.cycles;
            const int64_t d = r.cycles - def[w];
            if (r.violations == 0 && r.cycles > 0) { lo[w] = std::min(lo[w], r.cycles); hi[w] = std::max(hi[w], r.cycles); }
            sb += fmt(" | %-22s %9" PRId64 " %+6" PRId64 "  %2" PRId64 "/%2" PRId64 "/%" PRId64 "/%" PRId64 "%s", ws[w].name.c_str(), r.cycles, d, r.link, r.eject, r.area, r.weIn,
                      r.violations > 0 ? fmt(" VIOLATION(%" PRIu64 ")", r.violations).c_str() : "");
            csv += pt.param + "," + pt.value + "," + ws[w].name + "," + std::to_string(r.cycles) + "," + std::to_string(d) + "," + std::to_string(r.violations)
                + "," + std::to_string(r.link) + "," + std::to_string(r.eject) + "," + std::to_string(r.area) + "," + std::to_string(r.weIn) + "\n";
            if (r.violations > 0) std::fprintf(stderr, "%s %s %s: %s\n", pt.param.c_str(), pt.value.c_str(), ws[w].name.c_str(), r.first_violation.c_str());
        }
        sb += '\n';
        std::fputs(sb.c_str(), stdout); std::fflush(stdout); sb.clear();
    }
    std::string band = "  band over every point without a violation:\n";
    for (size_t w = 0; w < ws.size(); w++)
        band += fmt("    %-24s default ", ws[w].name.c_str()) + grouped_w(def[w], 9) + "; min " + grouped_w(lo[w], 9) + " ("
            + sortkit::fixed(100.0 * double(lo[w] - def[w]) / double(def[w]), 2, true) + "%), max " + grouped_w(hi[w], 9) + " ("
            + sortkit::fixed(100.0 * double(hi[w] - def[w]) / double(def[w]), 2, true) + "%)\n";
    std::fputs(band.c_str(), stdout);
    mkdir("results", 0777);
    const std::string path = std::string("results/sweep-") + (full ? "full" : "quick") + ".csv";
    if (!sortkit::write_file(path, csv)) { std::fprintf(stderr, "sweep: cannot write %s\n", path.c_str()); return 1; }
    return 0;
}
