// bench_threads: the threads' measurements (WORKER4). Timing builds: clang release (D18), pinned (D13).
//
//   bench_threads --form 1 --chip mesh|synth --chips N --threads T --cpus LIST [--cycles C] [--warmup W] [--rate R]
//       Independent chips: N chips split over T threads (contiguous shares), each thread pinned to the next CPU of LIST,
//       each chip built, run and digested on its thread. One line:
//       threads form=1 chip=mesh chips=N threads=T cycles=C secs=<wall> rate=<chip-cycles/s> slowest=<per-thread
//       chip-cycles/s> mean=<...> us_per_chip_cycle=<mean per thread> words_per_chip=<Sizes> digest=<all chips'>
//   bench_threads --sizes --chip mesh|synth: one chip's footprint by structure and by type (D15)
//   bench_threads --form 2 --threads T [--chip mesh|synth] [--domains D] [--partition rows|tiles] [--barrier spin|yield|block]
//       [--spins N] [--due-off] [--rate R] [--engines E]: one chip over T threads (form2() below)
//
// The digest folds every chip's digest in chip order: it must not depend on the threads or the CPUs.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "base/alloc_guard.h"
#include "base/fatal.h"
#include "config.h"
#include "mesh_fw.h"
#include "internals.h"
#include "parallel.h"
#include "../../tests/synth.h"

using namespace sim;

namespace {

struct Args {
    int form = 1;
    std::string chip = "mesh";
    unsigned chips = 1, threads = 1;
    std::vector<int> cpus;
    Cycle cycles = 20000, warmup = 2000;
    double rate = 0.35;
    uint64_t seed = 1;
    bool sizes = false;
    unsigned domains = 0;          // form 2: 0 = as many as threads
    std::string barrier = "yield"; // form 2: spin (never yields), yield (after --spins pauses), block (futex after them)
    unsigned spins = 2000;
    bool due_off = false;          // form 2 with one domain: the due mask off, as in a split machine's domains
    unsigned engines = 0;          // form 2 (W10): Options::engines (0: one per thread)
    bool time_threads = false;     // form 2 (W10): Options::time_threads, the phase shares printed as times="..."
    std::string partition = "rows";   // form 2 (W10): rows (mesh: bands of rows; synth: clusters in order), or tiles
                                      // (mesh: one domain per tile, 64; synth: one per cluster), spread over the threads
};

std::vector<int> parse_cpus(const char* s) {
    std::vector<int> v;
    for (const char* p = s; *p;) {
        char* e;
        long a = std::strtol(p, &e, 10), b = a;
        if (*e == '-') b = std::strtol(e + 1, &e, 10);
        for (long k = a; k <= b; k++) v.push_back(int(k));
        p = *e == ',' ? e + 1 : e;
        if (e == p && *p) FATAL("--cpus: can't read '%s'", s);
    }
    return v;
}

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        auto val = [&] { if (i + 1 >= argc) FATAL("%s needs a value", argv[i]); return argv[++i]; };
        if (!std::strcmp(argv[i], "--form")) a.form = std::atoi(val());
        else if (!std::strcmp(argv[i], "--chip")) a.chip = val();
        else if (!std::strcmp(argv[i], "--chips")) a.chips = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--threads")) a.threads = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--cpus")) a.cpus = parse_cpus(val());
        else if (!std::strcmp(argv[i], "--cycles")) a.cycles = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--warmup")) a.warmup = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--rate")) a.rate = std::atof(val());
        else if (!std::strcmp(argv[i], "--seed")) a.seed = std::strtoull(val(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--sizes")) a.sizes = true;
        else if (!std::strcmp(argv[i], "--domains")) a.domains = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--barrier")) a.barrier = val();
        else if (!std::strcmp(argv[i], "--spins")) a.spins = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--due-off")) a.due_off = true;
        else if (!std::strcmp(argv[i], "--partition")) a.partition = val();
        else if (!std::strcmp(argv[i], "--engines")) a.engines = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--time")) a.time_threads = true;
        else FATAL("unknown option %s", argv[i]);
    }
    if (a.form == 1 && (a.threads < 1 || a.chips < a.threads)) FATAL("need 1 <= threads <= chips");
    return a;
}

