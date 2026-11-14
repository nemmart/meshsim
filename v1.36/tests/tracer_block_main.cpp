// tracer_block_main.cpp: the Tracer alone on a segment of a real workload (tracer_block.h's bench), for its cycles
// against the RTL's (Snarky One's tracer_acceptance_test, which this follows):
//   tracer_block --inputs DIR --program NAME [--segment S] [--windows N] [--compression C] [--fill L[,M] | --fill model]
//                [--ref FILE] [--rows DIR] [--quiet]
// DIR is the inputs' directory (~/meshsim-inputs: w26-jumpmap/NAME, v1261/NAME). --fill: the fill port's read latency
// in cycles, the guest's file's and the map's (the RTL bench's memory system; 1 by default), or `model`, the
// simulator's own memory system (17.9.14). --ref: the RTL's figures, a `windows-*.txt` of its instrumented acceptance
// (one line a run): every run's cycles and counters are compared with it, and a difference fails. --rows: a directory
// of the RTL's `rows-NAME-sS-rR-c0-lL.txt`: each run with a file has every row's start and completion cycle compared.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "chain.h"
#include "tracer_block.h"
#include "units/mem_system.h"

namespace {

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
void set_cell(FileStore& fs, FileStore::File f, uint32_t cell, uint64_t value) {
    FileStore::Walk w;
    fs.walk(f.root, f.depth, int64_t(cell) / 16, w);
    if (w.data_line < 0) { std::fprintf(stderr, "tracer_block: an update of a cell with no page\n"); std::exit(1); }
    uint64_t line[16];
    fs.read_line(w.data_line, line);
    line[cell % 16] = value;
    fs.write_line(w.data_line, line);
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

}  // namespace

int main(int argc, char** argv) {
    std::string inputs = std::getenv("MESHSIM_INPUTS") ? std::getenv("MESHSIM_INPUTS") : "", program, ref, rows_dir, fill = "1";
    unsigned segment = 0, windows = 64, compression = 0;
    bool quiet = false;
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
        else { std::fprintf(stderr, "tracer_block: unknown option %s\n", a.c_str()); return 2; }
    }
    if (inputs.empty() || program.empty()) { std::fprintf(stderr, "tracer_block: --inputs DIR --program NAME\n"); return 2; }
    int lat[2] = {0, 0};
    if (fill != "model") {
        lat[0] = std::atoi(fill.c_str());
        const size_t c = fill.find(',');
        lat[1] = c == std::string::npos ? lat[0] : std::atoi(fill.c_str() + c + 1);
        if (lat[0] < 1 || lat[1] < 1) { std::fprintf(stderr, "tracer_block: --fill %s\n", fill.c_str()); return 2; }
    }

    // ---- the inputs: the image and the map, the segments, the tables
    std::string err;
    FileStore store;
    chain::W26 w;
    const std::string wdir = inputs + "/w26-jumpmap/" + program;
    if (!chain::load_w26(wdir, store, w, err)) { std::fprintf(stderr, "tracer_block: %s\n", err.c_str()); return 1; }
    std::vector<chain::Seg> segs;
    if (!chain::read_segments(wdir + "/segments.seg", segs, err)) { std::fprintf(stderr, "tracer_block: %s\n", err.c_str()); return 1; }
    if (segment >= segs.size()) { std::fprintf(stderr, "tracer_block: segment %u of %zu\n", segment, segs.size()); return 1; }
    const ReplayTables tables = ReplayTables::load(inputs + "/v1261/" + program);
    // the guest's file at the segment's start: the update lists of the segments before it; its pages owned, the ones
    // the segment writes
    for (unsigned s = 0; s < segment; s++) {
        store.set_ownership(w.img, segs[s].written_pages());
        for (const auto& u : segs[s].updates) set_cell(store, w.img, u.first, u.second);
    }
    const chain::Seg& sg = segs[segment];
    store.set_ownership(w.img, sg.written_pages());

    MemSystem ms(&store);
    ms.map_root_for_stats = w.map.root;
    Tracer tr(66);
    tr.mem = &ms;
    tr.cold_mem_on_state_write = true;
    tr.fill_latency[0] = lat[0]; tr.fill_latency[1] = lat[1];
    Rows rows;
    tr.on_row = on_row; tr.on_row_ctx = &rows;

    std::vector<RefRun> refs;
    if (!ref.empty() && !load_ref(ref, refs)) { std::fprintf(stderr, "tracer_block: cannot read %s\n", ref.c_str()); return 1; }

    // ---- the record (17.9.2): the state word, the advice field, the registers, the files word
    uint64_t rec[2 * Tracer::REC_WORDS] = {};
    rec[0] = sg.state[0];
    rec[1] = (sg.state[1] & 0xFFFFFFFFull) | (sg.state[2] & 0xFFFF) << 32;
    rec[3] = uint64_t(sg.advice.size()) << 38;
    for (int r = 0; r < 128; r++) rec[4 + r] = sg.state[3 + r];
    Tracer::files_word(w.img.root, w.img.depth, w.map.root, w.map.depth, w.log2k, w.lowest, rec + 2 * Tracer::REC_FILES);

