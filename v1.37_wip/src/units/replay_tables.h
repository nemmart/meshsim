// replay_tables.h: the universal expansion tables as delivered (REPLAY-INPUT-REQUIREMENTS.md section 2, v1.25), ported
// from `ref`'s test/ReplayTables.java (owned, used read-only). The decode table, the ROM, the
// controller-fed template words and their index, keyed by the normalized template-index key (wip/tables/README.md,
// DECISIONS-v1.25 s5). One set per jolt-cpp version: `sim/data/tables/replayer/`, read in place.
//
// Configuration phase only (D6): loading uses the STL. What a unit keeps (the decode table, the ROM) is plain arrays,
// copied into its own state; the templates are for the controller's side (the ideal controller, the region image).
// A table file of the wrong size, or an index out of order or inconsistent with its templates, is FATAL.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace units {
namespace tracer {

struct ReplayTables {
    uint16_t table[2048] = {};                // the decode table (4.19.5)
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    // The index, in template-number order (sorted by key): key, word offset, length, rd0, class.
    std::vector<uint32_t> key, offset, length, rd0, cls;
    // Every template's words, by template number: {lo, hi} per word.
    std::vector<std::vector<uint64_t>> words;
    // The files' bytes as read (for hashing against a manifest and for region images).
    std::vector<uint8_t> decode_bytes, rom_bytes, template_bytes, index_bytes;
    std::string dir;

    int count() const { return int(key.size()); }

    // key(w), the normalized template-index key: every bit of the presented code-stream word w except register fields.
    // Class 1, an inline (w[6:0] = 0001011): rd, rs1 and rs2 cleared. Class 2, CSRRW/CSRRS (w[6:0] = 1110011, funct3
    // 1 or 2): rd and rs1 renamed by first appearance (x0 stays 0; a non-zero rd becomes 1; rs1 becomes 0 if x0, 1 if
    // equal to a non-zero rd or if rd is x0, else 2). Any other word is its own key.
    static uint32_t key_of(uint32_t w);
    // The template number serving presented word w, or -1 (a binary search: the index is sorted by key).
    int lookup(uint32_t w) const;

    // The tables' place: $SIM_REPLAY_TABLES, else data/tables/replayer (relative to sim/, where the tests run).
    static std::string default_dir();
    // Loads decode_table.bin, rom.bin, templates.bin and template_index.bin from dir.
    static ReplayTables load(const std::string& dir);
};

}  // namespace tracer
}  // namespace units
