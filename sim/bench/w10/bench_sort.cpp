// bench_sort (WORKER10): the 16K sort on W9's chip with W8's, W11's and W12's units, timed over M2Sort's counted span
// (L1's GO to L2's all-READY, as W7's baseline: setup, the boot and the input load excluded). The machine and the
// sequence are W12's test_sort's (tests/test_sort.cpp, SortChip): the boot run to B by polling, the input loaded and the
// sequence released at B. Its span is found once by a polling run (the phases as C&C records them, PhaseLog's rule)
// and checked in every timed machine afterwards (its last GO and all-READY at the same cycles).
//
//   bench_sort --keys keys-16384/uniform.keys [--threads T] [--partition one|rows|tiles|load|profile] [--reps R]
//              [--cpus a,b,..] [--time] [--phases] [--units] [--windows W] [--trace-map F --trace-from C
//              --trace-window W --trace-windows N] [--span GO END]
//
// One line per configuration:
//   sort threads=T part=X span=C reps=R us_per_cycle=<median> us_min us_max secs=<median> words digest times partition
// --phases: per phase (98), us per cycle (median over the reps). --units: one thread, calibrating, each unit kind's
// time over the span. --windows W: one thread, windows of W cycles over the span, cost by steps a cycle.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "config.h"
#include "config_impl.h"
#include "internals.h"
#include "../../tests/golden_run.h"
#include "../../tests/sort_input.h"
#include "units/chip.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

const char* kImages = "tests/golden-m2/images";

uint32_t symbol(const std::string& name) {   // test_sort's: the image's .sym
    std::ifstream f(std::string(kImages) + "/sort.sym");
    for (std::string ln; std::getline(f, ln); ) {
        std::istringstream s(ln);
        std::vector<std::string> w; for (std::string x; s >> x; ) w.push_back(x);
        if (!w.empty() && w.back() == name) return uint32_t(std::stoul(w[0], nullptr, 16));
    }
    return ~0u;
}

struct Args {
    std::string keys, part = "one", trace_map;
    unsigned threads = 1, reps = 3, windows = 0;
    std::vector<int> cpus;
    bool time = false, phases = false, units = false;
    uint64_t trace_from = 0, trace_window = 3, trace_windows = 100;
    uint64_t span_go = 0, span_end = 0;   // --span GO END: skip the polling run (its phases unknown: --phases needs it)
};

// SortChip of test_sort.cpp, without the fingerprints: the image, the boot, M2Sort's sequence, the chip.
struct SortRun {
    test::Images im;
    Hops hops;
    CncProgram prog;
    std::vector<uint32_t> mem;
    G1Ctx gx;
    SramBlocks sram;
    std::unique_ptr<sim::Sim> m;
    uint64_t B = 0;

