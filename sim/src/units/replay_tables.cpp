// replay_tables.cpp: ReplayTables (replay_tables.h), ported from `ref`'s test/ReplayTables.java.
#include "units/replay_tables.h"

#include <cstdio>
#include <cstdlib>

#include "base/fatal.h"

namespace units {
namespace tracer {

namespace {

std::vector<uint8_t> read_all(const std::string& path) {
    std::vector<uint8_t> b;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) FATAL("ReplayTables: cannot open %s", path.c_str());
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return b;
}
uint64_t le(const std::vector<uint8_t>& b, size_t at, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; i--) v = (v << 8) | b[at + i];
    return v;
}

}  // namespace

uint32_t ReplayTables::key_of(uint32_t w) {
    const uint32_t op = (w | 1) & 0x7F, f3 = (w >> 12) & 7;
    if (op == 0x0B) return w & ~(31u << 7 | 31u << 15 | 31u << 20);
    if (op == 0x73 && (f3 == 1 || f3 == 2)) {
        const uint32_t rd = (w >> 7) & 31, rs1 = (w >> 15) & 31;
        const uint32_t nrd = rd == 0 ? 0 : 1;
        const uint32_t nrs1 = rs1 == 0 ? 0 : (rd == 0 || rs1 == rd) ? 1 : 2;
        return (w & ~(31u << 7 | 31u << 15)) | nrd << 7 | nrs1 << 15;
    }
    return w;
}

int ReplayTables::lookup(uint32_t w) const {
    const uint32_t k = key_of(w);
    int lo = 0, hi = int(key.size()) - 1;
    while (lo <= hi) {
        const int m = int(unsigned(lo + hi) >> 1);
        if (key[m] == k) return m;
        if (key[m] < k) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

std::string ReplayTables::default_dir() {
    const char* p = std::getenv("SIM_REPLAY_TABLES");
    return p ? p : "data/tables/replayer";
}

ReplayTables ReplayTables::load(const std::string& dir) {
    ReplayTables t;
    t.dir = dir;
    t.decode_bytes = read_all(dir + "/decode_table.bin");
    t.rom_bytes = read_all(dir + "/rom.bin");
    t.template_bytes = read_all(dir + "/templates.bin");
    t.index_bytes = read_all(dir + "/template_index.bin");
    if (t.decode_bytes.size() != 4096 || t.rom_bytes.size() != 8192 || t.index_bytes.size() % 16 != 0 ||
        t.template_bytes.size() % 16 != 0)
        FATAL("ReplayTables: %s: table sizes %zu, %zu, %zu, %zu", dir.c_str(), t.decode_bytes.size(), t.rom_bytes.size(),
              t.index_bytes.size(), t.template_bytes.size());
    for (int e = 0; e < 2048; e++) t.table[e] = uint16_t(le(t.decode_bytes, 2 * e, 2));
    for (int j = 0; j < 512; j++) {
        t.rom_lo[j] = le(t.rom_bytes, 16 * j, 8);
        t.rom_hi[j] = le(t.rom_bytes, 16 * j + 8, 8);
    }
    const size_t n = t.index_bytes.size() / 16, nw = t.template_bytes.size() / 16;
    for (size_t k = 0; k < n; k++) {
        t.key.push_back(uint32_t(le(t.index_bytes, 16 * k, 4)));
        t.offset.push_back(uint32_t(le(t.index_bytes, 16 * k + 4, 4)));
        t.length.push_back(uint32_t(le(t.index_bytes, 16 * k + 8, 4)));
        t.rd0.push_back(t.index_bytes[16 * k + 12]);
        t.cls.push_back(t.index_bytes[16 * k + 13]);
        if (k > 0 && t.key[k - 1] >= t.key[k]) FATAL("ReplayTables: %s: index not sorted by key at %zu", dir.c_str(), k);
        if (uint64_t(t.offset[k]) + t.length[k] > nw)
            FATAL("ReplayTables: %s: template %zu runs past templates.bin", dir.c_str(), k);
        std::vector<uint64_t> tw(2 * size_t(t.length[k]));
        for (uint32_t i = 0; i < t.length[k]; i++) {
            tw[2 * i] = le(t.template_bytes, 16 * (size_t(t.offset[k]) + i), 8);
            tw[2 * i + 1] = le(t.template_bytes, 16 * (size_t(t.offset[k]) + i) + 8, 8);
        }
        if (t.length[k] > 0 && ((tw[0] >> 32) & 0xFFF) != t.length[k] - 1)
            FATAL("ReplayTables: %s: template %zu's length is not its first seq_remaining + 1", dir.c_str(), k);
        t.words.push_back(std::move(tw));
    }
    return t;
}

}  // namespace tracer
}  // namespace units
