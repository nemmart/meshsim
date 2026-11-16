// fwimage.cpp: the data a chip run's firmware is built around, made here: the sort runs' key files and the master
// tile's run images (fw/README.md, "The run image"). The formats are the RTL's (Snarky One's xtask/src/sort_images.rs:
// `sort_test_keys`, `write_key_file`, `run_image`, `trace_run_image`, `loaded_rows`), written from that description.
//
//   fwimage keys RECORDS OUT                          SortTest's built-in `uniform` key set, RECORDS keys a tile, as
//                                                     SortTest's key file (tests/sort_input.h's read_keys reads it)
//   fwimage run sort kv32|k128 RECORDS TILE.elf OUT   a sort run's image: the header, then the tile image's rows
//   fwimage run memory OUT                            the memory run's: the header alone
//   fwimage run trace REPLAY.elf TRACER.elf OUT       the trace run's: the replay image's run image, then the tracer
//                                                     tile's image
//   fwimage check-run MASTER.elf RUN.bin              exit 0 only if the master's image holds RUN.bin, byte for byte,
//                                                     in its loaded bytes at its symbol RUN_IMAGE
//   fwimage loaded ELF OUT                            what a loader takes from an ELF file and nothing else: its entry
//                                                     point, then each PT_LOAD segment's physical address, file size,
//                                                     memory size and file bytes. Two files with equal dumps load the
//                                                     same bytes at the same addresses
//
// A key file is little-endian 32-bit words: the magic 0x4B455953, the 64 tiles' record counts, then the keys tile by
// tile. The keys are java.util.Random's nextInt() from the seed 11 + "uniform".hashCode(), as SortTest.keys draws them.
//
// A run image is little-endian 32-bit words: the magic "SRUN", the kind (0 a sort, 1 the memory run, 2 the trace run),
// the format (0 kv32, 1 k128), the records a tile, the image's arguments' row's address (0: none), the tile count, the
// image's IMEM rows, its DMEM rows, their load address; then the IMEM rows and the DMEM rows, four words a row. The
// trace run's goes on with the tracer tile's image: its IMEM rows, its DMEM rows, their load address, then the rows.
// An image's rows are its ELF file's loaded segments' file bytes, the code's from IMEM's base and the data's from
// TILE_DATA, each padded with zeros to whole 16-byte rows (.bss is not landed: the image's start-up zeroes it).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t RUN_MAGIC = 0x5352554E;                       // "SRUN"
constexpr uint32_t KIND_SORT = 0, KIND_MEMORY = 1, KIND_TRACE = 2;
constexpr uint32_t IMEM_BASE = 0x01000000;                       // machine-spec 5.1
constexpr uint32_t DMEM_FIRST = 0x10;                            // DMEM past its 16 reserved bytes (6.3)
constexpr uint32_t ARGUMENTS = DMEM_FIRST;                       // the tile image's arguments' row (fw/sort/port/mesh.h)
constexpr uint32_t TILE_DATA = ARGUMENTS + 16;                   // a loaded image's data, past that row
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

    // The value of the symbol `name` (from the symbol table the section headers give); false if there is none.
    bool symbol(const char* name, uint32_t& value) const {
        const uint32_t shoff = u32(32);
        const uint16_t shentsize = u16(46), shnum = u16(48);
        for (uint16_t i = 0; i < shnum; i++) {
            const size_t s = size_t(shoff) + size_t(i) * shentsize;
            if (u32(s + 4) != 2) continue;                                 // SHT_SYMTAB
            const uint32_t offset = u32(s + 16), size = u32(s + 20), link = u32(s + 24);
            const size_t strings = size_t(shoff) + size_t(link) * shentsize;
            const uint32_t str_offset = u32(strings + 16), str_size = u32(strings + 20);
            need(str_offset, str_size);
            for (uint32_t at = 0; at + 16 <= size; at += 16) {
                const uint32_t n = u32(offset + at);
                if (n >= str_size) continue;
                const char* s_name = reinterpret_cast<const char*>(data.data()) + str_offset + n;
                if (strnlen(s_name, str_size - n) < str_size - n && std::strcmp(s_name, name) == 0) {
                    value = u32(offset + at + 4);
                    return true;
                }
            }
        }
        return false;
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

// ---- the run images ----

struct Image {   // an image the master loads on a tile (6.5)
    Bytes imem, dmem;   // whole 16-byte rows
    uint32_t dmem_address = 0, arguments = 0;
};

// The bytes the master lands: each loaded segment's file bytes, the code from IMEM's base and the data from
// TILE_DATA, padded to whole rows.
Image loaded_rows(const std::string& path, uint32_t arguments) {
    const Elf elf(path);
    Image image;
    image.dmem_address = TILE_DATA;
    image.arguments = arguments;
    bool code = false, data = false;
    for (const Segment& s : elf.segments) {
        if (s.file_bytes == 0) continue;
        Bytes rows(s.bytes, s.bytes + s.file_bytes);
        rows.resize((rows.size() + 15) / 16 * 16, 0);
        if (s.address == IMEM_BASE && !code) { image.imem = rows; code = true; }
        else if (s.address == TILE_DATA && !data) { image.dmem = rows; data = true; }
        else die(path + ": a loaded segment that is neither the one at IMEM's base nor the one at TILE_DATA");
    }
    if (!code) die(path + ": no code at IMEM's base");
    return image;
}

Bytes run_image(uint32_t kind, uint32_t format, uint32_t records, const Image& image) {
    Bytes out;
    for (uint32_t word : {RUN_MAGIC, kind, format, records, image.arguments, TILES, uint32_t(image.imem.size() / 16),
                          uint32_t(image.dmem.size() / 16), image.dmem_address})
        put32(out, word);
    out.insert(out.end(), image.imem.begin(), image.imem.end());
    out.insert(out.end(), image.dmem.begin(), image.dmem.end());
    return out;
}

Bytes trace_run_image(const Image& replay, const Image& tracer) {
    Bytes out = run_image(KIND_TRACE, 0, 0, replay);
    for (uint32_t word : {uint32_t(tracer.imem.size() / 16), uint32_t(tracer.dmem.size() / 16), tracer.dmem_address})
        put32(out, word);
    out.insert(out.end(), tracer.imem.begin(), tracer.imem.end());
    out.insert(out.end(), tracer.dmem.begin(), tracer.dmem.end());
    return out;
}

// Whether the master's loaded bytes hold `run` at its symbol RUN_IMAGE.
int check_run(const std::string& master_path, const std::string& run_path) {
    const Elf master(master_path);
    const Bytes run = read_file(run_path);
    uint32_t at = 0;
    if (!master.symbol("RUN_IMAGE", at)) die(master_path + ": no symbol RUN_IMAGE");
    for (const Segment& s : master.segments) {
        if (at < s.address || at - s.address > s.file_bytes) continue;
        const uint32_t offset = at - s.address;
        if (run.size() > s.file_bytes - offset || std::memcmp(s.bytes + offset, run.data(), run.size()) != 0) {
            std::printf("DIFFERS: %s at RUN_IMAGE (0x%x) against %s\n", master_path.c_str(), at, run_path.c_str());
            return 1;
        }
        return 0;
    }
    std::printf("DIFFERS: %s: RUN_IMAGE (0x%x) is in no loaded segment\n", master_path.c_str(), at);
    return 1;
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
    } else if (command == "run" && argc == 7 && std::strcmp(argv[2], "sort") == 0) {
        const std::string format = argv[3];
        if (format != "kv32" && format != "k128") die("the format is kv32 or k128");
        write_file(argv[6], run_image(KIND_SORT, format == "k128", number(argv[4]), loaded_rows(argv[5], ARGUMENTS)));
    } else if (command == "run" && argc == 4 && std::strcmp(argv[2], "memory") == 0) {
        write_file(argv[3], run_image(KIND_MEMORY, 0, 0, Image{}));
    } else if (command == "run" && argc == 6 && std::strcmp(argv[2], "trace") == 0) {
        write_file(argv[5], trace_run_image(loaded_rows(argv[3], 0), loaded_rows(argv[4], 0)));
    } else if (command == "check-run" && argc == 4) {
        return check_run(argv[2], argv[3]);
    } else if (command == "loaded" && argc == 4) {
        write_file(argv[3], loaded_dump(argv[2]));
    } else {
        std::fprintf(stderr, "usage: fwimage keys RECORDS OUT\n"
                             "       fwimage run sort kv32|k128 RECORDS TILE.elf OUT\n"
                             "       fwimage run memory OUT\n"
                             "       fwimage run trace REPLAY.elf TRACER.elf OUT\n"
                             "       fwimage check-run MASTER.elf RUN.bin\n"
                             "       fwimage loaded ELF OUT\n");
        return 2;
    }
    return 0;
}
