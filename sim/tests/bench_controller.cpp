// bench_controller: the controller's speed (D13, D18: run it pinned to one P-core, clang release).
//
//   bench_controller [--iters N] [--reps R] [--trace] [--only engine|core] [--threads T --cpus a,b,...] [--write DIR]
//
// The program: booted as `hello` is (the boot ROM, the replayed command and GO), then a loop of N iterations of nine
// instructions, 13 cycles: a data-memory load and store, three ALU operations, a multiply (3 cycles), a register-block load
// (EVENTS_PEEK, 2 cycles), the count and a taken branch (2 cycles); then SIM_EXIT. Checked first: at N = 1000 the engine's
// closing block equals the direct driver's, and every run halts by SIM_EXIT with the count in data memory.
//   engine   the three units on the framework, ready-driven, the core untraced (--trace: traced): instructions/s,
//            cycles/s, ns per unit step (the run alone: not the build)
//   core     the same program from its entry (no boot) on the plain core (rv32.h) with a constant environment, stepped
//            when it asks: the interpreter's own cost, no engine and no register block
//   threads  (--threads T) T controllers, each running the program on its own thread (W4's run_independent; thread k on
//            cpus[k % size]): the aggregate instructions/s and each thread's ns per step
//   --write DIR  writes the program as DIR/loop.code.bin (for ref: tests/ref/SpeedRun.java, TraceRun) and, for N up to
//            10^5, the engine's trace of it as DIR/loop.trace, then stops
// Run from sim/ (it reads the hello golden's replayed lines and the boot ROM).
#include <algorithm>
#include <chrono>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "golden_run.h"
#include "parallel.h"
#include "rv32_asm.h"

using namespace test;
using namespace rv32_asm;
using units::Rv32;

static std::vector<uint32_t> program(uint32_t n) {
    uint32_t hi = (n + 0x800) >> 12;
    int32_t lo = int32_t(n - (hi << 12));
    return {
        ADDI(1, 0, 0x100),                     // the data word
        LUI(2, hi << 12), ADDI(2, 2, lo),      // the count
        ADDI(10, 0, -2048),                    // REG_BASE
        LW(3, 1, 0),                           // loop:
        ADDI(3, 3, 1),
        SW(3, 1, 0),
        R(0, 3, 4, 0, 4),                      // add x4, x4, x3
        R(0, 4, 5, 4, 5),                      // xor x5, x5, x4
        R(1, 5, 4, 0, 6),                      // mul x6, x4, x5
        LW(9, 10, 0x1C),                       // EVENTS_PEEK
        ADDI(2, 2, -1),
        B(-32, 0, 2, 1),                       // bne x2, x0, loop
        ADDI(7, 0, -1544),                     // SIM_EXIT (0xFFFFF9F8)
        SW(0, 7, 0),
    };
}

