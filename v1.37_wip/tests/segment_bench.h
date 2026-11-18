// segment_bench.h: the segment on the chip, as the RTL's chip tests run it (Snarky One's rtl/chip/test/src/
// chip_trace_test.rs, `run_chip`, with its bench.rs): a guest traced by the tracer tile's Tracer, window by window,
// each window's record and streams delivered to its compute tile, and every window replayed there, all run by meshsim's
// firmware, v1.35's upgraded to v1.36 (fw/README.md): C&C 1's program on the master tile (fw/cc/cc1.c), the tracer
// tile's (fw/tracer/tracer_ctl.c) and the replay controller on every compute tile (fw/replay/replay_ctl.c), both loaded
// by C&C 1 at their LOADs, and the memory tile's (fw/cc/memory.c).
//
// THE SEGMENT (`Segment`) is what the RTL's test builds before its run, from its models, which share no code with the
// design and none with the simulator: the guest's memory file and its jump-target map as the host has them, the loaded
// record, the advice, the expected end state, and each window as it must be delivered and replayed (its record, its
// three streams' meaningful bits, its rows). It comes from a fixture the RTL's test wrote (tests/segment/rtl/: the
// generated guests, with the RTL chip's own figures), or from a real workload's recorded files (tests/segment_real.h).
//
// THE RUN, bench.rs's and run_chip's: before the first cycle the master's and the memory tile's programs in their IMEM
// and DMEM (boot_local, 6.5), and in the master's scratchpad the two images it loads, with its boot_desc and its
// commands (CC_SEGMENT, CC_WAIT, CC_EXIT: tests/test_sort.cpp says how), as v1.35's harness placed them; in the tracer
// tile's scratchpad its layout (fw/tracer/tracer_layout.h): its header, the replayer's tables, the core-fed set's index
// and templates, the loaded record, the advice and the expected end state; in each compute tile's its replay layout's
// header (fw/replay/replay_layout.h), and in those with a window the tables, the index and the templates. Then the
// chip runs to C&C 1's exit word, polled every 512 cycles.
//
// THE MEMORY SYSTEM behind the Tracer's fill port is Open (17.9.14), and the RTL's test plays it at the chip's boundary
// (`Fill`): each read answered after a latency of its kind's (a code line's, a data line's, a map line's), each write
// applied as it is accepted and answered `ok`, every ready 0 one cycle in four, all from one xorshift32 stream, drawn
// cycle by cycle. `Fill` here is that model, draw for draw, on the Tracer's fill port (units::tracer::Tracer::FillPort):
// with the chip's cycles the RTL's, its draws fall on the same requests. It is the test's, not hardware.
//
// THE CHECKS, run_chip's: C&C 1's mailbox (its commands done, every tile's result and the tracer tile's, the LOADs, no
// other request), the tracer tile's results (every window run, no error, the advice used up, the end state the
// expected one, no stray request) and every compute tile's (its pass, no error; ROWS_RUN); every window's rows out of
// its tile's row port against the segment's; every window's delivery read
// from its tile's scratchpad; each replayer's end registers against the next window's record as it landed, the last
// against the Tracer's last record, and that record against the segment's end; the fill port (one read and one write
// outstanding); the toast pin; for a guest built to miss, that it did. THE CYCLES are printed, the simulator's own
// figures for meshsim's firmware: chip-level cycle equality with the RTL, under the RTL's own firmware, was established
// at meshsim's commit 5a87d658 and is no longer checked (a fixture's RTL figures, `Rtl`, are read and not compared).
//
// Test-side code (the configuration regime): the STL throughout.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "mach.h"
#include "units/mem_system.h"
#include "units/tracer.h"

