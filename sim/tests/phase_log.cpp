// phase_log.cpp (WORKER20, P2): ref's PhaseLog (phase_log.h).
#include "phase_log.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

#include "base/fatal.h"
#include "java_format.h"
#include "mach.h"
#include "units/big_sram.h"
#include "units/gather.h"
#include "units/inject.h"
#include "units/ag.h"
#include "units/router.h"
#include "units/table_store.h"
#include "units/tile_ctl.h"
#include "units/write_engine.h"

namespace mach {

const char* const PhaseLog::COUNTS[NC] = {"instr", "wfiCycles", "sendStalls", "agReq", "agStalls", "agLate", "agRuns", "gatherReq",
                                          "rpReads", "injected", "ejected", "latSumG", "latNG", "latSumA", "latNA", "latSumR", "latNR",
                                          "unreadOverwrites"};
const char* const PhaseLog::PEAKS[NP] = {"link", "eject", "toCnC", "weIn", "gatherQ", "area", "ptrReq", "histReq", "agTable", "readPort"};

namespace {
std::string sfmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string sfmt(const char* f, ...) {
    va_list ap, ap2;
    va_start(ap, f);
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, f, ap2);
    va_end(ap2);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    std::vsnprintf(s.data(), s.size() + 1, f, ap);
    va_end(ap);
    return s;
}
}  // namespace

PhaseLog::PhaseLog(Chip& c) : c_(&c) {
    if (!sim::check::kOn) FATAL("PhaseLog: the debug and suite builds only (its FIFO peaks and latencies are kept there: MODE=suite)");
    namer = [](int32_t w) { return w < 0 ? std::string("-") : sfmt("cmd %02X", unsigned(uint32_t(w) >> 24)); };
}

// The core's counters as ref's syncCounters has them: the cycles the C++ core skipped since its last step (asleep in WFI
// or stalled; Rv32::catch_up) counted as they will be when it next steps.
void PhaseLog::core(unsigned k, int64_t& instr, int64_t& wfi, int64_t& stalled) const {
    const TileRef t = c_->tile(k);
    const units::Rv32& r = t.unit<units::TileCore>("core").rv;
    instr = int64_t(r.instructions); wfi = int64_t(r.cycles_wfi); stalled = int64_t(r.cycles_stalled);
    if (r.halt != units::Rv32::RUN || r.ub || r.parked) return;
    const int64_t gap = c_->cycle() - int64_t(r.last);
    if (gap <= 0) return;
    if (r.in_wfi) wfi += gap; else stalled += gap;
}

std::array<int64_t, PhaseLog::NC> PhaseLog::counters() const {
    std::array<int64_t, NC> v = {};
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c_->tile(k);
        int64_t instr, wfi, stalled;
        core(k, instr, wfi, stalled);
        const units::TileCore& core = p.unit<units::TileCore>("core");
        const units::AddrGen& ag = p.unit<units::AddrGen>("ag");
        const units::Injection& in = p.unit<units::Injection>("inj");
        v[0] += instr; v[1] += wfi; v[2] += int64_t(core.rv.stall_send);
        v[3] += int64_t(ag.cum_requests + ag.requests); v[4] += int64_t(ag.cum_stall + ag.stall_cycles);
        v[5] += int64_t(ag.cum_rot_late + ag.rotations_late); v[6] += int64_t(ag.runs);
        v[7] += int64_t(p.unit<units::Gather>("gather").requests); v[8] += int64_t(core.rp_stored);
        v[9] += int64_t(in.injected); v[10] += int64_t(p.unit<units::Router>("router").ejected);
#ifdef SIM_CHECKS
        using RS = units::InjReadSide;
        v[11] += int64_t(in.rs.lat_sum[RS::GATHER]); v[12] += int64_t(in.rs.lat_n[RS::GATHER]);
        v[13] += int64_t(in.rs.lat_sum[RS::AG]); v[14] += int64_t(in.rs.lat_n[RS::AG]);
        v[15] += int64_t(in.rs.lat_sum[RS::READPORT]); v[16] += int64_t(in.rs.lat_n[RS::READPORT]);
#endif
        v[17] += 0;                                          // unreadOverwrites: W26's invariant 8 tracking (pending)
    }
    return v;
}

