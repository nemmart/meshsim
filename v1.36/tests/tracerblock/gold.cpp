// gold.cpp: gold's files and the checks of a run against them (gold.h).
#include "gold.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace tracer_block {

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
uint32_t u32_at(const uint8_t* b, size_t at) { uint32_t v; std::memcpy(&v, b + at, 4); return v; }
uint64_t u64_at(const uint8_t* b, size_t at) { uint64_t v; std::memcpy(&v, b + at, 8); return v; }
std::vector<uint64_t> u64s(const uint8_t* b, size_t bytes) {
    std::vector<uint64_t> v(bytes / 8);
    if (!v.empty()) std::memcpy(v.data(), b, 8 * v.size());
    return v;
}

// The update words: runs of a header, the first cell in its low 32 bits and the count in its high 32, then that many
// values.
std::vector<std::pair<uint64_t, uint64_t>> update_runs(const std::vector<uint64_t>& w) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    size_t at = 0;
    while (at < w.size()) {
        const uint64_t cell = w[at] & 0xFFFFFFFFull, count = w[at] >> 32;
        for (uint64_t k = 0; k < count && at + 1 + k < w.size(); k++) out.push_back({cell + k, w[at + 1 + k]});
        at += 1 + count;
    }
    return out;
}

// Frame 5: a u32 count, then that many 64-bit words coded against a true LRU of 32, from zeros, least significant bit
// first within each byte: a hit is a 1 and a 5-bit index; a miss a 0, a size bit, 1 for 64 bits, and the value.
std::vector<uint64_t> lru_32(const uint8_t* data, size_t bytes) {
    const uint32_t count = u32_at(data, 0);
    const uint8_t* codes = data + 4;
    const size_t code_bits = 8 * (bytes - 4);
    size_t position = 0;
    auto take = [&](unsigned width) {
        uint64_t value = 0;
        for (unsigned bit = 0; bit < width; bit++, position++)
            if (position < code_bits) value |= uint64_t((codes[position / 8] >> (position % 8)) & 1) << bit;
        return value;
    };
    uint64_t table[32] = {};
    std::vector<uint64_t> values;
    values.reserve(count);
    for (uint32_t n = 0; n < count; n++) {
        uint64_t value;
        unsigned place;
        if (take(1) == 1) { place = unsigned(take(5)); value = table[place]; }
        else { value = take(take(1) == 1 ? 64 : 32); place = 31; }
        for (unsigned k = place; k > 0; k--) table[k] = table[k - 1];
        table[0] = value;
        values.push_back(value);
    }
    return values;
}


// Bits [low + width - 1 : low] of a 128-bit word, width <= 32.
uint32_t field(uint64_t lo, uint64_t hi, unsigned low, unsigned width) {
    const uint64_t v = low >= 64 ? hi >> (low - 64) : low == 0 ? lo : (lo >> low) | (hi << (64 - low));
    return uint32_t(v & ((uint64_t(1) << width) - 1));
}
void set_field(uint64_t& lo, uint64_t& hi, unsigned low, uint64_t v) {
    if (low >= 64) { hi |= v << (low - 64); return; }
    lo |= v << low;
    if (low != 0) hi |= v >> (64 - low);
}

const char* const kStream[3] = {"code", "rlog", "wlog"};
constexpr unsigned kValueBits[3] = {32, 64, 64};
constexpr size_t ROW_BYTES = 80;
// the row opcodes the account reads (17.8.7)
enum : uint8_t { OP_BEQ = 28, OP_BNE = 29, OP_BLT = 30, OP_BGE = 31, OP_BLTU = 32, OP_BGEU = 33, OP_JAL = 34, OP_JALR = 35,
                 OP_SD = 53, OP_LD = 54, OP_ADVICE = 126 };
bool is_m(uint8_t op) { return op == 43 || op == 45 || op == 65 || op == 118 || op == 119; }

}  // namespace