namespace segment {

constexpr unsigned T = 64;                    // TILE_COUNT
constexpr unsigned RECORD_WORDS = 68, ROW_LONGS = 10, LINE_BYTES = 128, HEADER_WORDS = 20;

struct U128 {
    uint64_t lo = 0, hi = 0;
    bool operator==(const U128& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const U128& o) const { return !(*this == o); }
};

// verif::Random (Snarky One's rtl/verif/src/lib.rs): xorshift32 (13, 17, 5) from a non-zero state.
struct Random {
    uint32_t state;
    explicit Random(uint32_t seed) : state(seed) {}
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    uint32_t below(uint32_t bound) { return next() % bound; }
    bool one_in(uint32_t period) { return below(period) == 0; }
};

// ---- the files as the host has them (17.9.4, 17.9.5): the RTL's tests' Guest and Map
enum : uint8_t { STATUS_OK = 0, STATUS_HOLE = 1, STATUS_NOT_OWNED = 2, STATUS_BEYOND = 3 };
struct Line {
    uint8_t status = STATUS_OK;
    uint8_t bytes[LINE_BYTES] = {};
};
struct Guest {
    uint64_t lowest = 0;
    uint32_t log_k = 0, depth = 0;
    std::unordered_map<uint64_t, Line> lines;   // by the file's line; absent: a hole, read as zeros
    // A line's status and contents: beyond past the depth's capacity, 2^(5 + 10 depth) lines.
    Line line(uint64_t index) const;
};
struct Map {
    uint32_t k = 0, p1 = 0, p3 = 0;
    std::vector<Line> lines;
    bool has_filler = false;                    // the contents of every line past `lines`, status ok; else beyond
    uint8_t filler[LINE_BYTES] = {};
};

// ---- a window as it must be delivered and replayed
struct Stream {
    uint64_t length = 0;                        // its bits
    std::vector<U128> entries;                  // its entries, the bits past the stream's end 0
};
struct Window {
    U128 record[RECORD_WORDS];                  // as its tile receives it (17.9.10)
    uint32_t scans[2] = {};                     // its run's code values and log words
    Stream streams[3];                          // code, read log, write log
    std::vector<uint64_t> rows;                 // ROW_LONGS a row: word k's low half, then its high
    size_t row_count() const { return rows.size() / ROW_LONGS; }
};

struct Segment {
    std::string name;
    unsigned windows = 0;                       // tiles 0 to windows - 1's
    uint32_t rows = 0;                          // RUN_CONFIG.rows, each window's
    uint32_t seed = 0;                          // the fill port's stream starts at seed + 1
    uint64_t cycle_limit = 0;
    bool misses = false;                        // the guest must miss in the Tracer's caches and its jump LRU
    Guest guest;
    Map map;
    U128 start[RECORD_WORDS], end[RECORD_WORDS];
    std::vector<uint64_t> advice;
    std::vector<Window> window;
};

// ---- the RTL chip's figures for a segment (a fixture's second half)
struct FillCall {
    uint64_t cycle = 0, line = 0, due = 0;
    uint32_t root = 0;
    uint8_t kind = 0, depth = 0, status = 0;    // kind: 0 a read, 1 a write
};
struct Rtl {
    bool present = false;
    uint64_t cycles = 0;                        // from the reset to the poll that saw the done word
    std::vector<uint32_t> summary, report;      // the master's summary, the tracer tile's report
    uint64_t fill_counts[4] = {};               // code, data and map lines read; lines written back
    std::vector<FillCall> fill;
    std::vector<std::vector<uint32_t>> row_cycles;   // each tile's rows' cycles out of its row port
    uint32_t tracer_header[HEADER_WORDS] = {}, replay_header[HEADER_WORDS] = {};   // as the RTL's test placed them
};

// A fixture (tests/segment/rtl/<name>.seg.gz, its README): false and `err` if it is not as the format says.
bool read_fixture(const std::string& path, Segment& s, Rtl& rtl, std::string& err);

// ---- the memory system on the fill port, the RTL test's (`Fill`)
class Fill {
public:
    // `offset`: the RTL test's cycle of the simulator's cycle t is t - offset.
    Fill(const Guest& guest, const Map& map, uint32_t seed, int64_t offset);
    units::tracer::Tracer::FillPort port;
    uint64_t counts[4] = {};
    std::vector<FillCall> calls;                // every request taken, in the RTL test's cycles
    std::string fault;                          // the first violation of the port's contract, or of the files

