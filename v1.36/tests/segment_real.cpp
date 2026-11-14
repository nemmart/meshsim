// segment_real.cpp: segment_real.h's loader.
#include "segment_real.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "chain.h"
#include "tracerblock/gold.h"
#include "units/replay_tables.h"

namespace segment {

namespace {

constexpr size_t PAGE_BYTES = 4096;
constexpr unsigned SEGMENT_WINDOWS = 64, WINDOW_ROWS = 65536;

bool read_bytes(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
uint64_t u64_at(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= uint64_t(b[at + i]) << (8 * i);
    return v;
}
// `"name": value` of proof.json, a number, decimal or a quoted `0x` one.
bool proof_value(const std::string& text, const char* name, uint64_t& out) {
    const std::string key = std::string("\"") + name + "\": ";
    const size_t at = text.find(key);
    if (at == std::string::npos) return false;
    std::string value;
    for (size_t i = at + key.size(); i < text.size() && text[i] != ',' && text[i] != '\n' && text[i] != '}'; i++) if (text[i] != '"' && text[i] != ' ') value += text[i];
    char* end = nullptr;
    out = std::strtoull(value.c_str(), &end, value.compare(0, 2, "0x") == 0 ? 16 : 10);
    return end && *end == 0 && !value.empty();
}

}  // namespace

std::string inputs_directory() {
    std::string dir;
    if (const char* env = std::getenv("MESHSIM_INPUTS"); env && *env) dir = env;
    std::vector<uint8_t> probe;
    if (dir.empty() || !read_bytes(dir + "/w26-jumpmap/evm-zone-23/proof.json", probe)) return "";
    return dir;
}

bool load_real(const std::string& inputs, const std::string& run, unsigned first, unsigned windows, Segment& s, std::string& err) {
    const std::string wdir = inputs + "/w26-jumpmap/" + run, vdir = inputs + "/v1261/" + run;
    if (first % SEGMENT_WINDOWS != 0 || windows == 0 || windows > T) { err = "a segment starts at a host segment's start (a multiple of 64 windows) and has at most 64 windows"; return false; }
    const unsigned k = first / SEGMENT_WINDOWS;
    char name[96];
    std::snprintf(name, sizeof name, "%s-%u-%u", run.c_str(), first, first + windows - 1);
    s = Segment();
    s.name = name;
    s.windows = windows; s.rows = WINDOW_ROWS;
    s.seed = 0x7ace0201;                                                 // chip_replay_test.rs's shape: the fill port's stream
    s.cycle_limit = uint64_t(windows) * WINDOW_ROWS * 12 + 4000000;      // and its limit
    s.misses = false;

    // ---- the guest at row 0 (chip_replay_test.rs, Workload::load)
    std::vector<uint8_t> bytes;
    if (!read_bytes(wdir + "/proof.json", bytes)) { err = "cannot read " + wdir + "/proof.json"; return false; }
    const std::string proof(bytes.begin(), bytes.end());
    uint64_t lowest = 0, log_k = 0, hash_k = 0, p1 = 0, p3 = 0;
    if (!proof_value(proof, "lowest_address", lowest) || !proof_value(proof, "log2_k", log_k) || !proof_value(proof, "hash_k", hash_k) ||
        !proof_value(proof, "hash_p1", p1) || !proof_value(proof, "hash_p3", p3)) { err = wdir + "/proof.json: its constants"; return false; }
    if (!read_bytes(wdir + "/map.lines", bytes) || bytes.size() != (size_t(LINE_BYTES) << hash_k)) { err = wdir + "/map.lines: 2^hash_k lines"; return false; }
    s.map.k = uint32_t(hash_k); s.map.p1 = uint32_t(p1); s.map.p3 = uint32_t(p3);
    s.map.lines.resize(size_t(1) << hash_k);
    for (size_t i = 0; i < s.map.lines.size(); i++) std::memcpy(s.map.lines[i].bytes, &bytes[i * LINE_BYTES], LINE_BYTES);
    // the pages the host's segments write: the Tracer's copy owns them (17.9.5)
    std::set<uint64_t> written;
    for (unsigned seg = 0;; seg++) {
        std::snprintf(name, sizeof name, "/seg/%03u.pages", seg);
        if (!read_bytes(wdir + name, bytes)) break;
        for (size_t at = 0; at + 8 <= bytes.size(); at += 8) written.insert(u64_at(bytes, at));
    }
    // the memory file's lines: the initial image's pages, by their offsets from the lowest address, and the written
    // pages it lacks, zeros; its depth the least whose capacity, 2^(5 + 10 depth) lines, holds 8 << log_k bytes
    s.guest.lowest = lowest; s.guest.log_k = uint32_t(log_k);
    for (s.guest.depth = 0; 5 + 10 * s.guest.depth + 4 < log_k; s.guest.depth++) {}
    auto owned = [&](uint64_t offset, const uint8_t* contents) {
        for (size_t i = 0; i < PAGE_BYTES / LINE_BYTES; i++) {
            Line l;
            l.status = written.count(offset) ? STATUS_OK : STATUS_NOT_OWNED;
            if (contents) std::memcpy(l.bytes, contents + i * LINE_BYTES, LINE_BYTES);
            s.guest.lines[offset / LINE_BYTES + i] = l;
        }
    };
    if (!read_bytes(wdir + "/initial.pages", bytes) || bytes.size() % (16 + PAGE_BYTES) != 0) { err = wdir + "/initial.pages: not whole pages"; return false; }
    std::set<uint64_t> present;
    for (size_t at = 0; at < bytes.size(); at += 16 + PAGE_BYTES) {
        const uint64_t offset = u64_at(bytes, at);
        if (offset % PAGE_BYTES != 0) { err = wdir + "/initial.pages: a page's offset"; return false; }
        present.insert(offset);
        owned(offset, &bytes[at + 16]);
    }
    for (uint64_t offset : written) if (!present.count(offset)) owned(offset, nullptr);

    // ---- the segment's start: host segment k's, with the update lists of the segments before it
    std::vector<chain::Seg> segs;
    if (!chain::read_segments(wdir + "/segments.seg", segs, err)) return false;
    if (k >= segs.size()) { err = "window " + std::to_string(first) + " is past the run's host segments"; return false; }
    for (unsigned seg = 0; seg < k; seg++)
        for (const auto& u : segs[seg].updates) {
            const auto it = s.guest.lines.find(u.first / 16);
            if (it == s.guest.lines.end() || it->second.status != STATUS_OK) { err = "an update of a cell no host segment's pages hold"; return false; }
            for (unsigned b = 0; b < 8; b++) it->second.bytes[8 * (u.first % 16) + b] = uint8_t(u.second >> (8 * b));
        }
    // the files word (17.9.2) of the guest's file and the map's, as the RTL's test writes it
    U128 files;
    files.lo = uint64_t(Fill::MEMORY_ROOT) | uint64_t(s.guest.depth) << 31 | uint64_t(Fill::MAP_ROOT) << 33;
    files.hi = uint64_t(Fill::MAP_DEPTH) | uint64_t(log_k) << 2 | (0x80000000ull - lowest) << 8;
    const chain::Seg& sg = segs[k];
    s.start[0].lo = sg.state[0];
    s.start[0].hi = (sg.state[1] & 0xFFFFFFFFull) | (sg.state[2] & 0xFFFF) << 32;
    for (unsigned i = 0; i < 64; i++) { s.start[2 + i].lo = sg.state[3 + 2 * i]; s.start[2 + i].hi = sg.state[4 + 2 * i]; }
    s.start[66] = files;

    // ---- the windows, from their files
    tracer_block::Gold gold;
    if (!tracer_block::read_gold(inputs + "/gold-v1.3/" + run + "/segments.seg", gold, err)) return false;
    if (k >= gold.segments.size() || gold.lowest != lowest || gold.log_k != log_k) { err = "gold-v1.3's segment file against w26-jumpmap's"; return false; }
    const units::tracer::ReplayTables tables = units::tracer::ReplayTables::load(vdir);
    tracer_block::Account account;
    tracer_block::Differences diffs;
    const uint64_t files_words[2] = {files.lo, files.hi};
    s.window.resize(windows);
    std::vector<uint64_t> advice_after(windows);                         // the advice rows through each window
    for (unsigned n = 0; n < windows; n++) {
        const unsigned index = first + n;
        Window& w = s.window[n];
        tracer_block::Expected e;
        account.window(vdir, index, 7, tables, gold, k, files_words, e, diffs);
        if (diffs.count != 0) { err = "window " + std::to_string(index) + ": its files (above)"; return false; }
        if (account.advice_rows > segs[k].advice.size()) { err = "window " + std::to_string(index) + ": more advice rows than host segment " + std::to_string(k) + "'s advice"; return false; }
        advice_after[n] = account.advice_rows;
        auto totals = [&](U128* record) {                                // words 1, 66 and 67 of a record of this run
            record[1].lo = e.record[2];
            record[1].hi = e.record[3] & ((uint64_t(1) << 38) - 1);         // (the advice field: below)
            record[66] = files;
            record[67].lo = e.record[2 * 67]; record[67].hi = e.record[2 * 67 + 1];
        };
        std::snprintf(name, sizeof name, "/window_%05u.state.bin", index);
        if (!read_bytes(vdir + name, bytes) || bytes.size() != 16 * RECORD_WORDS) { err = vdir + name + ": a record is 68 words"; return false; }
        for (unsigned word = 0; word < RECORD_WORDS; word++) { w.record[word].lo = u64_at(bytes, 16 * word); w.record[word].hi = u64_at(bytes, 16 * word + 8); }
        totals(w.record);
        if (n == 0)   // the segment's start is its first window's
            for (unsigned word = 0; word < 66; word++)
                if (word != 1 && w.record[word] != s.start[word]) { err = "host segment " + std::to_string(k) + "'s start against window " + std::to_string(index) + "'s state.bin, word " + std::to_string(word); return false; }
        for (unsigned stream = 0; stream < 3; stream++) {
            Stream& st = w.streams[stream];
            st.length = e.bits[stream];
            for (size_t i = 0; i + 1 < e.streams[stream].size(); i += 2) {
                U128 entry; entry.lo = e.streams[stream][i]; entry.hi = e.streams[stream][i + 1];
                const uint64_t end = st.length - (i / 2) * 128;
                if (end < 64) { entry.hi = 0; entry.lo &= (uint64_t(1) << end) - 1; }
                else if (end < 128) entry.hi &= (uint64_t(1) << (end - 64)) - 1;
                st.entries.push_back(entry);
            }
        }
        std::snprintf(name, sizeof name, "/window_%05u.rows.bin", index);
        if (!read_bytes(vdir + name, bytes) || bytes.size() != size_t(WINDOW_ROWS) * 8 * ROW_LONGS) { err = vdir + name + ": a window's rows"; return false; }
        w.rows.resize(size_t(WINDOW_ROWS) * ROW_LONGS);
        for (size_t i = 0; i < w.rows.size(); i++) w.rows[i] = u64_at(bytes, 8 * i);
        if (n + 1 == windows) {                                          // the Tracer's last record: the last run's end, its totals
            for (unsigned word = 0; word < RECORD_WORDS; word++) { s.end[word].lo = e.record[2 * word]; s.end[word].hi = e.record[2 * word + 1]; }
            totals(s.end);
        }
    }
    // The advice: the values the segment's advice rows take, host segment k's first; the advice field [127:102] of the
    // loaded record is their number, of each window's record what is left after its run (17.9.2), 0 at the end.
    const uint64_t advice = advice_after[windows - 1];
    s.advice.assign(segs[k].advice.begin(), segs[k].advice.begin() + long(advice));
    s.start[1].hi = advice << 38;
    for (unsigned n = 0; n < windows; n++) s.window[n].record[1].hi |= (advice - advice_after[n]) << 38;
    s.end[1].hi |= (advice - advice_after[windows - 1]) << 38;
    return true;
}

}  // namespace segment
