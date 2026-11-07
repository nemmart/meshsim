// golden_run.h: the golden-trace harness (sim/tests/TRACE-FORMAT.md, DECISIONS D3 and D12). For one run of
// tests/golden/RUNS: read its golden trace, load its images and the boot ROM, check the golden's header against our own
// configuration (refusing on any difference, named), run the controller, write its trace and compare line for line.
//
// Two drivers produce the trace:
//   direct  a small loop over the plain logic (rv32.h, regblock.h) in ref's order, no framework: a test tool, kept as a
//           regression check (it tells a unit bug from an engine bug), not a second simulator;
//   engine  the three units of units/controller.h on the framework (once W3's engine lands).
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
    std::string schedule;   // sim/fw/SCHEDULE, or empty
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

// Line-for-line comparison: empty if equal, else the first difference with context.
std::string compare(const std::vector<std::string>& ours, const std::vector<std::string>& golden);

// One golden, start to finish, with the direct driver: empty if it matches, else why not (refusal or difference).
std::string check_direct(const RunSpec& r);

}  // namespace test
