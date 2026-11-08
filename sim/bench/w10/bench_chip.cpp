// bench_chip (WORKER10): W9's chip (build_chip with W8's G1 units in every tile) running a program from cycle 0, timed,
// one thread or form 2, by placement. The benchmark that counts until the sort runs at 16K (owner, 2026-11-07): the
// sort's boot (the sort image loaded over the network and port B: 14,791 cycles before any engine runs), wfi, regs.
//
//   bench_chip --prog sort|wfi|regs [--cycles N] [--threads T] [--partition one|rows|tiles|load|profile]
//              [--cpus a,b,..] [--time] [--crossing-ns X] [--reps R] [--barrier counter|flags|flags-spin]
//
// --cycles: default sort 14791 (the boot), wfi and regs: until every core has stopped (checked every 64 cycles; the
// count is the cycles run). rows: row_bands(T); tiles: one domain per tile (grouped in order, one engine per thread);
// load: Placement::by_load, 1 per unit; profile: by_load from a one-thread calibration run of the same program and
// cycles. The first cycle runs outside the timing (a split machine's first run starts its workers). One line:
//   chip prog=P threads=T part=X cycles=C reps=R us_per_cycle=<median> us_min=.. us_max=.. words=W digest=D times="..."
//   partition="..." (times: the last machine's; with --time, its phase shares)
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "config.h"
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
    std::string barrier = "counter";   // --barrier counter (W4's) | flags (Options::barrier_flags) | flags-spin
    unsigned reps = 5;         // --reps: machines built and run in this process, after one untimed (a run is milliseconds:
                               // the clock's ramp, first touches and the workers' start would otherwise dominate)
};

struct World {   // what the machine reads while it runs: outlives it
    test::Images im;
    Hops hops;
    CncProgram prog;
    std::vector<uint32_t> mem;
    G1Ctx gx;
};

void prepare(World& w, const std::string& name) {
    test::RunSpec rs; rs.name = name; rs.dir = "tests/golden-m2/images"; rs.image = name;
    std::string err;
    if (!test::load_images(rs, w.im, err)) FATAL("bench_chip: %s", err.c_str());
    w.hops = load_hops("ref/meshsim/hops.txt");
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
        else if (!std::strcmp(argv[i], "--cpus")) {
            const char* s = val();
            while (*s) { a.cpus.push_back(std::atoi(s)); while (*s && *s != ',') s++; if (*s) s++; }
        } else FATAL("bench_chip: unknown argument %s", argv[i]);
    }
    if (a.prog != "sort" && a.prog != "wfi" && a.prog != "regs") FATAL("--prog %s: sort, wfi or regs", a.prog.c_str());
    const uint64_t cycles = a.cycles ? a.cycles : a.prog == "sort" ? 14791 : a.prog == "regs" ? 824 : 0;
    const std::string part = a.threads == 1 && a.part != "load" && a.part != "profile" ? std::string("one") : a.part;

    sim::LoadProfile prof;
    sim::Options opt;
    opt.threads = a.threads;
    opt.cpus = a.cpus;
    opt.time_threads = a.time && a.threads > 1;
    if (a.crossing_ns >= 0) opt.crossing_ns = a.crossing_ns;
    if (a.barrier != "counter" && a.barrier != "flags" && a.barrier != "flags-spin") FATAL("--barrier %s", a.barrier.c_str());
    opt.barrier_flags = a.barrier != "counter";
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
    for (unsigned r = 0; r <= a.reps; r++) {
        World rw;
        prepare(rw, a.prog);
        sim::Sim m = build(rw, a, opt, part);
        m.run(1);                                         // the first run starts the workers: outside the count
        const auto t0 = std::chrono::steady_clock::now();
        ran = 1 + run_prog(m, cycles ? cycles - 1 : 0, 2000000);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (r == 0) continue;
        us.push_back(1e6 * secs / double(ran));
        words = m.sizes().total_words; digest = m.digest(); times = m.thread_times(); partition = m.partition();
    }
    std::vector<double> so(us);
    std::sort(so.begin(), so.end());
    const double medv = so.empty() ? 0 : so[so.size() / 2];
    std::printf("chip prog=%s threads=%u part=%s barrier=%s cycles=%" PRIu64 " reps=%u us_per_cycle=%.3f us_min=%.3f us_max=%.3f "
                "words=%llu digest=%016llx times=\"%s\" partition=\"%s\"\n",
                a.prog.c_str(), a.threads, part.c_str(), a.barrier.c_str(), ran, a.reps, medv, so.empty() ? 0 : so.front(), so.empty() ? 0 : so.back(),
                (unsigned long long)words, (unsigned long long)digest, times.c_str(), partition.c_str());
    return 0;
}