// The decode table's index of code value w (17.8.6).
unsigned decode_index(uint32_t w) {
    switch ((w & 0x7F) | 1) {
        case 0x2F: return 1u << 10 | ((w >> 12) & 1) << 5 | ((w >> 27) & 31);
        case 0x0B: return 1u << 10 | 1u << 8 | ((w >> 25) & 31) << 3 | ((w >> 12) & 7);
        case 0x73: {
            const uint32_t f3 = (w >> 12) & 7;
            const uint32_t sel = f3 != 0 ? f3 : (w | 1) == 0x00000073 ? 8 : (w | 1) == 0x00100073 ? 9 : (w | 1) == 0x30200073 ? 10 : 11;
            return 1u << 10 | 2u << 8 | sel;
        }
        default: return ((w >> 2) & 31) << 5 | ((w >> 12) & 7) << 2 | ((w >> 30) & 1) << 1 | ((w >> 25) & 1);
    }
}

void Differences::add(const char* fmt, ...) {
    count++;
    if (shown >= 20) return;
    shown++;
    std::va_list ap;
    va_start(ap, fmt);
    std::printf("  DIFFERS ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
}

bool Lru::touch(uint64_t entry) {
    lookups++;
    bool hit = false;
    for (size_t k = 0; k < entries.size(); k++)
        if (entries[k] == entry) { entries.erase(entries.begin() + long(k)); hit = true; break; }
    if (!hit) {
        misses++;
        if (entries.size() == capacity) entries.pop_back();
    }
    entries.insert(entries.begin(), entry);
    return hit;
}

// A frame is a u32 length in 32-bit words, its header's two among them, a u32 type, and its data.
bool read_gold(const std::string& path, Gold& out, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) { err = "cannot read " + path; return false; }
    bool parameters = false, map = false;
    size_t at = 0;
    while (at + 8 <= bytes.size()) {
        const size_t length = u32_at(bytes.data(), at);
        const uint32_t kind = u32_at(bytes.data(), at + 4);
        if (length < 2 || at + 4 * length > bytes.size()) { err = path + ": a frame's length at byte " + std::to_string(at); return false; }
        const uint8_t* data = bytes.data() + at + 8;
        const size_t n = 4 * length - 8;
        if ((kind >= 3 && kind <= 5) && out.segments.empty()) { err = path + ": a segment's frame before its frame 2"; return false; }
        switch (kind) {
            case 7:   // the proof parameters: a version, a field count, then u64 fields
                if (n < 24 || u32_at(data, 0) != 1) { err = path + ": frame 7"; return false; }
                out.log_k = uint32_t(u64_at(data, 8));
                out.lowest = u64_at(data, 16);
                parameters = true;
                break;
            case 1: break;   // the page counts: not read
            case 6: {        // the jump table: k, p1, p3, then its 2^k lines
                out.map_k = u32_at(data, 0); out.map_p1 = u32_at(data, 4); out.map_p3 = u32_at(data, 8);
                out.map = u64s(data + 12, n - 12);
                if (out.map_k > 28 || out.map.size() != size_t(16) << out.map_k) { err = path + ": frame 6's lines"; return false; }
                map = true;
                break;
            }
            case 2: {
                if (n != 16 * size_t(RECORD_WORDS)) { err = path + ": frame 2 is not the 68-word record (the v1.3 file)"; return false; }
                out.segments.emplace_back();
                std::memcpy(out.segments.back().record, data, n);
                break;
            }
            case 3: out.segments.back().advice = u64s(data, n); break;
            case 4: out.segments.back().updates = update_runs(u64s(data, n)); break;
            case 5: out.segments.back().updates = update_runs(lru_32(data, n)); break;
            default: err = path + ": frame type " + std::to_string(kind); return false;
        }
        at += 4 * length;
    }
    if (!parameters || !map) { err = path + ": no frame 7 or no frame 6 (the v1.3 file)"; return false; }
    return true;
}

