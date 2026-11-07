// golden_main.cpp: `golden [names...]`: runs the golden traces (all of tests/golden/RUNS, or those named) and reports each.
// Run from sim/. Exit status 1 if any differs or is refused.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "golden_run.h"

int main(int argc, char** argv) {
    auto runs = test::read_runs();
    if (runs.empty()) { fprintf(stderr, "golden: no runs (run from sim/: tests/golden/RUNS)\n"); return 2; }
    std::vector<const test::RunSpec*> todo;
    for (int i = 1; i < argc; i++) {
        const test::RunSpec* r = test::find_run(runs, argv[i]);
        if (!r) { fprintf(stderr, "golden: no run named %s\n", argv[i]); return 2; }
        todo.push_back(r);
    }
    if (todo.empty()) for (auto& r : runs) todo.push_back(&r);
    unsigned bad = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const test::RunSpec* r : todo) {
        std::string d = test::check_direct(*r);
        if (d.empty()) printf("SAME  %s\n", r->name.c_str());
        else { printf("DIFF  %s\n%s\n", r->name.c_str(), d.c_str()); bad++; }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("golden (direct): %zu runs, %u differ, %.3f s\n", todo.size(), bad, s);
    return bad ? 1 : 0;
}
