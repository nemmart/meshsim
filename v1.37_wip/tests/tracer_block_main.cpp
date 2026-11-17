// tracer_block_main.cpp: the Tracer alone (tracer_block.h's bench), as the RTL's acceptance runs it (Snarky One's
// tracer_acceptance_test, which this follows check for check), on a segment of a real workload or on a synthetic guest:
//   tracer_block --inputs DIR --program NAME [--segment S] [--windows N] [--compression C] [--fill L[,M] | --fill model | --fill cache]
//                [--ref FILE] [--rows DIR] [--quiet]
//   tracer_block --synthetic [SEED] [--quiet]
//
// A REAL SEGMENT. DIR is the inputs' directory (~/meshsim-inputs, read only: gold-v1.3/NAME, w26-jumpmap/NAME,
// v1261/NAME). --fill: the fill port's read latency in cycles, the guest's file's and the map's (the RTL bench's memory
// system; 1 by default), or `model`, the simulator's own memory system (17.9.14), or `cache`, the cache's controller,
// the AXI arbiter and the LPDDR model (units/cache_system.h; updater_wip, WORKER5).
//   Checked, every run, against the window's files (tests/tracerblock/gold.h): output queue 0's 68 words; output
//   queues 1-3 entry for entry through each stream's last value, their number and the length words; STATE_LENGTH,
//   ROWS_RUN, STATUS 0 and ERRORS 0 after done, no live event; each post's code value and row offset; STAT_MISS_I,
//   STAT_MISS_D and STAT_PROBES against ideal-LRU models over the window's rows, and the fill port's reads against
//   them; the cycles adding up (17.9.8). And the segment run whole: its last record against the next segment's frame
//   2, the advice field 0, and the guest's file against the update list, the cells of the lines the data cache still
//   holds excepted. What the RTL's test checks and this does not: the DMA's packets' headers (the DMA here is the
//   bench's own, with no packet) and the handshake monitors (the model has no handshake to break).
//   --ref: the RTL's figures, a `windows-*.txt` of its instrumented acceptance (tests/tracerblock/rtl/): every run's
//   cycles, counters and counts are compared with it. --rows: a directory of the RTL's `rows-NAME-sS-rR-c0-lL.txt`:
//   each run with a file has every row's start and completion cycle compared.
//   Printed: the report the RTL's test prints, and the simulator's speed.
//
// A SYNTHETIC GUEST (tests/tracerblock/guest.h): a small RV64IMAC program built in memory, run through the same bench
// with no file outside this folder but the universal tables, and checked against an interpreter of it written there.
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "chain.h"
#include "tracer_block.h"
#include "tracerblock/gold.h"
#include "tracerblock/guest.h"
#include "tracerblock/report.h"
#include "units/mem_system.h"
#include "units/cache_system.h"
#include "cache_figures.h"
#include <memory>

namespace {

using tracer_block::Differences;
using tracer_block::Figures;
using units::tracer::FileStore;
using units::tracer::MemSystem;
using units::tracer::ReplayTables;
using units::tracer::Tracer;

struct Rows { std::vector<int64_t> start, done; };
void on_row(void* ctx, int64_t t, bool start) {
    Rows& r = *static_cast<Rows*>(ctx);
    (start ? r.start : r.done).push_back(t);
}

// A doubleword of the guest's file, written in place (a segment's update list, applied before a later segment).
void set_cell(FileStore& fs, FileStore::File f, uint64_t cell, uint64_t value) {
    FileStore::Walk w;
    fs.walk(f.root, f.depth, int64_t(cell / 16), w);
    if (w.data_line < 0) { std::fprintf(stderr, "tracer_block: an update of a cell with no page\n"); std::exit(1); }
    uint64_t line[16];
    fs.read_line(w.data_line, line);
    line[cell % 16] = value;
    fs.write_line(w.data_line, line);
}
uint64_t get_cell(const FileStore& fs, FileStore::File f, uint64_t cell) {
    FileStore::Walk w;
    fs.walk(f.root, f.depth, int64_t(cell / 16), w);
    if (w.data_line < 0) return 0;
    uint64_t line[16];
    fs.read_line(w.data_line, line);
    return line[cell % 16];
}

struct RefRun { int64_t v[37]; };   // v[k]: column k of the file, from 1
bool load_ref(const std::string& path, std::vector<RefRun>& out) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[2048];
    while (std::fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        RefRun r{};
        char* p = std::strchr(line, ' ');   // after the workload's name
        for (int k = 2; k <= 36 && p; k++) r.v[k] = std::strtoll(p, &p, 10);
        out.push_back(r);
    }
    std::fclose(f);
    return true;
}