int Gold::probe(uint32_t key, bool& found) const {
    found = false;
    uint32_t line = map_k == 0 ? 0 : uint32_t(key * map_p1) >> (32 - map_k);
    for (int n = 1; n <= 3; n++) {
        const uint64_t* e = &map[size_t(line) * 16];
        bool free_entry = false;
        for (int j = 0; j < 16; j++) {
            if (uint32_t(e[j]) == key) { found = true; return n; }
            if (uint32_t(e[j]) == 0xFFFFFFFFu) free_entry = true;
        }
        if (free_entry) return n;
        line = uint32_t((uint64_t(line) + map_p3) % (uint64_t(1) << map_k));
    }
    return 3;
}

StreamSize stream_size(uint64_t lo, uint64_t hi, int stream) {
    StreamSize s;
    s.words = field(lo, hi, unsigned(16 * stream), 16);
    s.values = field(lo, hi, unsigned(48 + 17 * stream), 17);
    s.compressed = field(lo, hi, unsigned(99 + stream), 1) == 1;
    return s;
}

uint64_t stream_bits(const std::vector<uint64_t>& entries, StreamSize size, int stream) {
    if (!size.compressed) return uint64_t(size.values) * kValueBits[stream];
    auto bit = [&](uint64_t position) {
        const size_t w = size_t(position / 64);
        return w < entries.size() && ((entries[w] >> (position % 64)) & 1) != 0;
    };
    uint64_t position = 0;
    for (uint32_t k = 0; k < size.values; k++) position += bit(position) ? 5 : bit(position + 1) ? 66 : 34;
    return position;
}

void Account::fetch(uint64_t pc, uint64_t lowest, bool wide) {
    const uint64_t offset = pc - lowest;
    instruction_cache.touch(offset >> 7);
    if (wide && offset % 128 == 126) instruction_cache.touch((offset >> 7) + 1);
}

