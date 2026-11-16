// chain.h: the readers of the pipeline's host-side files, as `ref`'s T1 reads them (TracerT1.loadW26, readSegments):
// what a segment's run starts from. Used by tracer_block (tests/tracer_block_main.cpp) and by acceptance C's real
// windows (tests/segment_real.cpp). Configuration and between-cycle code only (D6 doesn't apply here).
//
//   - the inputs (TracerT1.loadW26): proof.json, initial.pages, frame6.bin and map.lines, seg/NNN.pages, into
//     one FileStore in `ref`'s allocation order (the image a depth-2 file, then the map);
//   - the segment file (TracerT1.readSegments: frames 2-5; frame 5's 32-entry LRU coding), and its writer for tests;
//   - a minimal JSON reader for proof.json.
// (v1.35's chain, T2's segments back to back under v1.35's firmware, and its landings, dump forms and oracle were
// here: retired with T2 when the chip test of the segment, tests/test_segment.cpp, took the segment on the chip.)
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "units/file_store.h"

namespace chain {

using units::tracer::FileStore;

// ---- a minimal JSON reader (objects, arrays, strings, numbers, literals): ReplayDelivery.Json's subset
struct Json {
    enum Kind : uint8_t { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
    bool b = false;
    double num = 0;
    std::string str;                                // a string, or a number's text (exact for large integers)
    std::vector<Json> arr;
    std::vector<std::string> keys;                  // an object's members: keys[i] -> obj[i]
    std::vector<Json> obj;
    const Json* get(const std::string& k) const;    // an object's member, or null
    int64_t integer() const;                        // a number's text (or a string's: "0x..." too) as an integer
};
bool parse_json(const std::string& text, Json& out, std::string& err);

// ---- the inputs
struct W26 {
    int log2k = 0, hash_k = 0, hash_p3 = 0, segments = 0;
    int32_t hash_p1 = 0;
    int64_t lowest = 0;
    FileStore::File img, map;
    unsigned img_pages = 0;
    std::vector<std::vector<int64_t>> written;      // seg/NNN.pages: the pages (offset / 4096), in the file's order
};
// Into `fs` (fresh): the image (create(2), put_page per initial.pages entry in order), then the map
// (create(depth_for(lines)), its pages). Every check of loadW26: flags 0, frame 6's type and length, map.lines equal
// to frame 6's lines, frame 6's constants equal to proof.json's.
bool load_w26(const std::string& dir, FileStore& fs, W26& w, std::string& err);

// ---- the segment file
struct Seg {
    uint64_t state[131] = {};                       // frame 2: pc, idx, off, the 128 registers
    std::vector<uint64_t> advice;                   // frame 3
    std::vector<std::pair<uint32_t, uint64_t>> updates;   // frames 4/5: (cell, value), cell in doublewords from lowest
    std::vector<int64_t> written_pages() const;     // the update list's pages, ascending (TreeSet's order)
};
bool read_segments(const std::string& path, std::vector<Seg>& out, std::string& err);
// The bytes of a segment file from its frames (synthetic tests): frame 2 per segment, frame 3 if advice, frame 4 (raw
// runs) or 5 (the LRU coding) for the updates.
std::vector<uint8_t> write_segments(const std::vector<Seg>& segs, bool lru);

}  // namespace chain
