// p4_drift.cpp (WORKER22; for the docs session, spec items 62 and 70, with the owner's agreement): EngineTest full's
// data sets (sim/ref/meshsim/test/EngineTest.java: the 16-group all-to-alls on uniform Data(7, 65536), zipfraw, trace
// and one99 at gaps 4, 0, 8; the local radix sort at 64 x 16,384 keys, 0/+1 skew) run as the suite runs them
// (tests/suite_engine.cpp, the same machine and loads), measuring per run:
//   - drift (4.13): the tiles whose ERRORS [3] is set (the register block's condition from the AG's drift event), and
//     the AG's drift events summed over the tiles (ag.h's drift_events, counted where the event is put);
//   - the cycles send-port loopback packets waited for a read token (Injection::token_waits, inject.h), summed over the
//     tiles (send-port packets to the tile itself or broadcast; 4.5); beside it the read side's window-full cycles (the SRAM
//     read queue waiting with no token free, reads and loopback-held tokens together; inject.h, F1's counter), summed;
//     and the run's cycles (start to the last tile's local wake, as the suite's).
// A control row first: EngineTest's drift group (tile 7's incoming writes in its send buffer's area), where drift is
// expected, so a 0 elsewhere is the detector's 0. A run with drift is run again cycle by cycle to name each tile's first
// drift cycle.
//
//   p4_drift [quick]      quick: Data(7, 2000) and 4,096 keys (a smoke run); default: the full-size set
//
// The counters it reads are kept in every build (no SIM_CHECKS needed). Test-side code (D6).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mach.h"
#include "p4_java.h"
#include "units/ag.h"
#include "units/inject.h"
#include "units/regblock.h"

using namespace mach;
namespace lw = units::lw;

namespace {

MachConfig cfg(bool skew) { MachConfig c = MachConfig::defaults(); c.skew = skew; c.check_errors = false; return c; }
constexpr uint32_t kDrift = 1u << units::RegBlockState::COND_DRIFT;

struct Fig { unsigned drift_tiles = 0; uint64_t drift_events = 0, token_wait = 0, window_full = 0; };
Fig figures(Chip& c) {
    Fig f;
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c.tile(k);
        if (p.block().conditions & kDrift) f.drift_tiles++;
        f.drift_events += p.unit<units::AddrGen>("ag").drift_events;
        const units::Injection& in = p.unit<units::Injection>("inj");
        f.token_wait += in.token_waits(uint64_t(c.cycle()));
        f.window_full += in.rs.window_full_cycles + (in.rs.wf_from ? uint64_t(c.cycle()) + 1 - in.rs.wf_from : 0);
    }
    return f;
}
void header() {
    std::printf("  %-26s %-10s %11s %13s %17s %17s %12s\n", "data set", "run", "drift tiles", "drift events", "token-wait cycles",
                "read window-full", "run cycles");
}
void row(const std::string& set, const std::string& run, const Fig& f, uint64_t ev, uint64_t tw, uint64_t wf, int64_t cycles) {
    std::printf("  %-26s %-10s %11u %13llu %17llu %17llu %12lld\n", set.c_str(), run.c_str(), f.drift_tiles, (unsigned long long)ev,
                (unsigned long long)tw, (unsigned long long)wf, (long long)cycles);
    std::fflush(stdout);
}
// A2A.run, cycle by cycle, recording each tile's first cycle with ERRORS [3]
std::string first_drift(Chip& c) {
    std::string s; bool seen[64] = {}, st[64] = {};
    const int64_t t0 = c.cycle(); const std::vector<int> sk = c.start_skew();
    for (int64_t i = 0; i < 30000000; i++) {
        for (unsigned k = 0; k < 64; k++) if (!st[k] && c.cycle() >= t0 + sk[k]) { c.tile(k).pulse(fields::PULSE_START); st[k] = true; }
        c.step();
        bool all = true;
        for (unsigned k = 0; k < 64; k++) {
            TileRef p = c.tile(k);
            if (!seen[k] && (p.block().conditions & kDrift)) { seen[k] = true; s += " t" + std::to_string(k) + "@" + std::to_string(c.cycle()); }
            all = all && p.unit<units::AddrGen>("ag").done && p.remaining(1) == 0 && p.mem_idle();
        }
        if (all) break;
    }
    return s;
}
void a2a_case(const std::string& name, const A2A& d, int gap, void (*tweak)(Chip&, const A2A&) = nullptr) {
    Chip c(cfg(tweak == nullptr));                          // the suite's all-to-all: skew on; the drift group: off
    d.load(c, gap);
    if (tweak) tweak(c, d);
    const int64_t cyc = A2A::run(c, 30000000);
    const Fig f = figures(c);
    row(name, "gap " + std::to_string(gap), f, f.drift_events, f.token_wait, f.window_full, cyc);
    if (f.drift_tiles || f.drift_events) {
        Chip r(cfg(tweak == nullptr)); d.load(r, gap); if (tweak) tweak(r, d);
        std::printf("    drift, each tile's first cycle (t<tile>@<cycle>):%s\n", first_drift(r).c_str());
    }
}
void drift_control(Chip& c, const A2A& d) {               // EngineTest.drift: tile 7 receives into its send buffer's area
    for (unsigned s = 0; s < 64; s++) c.tile(7).set_ptr(s, uint32_t(0x10000 + 2 * d.dstoff[s][7]));
}