struct Setup { RunSpec r; Golden g; Images im; units::TileConfig c; std::vector<std::string> header; };
static bool setup(uint32_t n, Setup& s) {
    auto runs = read_runs();
    const RunSpec* r = find_run(runs, "hello");
    std::string err;
    if (!r || !read_golden("tests/golden/hello.trace", s.g, err) || !load_images(*r, s.im, err)) {
        fprintf(stderr, "bench: %s\n", err.c_str()); return false;
    }
    s.r = *r;
    s.im.code = program(n);
    s.im.data.clear();
    s.c = tile_config(0);
    s.header = header_lines(s.r, s.im, s.c);
    s.g.end_cycle = 100 + 13ull * n + 100;     // the boot, the loop, the exit, with room: the core halts before
    return true;
}
static bool halted_right(const Rv32& c, const std::vector<uint32_t>& dmem, uint32_t n) {
    return c.stop == units::Stop::halt && c.halt == Rv32::HALT_EXIT && dmem[0x100 / 4] == n;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uint32_t iters = 10000000, reps = 3;
    bool trace = false;
    const char* only = "";
    unsigned threads = 0;
    const char* write_dir = nullptr;
    std::vector<int> cpus;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--iters") && i + 1 < argc) iters = uint32_t(atol(argv[++i]));
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = uint32_t(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--trace")) trace = true;
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = unsigned(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--write") && i + 1 < argc) write_dir = argv[++i];
        else if (!strcmp(argv[i], "--cpus") && i + 1 < argc)
            for (char* t = strtok(argv[++i], ","); t; t = strtok(nullptr, ",")) cpus.push_back(atoi(t));
        else { fprintf(stderr, "usage: bench_controller [--iters N] [--reps R] [--trace] [--only engine|core] [--threads T --cpus a,b,...]\n"); return 2; }
    }
    // The check: the engine and the direct driver agree on the program, and it does what it should.
    {
        Setup s;
        if (!setup(1000, s)) return 2;
        Tile a(s.im, s.c, 20000), b(s.im, s.c, 20000);
        EngineRun e = run_engine(a, s.g, s.header, sim::Options{});
        std::string d = compare(e.lines, run_direct(b, s.g, s.header));
        if (!d.empty()) { fprintf(stderr, "bench: the engine and the direct driver differ:\n%s", d.c_str()); return 1; }
        if (!halted_right(b.core, b.dmem, 1000)) { fprintf(stderr, "bench: the program didn't end as it should\n"); return 1; }
        printf("check: N = 1000, engine = direct driver (%zu lines), SIM_EXIT, count %u\n", e.lines.size(), b.dmem[0x100 / 4]);
    }
    Setup s;
    if (!setup(iters, s)) return 2;
    if (write_dir) {                                    // --write DIR: the program for ref (DIR/loop.code.bin) and our trace
        std::string dir = write_dir;
        FILE* f = fopen((dir + "/loop.code.bin").c_str(), "wb");
        if (!f) { fprintf(stderr, "bench: cannot write %s/loop.code.bin\n", write_dir); return 2; }
        for (uint32_t w : s.im.code) for (int b = 0; b < 4; b++) fputc(int(w >> (8 * b) & 0xFF), f);
        fclose(f);
        if (iters > 100000) {                           // the trace only for small N (it is a line per instruction)
            printf("wrote %s/loop.code.bin (%zu words, N %u); no trace above N = 100000\n", write_dir, s.im.code.size(), iters);
            return 0;
        }
        Tile t(s.im, s.c, uint32_t(9ull * iters + 1000));
        EngineRun e = run_engine(t, s.g, s.header, sim::Options{});
        FILE* o = fopen((dir + "/loop.trace").c_str(), "w");
        if (!o) return 2;
        for (const std::string& l : e.lines) fprintf(o, "%s\n", l.c_str());
        fclose(o);
        printf("wrote %s/loop.code.bin (%zu words) and %s/loop.trace (%zu lines, N %u)\n", write_dir, s.im.code.size(),
               write_dir, e.lines.size(), iters);
        return 0;
    }
    if (threads) {                                      // T controllers on T threads, the same program each
        for (uint32_t k = 0; k < reps; k++) {
            std::vector<std::unique_ptr<Tile>> tile(threads);
            std::vector<EngineRun> res(threads);
            sim::run_independent(threads,
                [&](unsigned i) {
                    tile[i] = std::make_unique<Tile>(s.im, s.c, 64);
                    tile[i]->core.log = nullptr;
                    return build_engine(*tile[i], s.g, sim::Options{});
                },
                [&](unsigned i, sim::Sim& sm) { res[i] = run_built(sm, *tile[i], s.g, s.header); },
                cpus);
            double slowest = 0, sum_ns = 0; uint64_t instr = 0; bool ok = true;
            for (unsigned i = 0; i < threads; i++) {
                slowest = std::max(slowest, res[i].seconds);
                instr += res[i].instructions;
                sum_ns += res[i].seconds / double(res[i].steps[0] + res[i].steps[1] + res[i].steps[2]) * 1e9;
                ok = ok && res[i].lines.size() > 3 && res[i].lines[res[i].lines.size() - 4].rfind("END halt 5", 0) == 0;
            }
            printf("threads %u: N %u each: %.2f M instructions/s in all (%.2f per thread), %.2f ns per step on average, "
                   "slowest thread %.3f s%s\n", threads, iters, instr / slowest / 1e6, instr / slowest / 1e6 / threads,
                   sum_ns / threads, slowest, ok ? "" : "  (A THREAD DID NOT HALT BY SIM_EXIT)");
        }
        return 0;
    }
    for (uint32_t k = 0; k < reps && strcmp(only, "core"); k++) {
        uint32_t cap = trace ? uint32_t(9ull * iters + 1000) : 64;
        Tile t(s.im, s.c, cap);
        if (!trace) t.core.log = nullptr;
        EngineRun e = run_engine(t, s.g, s.header, sim::Options{});
        uint64_t steps = e.steps[0] + e.steps[1] + e.steps[2];
        uint64_t cycles = 0;
        for (const std::string& l : e.lines) if (l.rfind("END ", 0) == 0) cycles = std::stoull(l.substr(l.rfind(' ') + 1));
        bool ok = e.lines.size() > 3 && e.lines[e.lines.size() - 4].rfind("END halt 5", 0) == 0;
        printf("engine%s: N %u: %llu instructions, %llu cycles, %llu steps (core %llu, block %llu, bench %llu) in %.3f s: "
               "%.2f M instructions/s, %.2f M cycles/s, %.2f ns per step%s\n",
               trace ? " traced" : "", iters, (unsigned long long)e.instructions, (unsigned long long)cycles,
               (unsigned long long)steps, (unsigned long long)e.steps[0], (unsigned long long)e.steps[1],
               (unsigned long long)e.steps[2], e.seconds, e.instructions / e.seconds / 1e6, cycles / e.seconds / 1e6,
               e.seconds / steps * 1e9, ok ? "" : "  (DID NOT HALT BY SIM_EXIT)");
    }
    // The plain core alone, from the program's entry (no boot ROM), with a constant environment (the block's reset view).
    struct Env {
        uint32_t regs[10] = {};
        uint32_t reg(unsigned r) const { return regs[r]; }
        bool wake() const { return true; }
        bool irq() const { return false; }
        bool can_push() const { return true; }
        void post(uint32_t, uint32_t, uint32_t) {}
    };
    for (uint32_t k = 0; k < reps && strcmp(only, "engine"); k++) {
        Tile t(s.im, s.c, 64);
        t.core.log = nullptr;
        t.core.pc = 0x01000000;                       // straight to the program: no boot ROM (it waits on the bench)
        Env env;
        env.regs[units::PARAMS] = t.block.params; env.regs[units::EVENTS] = 1u << units::EV_GO;
        env.regs[units::EVENTS_MASK] = units::EVENTS_MASK_RESET;
        uint64_t steps = 0, now = 1;
        auto t0 = std::chrono::steady_clock::now();
        while (now != Rv32::kNever && now <= s.g.end_cycle) { uint64_t next = t.core.step(env, now); steps++; now = next; }
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool ok = halted_right(t.core, t.dmem, iters);
        printf("core alone: N %u: %llu instructions, %llu cycles, %llu steps in %.3f s: %.2f M instructions/s, "
               "%.2f ns per step%s\n", iters, (unsigned long long)t.core.instructions, (unsigned long long)t.core.mcycle,
               (unsigned long long)steps, sec, t.core.instructions / sec / 1e6, sec / steps * 1e9,
               ok ? "" : "  (DID NOT HALT BY SIM_EXIT)");
    }
    return 0;
}
