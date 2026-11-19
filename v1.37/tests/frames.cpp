// frames.cpp: frames.h's reader.
#include "frames.h"

#include <cstdio>

namespace frames {

namespace {

// A cursor over a frame's data as 32-bit words (every field of the header frames is one, or two; HF).
struct Words {
    const File& file;
    size_t at, end;
    bool ok = true;
    uint32_t u32() { if (at + 4 > end) { ok = false; return 0; } const uint32_t v = file.u32(at); at += 4; return v; }
    uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    // A string: a u32 length in bytes, then the bytes, padded to a whole number of 32-bit words.
    std::string string() {
        const uint32_t n = u32();
        const size_t padded = (size_t(n) + 3) / 4 * 4;
        if (!ok || at + padded > end) { ok = false; return ""; }
        std::string s(reinterpret_cast<const char*>(&file.bytes[at]), n);
        at += padded;
        return s;
    }
    // What is left is the frame's padding to 128 bits: under 16 bytes, all zero.
    bool at_end() {
        if (!ok || end - at >= 16) return false;
        for (size_t i = at; i < end; i++) if (file.bytes[i] != 0) return false;
        return true;
    }
};

}  // namespace

bool File::read(const std::string& path, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot read " + path; return false; }
    bytes.clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    if (!parse(err)) { err = path + ": " + err; return false; }
    return true;
}

bool File::parse(std::string& err) {
    frame.clear();
    for (size_t at = 0; at < bytes.size();) {
        if (at + 16 > bytes.size()) { err = "a frame's header word past the file's end"; return false; }
        Frame f;
        f.at = at;
        f.compression = uint32_t(bytes[at]);
        for (size_t i = at + 1; i < at + 8; i++) if (bytes[i] != 0) { err = "a frame header's reserved bits"; return false; }
        f.type = u32(at + 8); f.length = u32(at + 12);
        if (f.type < 1 || f.type > 9 || f.compression > 1) { err = "a frame's type or compression"; return false; }
        if (at + 16 + 16 * size_t(f.length) > bytes.size()) { err = "a frame's data past the file's end"; return false; }
        frame.push_back(f);
        at += 16 + 16 * size_t(f.length);
    }
    return true;
}

std::vector<const Frame*> File::of(uint32_t type) const {
    std::vector<const Frame*> out;
    for (const Frame& f : frame) if (f.type == type) out.push_back(&f);
    return out;
}

bool File::program_header(const Frame& f, ProgramHeader& out) const {
    Words w{*this, f.data(), f.data() + 16 * size_t(f.length)};
    out.stem = w.string();
    out.file_pages = w.u32(); out.words64 = w.u32(); out.words128 = w.u32(); out.pages = w.u32();
    return f.type == PROGRAM_HEADER && w.at_end();
}

bool File::table_header(const Frame& f, TableHeader& out) const {
    Words w{*this, f.data(), f.data() + 16 * size_t(f.length)};
    out.path = w.string(); out.base = w.string();
    out.k = w.u32(); out.p1 = w.u32(); out.p3 = w.u32();
    out.words64 = w.u32(); out.words128 = w.u32(); out.pages = w.u32();
    return f.type == TABLE_HEADER && w.at_end();
}

bool File::proof_header(const Frame& f, unsigned asics, ProofHeader& out) const {
    Words w{*this, f.data(), f.data() + 16 * size_t(f.length)};
    out.version = w.u32();
    out.stem = w.string(); out.workdir = w.string();
    const uint32_t n = w.u32();
    out.fields.clear();
    for (uint32_t i = 0; i < n && w.ok; i++) out.fields.push_back(w.u64());
    out.data_words64 = w.u32(); out.data_words128 = w.u32(); out.data_pages = w.u32();
    out.segment_words64.assign(asics, 0);
    for (uint32_t& x : out.segment_words64) x = w.u32();
    out.segments_words128 = w.u32();
    out.pages_written.assign(asics, 0);
    for (uint32_t& x : out.pages_written) x = w.u32();
    out.per_asic.assign(asics, {});
    for (auto& a : out.per_asic) for (uint32_t& x : a) x = w.u32();
    return f.type == PROOF_HEADER && w.at_end();
}

int depth_of(uint64_t pages) {
    int d = 1;
    for (uint64_t reach = 1024; reach < pages; reach *= 1024) d++;
    return d;
}
uint64_t table_pages(uint32_t k) {
    const uint64_t pages = (uint64_t(128) << k) / 4096;
    return pages ? pages : 1;
}

std::vector<uint64_t> update_words(const File& file, const Frame& f, size_t count) {
    std::vector<uint64_t> out;
    out.reserve(count);
    const size_t at = f.data(), bits = size_t(f.length) * 128;
    size_t pos = 0;
    auto take = [&](unsigned n) {
        uint64_t v = 0;
        for (unsigned i = 0; i < n; i++, pos++)
            if (pos < bits && (file.bytes[at + pos / 8] >> (pos % 8) & 1)) v |= uint64_t(1) << i;
        return v;
    };
    if (f.compression == 0) {
        for (size_t i = 0; i < count; i++) out.push_back(take(64));
        return out;
    }
    uint64_t table[32] = {};
    for (size_t i = 0; i < count; i++) {
        uint64_t v;
        unsigned shift;                                   // the entries that move back by one
        if (take(1)) { const unsigned index = unsigned(take(5)); v = table[index]; shift = index; }
        else { v = take(1) ? take(64) : take(32); shift = 31; }
        for (unsigned k = shift; k > 0; k--) table[k] = table[k - 1];
        table[0] = v;
        out.push_back(v);
    }
    return out;
}

}  // namespace frames