Sim make_chip(const Args& a, unsigned k) {
    Config cfg;
    if (a.chip == "mesh") mesh::build(cfg, a.seed + k, a.rate);
    else if (a.chip == "synth") {
        synth::Params p;
        p.seed = a.seed + k;
        p.clusters = 8; p.nodes_per_cluster = 8; p.gap = 1;
        synth::build(cfg, p);
    } else FATAL("--chip %s: mesh or synth", a.chip.c_str());
    return cfg.build();
}

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// A small spinning start line, so every thread starts timing together after its chips are built.
struct StartLine {
    std::atomic<unsigned> ready{0};
    void arrive_and_wait(unsigned n) { ready.fetch_add(1); while (ready.load() < n) std::this_thread::yield(); }
};

// One chip's footprint (D15), from Sizes: every line by structure and by type, in bytes.
int sizes(const Args& a) {
    Sim s = make_chip(a, 0);
    const Sizes& z = s.sizes();
    std::printf("sizes chip=%s total_bytes=%llu chips=%u\n", a.chip.c_str(), (unsigned long long)(8 * z.total_words), z.chips);
    for (const SizeLine& l : z.by_structure)
        std::printf("sizes chip=%s structure=\"%s\" bytes=%llu count=%u\n", a.chip.c_str(), l.what.c_str(), (unsigned long long)(8 * l.words), l.count);
    for (const SizeLine& l : z.by_type)
        std::printf("sizes chip=%s type=\"%s\" bytes=%llu count=%u\n", a.chip.c_str(), l.what.c_str(), (unsigned long long)(8 * l.words), l.count);
    return 0;
}

int form1(const Args& a) {
    (void)make_chip(a, 0);                         // register the unit types on this thread first (until W3's E1)
    std::vector<uint64_t> dig(a.chips);
    std::vector<double> busy(a.threads);
    uint64_t words = 0;
    StartLine start;
    std::vector<std::thread> th;
    for (unsigned t = 0; t < a.threads; t++)
        th.emplace_back([&, t] {
            if (!a.cpus.empty()) pin_to_cpu(a.cpus[t % a.cpus.size()]);
            const unsigned lo = a.chips * t / a.threads, hi = a.chips * (t + 1) / a.threads;
            std::vector<Sim> sims;
            for (unsigned k = lo; k < hi; k++) sims.push_back(make_chip(a, k));
            if (t == 0) words = sims[0].sizes().words_per_chip;
            for (auto& s : sims) s.run(a.warmup);
            start.arrive_and_wait(a.threads);
            const double b0 = now_s();
            begin_run();
            constexpr Cycle kChunk = 1000;             // the thread's chips in turn, a chunk each
            for (Cycle done = 0; done < a.cycles; done += kChunk)
                for (auto& s : sims) s.run(std::min(kChunk, a.cycles - done));
            end_run();
            busy[t] = now_s() - b0;
            for (unsigned k = lo; k < hi; k++) dig[k] = sims[k - lo].digest();
        });
    for (auto& x : th) x.join();
    double slowest = 0, mean = 0;
    for (unsigned t = 0; t < a.threads; t++) {
        const double share = double(a.chips * (t + 1) / a.threads - a.chips * t / a.threads);
        const double r = share * double(a.cycles) / busy[t];
        slowest = t == 0 || r < slowest ? r : slowest;
        mean += r / a.threads;
    }
    double maxbusy = 0;
    for (double b : busy) maxbusy = b > maxbusy ? b : maxbusy;
    uint64_t h = 0;
    for (uint64_t d : dig) h = mesh::fold(h, d);
    const double total = double(a.chips) * double(a.cycles);
    std::printf("threads form=1 chip=%s chips=%u threads=%u cycles=%llu secs=%.4f rate=%.0f slowest=%.0f mean=%.0f "
                "us_per_chip_cycle=%.3f words_per_chip=%llu digest=%016llx\n",
                a.chip.c_str(), a.chips, a.threads, (unsigned long long)a.cycles, maxbusy, total / maxbusy, slowest, mean,
                1e6 / mean, (unsigned long long)words, (unsigned long long)h);
    return 0;
}