    SortRun(const Args& a, sim::Options opt, const std::string& part) : hops(load_hops("ref/meshsim/hops.txt")) {
        test::RunSpec rs; rs.name = "sort"; rs.dir = kImages; rs.image = "sort";
        std::string err;
        if (!test::load_images(rs, im, err)) FATAL("bench_sort: %s", err.c_str());
        prog.load_words(lw::BROADCAST, 0x01000000, im.code.data(), im.code.size());
        prog.load_words(lw::BROADCAST, im.data_at, im.data.data(), im.data.size());
        const uint32_t entry = im.entry;
        prog.command(lw::BROADCAST, 0x4Au << 24, &entry, 1);
        prog.run_when_ready(0);
        prog.release();
        using namespace sortin;
        const int fmt = FMT_K128;
        std::vector<uint32_t> cmd = {CMD_L1 << 24 | uint32_t(fmt), CMD_SPLIT << 24, CMD_XB << 24, CMD_RED << 24, CMD_XC << 24,
                                     CMD_TH << 24, CMD_SWEEP << 24, CMD_DESC << 24, CMD_K << 24};
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K1 << 24 | l);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K2 << 24 | l);
        cmd.push_back(CMD_V << 24);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V1 << 24 | l);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V2 << 24 | l);
        cmd.push_back(CMD_L2 << 24);
        prog.command(lw::BROADCAST, cmd[0]).go();
        for (size_t i = 1; i <= cmd.size(); i++) {
            if (cmd[i - 1] >> 24 == CMD_SPLIT)
                for (int b = 0; b < 66; b++) prog.command(lw::BROADCAST, CMD_BARRIER << 24).at_all_ready(0).go();
            prog.command(lw::BROADCAST, i < cmd.size() ? cmd[i] : CMD_EXIT << 24).at_all_ready(0).go();
        }
        prog.release();
        TileConfig tc = test::tile_config(0);
        mem.assign(64 * 2 * MEM_WORDS, 0);
        gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
        gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
        gx.cfg.sram_running_sum = false;                  // D31 (W11): no fingerprints here, so no running SRAM sum
        gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data();
        gx.sram = &sram;
        uint8_t dom[64];
        ChipSpec spec;
        spec.net = NetConfig{}; spec.hops = &hops; spec.program = &prog; spec.control = add_g1; spec.control_ctx = &gx;
        if (part == "rows") { row_bands(a.threads, dom); spec.domain = dom; }
        else if (part == "tiles") { for (unsigned k = 0; k < 64; k++) dom[k] = uint8_t(k); spec.domain = dom; }
        else if (part == "load" || part == "profile") opt.placement = sim::Options::Placement::by_load;
        else if (part != "one") FATAL("--partition %s: one, rows, tiles, load or profile", part.c_str());
        sim::Config cfg(opt);
        build_chip(cfg, spec);
        m = std::make_unique<sim::Sim>(cfg.build());
    }
    bool all_waiting() const {
        for (unsigned k = 0; k < 64; k++) {
            const TileCore& c = m->unit<TileCore>(tile_unit(k, "core"));
            if (!c.rv.in_wfi || c.rv.halt != Rv32::RUN) return false;
        }
        return true;
    }
    void boot() {   // ref's runUntil(!cc.busy() && allWaiting()), between cycles
        const Cnc& cc = m->unit<Cnc>("cnc");
        for (uint64_t c = 1; c <= 100000; c++) {
            m->run(1);
            if (!cc.busy() && all_waiting()) { B = c; return; }
        }
        FATAL("bench_sort: the boot didn't end by cycle 100000");
    }
    void load(const sortin::Keys& k) {
        const uint32_t s = symbol("S");
        std::vector<BigSram*> sr(64);
        for (unsigned t = 0; t < 64; t++) sr[t] = &m->unit<BigSram>(tile_unit(t, "sram"));
        sortin::load_input(k, sortin::FMT_K128, s,
                           [&](uint32_t t, uint32_t c, uint32_t v) { sr[t]->poke(c, v); },
                           [&](uint32_t t, uint32_t a, uint32_t v) { mem[size_t(t) * 2 * MEM_WORDS + ((a & 0x7FFF) >> 2)] = v; });
        m->unit<Cnc>("cnc").release_next();
        m->wake("cnc");
    }
};

struct Span { uint64_t go = 0, end = 0; std::vector<std::pair<uint64_t, uint64_t>> phases; };

// The phases by polling (test_sort's PhaseLog rule): a GO opens one, the next all-READY closes it.
Span find_span(SortRun& s) {
    Span sp;
    const Cnc& cc = s.m->unit<Cnc>("cnc");
    int64_t go_seen = cc.last_go_at, ready_seen = cc.last_all_ready_at;
    uint64_t c = s.B;
    std::vector<std::pair<int64_t, int64_t>> ph;
    for (;;) {
        s.m->run(1); c++;
        if (cc.last_all_ready_at != ready_seen) { ready_seen = cc.last_all_ready_at; if (!ph.empty()) ph.back().second = ready_seen; }
        if (cc.last_go_at != go_seen) { go_seen = cc.last_go_at; ph.push_back({go_seen, -1}); }
        bool stopped = !cc.busy();
        for (unsigned t = 0; t < 64 && stopped; t++) stopped = s.m->unit<TileCore>(tile_unit(t, "core")).rv.halt != Rv32::RUN;
        if (stopped || c > 3000000) break;
    }
    if (ph.size() < 2) FATAL("bench_sort: %zu phases", ph.size());
    sp.go = uint64_t(ph[0].first);
    sp.end = uint64_t(ph[ph.size() - 2].second);   // L2's all-READY: L2 is the last phase but one (EXIT's the last)
    for (size_t i = 0; i + 1 < ph.size(); i++) sp.phases.push_back({uint64_t(ph[i].first), uint64_t(ph[i].second)});
    return sp;
}