std::array<int64_t, PhaseLog::NP> PhaseLog::peaks() const {   // (the debug and suite builds: their peaks are kept there)
    std::array<int64_t, NP> pk = {};
#ifdef SIM_CHECKS
    auto mx = [](int64_t& a, int64_t b) { if (b > a) a = b; };
    sim::Sim& s = c_->sim();
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c_->tile(k);
        const units::Router& r = p.unit<units::Router>("router");
        for (unsigned o = 0; o < units::Router::kOut; o++)
            for (unsigned f = 0; f < r.nfeed[o]; f++) mx(pk[o < 4 ? 0 : o == 4 ? 1 : 2], r.q[r.first[o] + f].peak);
        mx(pk[3], s.fifo_peak(p.unit<units::WriteEngine>("we").in));
        mx(pk[4], s.fifo_peak(p.unit<units::Gather>("gather").in));
        const units::BigSram& m = p.unit<units::BigSram>("sram");
        for (unsigned a = 0; a < units::BigSram::AREAS; a++) mx(pk[5], m.queue_peak[a]);
        const units::TableStore& ts = p.unit<units::TableStore>("ts");
        mx(pk[6], ts.q_we.peak); mx(pk[7], p.unit<units::HistStore>("hist").q.peak);
        mx(pk[8], ts.q_stream.peak); mx(pk[8], ts.q_group.peak);
        mx(pk[9], rp_peak_[k]);
    }
#endif
    return pk;
}

void PhaseLog::reset_peaks() {
#ifdef SIM_CHECKS
    sim::Sim& s = c_->sim();
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c_->tile(k);
        units::Router& r = p.unit<units::Router>("router");
        for (auto& f : r.q) f.peak = f.count;
        s.fifo_restart_peak(p.unit<units::WriteEngine>("we").in);
        s.fifo_restart_peak(p.unit<units::Gather>("gather").in);
        units::TableStore& ts = p.unit<units::TableStore>("ts");
        ts.q_we.peak = ts.q_we.n; ts.q_stream.peak = ts.q_stream.n; ts.q_group.peak = ts.q_group.n;
        units::HistStore& h = p.unit<units::HistStore>("hist");
        h.q.peak = h.q.n;
        units::BigSram& m = p.unit<units::BigSram>("sram");
        for (unsigned a = 0; a < units::BigSram::AREAS; a++) m.queue_peak[a] = m.count[a];
        const units::TileCore& core = p.unit<units::TileCore>("core");
        rp_peak_[k] = int64_t(core.rp_stored - p.unit<units::Injection>("inj").rs.rp_injected_n);   // ref's peak = inUse()
        rp_seen_[k] = core.rp_stored;
        units::Injection& in = p.unit<units::Injection>("inj");
        in.rs.lat_max[0] = in.rs.lat_max[1] = in.rs.lat_max[2] = 0;
    }
#endif
}

void PhaseLog::after_cycle() {
    for (unsigned k = 0; k < 64; k++) {
        const TileRef p = c_->tile(k);
        const units::TileCore& core = p.unit<units::TileCore>("core");
        if (core.rp_stored == rp_seen_[k]) continue;
        rp_seen_[k] = core.rp_stored;
        const int64_t used = int64_t(core.rp_stored - p.unit<units::Injection>("inj").rs.rp_injected_n);
        if (used > rp_peak_[k]) rp_peak_[k] = used;
    }
}

