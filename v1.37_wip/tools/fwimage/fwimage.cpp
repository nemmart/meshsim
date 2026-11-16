// fwimage.cpp: the sort runs' key files, made here, and an ELF file's loaded bytes, to compare two builds. The key
// file's format is the RTL's (Snarky One's xtask/src/sort_images.rs: `sort_test_keys`, `write_key_file`), written
// from that description.
//
//   fwimage keys RECORDS OUT                          SortTest's built-in `uniform` key set, RECORDS keys a tile, as
//                                                     SortTest's key file (tests/sort_input.h's read_keys reads it)
//   fwimage loaded ELF OUT                            what a loader takes from an ELF file and nothing else: its entry
//                                                     point, then each PT_LOAD segment's physical address, file size,
//                                                     memory size and file bytes. Two files with equal dumps load the
//                                                     same bytes at the same addresses
//
// A key file is little-endian 32-bit words: the magic 0x4B455953, the 64 tiles' record counts, then the keys tile by
// tile. The keys are java.util.Random's nextInt() from the seed 11 + "uniform".hashCode(), as SortTest.keys draws them.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t TILES = 64;
constexpr uint32_t KEY_MAGIC = 0x4B455953;                       // "KEYS"
constexpr uint32_t PT_LOAD = 1;

[[noreturn]] void die(const std::string& message) {
    std::fprintf(stderr, "fwimage: %s\n", message.c_str());
    std::exit(1);
}

Bytes read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) die(path + ": cannot open");
    Bytes data;
    uint8_t buffer[65536];
    for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) != 0;) data.insert(data.end(), buffer, buffer + n);
    std::fclose(f);
    return data;
}

void write_file(const std::string& path, const Bytes& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) die(path + ": cannot write");
    if (!data.empty() && std::fwrite(data.data(), 1, data.size(), f) != data.size()) die(path + ": short write");
    if (std::fclose(f) != 0) die(path + ": cannot write");
}

void put32(Bytes& out, uint32_t word) {
    for (int i = 0; i < 4; i++) out.push_back(uint8_t(word >> (8 * i)));
}

// ---- an ELF32 little-endian file: its loaded segments and its symbols ----

struct Segment {
    uint32_t address, file_bytes, memory_bytes;   // p_paddr, p_filesz, p_memsz
    const uint8_t* bytes;                         // its file bytes
};

struct Elf {
    std::string path;
    Bytes data;
    uint32_t entry = 0;
    std::vector<Segment> segments;                // PT_LOAD, in the file's order

    uint16_t u16(size_t at) const { need(at, 2); return uint16_t(data[at] | data[at + 1] << 8); }
    uint32_t u32(size_t at) const {
        need(at, 4);
        return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
    }
    void need(size_t at, size_t bytes) const {
        if (at > data.size() || bytes > data.size() - at) die(path + ": truncated");
    }

    explicit Elf(const std::string& p) : path(p), data(read_file(p)) {
        static const uint8_t ident[6] = {0x7f, 'E', 'L', 'F', 1, 1};   // ELF, 32-bit, little-endian
        if (data.size() < 52 || std::memcmp(data.data(), ident, 6) != 0) die(path + ": not a 32-bit little-endian ELF file");
        entry = u32(24);
        const uint32_t phoff = u32(28);
        const uint16_t phentsize = u16(42), phnum = u16(44);
        for (uint16_t i = 0; i < phnum; i++) {
            const size_t h = size_t(phoff) + size_t(i) * phentsize;
            if (u32(h) != PT_LOAD) continue;
            const uint32_t offset = u32(h + 4), file_bytes = u32(h + 16);
            need(offset, file_bytes);
            segments.push_back({u32(h + 12), file_bytes, u32(h + 20), data.data() + offset});
        }
    }

};

// ---- the keys: SortTest's uniform set ----

struct JavaRandom {   // java.util.Random: the 48-bit linear congruential generator, as Java specifies it
    static constexpr uint64_t MULTIPLIER = 0x5DEECE66Dull, MASK = (1ull << 48) - 1;
    uint64_t seed;
    explicit JavaRandom(int64_t s) : seed((uint64_t(s) ^ MULTIPLIER) & MASK) {}
    uint32_t next_int() {   // nextInt(): next(32)
        seed = (seed * MULTIPLIER + 0xB) & MASK;
        return uint32_t(seed >> 16);
    }
};

uint32_t java_hash(const char* text) {   // String.hashCode, for ASCII
    uint32_t hash = 0;
    for (; *text; text++) hash = hash * 31 + uint8_t(*text);
    return hash;
}

Bytes key_file(uint32_t records) {
    // SortTest.keys(kind, n, 11 + kind.hashCode()): the seed is an int, sign-extended.
    JavaRandom random{int64_t(int32_t(11 + java_hash("uniform")))};
    Bytes out;
    out.reserve(4 * (size_t(1) + TILES + size_t(TILES) * records));
    put32(out, KEY_MAGIC);
    for (uint32_t t = 0; t < TILES; t++) put32(out, records);
    for (uint32_t t = 0; t < TILES; t++)
        for (uint32_t j = 0; j < records; j++) put32(out, random.next_int());
    return out;
}

Bytes loaded_dump(const std::string& path) {
    const Elf elf(path);
    Bytes out;
    put32(out, elf.entry);
    for (const Segment& s : elf.segments) {
        if (s.memory_bytes == 0) continue;   // nothing to load
        put32(out, s.address); put32(out, s.file_bytes); put32(out, s.memory_bytes);
        out.insert(out.end(), s.bytes, s.bytes + s.file_bytes);
    }
    return out;
}

uint32_t number(const char* text) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (*text == 0 || *end != 0 || value > 1u << 20) die(std::string("bad count '") + text + "'");
    return uint32_t(value);
}

}   // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "keys" && argc == 4) {
        write_file(argv[3], key_file(number(argv[2])));
    } else if (command == "loaded" && argc == 4) {
        write_file(argv[3], loaded_dump(argv[2]));
    } else {
        std::fprintf(stderr, "usage: fwimage keys RECORDS OUT\n"
                             "       fwimage loaded ELF OUT\n");
        return 2;
    }
    return 0;
}
