// report.cpp: the report (report.h).
#include "report.h"

#include <cinttypes>
#include <cstdio>

namespace tracer_block {

using units::tracer::Tracer;

void report(const std::string& name, uint32_t compression, const std::string& fill, const Figures& f) {
    auto per = [](double cycles, uint64_t count) { return count == 0 ? 0.0 : cycles / double(count); };
    auto rate = [](uint64_t misses, uint64_t lookups) { return lookups == 0 ? 0.0 : 100.0 * (1.0 - double(misses) / double(lookups)); };
    const uint64_t real_rows = f.rows - f.padding_rows;
    const uint64_t* c = f.counters;
    const uint64_t schedules = 4 * (f.rows - f.rows_m) + 6 * f.rows_m;
    const uint64_t stalls = c[Tracer::ST_STALL_FETCH] + c[Tracer::ST_STALL_DATA] + c[Tracer::ST_STALL_MAP];
    if (fill == "model") {
        std::printf("%s, compression %u, the fill port on the simulator's memory system (17.9.14):\n", name.c_str(), compression);
    } else {
        const size_t comma = fill.find(',');
        std::printf("%s, compression %u, a fill read's latency %s of the guest's file and %s of the map's:\n", name.c_str(), compression,
                    fill.substr(0, comma).c_str(), comma == std::string::npos ? fill.c_str() : fill.c_str() + comma + 1);
    }
    std::printf("  windows %" PRIu64 "; rows %" PRIu64 "; padding rows %" PRIu64 "; instructions %" PRIu64 " (%.4f rows an instruction); M rows %" PRIu64 "\n",
                f.windows, f.rows, f.padding_rows, f.instructions, per(double(real_rows), f.instructions), f.rows_m);
    // The rows that are not padding: the cycles less the padding rows', at the cycles a row of the windows that are all
    // padding (4 with none).
    const double padding_cycles = f.padding_window_rows == 0 ? 4.0 : per(double(f.padding_window_cycles), f.padding_window_rows);
    const double real_cycles = double(f.rows_cycles) - double(f.padding_rows) * padding_cycles;
    std::printf("  cycles, START to the rows' end: %" PRIu64 "; a row %.4f; a row that is not padding %.4f, an instruction %.4f; a row of the "
                "windows all padding %.4f (%" PRIu64 " rows)\n",
                f.rows_cycles, per(double(f.rows_cycles), f.rows), real_cycles / double(real_rows ? real_rows : 1),
                real_cycles / double(f.instructions ? f.instructions : 1), padding_cycles, f.padding_window_rows);
    std::printf("  the rows' schedules %" PRIu64 " (%.4f a row); STAT_STALL_FETCH %" PRIu64 " (%.4f); STAT_STALL_DATA %" PRIu64 " (%.4f); "
                "STAT_STALL_MAP %" PRIu64 " (%.4f); in no counter %" PRId64 " (the waits on the core %" PRIu64 ", and 3 a run of the measure's)\n",
                schedules, per(double(schedules), f.rows), c[Tracer::ST_STALL_FETCH], per(double(c[Tracer::ST_STALL_FETCH]), f.rows),
                c[Tracer::ST_STALL_DATA], per(double(c[Tracer::ST_STALL_DATA]), f.rows), c[Tracer::ST_STALL_MAP],
                per(double(c[Tracer::ST_STALL_MAP]), f.rows), int64_t(f.rows_cycles) - int64_t(schedules) - int64_t(stalls), f.waits_on_core);
    std::printf("  instruction cache: look-ups %" PRIu64 ", misses %" PRIu64 " (hits %.4f%%); data cache: memory rows %" PRIu64 " (%" PRIu64 " loads, %" PRIu64
                " stores), misses %" PRIu64 " (hits %.4f%%), write-backs %" PRIu64 ", lines taken back from the write-back buffer %" PRIu64 "\n",
                f.fetch_lookups, c[Tracer::ST_MISS_I], rate(c[Tracer::ST_MISS_I], f.fetch_lookups), f.data_lookups, f.loads, f.stores,
                c[Tracer::ST_MISS_D], rate(c[Tracer::ST_MISS_D], f.data_lookups), f.write_backs, f.take_backs);
    std::printf("  jump LRU: control transfers %" PRIu64 ", probes %" PRIu64 " (hits %.4f%%), map lines read %" PRIu64 " (%.4f a probe); core-fed "
                "expansions posted %" PRIu64 "; rows that set their successor's pc (a branch, JAL or JALR) %" PRIu64 "\n",
                f.jump_lookups, c[Tracer::ST_PROBES], rate(c[Tracer::ST_PROBES], f.jump_lookups), f.map_lines,
                per(double(f.map_lines), c[Tracer::ST_PROBES]), f.posts, f.sets_pc);
    std::printf("  the drain: %" PRIu64 " cycles, %.0f a window; packets %" PRIu64 "; STAT_STALL_OUTPUT %" PRIu64 "; STAT_CYCLES: %" PRIu64 "\n",
                f.drain_cycles, per(double(f.drain_cycles), f.windows), f.packets, c[Tracer::ST_STALL_OUTPUT], c[Tracer::ST_CYCLES]);
    std::printf("  the simulator: %" PRId64 " cycles in %.2f s, %.2f Mcycles/s\n", f.model_cycles, f.seconds,
                f.seconds > 0 ? double(f.model_cycles) / f.seconds / 1e6 : 0.0);
}

}  // namespace tracer_block
