// rtl_images.h: the RTL's firmware images as the simulator's tests read them (fw/rtl/: the ELF files and lists that
// Snarky One's `mise run core-images` builds, vendored unchanged; fw/rtl/boot/README.md and fw/rtl/tile/README.md say
// from where). Configuration regime only.
//
//   boot ROM      fw/rtl/boot/boot_rom.hex, one 32-bit word a line, and boot_rom.sym, `nm`'s lines (the handler's address,
//                 mtvec's reset value: machine-spec 6.2)
//   an ELF file   its loadable segments as 16-byte rows, as the RTL's tests land them (rtl/tile/base/test/src/
//                 tile_programs.rs's program_rows): for each segment, each row of its memory image at its address, its
//                 file bytes then zeros (to its memory size, and to the row's end past it)
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "base/fatal.h"

namespace rtl_images {

inline std::string read_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) FATAL("rtl_images: cannot read %s (run from the version's folder)", path.c_str());
    std::string b;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) b.append(buf, n);
    std::fclose(f);
    return b;
}

// A words file: one 32-bit word a line, in hexadecimal.
inline std::vector<uint32_t> hex_words(const std::string& path) {
    const std::string b = read_file(path);
    std::vector<uint32_t> w;
    for (size_t at = 0; at < b.size();) {
        size_t e = b.find('\n', at);
        if (e == std::string::npos) e = b.size();
        if (e > at) w.push_back(uint32_t(std::stoul(b.substr(at, e - at), nullptr, 16)));
        at = e + 1;
    }
    return w;
}

// A symbol's address in an `nm` listing ("02000068 T handler").
inline uint32_t symbol(const std::string& path, const std::string& name) {
    const std::string b = read_file(path);
    for (size_t at = 0; at < b.size();) {
        size_t e = b.find('\n', at);
        if (e == std::string::npos) e = b.size();
        const std::string line = b.substr(at, e - at);
        const size_t sp = line.rfind(' ');
        if (sp != std::string::npos && line.substr(sp + 1) == name) return uint32_t(std::stoul(line.substr(0, line.find(' ')), nullptr, 16));
        at = e + 1;
    }
    FATAL("rtl_images: no symbol %s in %s", name.c_str(), path.c_str());
}

// The boot ROM (6.5): its words, and its handler's byte offset in it.
struct BootRom {
    std::vector<uint32_t> words;
    uint32_t handler = 0;
};
inline BootRom boot_rom(const std::string& dir = "fw/rtl/boot") {
    BootRom r;
    r.words = hex_words(dir + "/boot_rom.hex");
    const uint32_t base = symbol(dir + "/boot_rom.sym", "_start");
    r.handler = symbol(dir + "/boot_rom.sym", "handler") - base;
    return r;
}

// A row a load lands: its address and its 16 bytes as two 64-bit words, little-endian.
struct Row {
    uint32_t address;
    uint64_t lo, hi;
};

inline uint32_t field(const std::string& file, size_t offset) {
    if (offset + 4 > file.size()) FATAL("rtl_images: an ELF field at %zu past the file's %zu bytes", offset, file.size());
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) v |= uint32_t(uint8_t(file[offset + i])) << (8 * i);
    return v;
}

// The rows of an ELF file's loadable segments.
inline std::vector<Row> program_rows(const std::string& path) {
    const std::string file = read_file(path);
    if (file.size() < 52 || field(file, 0) != 0x464c457f) FATAL("rtl_images: %s is not an ELF file", path.c_str());
    const size_t table = field(file, 28);
    const unsigned count = uint8_t(file[44]) | unsigned(uint8_t(file[45])) << 8;
    std::vector<Row> rows;
    for (unsigned index = 0; index < count; index++) {
        const size_t header = table + 32 * index;
        const uint32_t kind = field(file, header), offset = field(file, header + 4), address = field(file, header + 12);
        const uint32_t file_bytes = field(file, header + 16), memory_bytes = field(file, header + 20);
        if (kind != 1 || memory_bytes == 0) continue;                  // PT_LOAD
        if (address % 16 != 0) FATAL("rtl_images: %s: a segment at 0x%x, not on a row", path.c_str(), address);
        for (uint32_t row = 0; row < (memory_bytes + 15) / 16; row++) {
            Row r{address + 16 * row, 0, 0};
            for (unsigned lane = 0; lane < 16; lane++) {
                const uint32_t at = 16 * row + lane;
                const uint64_t byte = at < file_bytes ? uint8_t(file[offset + at]) : 0;
                (lane < 8 ? r.lo : r.hi) |= byte << (8 * (lane & 7));
            }
            rows.push_back(r);
        }
    }
    return rows;
}

// A list of programs (programs.txt): a line per program, its kind and its ELF file's name in the list's directory.
inline std::vector<std::string> program_list(const std::string& dir, unsigned kinds) {
    const std::string b = read_file(dir + "/programs.txt");
    std::vector<std::string> files(kinds);
    for (size_t at = 0; at < b.size();) {
        size_t e = b.find('\n', at);
        if (e == std::string::npos) e = b.size();
        if (e > at) {
            const std::string line = b.substr(at, e - at);
            const size_t sp = line.find(' ');
            if (sp == std::string::npos) FATAL("rtl_images: %s/programs.txt: a bad line: %s", dir.c_str(), line.c_str());
            const unsigned kind = unsigned(std::stoul(line.substr(0, sp)));
            if (kind >= kinds || !files[kind].empty()) FATAL("rtl_images: %s/programs.txt: kind %u unknown or twice", dir.c_str(), kind);
            files[kind] = dir + "/" + line.substr(sp + 1);
        }
        at = e + 1;
    }
    for (unsigned k = 0; k < kinds; k++) if (files[k].empty()) FATAL("rtl_images: %s/programs.txt: kind %u not listed", dir.c_str(), k);
    return files;
}

}  // namespace rtl_images