void Account::window(const std::string& dir, unsigned index, uint32_t compression, const units::tracer::ReplayTables& tables,
                     const Gold& gold, unsigned segment, const uint64_t files[2], Expected& out, Differences& d) {
    char name[64];
    auto file = [&](const char* part, std::vector<uint8_t>& bytes) {
        std::snprintf(name, sizeof name, "/window_%05u.%s.bin", index, part);
        if (!read_file(dir + name, bytes)) { d.add("window %u: cannot read %s%s", index, dir.c_str(), name); return false; }
        return true;
    };
    auto words = [&](const char* part, std::vector<uint64_t>& w) {
        std::vector<uint8_t> bytes;
        if (!file(part, bytes)) return false;
        if (bytes.size() % 16 != 0) { d.add("window %u: %s is not whole 128-bit words", index, part); return false; }
        w = u64s(bytes.data(), bytes.size());
        return true;
    };
    auto core_fed = [&](uint32_t w) {
        const unsigned cls = tables.table[decode_index(w)] & 3;
        return (w & 3) != 0 && (cls == 1 || cls == 2);
    };
    out = Expected();
    const uint64_t lowest = gold.lowest;
    std::vector<uint64_t> start, pick, end, raw_streams[3];
    if (!words("state", start) || !words("state-pick", pick) || !words("end", end)) return;
    if (start.size() != 2 * RECORD_WORDS || pick.size() != 2 * RECORD_WORDS || end.size() != 2 * RECORD_WORDS) {
        d.add("window %u: a record is 68 words", index);
        return;
    }
    StreamSize raw[3];
    for (int s = 0; s < 3; s++) {
        raw[s] = stream_size(start[2], start[3], s);
        if (raw[s].compressed) d.add("window %u: state.bin's %s is not raw", index, kStream[s]);
        if (!words(kStream[s], raw_streams[s])) return;
    }
    std::vector<uint32_t> code;
    for (size_t k = 0; k < raw_streams[0].size() && code.size() < raw[0].values; k++) {
        code.push_back(uint32_t(raw_streams[0][k]));
        if (code.size() < raw[0].values) code.push_back(uint32_t(raw_streams[0][k] >> 32));
    }
    if (code.size() != raw[0].values) { d.add("window %u: code.bin's values", index); return; }

    // ---- the rows
    std::vector<uint8_t> rows_bytes;
    if (!file("rows", rows_bytes)) return;
    if (rows_bytes.size() != ROW_BYTES * size_t(WINDOW_ROWS)) { d.add("window %u: rows.bin's rows", index); return; }
    const uint8_t* rb = rows_bytes.data();
    auto row_pc = [&](int row) { return u64_at(rb, ROW_BYTES * size_t(row) + 16); };
    const uint64_t start_pc = start[0], end_pc = end[0];
    const uint32_t start_offset = uint32_t(start[1] >> 32) & 0xFFFF;
    size_t taken = 0;
    bool wide = false, at_start = start_offset == 0;
    auto next_code = [&](uint32_t& w) {
        if (taken >= code.size()) { d.add("window %u: the rows' instructions run past the code stream's values", index); return false; }
        w = code[taken++];
        return true;
    };
    // Inside an expansion at its start, the instruction's code value is the run's first again; a segment fetches it,
    // and presents it again.
    if (start_offset != 0) {
        uint32_t w = 0;
        if (!next_code(w)) return;
        wide = (w & 1) == 1;
        if (index % SEGMENT_WINDOWS == 0) {
            fetch(start_pc, lowest, wide);
            instructions++;
            if (core_fed(w)) out.posts.push_back({w, start_offset});
        }
    }
    size_t log_rows = 0, store_rows = 0;
    for (int row = 0; row < WINDOW_ROWS; row++) {
        rows++;
        const uint8_t* r = rb + ROW_BYTES * size_t(row);
        const uint64_t pc = row_pc(row);
        uint32_t w = 0;
        if (pc == 0) {   // a padding row: its code value 0, nothing fetched or run
            if (!next_code(w)) return;
            if (w != 0) d.add("window %u, row %d: a padding row's code value 0x%08x", index, row, w);
            padding_rows++;
            at_start = true;
            continue;
        }
        if (at_start) {
            if (!next_code(w)) return;
            if ((w & 3) == 0) d.add("window %u, row %d: an instruction's code value 0x%08x", index, row, w);
            wide = (w & 1) == 1;
            fetch(pc, lowest, wide);
            instructions++;
            if (core_fed(w)) out.posts.push_back({w, 0});
        }
        const uint8_t opcode = r[4];
        const uint64_t address = u64_at(r, 64);
        rows_m += is_m(opcode) ? 1 : 0;
        if (opcode == OP_LD || (opcode >= 110 && opcode <= 112)) {
            data_cache.touch((address - lowest) >> 7);
            loads++;
            log_rows++;
        } else if (opcode == OP_SD || (opcode >= 113 && opcode <= 115)) {
            data_cache.touch((address - lowest) >> 7);
            stores++;
            store_rows++;
        } else if (opcode == OP_ADVICE) {
            advice_rows++;
            log_rows++;
        }
        // Its expansion's last row sets the new pc (17.8.7): a taken branch's and JAL's pc + the immediate, JALR's
        // (rs1 + the immediate) & ~1. One other than pc is a control transfer, its target looked up, or pc 0, the
        // program's end: each a read-log word.
        const bool last = (uint32_t(r[8]) | uint32_t(r[9]) << 8) == 0;
        if (last) {
            const uint64_t next_pc = row + 1 < WINDOW_ROWS ? row_pc(row + 1) : end_pc;
            const uint64_t immediate = u64_at(r, 24), rs1 = u64_at(r, 32), rs2 = u64_at(r, 40);
            sets_pc += opcode >= OP_BEQ && opcode <= OP_JALR ? 1 : 0;
            bool taken_branch = false;
            switch (opcode) {
                case OP_BEQ: taken_branch = rs1 == rs2; break;
                case OP_BNE: taken_branch = rs1 != rs2; break;
                case OP_BLT: taken_branch = int64_t(rs1) < int64_t(rs2); break;
                case OP_BGE: taken_branch = int64_t(rs1) >= int64_t(rs2); break;
                case OP_BLTU: taken_branch = rs1 < rs2; break;
                case OP_BGEU: taken_branch = rs1 >= rs2; break;
                default: break;
            }
            uint64_t new_pc = pc;
            if (opcode == OP_JAL) new_pc = immediate == 0 ? (next_pc < pc ? next_pc : pc) : pc + immediate;
            else if (opcode == OP_JALR) new_pc = (rs1 + immediate) & ~uint64_t(1);
            else if (taken_branch) new_pc = pc + immediate;
            if (new_pc == pc) {
                if (next_pc != pc + (wide ? 4 : 2)) d.add("window %u, row %d: the next row's pc is not in sequence", index, row);
            } else {
                if (next_pc != new_pc) d.add("window %u, row %d: the next row's pc is not the transfer's", index, row);
                log_rows++;
                if (new_pc != 0) {
                    const uint64_t target = new_pc - lowest;
                    if (target >> 32) d.add("window %u, row %d: a target beyond the map's keys", index, row);
                    if (!jump_lru.touch(target)) {
                        bool found = false;
                        map_lines += uint64_t(gold.probe(uint32_t(target), found));
                        if (!found) d.add("window %u, row %d: the target 0x%llx is not in the map", index, row, (unsigned long long)target);
                    }
                }
            }
        }
        at_start = last;
    }
    if (taken != code.size()) d.add("window %u: the rows' instructions against the code stream's values", index);
    // every row that takes a read-log word, and every store row, was seen
    if (log_rows != raw[1].values) d.add("window %u: the rows' read-log words against the read log's values", index);
    if (store_rows != raw[2].values) d.add("window %u: the store rows against the write log's values", index);
    posts += out.posts.size();

    // ---- what the run sends
    for (int s = 0; s < 3; s++) {
        out.sizes[s] = (compression >> s) & 1 ? stream_size(pick[2], pick[3], s) : raw[s];
        if (out.sizes[s].compressed) {
            const std::string part = std::string(kStream[s]) + "-lru";
            if (!words(part.c_str(), out.streams[s])) return;
        } else {
            out.streams[s] = raw_streams[s];
        }
        if (out.streams[s].size() != 2 * size_t(out.sizes[s].words)) d.add("window %u: %s's file against the stream word's words", index, kStream[s]);
        out.bits[s] = stream_bits(out.streams[s], out.sizes[s], s);
        if ((out.bits[s] + 127) / 128 != out.sizes[s].words) d.add("window %u: %s's bits", index, kStream[s]);
    }
    const uint64_t advice_total = gold.segments[segment].advice.size();
    if (advice_rows > advice_total) d.add("window %u: more advice rows than the segment's advice", index);
    const uint64_t advice_left = advice_total - advice_rows;
    std::memcpy(out.record, end.data(), sizeof out.record);
    uint64_t lo = 0, hi = 0;
    for (int s = 0; s < 3; s++) {
        set_field(lo, hi, unsigned(16 * s), out.sizes[s].words);
        set_field(lo, hi, unsigned(48 + 17 * s), out.sizes[s].values);
        set_field(lo, hi, unsigned(99 + s), out.sizes[s].compressed ? 1 : 0);
    }
    set_field(lo, hi, 102, advice_left);
    out.record[2] = lo; out.record[3] = hi;
    out.record[2 * 66] = files[0]; out.record[2 * 66 + 1] = files[1];
    // word 67: the record's bits of the inline templates, the only ones the controller stores (17.9.11)
    uint64_t inlines[2] = {0, 0};
    for (int n = 0; n < tables.count() && n < 128; n++)
        if ((tables.table[decode_index(tables.key[size_t(n)])] & 3) == 1) inlines[n / 64] |= uint64_t(1) << (n % 64);
    out.record[2 * 67] = pick[2 * 67] & inlines[0]; out.record[2 * 67 + 1] = pick[2 * 67 + 1] & inlines[1];
    if (start[2 * 67] != pick[2 * 67] || start[2 * 67 + 1] != pick[2 * 67 + 1]) d.add("window %u: the records' bitmaps", index);
}

