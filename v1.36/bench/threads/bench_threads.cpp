// bench_threads: the threads' measurements. Timing builds: clang release (D18), pinned (D13).
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
#include "barrier.h"
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
    std::string barrier = "auto";  // form 2: spin (never yields), yield (after --spins pauses), block (futex after them):
                                   // the counter barrier; flags, flags-spin: the flags barrier; auto: the default
    unsigned spins = 2000;
    bool due_off = false;          // form 2 with one domain: the due mask off, as in a split machine's domains
    unsigned engines = 0;          // form 2: Options::engines (0: one per thread)
    bool time_threads = false;     // form 2: Options::time_threads, the phase shares printed as times="..."
    bool barrier_only = false;     // form 3: the barrier alone
    unsigned units = 64, active = 1;   // form 4: units in all, busy among them
    double crossing_ns = -1;       // form 2: Options::crossing_ns (default: the framework's)
    std::string partition = "rows";   // form 2: rows (mesh: bands of rows; synth: clusters in order), or tiles
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
        else if (!std::strcmp(argv[i], "--barrier-only")) a.barrier_only = true;
        else if (!std::strcmp(argv[i], "--units")) a.units = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--active")) a.active = unsigned(std::atoi(val()));
        else if (!std::strcmp(argv[i], "--crossing-ns")) a.crossing_ns = std::atof(val());
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
    (void)make_chip(a, 0);                         // register the unit types on this thread first (until the E1)
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
// tiles, domain c. Domains are spread over the threads in order. On the mesh every crossing is a Link of 3 (words and
// credits), so the barrier is every 3 cycles. One line:
//   threads form=2 chip=C threads=T domains=D partition=P barrier=B spins=N due=on|off cycles=C secs=S
//   rate=<chip-cycles/s> us_per_chip_cycle=<wall> words=<Sizes> digest=<D> partition="..."
int form2(const Args& a) {
    const bool tiles = a.partition == "tiles";
    const bool by_load = a.partition == "load" || a.partition == "profile";   // the framework places (no domains)
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
    if (a.crossing_ns >= 0) o.crossing_ns = a.crossing_ns;
    o.cpus = a.cpus;
    if (by_load) { o.placement = Options::Placement::by_load; o.threads = a.threads; if (a.partition == "profile") o.load = &prof; }
    o.barrier_block = a.barrier == "block";
    o.barrier = a.barrier.rfind("flags", 0) == 0 ? Options::Barrier::flags
              : a.barrier == "auto" ? Options::Barrier::automatic : Options::Barrier::counter;
    o.barrier_spins = (a.barrier == "spin" || a.barrier == "flags-spin") ? ~0u : a.spins;
    Config cfg(o);
    add_chip(cfg, D, tiles);
    Sim s = cfg.build();
    if (a.due_off) {
        if (detail::Internals::impl(s)->threads) FATAL("--due-off: one engine only (a split machine's engines have it off already)");
        detail::Internals::impl(s)->e.due_ok = false;
    }
    const bool split = detail::Internals::impl(s)->threads != nullptr;   // several domains may be one engine
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

// Form 3: the bare interval. T domains on T threads, each one unit that steps every cycle and does nothing but,
// with --rate R > 0, put on a Link of latency 3 to the next domain with that probability (a ring): what a barrier
// interval costs with no work (publish, the barrier, the drain, clear, the engine's per-interval entry and exit).
// And --barrier-only: T threads doing nothing but Barrier::wait, rounds per second (the barrier alone).
//   threads form=3 threads=T barrier=B spins=N rate=R cycles=C secs=S us_per_cycle=U us_per_interval=I times="..."
struct Idle {
    LinkOut out; LinkIn in;
    uint64_t rng; double rate; uint64_t got = 0;
    Idle(Ports& p, uint64_t seed, double r) : rng(seed), rate(r) { p.out(out, "out", 1); p.in(in, "in", 1); }
    uint64_t digest() const { return hash_fields(rng, got); }
    Wake step(Ctx& c) {
        if (c.ready(in)) { got++; c.take(in); }
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        if (rate > 0 && double(rng >> 11) * (1.0 / 9007199254740992.0) < rate) c.put(out, rng);
        return c.next();
    }
};
int form3(const Args& a) {
    const unsigned T = std::max(2u, a.threads);
    if (a.barrier_only) {
        detail::Barrier b;
        b.init(T, a.barrier == "block", (a.barrier == "spin" || a.barrier == "flags-spin") ? ~0u : a.spins, a.barrier.rfind("flags", 0) == 0);
        std::vector<std::thread> th;
        const uint64_t rounds = a.cycles;
        double secs = 0;
        for (unsigned t = 0; t < T; t++)
            th.emplace_back([&, t] {
                if (!a.cpus.empty()) pin_to_cpu(a.cpus[t % a.cpus.size()]);
                b.wait(t);
                const double t0 = now_s();
                for (uint64_t r = 0; r < rounds; r++) b.wait(t);
                if (t == 0) secs = now_s() - t0;
            });
        for (auto& x : th) x.join();
        std::printf("threads form=3 barrier_only threads=%u barrier=%s spins=%u rounds=%llu secs=%.4f us_per_round=%.4f\n",
                    T, a.barrier.c_str(), a.spins, (unsigned long long)rounds, secs, 1e6 * secs / double(rounds));
        return 0;
    }
    Options o;
    o.threads = T;
    o.cpus = a.cpus;
    o.barrier_block = a.barrier == "block";
    o.barrier = a.barrier.rfind("flags", 0) == 0 ? Options::Barrier::flags
              : a.barrier == "auto" ? Options::Barrier::automatic : Options::Barrier::counter;
    o.barrier_spins = (a.barrier == "spin" || a.barrier == "flags-spin") ? ~0u : a.spins;
    o.time_threads = a.time_threads;
    Config cfg(o);
    std::vector<Idle*> u;
    for (unsigned d = 0; d < T; d++) { cfg.domain(d); u.push_back(&cfg.add<Idle>("u" + std::to_string(d), uint64_t(d + 1), a.rate)); }
    for (unsigned d = 0; d < T; d++) cfg.link(u[d]->out, u[(d + 1) % T]->in, 3);
    Sim s = cfg.build();
    s.run(a.warmup);
    const double t0 = now_s();
    s.run(a.cycles);
    const double secs = now_s() - t0;
    std::printf("threads form=3 threads=%u barrier=%s spins=%u rate_in=%.2f cycles=%llu secs=%.4f us_per_cycle=%.4f "
                "us_per_interval=%.4f digest=%016llx times=\"%s\"\n", T, a.barrier.c_str(), a.spins, a.rate,
                (unsigned long long)a.cycles, secs, 1e6 * secs / double(a.cycles), 3e6 * secs / double(a.cycles),
                (unsigned long long)s.digest(), s.thread_times().c_str());
    return 0;
}

// Form 4: the quiet case. --units N units on one thread, --active K of them busy (each cycle: a little hashing and
// a put on a Link of 1 to the next busy unit, a ring), the rest asleep after their first step (each with a Link in
// and out, wired in a ring of their own that never carries a word). Cost per cycle against N and K: does an idle unit
// cost anything per cycle?
//   threads form=4 units=N active=K cycles=C secs=S us_per_cycle=U ns_per_active_step=X words=W
struct Busy {
    LinkOut out; LinkIn in; uint64_t h;
    explicit Busy(Ports& p, uint64_t seed) : h(seed) { p.out(out, "out", 1); p.in(in, "in", 1); }
    uint64_t digest() const { return hash_fields(h); }
    Wake step(Ctx& c) {
        if (c.ready(in)) { h = hash_mix(h, c.peek(in)[0]); c.take(in); }
        for (int r = 0; r < 8; r++) h = hash_mix(h, uint64_t(r));
        c.put(out, h);
        return c.next();
    }
};
struct Asleep {
    LinkOut out; LinkIn in; uint64_t n = 0;
    explicit Asleep(Ports& p) { p.out(out, "out", 1); p.in(in, "in", 1); }
    uint64_t digest() const { return hash_fields(n); }
    Wake step(Ctx& c) { n++; if (c.ready(in)) c.take(in); return Wake::none(); }
};
int form4(const Args& a) {
    const unsigned N = a.units, K = a.active;
    if (K > N || K == 0) FATAL("--active %u of --units %u: 1 <= active <= units", K, N);
    Options o;
    Config cfg(o);
    std::vector<Busy*> b; std::vector<Asleep*> z;
    for (unsigned i = 0; i < K; i++) b.push_back(&cfg.add<Busy>("b" + std::to_string(i), uint64_t(i + 1)));
    for (unsigned i = 0; i < N - K; i++) z.push_back(&cfg.add<Asleep>("z" + std::to_string(i)));
    for (unsigned i = 0; i < K; i++) cfg.link(b[i]->out, b[(i + 1) % K]->in, 1);
    for (unsigned i = 0; i < z.size(); i++) cfg.link(z[i]->out, z[(i + 1) % z.size()]->in, 3);
    Sim s = cfg.build();
    if (!a.cpus.empty()) pin_to_cpu(a.cpus[0]);
    s.run(a.warmup);
    const double t0 = now_s();
    s.run(a.cycles);
    const double secs = now_s() - t0;
    std::printf("threads form=4 units=%u active=%u cycles=%llu secs=%.4f us_per_cycle=%.4f ns_per_active_step=%.2f words=%llu digest=%016llx\n",
                N, K, (unsigned long long)a.cycles, secs, 1e6 * secs / double(a.cycles), 1e9 * secs / double(a.cycles) / K,
                (unsigned long long)s.sizes().total_words, (unsigned long long)s.digest());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    if (a.sizes) return sizes(a);
    if (a.form == 1) return form1(a);
    if (a.form == 2) return form2(a);
    if (a.form == 3) return form3(a);
    if (a.form == 4) return form4(a);
    FATAL("--form %d: 1, 2, 3 or 4", a.form);
}
