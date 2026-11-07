// golden_main.cpp: `golden [--direct | --engine] [names...]`: runs the golden traces (all of tests/golden/RUNS, or those
// named) and reports each. --direct (the default): the direct driver. --engine: the units on the framework, ready-driven
// and under every debug check (shuffle seeds 1-3, instance-major, step-all), each trace equal to the golden and each
// digest to the ready-driven run's. Run from sim/. Exit status 1 if any differs or is refused.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "golden_run.h"

int main(int argc, char** argv) {
    auto runs = test::read_runs();
    if (runs.empty()) { fprintf(stderr, "golden: no runs (run from sim/: tests/golden/RUNS)\n"); return 2; }
    bool engine = false;
    unsigned bench = 0;
    std::vector<const test::RunSpec*> todo;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--engine")) { engine = true; continue; }
        if (!strcmp(argv[i], "--direct")) { engine = false; continue; }
        if (!strcmp(argv[i], "--bench") && i + 1 < argc) { bench = unsigned(atoi(argv[++i])); continue; }
        const test::RunSpec* r = test::find_run(runs, argv[i]);
        if (!r) { fprintf(stderr, "golden: no run named %s\n", argv[i]); return 2; }
        todo.push_back(r);
    }
    if (todo.empty()) for (auto& r : runs) todo.push_back(&r);
    if (bench) {                                       // golden --bench REPS [names]: the engine's speed, runs alone
        for (bool trace : {true, false}) {
        test::BenchResult all;
        for (const test::RunSpec* r : todo) {
            test::BenchResult b = test::bench_engine(*r, bench, trace);
            all.seconds += b.seconds; all.instructions += b.instructions; all.cycles += b.cycles; all.steps += b.steps;
        }
        printf("engine, ready-driven, %s: %llu instructions, %llu cycles, %llu unit steps in %.3f s: %.2f M instructions/s, "
               "%.2f M cycles/s, %.1f ns per step\n", trace ? "traced" : "no trace", (unsigned long long)all.instructions, (unsigned long long)all.cycles,
               (unsigned long long)all.steps, all.seconds, all.instructions / all.seconds / 1e6, all.cycles / all.seconds / 1e6,
               all.seconds / all.steps * 1e9);
        }
        return 0;
    }
    unsigned bad = 0;
    auto opts = test::debug_check_options();
    if (engine && !test::engine_has_debug_checks()) {
        printf("note: the engine ignores the debug-check options (W3's increment 5): ready-driven only\n");
        opts.resize(1);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (const test::RunSpec* r : todo) {
        std::string d = engine ? test::check_engine(*r, opts) : test::check_direct(*r);
        if (d.empty()) printf("SAME  %s\n", r->name.c_str());
        else { printf("DIFF  %s\n%s\n", r->name.c_str(), d.c_str()); bad++; }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("golden (%s): %zu runs, %u differ, %.3f s\n", engine ? (opts.size() > 1 ? "engine, 6 option sets each" : "engine, ready-driven") : "direct", todo.size(), bad, s);
    return bad ? 1 : 0;
}
