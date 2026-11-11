// bench_chip (WORKER10): W9's chip (build_chip with W8's G1 units in every tile) running a program from cycle 0, timed,
// one thread or form 2, by placement. The benchmark that counts until the sort runs at 16K (owner, 2026-11-07): the
// sort's boot (the sort image loaded over the network and port B: 14,791 cycles before any engine runs), wfi, regs.
//
//   bench_chip --prog sort|wfi|regs [--cycles N] [--threads T] [--partition one|rows|tiles|load|profile]
//              [--units] [--windows W] [--trace-map F [--trace-from C --trace-window W --trace-windows N]] [--cpus a,b,..] [--time] [--crossing-ns X] [--reps R] [--barrier auto|counter|flags|flags-spin]
//
// --cycles: default sort 14791 (the boot), wfi and regs (whole, since G3): until every core has stopped (every 64 cycles; the
// count is the cycles run). rows: row_bands(T); tiles: one domain per tile (grouped in order, one engine per thread);
// load: Placement::by_load, 1 per unit; profile: by_load from a one-thread calibration run of the same program and
// cycles. The first cycle runs outside the timing (a split machine's first run starts its workers). One line:
//   chip prog=P threads=T part=X cycles=C reps=R us_per_cycle=<median> us_min=.. us_max=.. words=W digest=D times="..."
//   partition="..." (times: the last machine's; with --time, its phase shares)
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "config.h"
#include "config_impl.h"
#include "internals.h"
#include "../../tests/golden_run.h"
#include "units/chip.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

struct Args {
    std::string prog = "sort", part = "one";
    uint64_t cycles = 0;
    unsigned threads = 1;
    std::vector<int> cpus;
    bool time = false;
    double crossing_ns = -1;   // --crossing-ns: Options::crossing_ns (default: the framework's)
    std::string trace_map;             // --trace-map FILE: the regions, then windows marked for a memory tracer (lackey)
    uint64_t trace_from = 2000, trace_window = 3, trace_windows = 300;   // --trace-from C --trace-window W --trace-windows N
    unsigned windows = 0;              // --windows W: one thread, the run in windows of W cycles: steps and time each (quiet case)
    bool units = false;                // --units: one thread, calibrating: each unit kind's time, steps and ns a step
    std::string barrier = "auto";      // --barrier auto (the framework's choice) | counter (W4's) | flags | flags-spin
    unsigned reps = 5;         // --reps: machines built and run in this process, after one untimed (a run is milliseconds:
                               // the clock's ramp, first touches and the workers' start would otherwise dominate)
};

struct World {   // what the machine reads while it runs: outlives it
    test::Images im;
    Hops hops;
    CncProgram prog;
    std::vector<uint32_t> mem;
    SramBlocks sram;                 // G3's big-SRAM blocks (W11), as test_chip
    G1Ctx gx;
};

void prepare(World& w, const std::string& name) {
    test::RunSpec rs; rs.name = name; rs.dir = "tests/golden-m2/images"; rs.image = name;
    std::string err;
    if (!test::load_images(rs, w.im, err)) FATAL("bench_chip: %s", err.c_str());
    w.hops = load_hops("data/hops.txt");
    w.prog.load_words(lw::BROADCAST, 0x01000000, w.im.code.data(), w.im.code.size());
    w.prog.load_words(lw::BROADCAST, w.im.data_at, w.im.data.data(), w.im.data.size());
    const uint32_t entry = w.im.entry;
    w.prog.command(lw::BROADCAST, 0x4Au << 24, &entry, 1);
    w.prog.run_when_ready(0).call(1);
    if (name == "wfi")
        w.prog.command(lw::BROADCAST, 1u << 24).go().command(lw::BROADCAST, 2u << 24).go().wait_cycles(700)
            .command(lw::BROADCAST, 3u << 24).go();
    w.prog.release();
    TileConfig tc = test::tile_config(0);
    w.mem.assign(64 * 2 * MEM_WORDS, 0);
    w.gx.cfg.timing = tc.timing; w.gx.cfg.periph_present = tc.periph_present; w.gx.cfg.mem_chunks = 1u << 20;
    w.gx.cfg.done_gate = true; w.gx.cfg.K = 8; w.gx.cfg.cm_latency = 2;
    w.gx.bootrom = w.im.bootrom.data(); w.gx.bootrom_words = uint32_t(w.im.bootrom.size()); w.gx.mem = w.mem.data();
    w.gx.sram = &w.sram;
}