// Form 2: one chip over several threads. The mesh: by rows, tile k is in domain (k / 8) * D / 8 (bands of rows); by
// tiles, domain k (64 domains). The synthetic chip (8 clusters of 8 nodes): by rows, cluster c in domain c * D / 8; by
// tiles, domain c. Domains are spread over the threads in order (W4). On the mesh every crossing is a Link of 3 (words and
// credits), so the barrier is every 3 cycles. One line:
//   threads form=2 chip=C threads=T domains=D partition=P barrier=B spins=N due=on|off cycles=C secs=S
//   rate=<chip-cycles/s> us_per_chip_cycle=<wall> words=<Sizes> digest=<D> partition="..."
int form2(const Args& a) {
    const bool tiles = a.partition == "tiles";
    const bool by_load = a.partition == "load" || a.partition == "profile";   // W10: the framework places (no domains)
    if (!tiles && !by_load && a.partition != "rows") FATAL("--partition %s: rows, tiles, load or profile", a.partition.c_str());
    const bool mesh_chip = a.chip == "mesh";
    if (!mesh_chip && a.chip != "synth") FATAL("--chip %s: mesh or synth", a.chip.c_str());
    const unsigned granules = mesh_chip ? mesh::kSide * mesh::kSide : 8;
    const unsigned D = by_load ? 1 : tiles ? granules : a.domains ? a.domains : a.threads;
    auto add_chip = [&](Config& cfg, unsigned dom_count, bool per_granule) {
        if (mesh_chip) {
            if (per_granule) mesh::build(cfg, a.seed, a.rate, [](unsigned k) { return k; });
            else mesh::build(cfg, a.seed, a.rate, [dom_count](unsigned k) { return (k / mesh::kSide) * dom_count / mesh::kSide; });
        } else {
            synth::Params p;
            p.seed = a.seed;
            p.clusters = 8; p.nodes_per_cluster = 8; p.gap = 1;
            if (per_granule) synth::build(cfg, p, [](unsigned c) { return c; });
            else synth::build(cfg, p, [dom_count](unsigned c) { return c * dom_count / 8; });
        }
    };
    // --partition profile: a calibration run first (one thread, the warmup plus the measured cycles), its loads kept
    LoadProfile prof;
    if (a.partition == "profile") {
        Options c; c.calibrate = true;
        Config cc(c);
        add_chip(cc, 1, false);
        Sim cal = cc.build();
        cal.run(a.warmup + a.cycles);
        prof = cal.load_profile();
    }
    Options o;
    o.threads = D > 1 ? a.threads : 1;
    o.engines = a.engines;
    o.time_threads = a.time_threads;
    o.cpus = a.cpus;
    if (by_load) { o.placement = Options::Placement::by_load; o.threads = a.threads; if (a.partition == "profile") o.load = &prof; }
    o.barrier_block = a.barrier == "block";
    o.barrier_spins = a.barrier == "spin" ? ~0u : a.spins;
    Config cfg(o);
    add_chip(cfg, D, tiles);
    Sim s = cfg.build();
    if (a.due_off) {
        if (detail::Internals::impl(s)->threads) FATAL("--due-off: one engine only (a split machine's engines have it off already)");
        detail::Internals::impl(s)->e.due_ok = false;
    }
    const bool split = detail::Internals::impl(s)->threads != nullptr;   // W10: several domains may be one engine
    const bool due = split ? false : detail::Internals::impl(s)->e.due_ok;
    if (o.threads == 1 && !a.cpus.empty()) pin_to_cpu(a.cpus[0]);
    s.run(a.warmup);
    begin_run();
    const double t0 = now_s();
    s.run(a.cycles);
    const double secs = now_s() - t0;
    end_run();
    std::printf("threads form=2 chip=%s threads=%u domains=%u engines=%u part=%s barrier=%s spins=%u due=%s rate_in=%.2f cycles=%llu "
                "secs=%.4f rate=%.0f us_per_chip_cycle=%.3f words=%llu digest=%016llx times=\"%s\" partition=\"%s\"\n",
                a.chip.c_str(), o.threads, D, a.engines, a.partition.c_str(), a.barrier.c_str(), a.spins, due ? "on" : "off", a.rate,
                (unsigned long long)a.cycles, secs, double(a.cycles) / secs, 1e6 * secs / double(a.cycles),
                (unsigned long long)s.sizes().total_words, (unsigned long long)s.digest(), s.thread_times().c_str(), s.partition().c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    if (a.sizes) return sizes(a);
    if (a.form == 1) return form1(a);
    if (a.form == 2) return form2(a);
    FATAL("--form %d: 1 or 2", a.form);
}