    tracer_block::Bench bench(tr, tables);
    bench.setup(uint32_t(w.hash_p1), uint32_t(w.hash_p3), uint32_t(w.hash_k));
    bench.segment(rec, sg.advice, 65536u | compression << 26);

    int64_t total_rows_cycles = 0, total_drain = 0, differs = 0, row_differs = 0, runs_equal = 0;
    uint32_t last[Tracer::STAT_N] = {};
    for (unsigned run = 0; run < windows; run++) {
        rows.start.clear(); rows.done.clear();
        tracer_block::RunOut o;
        bench.run(o);
        if (!o.done) {
            std::printf("run %u: not done: ERRORS 0x%x after %u rows (cycle %lld)\n", run, o.errors, o.rows_run, (long long)bench.cycle);
            for (int64_t k = 0; k < tr.n_errors_logged && k < Tracer::FACTS; k++)
                std::printf("  %s at %lld (0x%llx, 0x%llx)\n", Tracer::error_name(tr.error_log[k].code), (long long)tr.error_log[k].t,
                            (unsigned long long)tr.error_log[k].a, (unsigned long long)tr.error_log[k].b);
            return 1;
        }
        const int64_t first = rows.start.empty() ? -1 : rows.start.front(), lastd = rows.done.empty() ? -1 : rows.done.back();
        total_rows_cycles += o.rows_cycles; total_drain += o.drain_cycles;
        if (!quiet)
            std::printf("run %2u: START %lld, first row +%lld, last row done +%lld, rows' end %lld cycles, drain %lld; stalls fetch %u data %u map %u "
                        "output %u; misses i %u d %u, probes %u; posts %zu\n",
                        run, (long long)o.start_store, (long long)(first - o.start_store), (long long)(lastd - o.start_store),
                        (long long)o.rows_cycles, (long long)o.drain_cycles, o.stat[1] - last[1], o.stat[2] - last[2], o.stat[3] - last[3],
                        o.stat[4] - last[4], o.stat[5] - last[5], o.stat[6] - last[6], o.stat[7] - last[7], o.post_opcode.size());
        for (int k = 0; k < Tracer::STAT_N; k++) last[k] = o.stat[k];
        if (run < refs.size()) {   // the RTL's: columns 8-16 and 25-32 of its windows file
            const RefRun& r = refs[run];
            const int64_t ours[] = {o.start_store, first, lastd, o.state_ready_load, o.done_load, o.rows_cycles, o.drain_cycles,
                                    o.stat[0], o.stat[1], o.stat[2], o.stat[3], o.stat[4], o.stat[5], o.stat[6], o.stat[7]};
            const int col[] = {8, 10, 11, 13, 14, 15, 16, 25, 26, 27, 28, 29, 30, 31, 32};
            const char* name[] = {"START's cycle", "first row's start", "last row's done", "state_ready's load", "done's load", "rows' cycles",
                                  "drain cycles", "STAT_CYCLES", "STAT_STALL_FETCH", "STAT_STALL_DATA", "STAT_STALL_MAP", "STAT_STALL_OUTPUT",
                                  "STAT_MISS_I", "STAT_MISS_D", "STAT_PROBES"};
            bool eq = true;
            for (int k = 0; k < 15; k++)
                if (ours[k] != r.v[col[k]]) {
                    eq = false;
                    if (differs < 40) std::printf("  DIFFERS run %u: %s %lld, the RTL's %lld\n", run, name[k], (long long)ours[k], (long long)r.v[col[k]]);
                    differs++;
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
    const double per_row = windows ? double(total_rows_cycles) / (65536.0 * windows) : 0;
    std::printf("%s segment %u, %u runs, compression %u, fill %s: rows' cycles %lld (%.4f a row), drain %lld; STAT_CYCLES %u, stalls fetch %u "
                "data %u map %u output %u; misses i %u d %u, probes %u; redirects %lld, write-backs %lld, take-backs %lld\n",
                program.c_str(), segment, windows, compression, fill.c_str(), (long long)total_rows_cycles, per_row, (long long)total_drain,
                last[0], last[1], last[2], last[3], last[4], last[5], last[6], last[7], (long long)tr.redirects, (long long)tr.writebacks,
                (long long)tr.take_backs);
    if (!refs.empty()) std::printf("against the RTL: %lld of %u runs equal in every figure, %lld figures differ\n", (long long)runs_equal, windows, (long long)differs);
    if (!rows_dir.empty()) std::printf("rows against the RTL's: %lld differ\n", (long long)row_differs);
    return differs != 0 || row_differs != 0 ? 1 : 0;
}
