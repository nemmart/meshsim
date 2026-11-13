// golden_run.h: the golden-trace harness (sim/tests/TRACE-FORMAT.md, DECISIONS D3 and D12). For one run of
// tests/golden/RUNS: read its golden trace, load its images and the boot ROM, check the golden's header against our own
// configuration (refusing on any difference, named), run the controller, write its trace and compare line for line.
//
// Two drivers produce the trace:
//   direct  a small loop over the plain logic (rv32.h, regblock.h) in ref's order, no framework: a test tool, kept as a
//           regression check (it tells a unit bug from an engine bug), not a second simulator;
//   engine  the three units of units/controller.h on the framework (once the engine lands).
// Paths are relative to sim/ (make runs there).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "units/controller.h"
#include "units/regblock.h"
#include "units/rv32.h"
#include "units/trace_log.h"

namespace test {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

struct RunSpec {
    std::string name;       // NAME: tests/golden/NAME.trace
    unsigned tile = 0;
    std::string dir;        // the images' directory
    std::string image;      // IMAGE: DIR/IMAGE.code.bin, .data.bin, .sym
    std::string schedule;   // sim/fw/m1/SCHEDULE, or empty
};
std::vector<RunSpec> read_runs(const char* runs_file = "tests/golden/RUNS");
const RunSpec* find_run(const std::vector<RunSpec>& runs, const std::string& name);

// A golden trace, parsed.
struct Golden {
    std::vector<std::string> lines;            // the whole file
    std::string program, image_line, bootrom_sha;
    unsigned tile = 0;
    KeyValues config;                          // in file order
    std::vector<std::string> tb;
    std::vector<units::BenchEvent> events;     // the replayed lines, as the bench's actions (in cycle order)
    uint64_t end_cycle = 0;
    size_t body_lines = 0;
};
bool read_golden(const std::string& path, Golden& g, std::string& err);

// What a run loads, and what it records in its header.
struct Images {
    std::vector<uint32_t> code, data, bootrom;
    uint32_t data_at = 0x10, entry = 0x01000000;
    std::string code_sha, data_sha, bootrom_sha;   // data_sha "none" without a data file
    std::vector<std::string> tb;                   // the schedule's steps (informative)
};
bool load_images(const RunSpec& r, Images& im, std::string& err);

// Our configuration, and the header we write (D12's keys, in TRACE-FORMAT's order).
units::TileConfig tile_config(unsigned tile);
KeyValues config_keys(const units::TileConfig& c);
std::vector<std::string> header_lines(const RunSpec& r, const Images& im, const units::TileConfig& c);
// D12: empty if the golden's header equals ours, else what differs, named, with both values.
std::string check_header(const Golden& g, const std::vector<std::string>& ours);

// The tile's plain state (the memories, the core, the block, the trace), built from a run and its configuration.
struct Tile {
    std::vector<uint32_t> dmem, imem;
    units::Rv32 core;
    units::RegBlockState block;
    units::TraceBuffer trace;
    Tile(const Images& im, const units::TileConfig& c, uint32_t trace_capacity);
};
// The closing block (TRACE-FORMAT) of a core and its data memory, the run ended at `end_cycle`.
std::vector<std::string> closing_lines(const units::Rv32& core, const uint32_t* dmem, uint64_t end_cycle);

// The direct driver: runs `t` to its stop, at most to the golden's last cycle; returns the whole trace.
std::vector<std::string> run_direct(Tile& t, const Golden& g, const std::vector<std::string>& header);

// The engine driver: the three units of units/controller.h on the framework, run to the golden's last cycle (the core
// stops itself before, and then nothing changes). `opt` selects the debug checks (shuffle, order, step-all).
struct EngineRun {
    std::vector<std::string> lines;   // the whole trace
    uint64_t digest = 0;              // Sim::digest() at the end
    std::string sleepers;             // Sim::sleepers() at the end
    uint64_t steps[3] = {};           // steps of the core, the block, the bench
    uint64_t instructions = 0;        // retired by the core
    double seconds = 0;               // the run alone (Sim::run), not the build or the formatting
};
EngineRun run_engine(Tile& t, const Golden& g, const std::vector<std::string>& header, const sim::Options& opt);
// Its two halves: build the tile's machine; run a built machine to the golden's last cycle and collect the trace.
sim::Sim build_engine(Tile& t, const Golden& g, const sim::Options& opt);
// `alloc_guard`: run under the allocation hook (always, except to show the missed-wake detector's message: its report
// allocates in the loop, NOTES "For the owner: the missed-wake report allocates").
EngineRun run_built(sim::Sim& s, Tile& t, const Golden& g, const std::vector<std::string>& header, bool alloc_guard = true);

// The debug-check option sets: ready-driven (type-major), shuffle seeds 1-3, instance-major, step-all.
std::vector<sim::Options> debug_check_options();

// The runs in a mixed order (k * 7 mod the count: a prime count of 67 visits every run), so each round of a threads test
// mixes riscv-tests and the M1 programs.
std::vector<RunSpec> mixed_order(const std::vector<RunSpec>& runs);
// M1's threads test: the runs in rounds of n, each round n controllers on n threads (the run_independent, thread k pinned
// to cpus[k % size] when cpus is given), each running a different golden, under each option set of `opts` (default: the
// six of debug_check_options()); every trace must equal its golden and every digest the same golden's run alone,
// ready-driven. Empty if all is well, else the differences.
std::string check_parallel(const std::vector<RunSpec>& runs, unsigned n, const std::vector<int>& cpus = {},
                           const std::vector<sim::Options>& opts = debug_check_options());

// Line-for-line comparison: empty if equal, else the first difference with context.
std::string compare(const std::vector<std::string>& ours, const std::vector<std::string>& golden);

// One golden on the engine under each option set in `opts`: every trace must equal the golden, and every digest the
// first's (opts[0] the ready-driven run). Where the engine's trace differs from the golden, the direct driver's is
// compared too, to say whether the unit or the engine is at fault. Empty if all is well.
std::string check_engine(const RunSpec& r, const std::vector<sim::Options>& opts);
// The debug-check option sets: ready-driven (type-major), shuffle seeds 1-3, instance-major, step-all.
// A first speed figure: the golden run `reps` times on the engine (ready-driven), the runs alone timed.
struct BenchResult { double seconds = 0; uint64_t instructions = 0, cycles = 0, steps = 0; };
BenchResult bench_engine(const RunSpec& r, unsigned reps, bool trace = true);


// One golden, start to finish, with the direct driver: empty if it matches, else why not (refusal or difference).
std::string check_direct(const RunSpec& r);

}  // namespace test