int64_t compare(const RunOut& o, const Expected& e, const char* context, Differences& d) {
    int64_t compared = 0;
    if (o.post_opcode.size() != e.posts.size()) d.add("%s: %zu posts, the files' %zu", context, o.post_opcode.size(), e.posts.size());
    for (size_t k = 0; k < o.post_opcode.size() && k < e.posts.size(); k++)
        if (o.post_opcode[k] != e.posts[k].first || (o.post_offset[k] & 0xFFFF) != e.posts[k].second)
            d.add("%s: post %zu: 0x%08x at row offset %u, the files' 0x%08x at %u", context, k, o.post_opcode[k], o.post_offset[k],
                  e.posts[k].first, e.posts[k].second);
    if (o.queue[0].size() != 2 * size_t(RECORD_WORDS)) d.add("%s: output queue 0's packets: %zu", context, o.queue[0].size() / 2);
    for (size_t w = 0; w < size_t(RECORD_WORDS) && 2 * w + 1 < o.queue[0].size(); w++) {
        const uint64_t glo = o.queue[0][2 * w], ghi = o.queue[0][2 * w + 1], wlo = e.record[2 * w], whi = e.record[2 * w + 1];
        if (glo == wlo && ghi == whi) continue;
        if (w == 1) {
            const StreamSize g0 = stream_size(glo, ghi, 0), g1 = stream_size(glo, ghi, 1), g2 = stream_size(glo, ghi, 2);
            d.add("%s: record word 1: the Tracer's 0x%016llx%016llx, the files' 0x%016llx%016llx (streams, words/values/coded: %u/%u/%d %u/%u/%d "
                  "%u/%u/%d, advice %u; the files' %u/%u/%d %u/%u/%d %u/%u/%d, advice %u)", context, (unsigned long long)ghi,
                  (unsigned long long)glo, (unsigned long long)whi, (unsigned long long)wlo, g0.words, g0.values, g0.compressed, g1.words,
                  g1.values, g1.compressed, g2.words, g2.values, g2.compressed, field(glo, ghi, 102, 23), e.sizes[0].words, e.sizes[0].values,
                  e.sizes[0].compressed, e.sizes[1].words, e.sizes[1].values, e.sizes[1].compressed, e.sizes[2].words, e.sizes[2].values,
                  e.sizes[2].compressed, field(wlo, whi, 102, 23));
        } else {
            d.add("%s: record word %zu: the Tracer's 0x%016llx%016llx, the files' 0x%016llx%016llx", context, w, (unsigned long long)ghi,
                  (unsigned long long)glo, (unsigned long long)whi, (unsigned long long)wlo);
        }
    }
    for (int s = 0; s < 3; s++) {
        const StreamSize size = e.sizes[s];
        const std::vector<uint64_t>& got = o.queue[s + 1];
        if (o.lengths[s] != size.words) d.add("%s: %s's length word %u, the files' %u", context, kStream[s], o.lengths[s], size.words);
        if (got.size() != 2 * size_t(size.words)) d.add("%s: %s's entries %zu, the files' %u", context, kStream[s], got.size() / 2, size.words);
        for (size_t k = 0; 2 * k + 1 < got.size() && 2 * k + 1 < e.streams[s].size(); k++) {
            // through the stream's last value: the last entry's padding is of any value (17.9.6)
            const uint64_t end = e.bits[s] > 128 * k ? e.bits[s] - 128 * k : 0;
            const uint64_t mlo = end >= 64 ? ~uint64_t(0) : (uint64_t(1) << end) - 1;
            const uint64_t mhi = end >= 128 ? ~uint64_t(0) : end > 64 ? (uint64_t(1) << (end - 64)) - 1 : 0;
            compared++;
            if (((got[2 * k] ^ e.streams[s][2 * k]) & mlo) != 0 || ((got[2 * k + 1] ^ e.streams[s][2 * k + 1]) & mhi) != 0)
                d.add("%s: %s's entry %zu of %u, its bits %llu: the Tracer's 0x%016llx%016llx, the files' 0x%016llx%016llx", context, kStream[s], k,
                      size.words, (unsigned long long)end, (unsigned long long)got[2 * k + 1], (unsigned long long)got[2 * k],
                      (unsigned long long)e.streams[s][2 * k + 1], (unsigned long long)e.streams[s][2 * k]);
        }
    }
    return compared;
}

}  // namespace tracer_block