void PhaseLog::on_go(int64_t t, int32_t word) {
    if (open_ >= 0) finish(phases[size_t(open_)], phases[size_t(open_)].end_at >= 0 ? phases[size_t(open_)].end_at : t);
    Phase p;
    p.index = int(phases.size()); p.word = word; p.name = namer(word); p.go_at = t;
    if (last_all_ready_seen_ >= 0) p.cnc_reaction = t - last_all_ready_seen_;
    const std::vector<int> skew = c_->start_skew();
    for (unsigned i = 0; i < 64; i++) { p.tile_go[i] = t + c_->cfg().go_latency + skew[i]; p.tile_ready[i] = -1; }
    phases.push_back(p);
    open_ = int(phases.size()) - 1;
    base_ = counters();
    for (unsigned k = 0; k < 64; k++) { int64_t in, w, st; core(k, in, w, st); base_instr_[k] = in; base_busy_[k] = in + st; }
    reset_peaks();
}
void PhaseLog::on_all_ready(int64_t t) {
    last_all_ready_seen_ = t;
    if (open_ >= 0 && phases[size_t(open_)].end_at < 0) phases[size_t(open_)].end_at = t;
}
void PhaseLog::on_ready(unsigned tile, int64_t t) {
    if (open_ < 0) return;
    Phase& p = phases[size_t(open_)];
    if (p.tile_ready[tile] < 0 && t >= p.tile_go[tile]) p.tile_ready[tile] = t;
}
void PhaseLog::close() {
    if (open_ < 0) return;
    Phase& p = phases[size_t(open_)];
    int64_t end = p.end_at;
    if (end < 0) {
        int64_t m = -1;
        for (int64_t r : p.tile_ready) m = std::max(m, r);
        end = m >= 0 ? m + c_->cfg().ready_latency : c_->cycle();
    }
    finish(p, end);
    open_ = -1;
}
void PhaseLog::finish(Phase& p, int64_t end) {
    p.end_at = end;
    const std::array<int64_t, NC> now = counters();
    for (int i = 0; i < NC; i++) p.n[i] = now[size_t(i)] - base_[size_t(i)];
    const std::array<int64_t, NP> pk = peaks();
    for (int i = 0; i < NP; i++) {
        p.peak[i] = pk[size_t(i)];
        bool found = false;
        for (auto& e : run_peak) if (e.first == PEAKS[i]) { e.second = std::max(e.second, pk[size_t(i)]); found = true; }
        if (!found) run_peak.push_back({PEAKS[i], pk[size_t(i)]});
    }
#ifdef SIM_CHECKS
    for (unsigned k = 0; k < 64; k++) {
        const TileRef t = c_->tile(k);
        const units::Injection& in = t.unit<units::Injection>("inj");
        for (int j = 0; j < 3; j++) p.lat_max[j] = std::max(p.lat_max[j], int64_t(in.rs.lat_max[j]));
    }
#endif
    for (unsigned k = 0; k < 64; k++) {
        int64_t in, w, st;
        core(k, in, w, st);
        p.tile_instr[k] = in - base_instr_[k]; p.tile_busy[k] = in + st - base_busy_[k];
    }
    base_ = now;
}

// ---- output (ref's report() and csv(), Java's formatting) ----
namespace {
double mean(int64_t s, int64_t n) { return n == 0 ? 0 : double(s) / double(n); }
std::string jf(double d, int prec, int width) { return sfmt("%*s", width, jfmt::fixed(d, prec).c_str()); }
}  // namespace