int real_segment(const std::string& inputs, const std::string& program, unsigned segment, unsigned windows, unsigned compression,
                 const std::string& fill, const std::string& ref, const std::string& rows_dir, bool quiet) {
    const auto began = std::chrono::steady_clock::now();
    int lat[2] = {0, 0};
    if (fill != "model" && fill != "cache") {
        lat[0] = std::atoi(fill.c_str());
        const size_t c = fill.find(',');
        lat[1] = c == std::string::npos ? lat[0] : std::atoi(fill.c_str() + c + 1);
        if (lat[0] < 1 || lat[1] < 1) { std::fprintf(stderr, "tracer_block: --fill %s\n", fill.c_str()); return 2; }
    }
    if (windows < 1 || windows > unsigned(tracer_block::SEGMENT_WINDOWS)) { std::fprintf(stderr, "tracer_block: --windows 1 to 64\n"); return 2; }

    // ---- the inputs: the image and the map, the segments (the pipeline's file, and gold's v1.3), the tables
    std::string err;
    FileStore store;
    chain::W26 w;
    const std::string wdir = inputs + "/w26-jumpmap/" + program, vdir = inputs + "/v1261/" + program;
    if (!chain::load_w26(wdir, store, w, err)) { std::fprintf(stderr, "tracer_block: %s\n", err.c_str()); return 1; }
    std::vector<chain::Seg> segs;
    if (!chain::read_segments(wdir + "/segments.seg", segs, err)) { std::fprintf(stderr, "tracer_block: %s\n", err.c_str()); return 1; }
    tracer_block::Gold gold;
    if (!tracer_block::read_gold(inputs + "/gold-v1.3/" + program + "/segments.seg", gold, err)) { std::fprintf(stderr, "tracer_block: %s\n", err.c_str()); return 1; }
    if (segment >= segs.size() || segment >= gold.segments.size()) { std::fprintf(stderr, "tracer_block: segment %u of %zu\n", segment, segs.size()); return 1; }
    const ReplayTables tables = ReplayTables::load(vdir);
    Differences diffs;

    // The files word (17.9.2): gold's log2 K and io_size, with the files' roots and depths.
    const tracer_block::GoldSegment& gs = gold.segments[segment];
    uint64_t files[2];
    Tracer::files_word(w.img.root, w.img.depth, w.map.root, w.map.depth, int(gold.log_k), int64_t(gold.lowest), files);
    if (gs.record[2 * 66] != 0 || gs.record[2 * 66 + 1] != (uint64_t(gold.log_k) << 2 | (0x80000000ull - gold.lowest) << 8))
        diffs.add("frame 2's word 66 against frame 7");
    // The two segment files, the pipeline's and gold's, say the same of the segment: the start, the advice, the
    // update list, the map's constants.
    {
        const chain::Seg& sg = segs[segment];
        bool same = int(gold.log_k) == w.log2k && int64_t(gold.lowest) == w.lowest && int(gold.map_k) == w.hash_k &&
                    gold.map_p1 == uint32_t(w.hash_p1) && gold.map_p3 == uint32_t(w.hash_p3) && gs.advice == sg.advice &&
                    gs.updates.size() == sg.updates.size();
        same = same && gs.record[0] == sg.state[0] && gs.record[1] == ((sg.state[1] & 0xFFFFFFFFull) | (sg.state[2] & 0xFFFF) << 32) &&
               gs.record[2] == 0 && gs.record[3] == uint64_t(sg.advice.size()) << 38;
        for (int r = 0; same && r < 128; r++) same = gs.record[4 + r] == sg.state[3 + r];
        for (size_t k = 0; same && k < gs.updates.size(); k++) same = gs.updates[k].first == sg.updates[k].first && gs.updates[k].second == sg.updates[k].second;
        if (!same) diffs.add("gold-v1.3's segment %u against w26-jumpmap's: the start, the advice, the update list or the constants", segment);
        // seg/NNN.pages against the update list's pages
        std::set<int64_t> listed;
        if (segment < w.written.size()) listed.insert(w.written[segment].begin(), w.written[segment].end());
        const std::vector<int64_t> pages = sg.written_pages();
        if (listed != std::set<int64_t>(pages.begin(), pages.end())) diffs.add("seg/%03u.pages against the update list's pages", segment);
    }
    // the guest's file at the segment's start: the update lists of the segments before it; its pages owned, the ones
    // the segment writes
    for (unsigned s = 0; s < segment; s++) {
        store.set_ownership(w.img, segs[s].written_pages());
        for (const auto& u : gold.segments[s].updates) set_cell(store, w.img, u.first, u.second);
    }
    store.set_ownership(w.img, segs[segment].written_pages());

    MemSystem ms(&store);
    ms.map_root_for_stats = w.map.root;
    Tracer tr(66);
    tr.mem = &ms;
    tr.cold_mem_on_state_write = true;
    tr.fill_latency[0] = lat[0]; tr.fill_latency[1] = lat[1];
    std::unique_ptr<units::CacheSystem> cs;   // --fill cache (updater_wip, WORKER5): the cache's controller on the fill port
    if (fill == "cache") { cs = std::make_unique<units::CacheSystem>(&store); cs->ctl.cfg.map_root_for_stats = w.map.root; cs->attach(tr); }
    auto port_reads = [&]() { return cs ? int64_t(cs->ctl.fill_reads) : ms.reads; };
    auto port_writes = [&]() { return cs ? int64_t(cs->ctl.fill_writes) : ms.writes; };
    Rows rows;
    tr.on_row = on_row; tr.on_row_ctx = &rows;

    std::vector<RefRun> refs;
    if (!ref.empty() && !load_ref(ref, refs)) { std::fprintf(stderr, "tracer_block: cannot read %s\n", ref.c_str()); return 1; }

    // ---- the record (17.9.2): gold's words 0-67, word 66 the files word
    uint64_t rec[2 * Tracer::REC_WORDS];
    std::memcpy(rec, gs.record, sizeof rec);
    rec[2 * Tracer::REC_FILES] = files[0]; rec[2 * Tracer::REC_FILES + 1] = files[1];

    tracer_block::Bench bench(tr, tables);
    bench.setup(gold.map_p1, gold.map_p3, gold.map_k);
    bench.segment(rec, gs.advice, 65536u | compression << 26);

    tracer_block::Account account;
    Figures fig;
    int64_t ref_differs = 0, row_differs = 0, runs_equal = 0, entries_compared = 0, posts_compared = 0;
    uint32_t last[Tracer::STAT_N] = {};
    int64_t last_waits = 0;
    tracer_block::RunOut o;
    for (unsigned run = 0; run < windows; run++) {
        char context[96];
        std::snprintf(context, sizeof context, "%s window %u (segment %u, run %u), compression %u", program.c_str(),
                      tracer_block::SEGMENT_WINDOWS * segment + run, segment, run, compression);
        const tracer_block::Account before = account;
        tracer_block::Expected expected;
        account.window(vdir, unsigned(tracer_block::SEGMENT_WINDOWS) * segment + run, compression, tables, gold, segment, files, expected, diffs);
        rows.start.clear(); rows.done.clear();
        bench.run(o);
        if (!o.done) {
            std::printf("FAIL %s: not done: ERRORS 0x%x after %u rows (cycle %lld)\n", context, o.errors, o.rows_run, (long long)bench.cycle);
            for (int64_t k = 0; k < tr.n_errors_logged && k < Tracer::FACTS; k++)
                std::printf("  %s at %lld (0x%llx, 0x%llx)\n", Tracer::error_name(tr.error_log[k].code), (long long)tr.error_log[k].t,
                            (unsigned long long)tr.error_log[k].a, (unsigned long long)tr.error_log[k].b);
            return 1;
        }
        // ---- the output against the window's files
        entries_compared += tracer_block::compare(o, expected, context, diffs);
        posts_compared += int64_t(expected.posts.size());
        uint32_t state_length = 0;
        tr.word(bench.cycle, Tracer::W_STATE_LENGTH, state_length);
        if (state_length != uint32_t(Tracer::REC_WORDS)) diffs.add("%s: STATE_LENGTH %u", context, state_length);
        if (o.status != 0) diffs.add("%s: STATUS 0x%x after done", context, o.status);
        if (o.errors != 0) diffs.add("%s: ERRORS 0x%x after done", context, o.errors);
        if ((o.events_peek & (0xFu << 5)) != 0) diffs.add("%s: a live event after done: EVENTS_PEEK 0x%x", context, o.events_peek);
        if (o.rows_run != uint32_t(tracer_block::WINDOW_ROWS)) diffs.add("%s: ROWS_RUN %u", context, o.rows_run);
        // ---- the counters that are exact (17.9.7), so far in the segment, and the fill port's reads
        if (o.stat[Tracer::ST_MISS_I] != account.instruction_cache.misses)
            diffs.add("%s: STAT_MISS_I %u, the model's %llu", context, o.stat[Tracer::ST_MISS_I], (unsigned long long)account.instruction_cache.misses);
        if (o.stat[Tracer::ST_MISS_D] != account.data_cache.misses)
            diffs.add("%s: STAT_MISS_D %u, the model's %llu", context, o.stat[Tracer::ST_MISS_D], (unsigned long long)account.data_cache.misses);
        if (o.stat[Tracer::ST_PROBES] != account.jump_lru.misses)
            diffs.add("%s: STAT_PROBES %u, the model's %llu", context, o.stat[Tracer::ST_PROBES], (unsigned long long)account.jump_lru.misses);
        // (a data miss on the line in the write-back buffer takes it back with no read, 17.9.5: none at the RTL bench's
        // latencies, where a write is answered the next cycle; the simulator's count of them otherwise)
        const uint64_t expected_reads = account.map_lines + account.instruction_cache.misses + account.data_cache.misses - uint64_t(tr.take_backs);
        if (uint64_t(port_reads()) != expected_reads)
            diffs.add("%s: the fill port's reads %lld, the models' %llu (the map's %llu and the caches' misses, less %lld lines taken back)", context, (long long)port_reads(),
                      (unsigned long long)expected_reads, (unsigned long long)account.map_lines, (long long)tr.take_backs);
        // ---- the cycles add up (17.9.8): the rows' schedules, the three stall counters, the waits on the core, and
        // the measure's 3 cycles a run (START's own, and the two to the load that shows state_ready)
        const int64_t m_rows = int64_t(account.rows_m - before.rows_m);
        const int64_t schedule = 4 * (tracer_block::WINDOW_ROWS - m_rows) + 6 * m_rows;
        const int64_t stalls = int64_t(o.stat[1] - last[1]) + int64_t(o.stat[2] - last[2]) + int64_t(o.stat[3] - last[3]);
        const int64_t waits_now = tr.stalls[Tracer::S_ADVICE] + tr.stalls[Tracer::S_EXP], waits = waits_now - last_waits;
        const int64_t rest = o.rows_cycles - schedule - stalls;
        if (rest != 3 + waits)
            diffs.add("%s: the cycles do not add up: %lld from START to the rows' end, the schedules %lld, the stall counters %lld, the waits "
                      "on the core %lld", context, (long long)o.rows_cycles, (long long)schedule, (long long)stalls, (long long)waits);
        last_waits = waits_now;

        const int64_t first = rows.start.empty() ? -1 : rows.start.front(), lastd = rows.done.empty() ? -1 : rows.done.back();
        fig.windows++;
        fig.rows_cycles += uint64_t(o.rows_cycles); fig.drain_cycles += uint64_t(o.drain_cycles);
        fig.waits_on_core += uint64_t(waits);
        for (int q = 0; q < 4; q++) fig.packets += o.queue[q].size() / 2;
        if (account.padding_rows - before.padding_rows == uint64_t(tracer_block::WINDOW_ROWS)) {
            fig.padding_window_rows += uint64_t(tracer_block::WINDOW_ROWS);
            fig.padding_window_cycles += uint64_t(o.rows_cycles);
        }
        if (!quiet)
            std::printf("run %2u: START %lld, first row +%lld, last row done +%lld, rows' end %lld cycles, drain %lld; stalls fetch %u data %u map %u "
                        "output %u; misses i %u d %u, probes %u; posts %zu\n",
                        run, (long long)o.start_store, (long long)(first - o.start_store), (long long)(lastd - o.start_store),
                        (long long)o.rows_cycles, (long long)o.drain_cycles, o.stat[1] - last[1], o.stat[2] - last[2], o.stat[3] - last[3],
                        o.stat[4] - last[4], o.stat[5] - last[5], o.stat[6] - last[6], o.stat[7] - last[7], o.post_opcode.size());
        for (int k = 0; k < Tracer::STAT_N; k++) last[k] = o.stat[k];
        if (run < refs.size()) {
            // the RTL's: columns 8-16 and 25-32 of its windows file, its cycles and counters; 17-24, the window's
            // counts and the cycles' split; 33-35, the fill port's requests
            const RefRun& r = refs[run];
            const int64_t ours[] = {o.start_store, first, lastd, o.state_ready_load, o.done_load, o.rows_cycles, o.drain_cycles,
                                    o.stat[0], o.stat[1], o.stat[2], o.stat[3], o.stat[4], o.stat[5], o.stat[6], o.stat[7],
                                    int64_t(account.instructions - before.instructions), int64_t(account.padding_rows - before.padding_rows), m_rows,
                                    int64_t(account.loads - before.loads), int64_t(account.stores - before.stores), int64_t(o.post_opcode.size()),
                                    schedule, rest, port_reads() - tr.probe_reads, tr.probe_reads, port_writes()};
            const int col[] = {8, 10, 11, 13, 14, 15, 16, 25, 26, 27, 28, 29, 30, 31, 32, 17, 18, 19, 20, 21, 22, 23, 24, 33, 34, 35};
            const char* name[] = {"START's cycle", "first row's start", "last row's done", "state_ready's load", "done's load", "rows' cycles",
                                  "drain cycles", "STAT_CYCLES", "STAT_STALL_FETCH", "STAT_STALL_DATA", "STAT_STALL_MAP", "STAT_STALL_OUTPUT",
                                  "STAT_MISS_I", "STAT_MISS_D", "STAT_PROBES", "instructions", "padding rows", "M rows", "loads", "stores",
                                  "posts", "the rows' schedules", "cycles in no counter", "reads of the guest's file", "reads of the map",
                                  "write-backs"};
            bool eq = true;
            for (int k = 0; k < 26; k++)
                if (ours[k] != r.v[col[k]]) {
                    eq = false;
                    if (ref_differs < 40) std::printf("  DIFFERS run %u: %s %lld, the RTL's %lld\n", run, name[k], (long long)ours[k], (long long)r.v[col[k]]);
                    ref_differs++;
                }
            if (eq) runs_equal++;
        }
        if (!rows_dir.empty() && compression == 0) {   // the RTL's rows files are its runs at compression 0
            char path[1024];
            std::snprintf(path, sizeof path, "%s/rows-%s-s%u-r%u-c0-l%d%s.txt", rows_dir.c_str(), program.c_str(), segment, run, lat[0],
                          lat[0] == lat[1] ? "" : ("-" + std::to_string(lat[1])).c_str());
            if (FILE* f = std::fopen(path, "r")) {
                long long k, s, d;
                int shown = 0;
                while (std::fscanf(f, "%lld %lld %lld", &k, &s, &d) == 3) {
                    const int64_t os = size_t(k) < rows.start.size() ? rows.start[size_t(k)] : -1, od = size_t(k) < rows.done.size() ? rows.done[size_t(k)] : -1;
                    if (os != s || od != d) {
                        if (shown++ < 3) std::printf("  ROW DIFFERS run %u row %lld: start %lld done %lld, the RTL's %lld %lld\n", run, k, (long long)os, (long long)od, s, d);
                        row_differs++;
                    }
                }
                std::fclose(f);
            }
        }
    }

    // ---- the segment's end, run whole: its last record against the next segment's start, the advice all taken, and
    // the guest's file against the update list
    int64_t cells_checked = -1;
    bool next_checked = false;
    if (windows == unsigned(tracer_block::SEGMENT_WINDOWS)) {
        const std::vector<uint64_t>& record = o.queue[0];
        if (record.size() == 2 * size_t(Tracer::REC_WORDS)) {
            if ((record[3] >> 38) != 0) diffs.add("%s segment %u: the advice field at the segment's end is %llu", program.c_str(), segment, (unsigned long long)(record[3] >> 38));
            if (segment + 1 < gold.segments.size()) {
                next_checked = true;
                const uint64_t* next = gold.segments[segment + 1].record;
                for (size_t word = 0; word < 66; word++)
                    if (word != 1 && (record[2 * word] != next[2 * word] || record[2 * word + 1] != next[2 * word + 1]))
                        diffs.add("%s segment %u: record word %zu against the next segment's frame 2", program.c_str(), segment, word);
            }
        }
        // the write-back buffer's line, written; then every cell of the update list, but those of the lines the data
        // cache holds
        for (int k = 0; k < 16; k++) bench.idle_cycle();
        if (tr.errors(bench.cycle) != 0) diffs.add("%s segment %u: ERRORS 0x%x after the last write-back", program.c_str(), segment, unsigned(tr.errors(bench.cycle)));
        if (cs) {   // the shared cache's dirty lines into LPDDR (the store) before the cells are read from it
            cachefig::print(program.c_str(), *cs);
            if (cachefig::faults(program.c_str(), *cs)) diffs.add("%s segment %u: the cache's faults", program.c_str(), segment);
            cs->ctl.write_dirty_to(store);
        }
        const std::set<uint64_t> held(account.data_cache.entries.begin(), account.data_cache.entries.end());
        cells_checked = 0;
        for (const auto& u : gs.updates) {
            if (held.count(u.first >> 4)) continue;
            const uint64_t got = get_cell(store, w.img, u.first);
            if (got != u.second)
                diffs.add("%s segment %u: the guest's file at cell 0x%llx is 0x%llx, the update list's 0x%llx", program.c_str(), segment,
                          (unsigned long long)u.first, (unsigned long long)got, (unsigned long long)u.second);
            cells_checked++;
        }
        if (cells_checked == 0 && !gs.updates.empty()) diffs.add("%s segment %u: no cell of the update list checked", program.c_str(), segment);
    }

    fig.rows = account.rows; fig.padding_rows = account.padding_rows; fig.instructions = account.instructions; fig.rows_m = account.rows_m;
    fig.loads = account.loads; fig.stores = account.stores; fig.posts = account.posts; fig.sets_pc = account.sets_pc;
    for (int k = 0; k < Tracer::STAT_N; k++) fig.counters[k] = last[k];
    fig.fetch_lookups = account.instruction_cache.lookups; fig.data_lookups = account.data_cache.lookups;
    fig.jump_lookups = account.jump_lru.lookups; fig.map_lines = account.map_lines;
    fig.write_backs = uint64_t(port_writes()); fig.take_backs = uint64_t(tr.take_backs);
    fig.model_cycles = bench.cycle;
    fig.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    tracer_block::report(program + " segment " + std::to_string(segment), compression, fill, fig);

    char end_text[160] = "the segment's end not checked (fewer than 64 runs)";
    if (cells_checked >= 0)
        std::snprintf(end_text, sizeof end_text, "the segment's end: the advice field 0, %s, %lld of %zu cells of the update list", next_checked
                      ? "the record against the next segment's start" : "no next segment", (long long)cells_checked, gs.updates.size());
    std::printf("against gold's files: %u runs: %u records of 68 words, %lld stream entries, %lld posts, the length words, ROWS_RUN, STATUS, "
                "ERRORS, the live events, STAT_MISS_I, STAT_MISS_D, STAT_PROBES, the fill port's reads, the cycles adding up; %s: %lld "
                "differences\n", windows, windows, (long long)entries_compared, (long long)posts_compared, end_text, (long long)diffs.count);
    if (!refs.empty()) std::printf("against the RTL: %lld of %u runs equal in every figure, %lld figures differ\n", (long long)runs_equal, windows, (long long)ref_differs);
    if (!rows_dir.empty()) std::printf("rows against the RTL's: %lld differ\n", (long long)row_differs);
    const bool ok = diffs.count == 0 && ref_differs == 0 && row_differs == 0;
    std::printf("%s %s segment %u, compression %u, fill %s\n", ok ? "PASS" : "FAIL", program.c_str(), segment, compression, fill.c_str());
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string inputs = std::getenv("MESHSIM_INPUTS") ? std::getenv("MESHSIM_INPUTS") : "", program, ref, rows_dir, fill = "1";
    unsigned segment = 0, windows = 64, compression = 0;
    bool quiet = false, synthetic = false;
    uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "tracer_block: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--inputs") inputs = val();
        else if (a == "--program") program = val();
        else if (a == "--segment") segment = unsigned(std::atoi(val()));
        else if (a == "--windows") windows = unsigned(std::atoi(val()));
        else if (a == "--compression") compression = unsigned(std::atoi(val()));
        else if (a == "--fill") fill = val();
        else if (a == "--ref") ref = val();
        else if (a == "--rows") rows_dir = val();
        else if (a == "--quiet") quiet = true;
        else if (a == "--synthetic") {
            synthetic = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') seed = std::strtoull(argv[++i], nullptr, 0);
        }
        else { std::fprintf(stderr, "tracer_block: unknown option %s\n", a.c_str()); return 2; }
    }
    if (synthetic) return tracer_block::synthetic(seed, quiet);
    if (inputs.empty() || program.empty()) { std::fprintf(stderr, "tracer_block: --inputs DIR --program NAME, or --synthetic [SEED]\n"); return 2; }
    return real_segment(inputs, program, segment, windows, compression, fill, ref, rows_dir, quiet);
}
