// file_store.h: the memory as files (spec 4.21.5, 7; DECISIONS-v1.3 sections 12, 20, 22), ported from `ref`'s
// FileStore.java (WORKER14). A simulated LPDDR of 4 KB pages addressed in 128 B units, holding files as trees of
// 1,024-entry index pages whose depth follows the length (0: the data page itself; 1: an index page of data pages; 2 and
// 3 more levels). An index entry is 32 bits: [30:0] the page's address in 128 B units, [31] the owned bit; 0xFFFFFFFF is
// no page (a hole), at every level. The storage only: the shared cache's timing is MemSystem's (mem_system.h).
//
// THE TWO REGIMES (D6). Building an image (create, put_page, cow_open, make_private, set_ownership, copy) is the
// configuration phase and allocates pages. The Tracer's cycle uses only walk, read_line and write_line, which never
// allocate. `ref` keeps its pages in a HashMap by address; addresses are handed out in order from `first`, 32 units
// apart, so here the pages are one dense array indexed by (address - first) / 32.
//
// `ref`'s IllegalStateException and IllegalArgumentException are FATAL here.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace units {
namespace tracer {

class FileStore {
public:
    static constexpr int32_t PAGE_UNITS = 32;  // 4 KB in 128 B units
    static constexpr uint32_t NONE = 0xFFFFFFFFu;
    static constexpr uint32_t OWNED = 0x80000000u;
    enum : int { ST_OK = 0, ST_HOLE = 1, ST_NOT_OWNED = 2, ST_BEYOND = 3 };
    static constexpr int PAGE_WORDS = 512;

    // A file: its root (128 B units) and depth (0-3).
    struct File {
        int32_t root = 0;
        int depth = 0;
        // The depth's capacity in 128 B lines (status 3 beyond it, SPEC-QUESTIONS Q2).
        int64_t capacity_lines() const { return int64_t(1) << (5 + 10 * depth); }
    };

    // One walk of (root, depth, line): the LPDDR lines it reads (index lines, then the data line), its status, the line.
    struct Walk {
        int32_t lines[4] = {};  // LPDDR line addresses (128 B units), in access order
        int n_lines = 0;
        int index_lines = 0;    // how many of them are index lines
        int status = 0;
        int32_t data_line = -1;  // the data line's address, or -1 (a hole, or beyond)
    };

    // first: the first page's address in 128 B units (address 0 is the superblock's, so tests start above it).
    explicit FileStore(int32_t first = PAGE_UNITS);

    size_t pages_allocated() const { return words_.size() / PAGE_WORDS; }

    // ---- configuration: building images ----
    // The depth a file of `bytes` needs (section 20): 0 to 4 KB, 1 to 4 MB, 2 to 4 GB, 3 to 4 TB.
    static int depth_for(int64_t bytes);
    // A new, empty file of the given depth that owns its root (depth 0: one data page).
    File create(int depth);
    // Writes a whole 4 KB page (512 words) into f at page number pg as its creator, allocating index pages and the data
    // page as needed. For building images, before any copy shares them.
    void put_page(File f, int64_t pg, const uint64_t* data);
    // Section 20's cow_open: a new file sharing everything with src but its root, copied with the owned bits cleared.
    File cow_open(File src);
    // Makes page pg of copy f private by path copying (section 20); the page's content is unchanged (the pre-copy of
    // section 22). A hole on the path is FATAL.
    void make_private(File f, int64_t pg);
    // The one-memory acceptance: every entry of f's tree has its owned bit cleared, then each page in `written`, in the
    // order given, is created as a zero page if absent (with the index pages its path needs) and every entry on its path
    // marked owned. Returns the pages created.
    int set_ownership(File f, const std::vector<int64_t>& written);
    // Every data page of f, by page number (tools and checks).
    void for_each_page(File f, const std::function<void(int64_t, const uint64_t*)>& fn) const;

    // ---- the walk (the translator's, spec 7, 4.21.5): the cycle loop's operations, no allocation ----
    // Walks the tree to `line`: ok only if every entry on the path is owned (Q1); a hole at any level stops the walk (the
    // line reads as zero); beyond the depth's capacity is status 3 at once (Q2).
    void walk(int32_t root, int depth, int64_t line, Walk& w) const;
    // The 16 words of an LPDDR line (a data line found by a walk).
    void read_line(int32_t line_addr, uint64_t* out) const;
    void write_line(int32_t line_addr, const uint64_t* in);
    // The doubleword at byte offset off of file f (0 in a hole): tools and tests.
    uint64_t load64(File f, int64_t off) const;

    // ---- restoring a store page by page (a test's replay of `ref`'s store): configuration only
    int32_t first() const { return first_; }
    int64_t next_free() const { return next_free_; }
    // New zero pages up to next_free (pages are only ever added, in address order).
    void grow_to(int64_t next_free);
    uint64_t* raw_page(int32_t a) { return page(a); }
    const uint64_t* raw_page(int32_t a) const { return page(a); }

    static uint32_t entry_of(const uint64_t* index, int i) { return uint32_t(index[i >> 1] >> (32 * (i & 1))); }
    static void set_entry(uint64_t* index, int i, uint32_t e) {
        const int s = 32 * (i & 1);
        index[i >> 1] = (index[i >> 1] & ~(uint64_t(0xFFFFFFFFu) << s)) | (uint64_t(e) << s);
    }
    static int32_t addr(uint32_t e) { return int32_t(e & 0x7FFFFFFFu); }

private:
    int32_t alloc(bool index);
    uint64_t* page(int32_t a);
    const uint64_t* page(int32_t a) const;
    void clear_owned(int32_t a, int levels);
    void for_each(int32_t a, int levels, int64_t prefix, const std::function<void(int64_t, const uint64_t*)>& fn) const;
    static int digit(int64_t pg, int level, int depth) { return int(uint64_t(pg) >> (10 * (depth - 1 - level))) & 1023; }

    int32_t first_;
    int64_t next_free_;
    std::vector<uint64_t> words_;  // page k (address first_ + 32 k) at words_[512 k]
};

}  // namespace tracer
}  // namespace units