long rss_kb() {
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;
    char l[256]; long v = -1;
    while (std::fgets(l, sizeof l, f)) if (!std::strncmp(l, "VmRSS:", 6)) { v = std::atol(l + 6); break; }
    std::fclose(f);
    return v;
}

volatile uint64_t g_trace_mark = 0;

double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        auto val = [&]() -> const char* { if (i + 1 >= argc) FATAL("%s needs a value", argv[i]); return argv[++i]; };
        if (!std::strcmp(argv[i], "--keys")) a.keys = val();
        else if (!std::strcmp(argv[i], "--threads")) a.threads = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--partition")) a.part = val();
        else if (!std::strcmp(argv[i], "--reps")) a.reps = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--time")) a.time = true;
        else if (!std::strcmp(argv[i], "--phases")) a.phases = true;
        else if (!std::strcmp(argv[i], "--units")) a.units = true;
        else if (!std::strcmp(argv[i], "--windows")) a.windows = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--trace-map")) a.trace_map = val();
        else if (!std::strcmp(argv[i], "--trace-from")) a.trace_from = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--trace-window")) a.trace_window = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--trace-windows")) a.trace_windows = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--span")) { a.span_go = std::strtoull(val(), nullptr, 10); a.span_end = std::strtoull(val(), nullptr, 10); }
        else if (!std::strcmp(argv[i], "--cpus")) {
            const char* s = val();
            while (*s) { a.cpus.push_back(std::atoi(s)); while (*s && *s != ',') s++; if (*s) s++; }
        } else FATAL("bench_sort: unknown argument %s", argv[i]);
    }
    if (a.keys.empty()) FATAL("bench_sort: --keys PATH (KeyGen's uniform set, 64 x 16384)");
    std::string err;
    const sortin::Keys keys = sortin::read_keys(a.keys, err);
    if (!err.empty()) FATAL("bench_sort: %s", err.c_str());
    const std::string part = a.threads == 1 && a.part != "load" && a.part != "profile" ? std::string("one") : a.part;

    // the span (and the phases), once, by polling a one-thread machine
    Span sp;
    if (a.span_go && !a.phases) { sp.go = a.span_go; sp.end = a.span_end; }
    else {
        SortRun s(a, sim::Options{}, "one");
        s.boot(); s.load(keys);
        sp = find_span(s);
    }
    std::fprintf(stderr, "bench_sort: span %llu -> %llu (%llu cycles), %zu phases\n",
                 (unsigned long long)sp.go, (unsigned long long)sp.end, (unsigned long long)(sp.end - sp.go), sp.phases.size());
    const uint64_t span = sp.end - sp.go;

    if (a.units || a.windows) {   // one thread: the units' costs over the span, or the cost by steps a cycle
        sim::Options co; co.calibrate = a.units;
        SortRun s(a, co, "one");
        s.boot(); s.load(keys);
        s.m->run(sp.go - (s.B));
        const sim::Engine& e = sim::detail::Internals::impl(*s.m)->e;
        auto total_steps = [&]() { uint64_t t = 0; for (uint32_t i = 0; i < e.ninst; i++) t += e.steps[i]; return t; };
        if (a.windows) {
            struct B { double ns = 0; uint64_t cyc = 0, steps = 0, n = 0; };
            std::map<int, B> bins;
            uint64_t done = 0;
            while (done < span) {
                const uint64_t n = std::min<uint64_t>(a.windows, span - done);
                const uint64_t s0 = total_steps();
                const auto t0 = std::chrono::steady_clock::now();
                s.m->run(n);
                const double d = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
                const double spc = double(total_steps() - s0) / double(n);
                const int bin = spc < 0.5 ? -1 : int(std::floor(std::log2(spc)));
                B& b = bins[bin]; b.ns += d; b.cyc += n; b.steps += total_steps() - s0; b.n++;
                done += n;
            }
            for (auto& [bin, b] : bins)
                std::printf("sort_quiet bin=%s windows=%llu cycles=%llu share=%.3f steps_per_cycle=%.2f us_per_cycle=%.4f ns_per_step=%.1f\n",
                            bin < 0 ? "<0.5" : ("2^" + std::to_string(bin)).c_str(), (unsigned long long)b.n, (unsigned long long)b.cyc,
                            double(b.cyc) / double(span), double(b.steps) / double(b.cyc), b.ns / 1e3 / double(b.cyc),
                            b.steps ? b.ns / double(b.steps) : 0.0);
            return 0;
        }
        const sim::LoadProfile p0 = s.m->load_profile();
        const auto t0 = std::chrono::steady_clock::now();
        s.m->run(span);
        const double wall_ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        const sim::LoadProfile p1 = s.m->load_profile();
        double pair = 0;
        { const int N = 1000000; uint64_t acc = 0; for (int i = 0; i < N; i++) { const uint64_t x = sim::sim_ticks(); acc += sim::sim_ticks() - x; } pair = double(acc) / N; }
        struct K { double ticks = 0; uint64_t steps = 0, n = 0; };
        std::map<std::string, K> kinds;
        std::map<std::string, uint64_t> steps0;
        double total = 0;
        for (size_t i = 0; i < p1.unit.size(); i++) {
            std::string kind = p1.unit[i];
            if (kind.size() > 1 && kind[0] == 't' && isdigit(kind[1]) && kind.find('.') != std::string::npos) kind = kind.substr(kind.find('.') + 1);
            K& k = kinds[kind];
            const double t = p1.load[i] - (i < p0.load.size() ? p0.load[i] : 0);
            k.ticks += t; k.n++;
            total += t;
        }
        // steps over the span: the engine's counters can't be split from the profile, so read them before and after
        (void)steps0;
        std::printf("sort_units span=%llu wall_us_per_cycle=%.3f (calibrating) units_us_per_cycle=%.3f (clock %.1f ns a step not taken off)\n",
                    (unsigned long long)span, wall_ns / 1e3 / double(span), total / p1.ticks_per_ns / 1e3 / double(span), pair / p1.ticks_per_ns);
        std::vector<std::pair<double, std::string>> order;
        for (auto& [nm, k] : kinds) order.push_back({k.ticks, nm});
        std::sort(order.rbegin(), order.rend());
        for (auto& [t, nm] : order)
            std::printf("sort_unit kind=%s count=%llu share=%.4f us_per_cycle=%.4f\n", nm.c_str(), (unsigned long long)kinds[nm].n,
                        total > 0 ? t / total : 0, t / p1.ticks_per_ns / 1e3 / double(span));
        return 0;
    }

    sim::LoadProfile prof;
    sim::Options opt;
    opt.threads = a.threads;
    opt.cpus = a.cpus;
    opt.time_threads = a.time && a.threads > 1;
    if (part == "profile") {   // calibrated over the whole sort (B to the end), one thread
        sim::Options co; co.calibrate = true;
        SortRun s(a, co, "one");
        s.boot(); s.load(keys);
        s.m->run(sp.end - s.B);
        prof = s.m->load_profile();
        opt.load = &prof;
    }

    if (!a.trace_map.empty()) {   // footprint: the map, then windows from --trace-from (bench_chip's protocol)
        SortRun s(a, opt, part);
        s.boot(); s.load(keys);
        const uint64_t from = a.trace_from ? a.trace_from : sp.go;
        if (from > s.B) s.m->run(from - s.B);
        FILE* f = std::fopen(a.trace_map.c_str(), "w");
        if (!f) FATAL("--trace-map %s", a.trace_map.c_str());
        std::fprintf(f, "marker %p\nwindows %llu\n", (const void*)&g_trace_mark, (unsigned long long)a.trace_windows);
        for (const auto& r : sim::detail::Internals::regions(*s.m))
            if (r.bytes) std::fprintf(f, "region %p %zu %d %s\n", r.base, r.bytes, r.domain, r.what.c_str());
        const unsigned T = part == "one" ? 1 : a.threads;
        for (unsigned k = 0; k < 64; k++) {
            const int dom = int(k * T / 64);
            std::fprintf(f, "region %p %zu %d units' memory (port B data and program)\n", (const void*)(s.mem.data() + size_t(k) * 2 * MEM_WORDS),
                         size_t(2 * MEM_WORDS * 4), dom);
            std::fprintf(f, "region %p %zu %d big SRAM block\n", (const void*)s.sram.block(k, s.gx.cfg.mem_chunks), size_t(s.gx.cfg.mem_chunks) * 4, dom);
        }
        std::fclose(f);
        for (uint64_t w = 0; w < a.trace_windows; w++) { g_trace_mark = w; s.m->run(a.trace_window); }
        g_trace_mark = ~uint64_t(0);
        std::printf("sort_trace threads=%u part=%s from=%llu windows=%llu of %llu\n", a.threads, part.c_str(), (unsigned long long)from,
                    (unsigned long long)a.trace_windows, (unsigned long long)a.trace_window);
        return 0;
    }

    // the timed machines: one untimed, then reps; each: boot, load, run to L1's GO, then the span (or phase by phase)
    std::vector<double> us, secs;
    std::vector<std::vector<double>> ph_us(sp.phases.size());
    uint64_t words = 0, digest = 0;
    std::string times, partition;
    long rss = 0; size_t sram_res = 0;
    for (unsigned r = 0; r <= a.reps; r++) {
        SortRun s(a, opt, part);
        s.boot(); s.load(keys);
        s.m->run(sp.go - s.B);
        const auto t0 = std::chrono::steady_clock::now();
        if (a.phases) {
            uint64_t at = sp.go;
            for (size_t i = 0; i < sp.phases.size(); i++) {
                const uint64_t to = sp.phases[i].second;
                const auto p0 = std::chrono::steady_clock::now();
                s.m->run(to - at);
                if (r > 0) ph_us[i].push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - p0).count() / double(to - at));
                at = to;
            }
        } else s.m->run(span);
        const double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const Cnc& cc = s.m->unit<Cnc>("cnc");
        if (uint64_t(cc.last_all_ready_at) != sp.end || uint64_t(cc.last_go_at) > sp.end)
            FATAL("bench_sort: the timed machine's L2 all-READY at %lld, the span's at %llu", (long long)cc.last_all_ready_at, (unsigned long long)sp.end);
        if (r == 0) continue;
        us.push_back(1e6 * d / double(span)); secs.push_back(d);
        words = s.m->sizes().total_words; digest = s.m->digest(); times = s.m->thread_times(); partition = s.m->partition();
        rss = rss_kb(); sram_res = s.sram.touched_bytes();
    }
    std::vector<double> so(us); std::sort(so.begin(), so.end());
    std::printf("sort threads=%u part=%s span=%llu reps=%u us_per_cycle=%.4f us_min=%.4f us_max=%.4f secs=%.4f words=%llu digest=%016llx "
                "rss_mb=%.1f sram_resident_mb=%.1f times=\"%s\" partition=\"%s\"\n", a.threads, part.c_str(), (unsigned long long)span,
                a.reps, med(us), so.empty() ? 0 : so.front(), so.empty() ? 0 : so.back(), med(secs), (unsigned long long)words,
                (unsigned long long)digest, rss / 1024.0, sram_res / 1048576.0, times.c_str(), partition.c_str());
    if (a.phases)
        for (size_t i = 0; i < sp.phases.size(); i++)
            std::printf("sort_phase i=%zu go=%llu end=%llu cycles=%llu us_per_cycle=%.4f\n", i, (unsigned long long)sp.phases[i].first,
                        (unsigned long long)sp.phases[i].second, (unsigned long long)(sp.phases[i].second - sp.phases[i].first), med(ph_us[i]));
    return 0;
}
