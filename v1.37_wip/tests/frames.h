// frames.h: the host's frames, read (v1.37; ../v1.4_wip/planning/HOST-FRAMES.md, "HF"): a frame file as gold writes it
// (../updater_wip/pipeline/gold: `frames`), its frames one after another, each a header word of 128 bits (the
// compression in [7:0], the type in [95:64], the length in 128-bit words in [127:96]) and its data. Written from HF, for
// the tests: what the master tile's program must find in each frame, and what the Updater must make of them.
//
// Test-side code (the configuration regime): the STL throughout.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace frames {

enum : uint32_t { PROGRAM_HEADER = 1, PROGRAM_DATA = 2, PROOF_HEADER = 3, TABLE_HEADER = 4, TABLE_DATA = 5, DATA_IMAGE = 6,
                  REGISTERS = 7, ADVICE = 8, SEGMENTS = 9 };
constexpr unsigned RECORD_WORDS = 68;

struct Frame {
    uint32_t type = 0, compression = 0, length = 0;   // length: the data's 128-bit words
    size_t at = 0;                                    // the header word's byte in the file; the data follows it
    size_t data() const { return at + 16; }
};

struct ProgramHeader {                                // frame 1
    std::string stem;
    uint32_t file_pages = 0, words64 = 0, words128 = 0, pages = 0;
};
struct TableHeader {                                  // frame 4
    std::string path, base;
    uint32_t k = 0, p1 = 0, p3 = 0, words64 = 0, words128 = 0, pages = 0;
};
struct ProofHeader {                                  // frame 3
    uint32_t version = 0;
    std::string stem, workdir;
    std::vector<uint64_t> fields;                     // log2 K, lowest_address, then a16z's MemoryLayout
    uint32_t data_words64 = 0, data_words128 = 0, data_pages = 0;
    std::vector<uint32_t> segment_words64;            // N
    uint32_t segments_words128 = 0;
    std::vector<uint32_t> pages_written;              // N
    std::vector<std::array<uint32_t, 3>> per_asic;    // N: the start image's pages, the final image's, the Tracer's copy's
};

struct File {
    std::vector<uint8_t> bytes;
    std::vector<Frame> frame;
    // The file read and cut into frames; false and `err` if it is not whole frames.
    bool read(const std::string& path, std::string& err);
    bool parse(std::string& err);                     // `bytes` already filled
    // 128-bit word w of frame f's data, as two 64-bit halves.
    uint64_t lo(const Frame& f, size_t w) const { return u64(f.data() + 16 * w); }
    uint64_t hi(const Frame& f, size_t w) const { return u64(f.data() + 16 * w + 8); }
    uint32_t u32(size_t at) const { uint32_t v = 0; for (unsigned i = 0; i < 4; i++) v |= uint32_t(bytes[at + i]) << (8 * i); return v; }
    uint64_t u64(size_t at) const { return uint64_t(u32(at)) | uint64_t(u32(at + 4)) << 32; }

    // The header frames. `asics` is N, the system's configuration (in no frame). False if the frame is not as HF says.
    bool program_header(const Frame& f, ProgramHeader& out) const;
    bool table_header(const Frame& f, TableHeader& out) const;
    bool proof_header(const Frame& f, unsigned asics, ProofHeader& out) const;
    // The frames of a kind, in the file's order.
    std::vector<const Frame*> of(uint32_t type) const;
};

// A file's depth from its length in 4 KB pages: the smallest d >= 1 with 1,024^d >= the length (a file always has an
// index level: gold's choice, the second design note's proposal).
int depth_of(uint64_t pages);
// A jump table's length in pages: 2^k lines of 128 bytes, at least one page.
uint64_t table_pages(uint32_t k);

// The update words of an update-word frame (2, 5, 6, 9), decoded: HF's "Update words", raw or LRU. `count` 64-bit words.
// A second decoder, apart from the Updater's, for the tests' expectations.
std::vector<uint64_t> update_words(const File& file, const Frame& f, size_t count);

}  // namespace frames