sim::Sim build(World& w, const Args& a, sim::Options opt, const std::string& part) {
    uint8_t dom[64];
    ChipSpec spec;
    spec.net = NetConfig{}; spec.hops = &w.hops; spec.program = &w.prog; spec.control = add_g1; spec.control_ctx = &w.gx;
    if (part == "rows") { row_bands(a.threads, dom); spec.domain = dom; }
    else if (part == "tiles") { for (unsigned k = 0; k < 64; k++) dom[k] = uint8_t(k); spec.domain = dom; }
    else if (part == "load" || part == "profile") opt.placement = sim::Options::Placement::by_load;
    else if (part != "one") FATAL("--partition %s: one, rows, tiles, load or profile", part.c_str());
    sim::Config cfg(opt);
    build_chip(cfg, spec);
    return cfg.build();
}

// the process's resident set (VmRSS), KB
long rss_kb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char l[256]; long v = -1;
    while (std::fgets(l, sizeof l, f)) if (!std::strncmp(l, "VmRSS:", 6)) { v = std::atol(l + 6); break; }
    std::fclose(f);
    return v;
}

volatile uint64_t g_trace_mark = 0;   // --trace-map: a store here starts a window (the tracer sees its address)
// tile k's slice of the bench's port-B memory (64 x 2 x MEM_WORDS words, data then program per tile, tile_ctl.h)
const void* w_tile_mem(const World& w, unsigned k) { return w.mem.data() + size_t(k) * 2 * MEM_WORDS; }

bool all_stopped(sim::Sim& m) {
    for (unsigned k = 0; k < 64; k++)
        if (m.unit<TileCore>(tile_unit(k, "core")).rv.halt == Rv32::RUN) return false;
    return true;
}