int64_t run_all_tiles(Chip& c) {                          // EngineTest.runAll
    const int64_t t0 = c.cycle(); bool st[64] = {};
    const std::vector<int> sk = c.start_skew();
    for (int i = 0; i < 50000000; i++) {
        for (unsigned k = 0; k < 64; k++) if (!st[k] && c.cycle() >= t0 + sk[k]) { c.tile(k).pulse(fields::PULSE_START); st[k] = true; }
        c.step();
        bool all = true; for (unsigned k = 0; k < 64; k++) if (!st[k] || !c.tile(k).phase_done()) { all = false; break; }
        if (all) return c.cycle() - t0;
    }
    return -1;
}
void radix(uint32_t N) {                                  // EngineTest.radixSort, as the suite runs it
    Chip c(cfg(true));
    const uint32_t A = 0x00000, B = 0x20000, C = 0x40000;
    p4::JRandom rnd(11);
    for (unsigned k = 0; k < 64; k++) for (uint32_t j = 0; j < N; j++) c.tile(k).sram_poke(A + j, uint32_t(rnd.next_int()));
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c.tile(k);
        p.set_stream(0, fields::stream_src(A), 0, fields::stream_count(N, fields::SIZE_32, 1, lw::M_AUG, 0));
        p.set_ptr(k, B); p.set_record_base(B); p.set_remaining(1, N);
        p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 1, 0, 1, 1));
    }
    const std::string name = "radix sort (" + std::to_string(N) + " keys)";
    Fig prev;
    int64_t total = 0;
    int64_t cyc = run_all_tiles(c); total += cyc;
    Fig f = figures(c); row(name, "augment", f, f.drift_events, f.token_wait, f.window_full, cyc); prev = f;
    uint32_t src = B, dst = C;
    for (uint32_t d = 1; d <= 4; d++) {
        for (unsigned k = 0; k < 64; k++) {
            TileRef p = c.tile(k);
            uint32_t pre = 0;
            for (unsigned b = 0; b < 256; b++) { const uint32_t n = p.hist_peek(b); p.set_ptr(b, dst + 2 * pre); pre += n; p.hist_poke(b, 0); }
            p.set_stream(0, fields::stream_src(src), 0, fields::stream_count(N, fields::SIZE_64, 1, lw::M_BUCKET, 0));
            p.set_remaining(1, N);
            p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 1, d, d < 4 ? d + 1 : 0, 1));
        }
        cyc = run_all_tiles(c); total += cyc;
        f = figures(c);
        row(name, "pass " + std::to_string(d), f, f.drift_events - prev.drift_events, f.token_wait - prev.token_wait, f.window_full - prev.window_full, cyc);
        prev = f; std::swap(src, dst);
    }
    row(name, "total", f, f.drift_events, f.token_wait, f.window_full, total);
}

}  // namespace

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::strcmp(argv[1], "quick") == 0;
    std::printf("drift (ERRORS [3]) and loopback token waits, EngineTest full's data sets [p4_drift%s]\n", quick ? " quick" : "");
    header();
    { A2A d(7, 500); a2a_case("control: EngineTest drift", d, 4, drift_control); }
    if (quick) { A2A d(7, 2000); for (int gap : {4, 0}) a2a_case("uniform (Data(7, 2000))", d, gap); radix(4096); return 0; }
    { A2A d(7, 65536); for (int gap : {4, 0, 8}) a2a_case("uniform (Data(7, 65536))", d, gap); }
    for (const char* m : {"zipfraw", "trace", "one99"}) { A2A d(std::string("data/") + m + ".txt"); for (int gap : {4, 0, 8}) a2a_case(m, d, gap); }
    radix(16384);
    return 0;
}