    Guest guest;                                // written by the write-backs
    static constexpr uint32_t MEMORY_ROOT = 0x01234567, MAP_ROOT = 0x07654321, MAP_DEPTH = 1;
private:
    Map map_;
    Random random_;
    int64_t offset_;
    int64_t drawn_ = -1;                        // the last cycle whose readies are drawn
    bool read_ready_ = false, write_ready_ = false;
    int64_t read_due_ = -1, write_due_ = -1;    // the outstanding requests' answers (-1: none)
    void draw_to(int64_t cycle);
    uint64_t between(uint32_t lo, uint32_t hi) { return lo + random_.below(hi - lo + 1); }
    static bool read_ready(void* ctx, int64_t t);
    static int64_t read(void* ctx, int64_t t, int32_t root, int depth, int64_t line, units::tracer::MemSystem::Response& r);
    static bool write_ready(void* ctx, int64_t t);
    static int64_t write(void* ctx, int64_t t, int32_t root, int depth, int64_t line, const uint64_t* data, int& status);
};

// ---- the run
struct Run {
    bool finished = false;
    uint64_t cycles = 0;                        // from the reset, to the poll that saw the done word
    uint64_t after_exit = 0;                    // v1.37: the cycles run after it, until the proof was settled
    std::vector<uint32_t> summary, report;   // C&C 1's mailbox, the tracer tile's tr_results
    std::vector<std::vector<uint64_t>> rows;    // each tile's rows as they left its row port, ROW_LONGS a row
    std::vector<std::vector<int64_t>> row_cycles;   // and the cycle each left in, the RTL test's cycles
    uint32_t released = 0;                      // C&C 1's mcycle at the windows' landing's release
    uint64_t fill_counts[4] = {};
    double seconds = 0;
    int failures = 0;                           // the checks that failed (each printed)
};
// ---- v1.37: the segment from the host's frames (tests/proof_bench.h; CHANGES-v1.37.md). With Options::proof set,
// run_chip builds the machine with the host link (MachConfig::proof) and the master runs fw/cc/master.c in place of
// cc1.c: nothing of the segment is placed (its record, its advice and its expected end state come from the frames,
// through the master), the Tracer's memory is the file store the Updater writes, in place of `Fill`, and the checks of
// C&C 1's mailbox, of the fill port and of the Tracer's last record are the proof's own. A segment with no `window`
// (a guest with no recorded files) is run with the checks that need none.
struct MasterPlaces {                           // where run_chip placed what the master must be told of: word addresses
    uint32_t tiles[5], tracer[5];               // an image: its code in the master's scratchpad, its rows, its data, its rows, the COUNT row
    uint32_t tr_record, tr_expect, tr_final, tr_advice, tr_advice_rows;   // the tracer tile's layout
};
struct Proof {
    virtual ~Proof() = default;
    // Before the first cycle, the master's image placed: what it is told, the link's bytes, the observers.
    virtual void place(mach::Mach& m, const std::string& master_elf, const MasterPlaces& places) = 0;
    // After the run, before run_chip's checks: the master's results read (run.summary, run.released).
    virtual void finished(mach::Mach& m, Run& run) = 0;
    // A record's word 66 as it must be on this machine: `recorded`'s with the roots and depths the master gave.
    virtual U128 files_word(const U128& recorded) const = 0;
    // The proof's own checks, after run_chip's: each failure printed, and counted in run.failures.
    virtual void check(mach::Mach& m, Run& run) = 0;
    // A run that must end early (a damaged packet file): run_chip's own checks, of a segment traced and replayed, are
    // left out, and the proof's are the run's.
    virtual bool ends_early() const = 0;
    // v1.37 (30.7): after the master's exit, whether what it sent has all arrived (the host's end has the reply); the
    // chip runs on until then, a poll at a time, to a limit. The run's cycles stay the exit's.
    virtual bool settled(mach::Mach&) { return true; }
};
struct Options {
    unsigned parts = 1;                         // the chip split over threads (mach.h)
    bool quiet = false;                         // only the failures and the summary lines
    const char* fw = nullptr;                   // the images' folder: rtl_images::fw_images()'s by default
    const char* tables = "data/tables/replayer/";
    Proof* proof = nullptr;                     // v1.37: the segment from the host's frames
    uint32_t lpddr_seed = 0;                    // v1.37: the LPDDR model's jitter (MachConfig::lpddr_seed)
};
// The segment on the chip, with run_chip's checks; `rtl`, the RTL chip's figures, is not compared (the header).
Run run_chip(const Segment& s, const Rtl* rtl, const Options& o = Options());

// The replay's cycles (chip_replay_test.rs's a_row): from the windows' landing's release to each window's last row,
// summed over the windows, and the rows.
struct ReplayCycles { uint64_t cycles = 0, rows = 0, to_first = 0; };
ReplayCycles replay_cycles(const Run& r, unsigned windows);

}  // namespace segment