std::string PhaseLog::report() const {
    std::string sb = "  #  phase              GO at    cycles  READY spread (slowest)  instr/tile  WFI%   AG req/tile  late  gather/tile  rd-port/tile  AG read lat (max)  inj%  ej%   peaks: link ej toCnC weIn gQ area ptr hist rdPort  C&C\n";
    int64_t tot = 0;
    for (const Phase& p : phases) {
        if (p.end_at < 0) continue;
        const int64_t cyc = p.cycles();
        tot += cyc;
        int64_t lo = INT64_MAX, hi = -1;
        int slow = -1;
        for (int i = 0; i < 64; i++) {
            if (p.tile_ready[i] < 0) continue;
            const int64_t d = p.tile_ready[i] - p.tile_go[i];
            if (d < lo) lo = d;
            if (d > hi) { hi = d; slow = i; }
        }
        const double tc = 64.0 * double(std::max<int64_t>(1, cyc));
        const std::string spread = hi >= 0 ? std::to_string(hi - lo) : "-", sl = slow >= 0 ? std::to_string(slow) : "-";
        sb += sfmt("  %-2d %-16s %8" PRId64 " %9" PRId64 "  %6s (t%-2s)         %s  %s  %s  %4" PRId64 "  %s  %s  %s (%4" PRId64 ")  %s  %s   %9" PRId64 " %2" PRId64
                   " %5" PRId64 " %4" PRId64 " %2" PRId64 " %4" PRId64 " %3" PRId64 " %3" PRId64 " %6" PRId64 "  %s\n",
                   p.index, p.name.c_str(), p.go_at, cyc, spread.c_str(), sl.c_str(), jf(double(p.n[0]) / 64.0, 0, 9).c_str(),
                   jf(100.0 * double(p.n[1]) / tc, 0, 4).c_str(), jf(double(p.n[3]) / 64.0, 0, 11).c_str(), p.n[5],
                   jf(double(p.n[7]) / 64.0, 0, 11).c_str(), jf(double(p.n[8]) / 64.0, 0, 12).c_str(), jf(mean(p.n[13], p.n[14]), 1, 8).c_str(),
                   p.lat_max[1], jf(100.0 * double(p.n[9]) / tc, 0, 4).c_str(), jf(100.0 * double(p.n[10]) / tc, 0, 3).c_str(), p.peak[0],
                   p.peak[1], p.peak[2], p.peak[3], p.peak[4], p.peak[5], p.peak[6], p.peak[7], p.peak[9],
                   p.cnc_reaction >= 0 ? std::to_string(p.cnc_reaction).c_str() : "-");
    }
    std::string rp = "{";
    for (size_t i = 0; i < run_peak.size(); i++) rp += (i ? ", " : "") + run_peak[i].first + "=" + std::to_string(run_peak[i].second);
    rp += "}";
    sb += sfmt("  total GO -> all READY over %zu phases: %s cycles; whole-run peaks: %s\n", phases.size(), jfmt::grouped(tot).c_str(), rp.c_str());
    return sb;
}

std::string PhaseLog::csv() const {
    std::string sb = "phase,name,word,go_at,end_at,cycles,ready_spread,slowest_tile,cnc_reaction";
    for (const char* k : COUNTS) sb += std::string(",") + k;
    for (const char* k : PEAKS) sb += std::string(",peak_") + k;
    sb += ",latmax_gather,latmax_ag,latmax_readport,slow_instr,slow_busy,tile_busy\n";
    for (const Phase& p : phases) {
        if (p.end_at < 0) continue;
        int64_t lo = INT64_MAX, hi = -1;
        int slow = -1;
        for (int i = 0; i < 64; i++) {
            if (p.tile_ready[i] < 0) continue;
            const int64_t d = p.tile_ready[i] - p.tile_go[i];
            lo = std::min(lo, d);
            if (d > hi) { hi = d; slow = i; }
        }
        sb += sfmt("%d,%s,0x%08X,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%d,%" PRId64, p.index, p.name.c_str(), uint32_t(p.word), p.go_at,
                   p.end_at, p.cycles(), hi >= 0 ? hi - lo : int64_t(-1), slow, p.cnc_reaction);
        for (int i = 0; i < NC; i++) sb += "," + std::to_string(p.n[i]);
        for (int i = 0; i < NP; i++) sb += "," + std::to_string(p.peak[i]);
        sb += sfmt(",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",", p.lat_max[0], p.lat_max[1], p.lat_max[2],
                   slow >= 0 ? p.tile_instr[slow] : int64_t(-1), slow >= 0 ? p.tile_busy[slow] : int64_t(-1));
        for (int i = 0; i < 64; i++) sb += (i > 0 ? ";" : "") + std::to_string(p.tile_busy[i]);
        sb += "\n";
    }
    return sb;
}

bool PhaseLog::csv(const std::string& path, std::string& err) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    const std::string s = csv();
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    std::fclose(f);
    if (!ok) err = "cannot write " + path;
    return ok;
}

}  // namespace mach
