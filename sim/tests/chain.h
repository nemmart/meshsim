// chain.h (WORKER17, v1.35): T2's segments back to back on the framework's chip, the memory carried from each segment
// to the next as `ref`'s T1 carries it (TracerT2.main, M3Seg's loop; sim/planning/sessions/WORKER17/NOTES.md). The
// inputs come from WORKER26's files and the delivery directly, not from `ref`'s dumps. Configuration and between-cycle
// code only (D6 doesn't apply here).
//
//   - WORKER26's inputs (TracerT1.loadW26): proof.json, initial.pages, frame6.bin and map.lines, seg/NNN.pages, into
//     one FileStore in `ref`'s allocation order (the image a depth-2 file, then the map);
//   - the segment file (TracerT1.readSegments: frames 2-5; frame 5's 32-entry LRU coding);
//   - the harness's landings before the boot (TracerT2.chip): C&C 3's tracer_layout.h and every tile's
//     replay_layout.h, as the (unit, chunk, value) list M3Inputs.sram writes;
//   - W13's dump forms (M3Inputs: "M3FILES1", "M3SRAM01"), written gzipped or hashed uncompressed (the oracle,
//     tests/golden-chain, is the sha256 of the uncompressed stream);
//   - the delivery's manifest (the windows with files: TracerT2's byIndex) and a minimal JSON reader for it.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "t2_harness.h"
#include "units/file_store.h"
#include "units/replay_tables.h"

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

// ---- WORKER26's inputs
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

// ---- the landings (TracerT2.chip; 64 windows unless `windows`; raw, rc 0)
// C&C 3's layout (tracer_layout.h) and the replay region's (replay_layout.h version 5, StageB's)
constexpr uint32_t C_HDR = 0, C_TABLES = 0x40, C_INDEX = 0xC40, C_RECORD = 0xE40, C_OWN = 0xF80, C_EXPECT = 0x10C0,
                   C_ADVICE = 0x2000, C_TEMPLATES = 0x58000, TL_MAGIC = 0x52435254, TL_VERSION = 1;
constexpr uint32_t S_HDR = 0x00000, S_TABLES = 0x00040, S_INDEX = 0x00C40, S_RECORD = 0x00E40, S_TEMPLATES = 0x58000,
                   S_CODE = 0x08000, S_RLOG = 0x18000, S_WLOG = 0x38000, RL_MAGIC = 0x594C5052;
struct MapInfo { int32_t mul = 0, step = 0, k = 0; };
// Units 0-63 the tiles, 66 C&C 3; in (unit, chunk) order, zero words left out (as M3Inputs.sram lists them).
void landings(const units::tracer::ReplayTables& tb, const W26& w, const Seg& sg, const Seg* next, unsigned windows,
              std::vector<t2::SramLanding>& out);

// ---- W13's dump forms
std::vector<uint8_t> files_stream(const FileStore& fs);                        // "M3FILES1"
std::vector<uint8_t> sram_stream(const std::vector<t2::SramLanding>& l);      // "M3SRAM01"
bool write_gz(const std::string& path, const std::vector<uint8_t>& bytes, std::string& err);

// ---- the oracle (tests/golden-chain/*.starts): per segment the start's line, and the landings' sha256 where given
struct Start { int64_t next_free = -1, pages = -1; uint64_t scan = 0; std::string sha, sram_sha; bool present = false; };
bool load_starts(const std::string& path, std::vector<Start>& out, std::string& err);

// ---- the delivery: the windows the manifest lists with files (TracerT2's byIndex), from NAME.manifest.json
bool manifest_windows(const std::string& delivery_dir, const std::string& name, std::set<unsigned>& with_files,
                      std::string& err);

// TracerRig.streamValues: `values` values of `bits` bits (32 or 64) packed in 128-bit words {lo, hi}.
std::vector<uint64_t> stream_values(const std::vector<uint64_t>& words, size_t values, unsigned bits, bool& short_input);

}  // namespace chain
