// report.h: a run's figures as the RTL's acceptance prints them (Snarky One's tracer_acceptance_test, `report`): the
// rows, the instructions, the cycles from START to the rows' end a row and an instruction, their split into the rows'
// schedules (17.9.7: 4 cycles, 6 for an M row), the three stall counters and the rest, the drain, the caches' and the
// jump LRU's hit rates, misses and probes, the write-backs; and the simulator's speed, a time and a rate (which
// run_tests.sh's mask() masks).
#pragma once

#include <cstdint>
#include <string>

#include "units/tracer.h"

namespace tracer_block {

struct Figures {
    uint64_t windows = 0, rows = 0, padding_rows = 0, instructions = 0, rows_m = 0, loads = 0, stores = 0, posts = 0, sets_pc = 0;
    uint64_t rows_cycles = 0, drain_cycles = 0;
    uint64_t padding_window_rows = 0, padding_window_cycles = 0;   // the windows that are all padding
    uint64_t counters[units::tracer::Tracer::STAT_N] = {};
    uint64_t fetch_lookups = 0, data_lookups = 0, jump_lookups = 0, map_lines = 0, write_backs = 0, take_backs = 0, packets = 0;
    uint64_t waits_on_core = 0;       // the simulator's count of the stall cycles in no counter (17.9.8)
    int64_t model_cycles = 0;
    double seconds = 0;
};

// fill: "L", "L,M" (the read latencies of the guest's file and the map's) or "model".
void report(const std::string& name, uint32_t compression, const std::string& fill, const Figures& f);

}  // namespace tracer_block