// Runs the program: a fixed count, or (count 0) until every core has stopped, checked every 64 cycles.
uint64_t run_prog(sim::Sim& m, uint64_t cycles, uint64_t cap) {
    if (cycles) { m.run(cycles); return cycles; }
    uint64_t n = 0;
    while (n < cap) { m.run(64); n += 64; if (all_stopped(m)) break; }
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        auto val = [&]() -> const char* { if (i + 1 >= argc) FATAL("%s needs a value", argv[i]); return argv[++i]; };
        if (!std::strcmp(argv[i], "--prog")) a.prog = val();
        else if (!std::strcmp(argv[i], "--cycles")) a.cycles = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--threads")) a.threads = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--partition")) a.part = val();
        else if (!std::strcmp(argv[i], "--time")) a.time = true;
        else if (!std::strcmp(argv[i], "--crossing-ns")) a.crossing_ns = std::atof(val());
        else if (!std::strcmp(argv[i], "--reps")) a.reps = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--barrier")) a.barrier = val();
        else if (!std::strcmp(argv[i], "--units")) a.units = true;
        else if (!std::strcmp(argv[i], "--trace-map")) a.trace_map = val();
        else if (!std::strcmp(argv[i], "--trace-from")) a.trace_from = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--trace-window")) a.trace_window = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--trace-windows")) a.trace_windows = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--windows")) a.windows = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--cpus")) {
            const char* s = val();
            while (*s) { a.cpus.push_back(std::atoi(s)); while (*s && *s != ',') s++; if (*s) s++; }
        } else FATAL("bench_chip: unknown argument %s", argv[i]);
    }
    if (a.prog != "sort" && a.prog != "wfi" && a.prog != "regs") FATAL("--prog %s: sort, wfi or regs", a.prog.c_str());
    const uint64_t cycles = a.cycles ? a.cycles : a.prog == "sort" ? 14791 : 0;   // wfi, regs: to their stop
    const std::string part = a.threads == 1 && a.part != "load" && a.part != "profile" ? std::string("one") : a.part;

    if (!a.trace_map.empty()) {   // footprint: the region map, then marked windows (bench/w10/trace_filter.cpp reads both)
        World rw;
        prepare(rw, a.prog);
        sim::Options o; o.threads = a.threads; o.cpus = a.cpus;
        sim::Sim m = build(rw, a, o, part);
        m.run(a.trace_from);
        FILE* f = std::fopen(a.trace_map.c_str(), "w");
        if (!f) FATAL("--trace-map %s: cannot write", a.trace_map.c_str());
        std::fprintf(f, "marker %p\nwindows %llu\n", (const void*)&g_trace_mark, (unsigned long long)a.trace_windows);
        auto reg = [&](const void* p, size_t bytes, int dom, const std::string& what) {
            if (bytes) std::fprintf(f, "region %p %zu %d %s\n", p, bytes, dom, what.c_str());
        };
        for (const auto& r : sim::detail::Internals::regions(m)) reg(r.base, r.bytes, r.domain, r.what);
        const unsigned T = part == "one" ? 1 : a.threads;
        for (unsigned k = 0; k < 64; k++) {                   // the units' own memory the bench gave them (G1Ctx)
            const int dom = int(k * T / 64);
            reg(w_tile_mem(rw, k), 2 * MEM_WORDS * 4, dom, "units' memory (port B data and program)");
            reg(rw.sram.block(k, rw.gx.cfg.mem_chunks), size_t(rw.gx.cfg.mem_chunks) * 4, dom, "big SRAM block");
        }
        std::fclose(f);
        for (uint64_t w = 0; w < a.trace_windows; w++) { g_trace_mark = w; m.run(a.trace_window); }
        g_trace_mark = ~uint64_t(0);
        std::printf("trace prog=%s threads=%u part=%s from=%llu windows=%llu of %llu cycles\n", a.prog.c_str(), a.threads,
                    part.c_str(), (unsigned long long)a.trace_from, (unsigned long long)a.trace_windows, (unsigned long long)a.trace_window);
        return 0;
    }
    if (a.windows) {   // the quiet case: per window, the steps taken (active units) against the time; reps machines, the
                       // median time per window; then by bins of steps a cycle
        std::vector<std::vector<double>> ns;              // per window, per rep
        std::vector<uint64_t> wsteps, wcycles;
        for (unsigned r = 0; r <= a.reps; r++) {
            World rw;
            prepare(rw, a.prog);
            sim::Sim m = build(rw, a, sim::Options{}, "one");
            const sim::Engine& e = sim::detail::Internals::impl(m)->e;
            auto total_steps = [&]() { uint64_t t = 0; for (uint32_t i = 0; i < e.ninst; i++) t += e.steps[i]; return t; };
            m.run(1);
            uint64_t done = 1, w = 0;
            const uint64_t want = cycles ? cycles : 2000000;
            while (done < want) {
                const uint64_t n = std::min<uint64_t>(a.windows, want - done);
                const uint64_t s0 = total_steps();
                const auto t0 = std::chrono::steady_clock::now();
                m.run(n);
                const double d = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
                const uint64_t st = total_steps() - s0;
                if (r > 0) {
                    if (w >= ns.size()) { ns.emplace_back(); wsteps.push_back(st); wcycles.push_back(n); }
                    ns[w].push_back(d);
                }
                done += n; w++;
                if (!cycles && done % 64 < n && all_stopped(m)) break;
            }
        }
        // per window: the median time; then bins by steps a cycle
        struct B { double ns = 0; uint64_t cyc = 0, steps = 0, n = 0; };
        std::map<int, B> bins;   // bin: steps a cycle, 0, 1, 2-3, 4-7, ... (powers of two)
        double tot_ns = 0; uint64_t tot_cyc = 0;
        for (size_t w = 0; w < ns.size(); w++) {
            std::vector<double> v = ns[w]; std::sort(v.begin(), v.end());
            const double med = v[v.size() / 2];
            const double spc = double(wsteps[w]) / double(wcycles[w]);
            const int bin = spc < 0.5 ? -1 : int(std::floor(std::log2(spc)));
            B& b = bins[bin]; b.ns += med; b.cyc += wcycles[w]; b.steps += wsteps[w]; b.n++;
            tot_ns += med; tot_cyc += wcycles[w];
        }
        std::printf("quiet prog=%s windows=%zu of %u cycles reps=%u us_per_cycle=%.3f\n", a.prog.c_str(), ns.size(), a.windows,
                    a.reps, tot_ns / 1e3 / double(tot_cyc));
        for (auto& [bin, b] : bins)
            std::printf("quiet bin=%s windows=%llu cycles=%llu steps_per_cycle=%.2f us_per_cycle=%.4f ns_per_step=%.1f\n",
                        bin < 0 ? "<0.5" : ("2^" + std::to_string(bin)).c_str(), (unsigned long long)b.n, (unsigned long long)b.cyc,
                        double(b.steps) / double(b.cyc), b.ns / 1e3 / double(b.cyc), b.steps ? b.ns / double(b.steps) : 0.0);
        return 0;
    }
    if (a.units) {   // the units' own costs: a calibrating run (each step timed), totals by kind (the name after "t<k>.")
        World cw;
        prepare(cw, a.prog);
        sim::Options co; co.calibrate = true;
        sim::Sim cal = build(cw, a, co, "one");
        cal.run(1);
        const auto t0 = std::chrono::steady_clock::now();
        const uint64_t ran = 1 + run_prog(cal, cycles ? cycles - 1 : 0, 2000000);
        const double wall_ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        const sim::LoadProfile p = cal.load_profile();
        // the clock's own cost inside each timed step (two reads, as run_form_cal): measured here, taken off each step
        double pair = 0;
        {
            const int N = 1000000;
            uint64_t acc = 0;
            const uint64_t c0 = sim::sim_ticks();
            for (int i = 0; i < N; i++) { const uint64_t a0 = sim::sim_ticks(); acc += sim::sim_ticks() - a0; }
            pair = double(acc) / N;
            (void)c0;
        }
        struct K { double ticks = 0; uint64_t steps = 0, n = 0; };
        std::map<std::string, K> kinds;
        double total = 0;
        for (size_t i = 0; i < p.unit.size(); i++) {
            std::string kind = p.unit[i];
            if (kind.size() > 1 && kind[0] == 't' && kind.find('.') != std::string::npos && isdigit(kind[1])) kind = kind.substr(kind.find('.') + 1);
            K& k = kinds[kind];
            const uint64_t st = cal.steps(p.unit[i]);
            const double t = std::max(0.0, p.load[i] - pair * double(st));
            k.ticks += t; k.steps += st; k.n++;
            total += t;
        }
        const double units_ns = total / p.ticks_per_ns;
        std::printf("units prog=%s cycles=%" PRIu64 " wall_us_per_cycle=%.3f (calibrating: two clock reads a step) "
                    "units_us_per_cycle=%.3f (the clock's %.1f ns a step taken off) rest_us_per_cycle=%.3f (delivery, scheduling, "
                    "and the clock again)\n", a.prog.c_str(), ran, wall_ns / 1e3 / double(ran),
                    units_ns / 1e3 / double(ran), pair / p.ticks_per_ns, (wall_ns - units_ns) / 1e3 / double(ran));
        std::vector<std::pair<double, std::string>> order;
        for (auto& [nm, k] : kinds) order.push_back({k.ticks, nm});
        std::sort(order.rbegin(), order.rend());
        for (auto& [t, nm] : order) {
            const K& k = kinds[nm];
            std::printf("unit kind=%s count=%llu share=%.4f us_per_cycle=%.4f steps_per_cycle=%.2f ns_per_step=%.1f\n", nm.c_str(),
                        (unsigned long long)k.n, total > 0 ? k.ticks / total : 0, k.ticks / p.ticks_per_ns / 1e3 / double(ran),
                        double(k.steps) / double(ran), k.steps ? k.ticks / p.ticks_per_ns / double(k.steps) : 0);
        }
        return 0;
    }
    sim::LoadProfile prof;
    sim::Options opt;
    opt.threads = a.threads;
    opt.cpus = a.cpus;
    opt.time_threads = a.time && a.threads > 1;
    if (a.crossing_ns >= 0) opt.crossing_ns = a.crossing_ns;
    if (a.barrier != "auto" && a.barrier != "counter" && a.barrier != "flags" && a.barrier != "flags-spin") FATAL("--barrier %s", a.barrier.c_str());
    opt.barrier = a.barrier == "auto" ? sim::Options::Barrier::automatic
                : a.barrier == "counter" ? sim::Options::Barrier::counter : sim::Options::Barrier::flags;
    if (a.barrier == "flags-spin") opt.barrier_spins = ~0u;
    if (part == "profile") {                              // the same program and cycles, one thread, calibrating
        World cw;
        prepare(cw, a.prog);
        sim::Options co; co.calibrate = true;
        sim::Sim cal = build(cw, a, co, "one");
        run_prog(cal, cycles, 2000000);
        prof = cal.load_profile();
        opt.load = &prof;
    }
    // one untimed machine, then `reps` timed ones; each its own World (the program's state is the machine's input)
    std::vector<double> us;
    uint64_t ran = 0, words = 0, digest = 0;
    std::string times, partition;
    long rss_built = 0, rss_ran = 0;
    size_t sram_alloc = 0, sram_res_built = 0, sram_res_ran = 0;
    for (unsigned r = 0; r <= a.reps; r++) {
        World rw;
        prepare(rw, a.prog);
        sim::Sim m = build(rw, a, opt, part);
        if (r == a.reps) { rss_built = rss_kb(); sram_res_built = rw.sram.touched_bytes(); }
        m.run(1);                                         // the first run starts the workers: outside the count
        const auto t0 = std::chrono::steady_clock::now();
        ran = 1 + run_prog(m, cycles ? cycles - 1 : 0, 2000000);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (r == 0) continue;
        us.push_back(1e6 * secs / double(ran));
        words = m.sizes().total_words; digest = m.digest(); times = m.thread_times(); partition = m.partition();
        if (r == a.reps) { rss_ran = rss_kb(); sram_alloc = rw.sram.allocated_bytes(); sram_res_ran = rw.sram.touched_bytes(); }
    }
    std::vector<double> so(us);
    std::sort(so.begin(), so.end());
    const double medv = so.empty() ? 0 : so[so.size() / 2];
    std::printf("chip prog=%s threads=%u part=%s barrier=%s cycles=%" PRIu64 " reps=%u us_per_cycle=%.3f us_min=%.3f us_max=%.3f "
                "words=%llu digest=%016llx rss_mb_built=%.1f rss_mb_ran=%.1f sram_mb=%.1f sram_resident_mb_built=%.2f "
                "sram_resident_mb_ran=%.2f times=\"%s\" partition=\"%s\"\n",
                a.prog.c_str(), a.threads, part.c_str(), a.barrier.c_str(), ran, a.reps, medv, so.empty() ? 0 : so.front(), so.empty() ? 0 : so.back(),
                (unsigned long long)words, (unsigned long long)digest, rss_built / 1024.0, rss_ran / 1024.0, sram_alloc / 1048576.0,
                sram_res_built / 1048576.0, sram_res_ran / 1048576.0, times.c_str(), partition.c_str());
    return 0;
}
