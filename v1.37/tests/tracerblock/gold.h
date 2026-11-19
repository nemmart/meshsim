// gold.h: what a run of the Tracer must send, from gold's files, and the checks of it: the first half of the RTL's
// acceptance (Snarky One's rtl/blocks/tracer_unit/test/src/tracer_acceptance_test.rs: `read_gold`, `Account::window`,
// `Expected`, `stream_size`, `stream_bits`, `compare`), ported check for check. Nothing here reads the simulator:
//   - gold's segment file, v1.3 (`gold-v1.3/<program>/segments.seg`, DECISIONS-v1.3 section 16): frame 7, log2 K and
//     lowest_address; frame 6, the jump-target map and its constants; a segment's frame 2, its start as the 68-word
//     record, frame 3, its advice, and frame 4 or 5, its update list;
//   - a window's files (`v1261/<program>/window_NNNNN.*.bin`): its records (`state`, the streams raw; `state-pick`,
//     the Tracer's pick), its end (`end`), its streams raw and LRU-coded, and its rows (`rows`, 80 bytes a row).
// The models of the caches and the jump LRU are written from 17.9.4 and 17.9.5: a true LRU of 128 lines over each
// instruction's fetch, a straddling one's two; one over each memory row's line; a true LRU of 64 over each control
// transfer's target, a miss probing the map as 17.9.4 says.
//
// Configuration and between-run code only: the STL throughout.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../tracer_block.h"
#include "units/replay_tables.h"

namespace tracer_block {

constexpr int WINDOW_ROWS = 65536, SEGMENT_WINDOWS = 64, RECORD_WORDS = 68;

// ---- gold's segment file, v1.3
struct GoldSegment {
    uint64_t record[2 * RECORD_WORDS] = {};                 // frame 2: {lo, hi} a word
    std::vector<uint64_t> advice;                           // frame 3
    std::vector<std::pair<uint64_t, uint64_t>> updates;     // frame 4 or 5: (cell, its value at the segment's end)
};
struct Gold {
    uint32_t log_k = 0;
    uint64_t lowest = 0;
    uint32_t map_k = 0, map_p1 = 0, map_p3 = 0;
    std::vector<uint64_t> map;                              // frame 6's lines, 16 entries each: key | base << 32
    std::vector<GoldSegment> segments;
    // 17.9.4's probe of `key` over the map: the lines it reads; found: a matching entry.
    int probe(uint32_t key, bool& found) const;
};
bool read_gold(const std::string& path, Gold& out, std::string& err);

// The decode table's index of code value w (17.8.6), written from the specification.
unsigned decode_index(uint32_t w);

// The stream word's fields (17.8.2), a stream's.
struct StreamSize {
    uint32_t words = 0, values = 0;
    bool compressed = false;
};
StreamSize stream_size(uint64_t lo, uint64_t hi, int stream);
// A stream's bits through its last value (17.8.12).
uint64_t stream_bits(const std::vector<uint64_t>& entries, StreamSize size, int stream);

// What a run must send, from its window's files.
struct Expected {
    uint64_t record[2 * RECORD_WORDS] = {};
    StreamSize sizes[3];
    std::vector<uint64_t> streams[3];                       // {lo, hi} an entry
    uint64_t bits[3] = {};
    std::vector<std::pair<uint32_t, uint32_t>> posts;       // each code value and row offset
};

// A true LRU over lines or keys, the latest first.
struct Lru {
    std::vector<uint64_t> entries;
    size_t capacity;
    uint64_t lookups = 0, misses = 0;
    explicit Lru(size_t n) : capacity(n) {}
    bool touch(uint64_t entry);   // a look-up: a hit moves its entry to the front, a miss replaces the oldest
};

// The differences found, counted; the first few printed.
struct Differences {
    int64_t count = 0;
    int shown = 0;
    void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

// What the rows of a segment's windows say of it: the models and the counts.
struct Account {
    Lru instruction_cache{128}, data_cache{128}, jump_lru{64};
    uint64_t map_lines = 0, advice_rows = 0, rows = 0, padding_rows = 0, instructions = 0, rows_m = 0, loads = 0, stores = 0,
             posts = 0, sets_pc = 0;
    // A window's rows against its raw code stream, into the models; and what the run must send. `files`: the files
    // word as written. A file that is not as the format says is a difference.
    void window(const std::string& dir, unsigned index, uint32_t compression, const units::tracer::ReplayTables& tables,
                const Gold& gold, unsigned segment, const uint64_t files[2], Expected& out, Differences& d);
private:
    void fetch(uint64_t pc, uint64_t lowest, bool wide);
};

// A run's output against its window's files: the posts, output queue 0's 68 words, the length words and output
// queues 1-3 entry for entry through each stream's last value. Returns the entries compared.
int64_t compare(const RunOut& o, const Expected& e, const char* context, Differences& d);

}  // namespace tracer_block
