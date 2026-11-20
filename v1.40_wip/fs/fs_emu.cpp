// fs_emu.cpp: the file system of FS-API.md, entirely in software (v1.40, step 3's start). See fs_emu.h.
//
// What lives where, as on the chip:
//   LPDDR (persists across a soft reset): page 0 the superblock (4.4); directories, streamed files and COW files as
//     trees of pages (23.5); directory entries of 128 bytes (4.3).
//   the memory tile's scratchpad (cleared at every reset): the two page maps (4.1), the fd table (4.6), the writers'
//     buffers (4.6), the reservations (4.1).
// Every access to LPDDR is a whole page or a whole 128-byte line, as the RW unit would make it, and is counted.
#include "fs_emu.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

namespace fsemu {
namespace {

// ------------------------------------------------------------------ fatal: a breach of a caller's rule (UB)
[[noreturn]] void fatal(const char* fmt, ...);

// ------------------------------------------------------------------ LPDDR
std::vector<uint8_t> g_lpddr;          // the pages, 4 KiB each
uint32_t g_pages = 0;                  // LPDDR's size in pages
std::vector<uint8_t> g_incache;        // per page: lines of it may be in the cache (the cache's truth, 23.6)
Stats g_stats;
bool g_tracing = false;

uint8_t* lp(uint32_t unit) {
    if (unit == NONE || uint64_t(unit) * LINE_BYTES >= g_lpddr.size()) fatal("LPDDR address 0x%x out of range", unit);
    return &g_lpddr[size_t(unit) * LINE_BYTES];
}
uint32_t pgno(uint32_t unit) { return unit / PAGE_UNITS; }
void page_write(uint32_t unit, const uint8_t* src) { std::memcpy(lp(unit), src, PAGE_BYTES); g_stats.lpddr_page_writes++; }
void page_read(uint32_t unit, uint8_t* dst) { std::memcpy(dst, lp(unit), PAGE_BYTES); g_stats.lpddr_page_reads++; }
void line_write(uint32_t unit, const uint8_t* src) { std::memcpy(lp(unit), src, LINE_BYTES); g_stats.lpddr_line_writes++; }
void line_read(uint32_t unit, uint8_t* dst) { std::memcpy(dst, lp(unit), LINE_BYTES); g_stats.lpddr_line_reads++; }

uint32_t get32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t get64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

// ------------------------------------------------------------------ the scratchpad: the page maps (4.1)
// Page p is bit p: 128 pages to a 128-bit word, as v1.37's master_layout.h has its map.
std::vector<uint32_t> g_taken, g_mbc;  // "taken", "may be cached"
uint32_t g_cursor = 1;                 // where the next search for a free page starts
bool bit(const std::vector<uint32_t>& m, uint32_t p) { return (m[p >> 5] >> (p & 31)) & 1u; }
void setbit(std::vector<uint32_t>& m, uint32_t p, bool v) { if (v) m[p >> 5] |= 1u << (p & 31); else m[p >> 5] &= ~(1u << (p & 31)); }

// ------------------------------------------------------------------ the scratchpad: the fd table (4.6)
constexpr int MAX_FDS = 512, FD_BYTES = 16, MAX_SLOTS = 32, MAX_RES = 8;
uint8_t g_fdtab[MAX_FDS * FD_BYTES];
// word 0: [19:0] root's page number (0: none); [21:20] depth; [24:22] mode; [25] COW; [26] in use; [31:27] slot
// word 1: length (bytes; pages for a COW file); word 2: the entry's address in units; word 3: the read position
struct Fd {
    uint32_t root_pg, depth, mode, cow, inuse, slot, length, entry, pos;
};
Fd fd_get(int fd) {
    const uint8_t* e = g_fdtab + fd * FD_BYTES;
    const uint32_t w0 = get32(e);
    return Fd{w0 & 0xFFFFFu, (w0 >> 20) & 3u, (w0 >> 22) & 7u, (w0 >> 25) & 1u, (w0 >> 26) & 1u, (w0 >> 27) & 31u,
              get32(e + 4), get32(e + 8), get32(e + 12)};
}
void fd_put(int fd, const Fd& f) {
    uint8_t* e = g_fdtab + fd * FD_BYTES;
    if (f.root_pg > 0xFFFFFu) fatal("a root past 2^20 pages");
    put32(e, f.root_pg | f.depth << 20 | f.mode << 22 | f.cow << 25 | f.inuse << 26 | f.slot << 27);
    put32(e + 4, f.length); put32(e + 8, f.entry); put32(e + 12, f.pos);
}
uint32_t root_unit(uint32_t root_pg) { return root_pg ? root_pg * PAGE_UNITS : NONE; }

// A writer's buffer (4.6): the data page it is filling, and the current line of each index level.
struct IndexLine { uint32_t page = NONE, line = 0; bool dirty = false; uint8_t bytes[LINE_BYTES]; };
struct Slot {
    bool used = false;
    uint32_t data_pg = NONE;           // the file's page number held, NONE none
    bool data_dirty = false;
    uint8_t data[PAGE_BYTES];
    IndexLine lines[3];
};
Slot g_slots[MAX_SLOTS];

// The reservations (4.1; a list each, † as against a third map).
struct Reservation { bool used = false; std::vector<uint32_t> pages; };
Reservation g_res[MAX_RES];

bool g_shutdown = false;
Caller g_caller = Caller::MASTER;
int g_tile = 0;
FILE* g_log = nullptr;

const char* who() {
    static char b[16];
    switch (g_caller) {
        case Caller::MASTER: return "master";
        case Caller::TRACER: return "tracer";
        case Caller::SHELL: return "shell";
        case Caller::TILE: std::snprintf(b, sizeof b, "tile %d", g_tile); return b;
    }
    return "?";
}
void logf(const char* fmt, ...) {
    if (!g_log) return;
    std::fprintf(g_log, "[%s] ", who());
    va_list a; va_start(a, fmt); std::vfprintf(g_log, fmt, a); va_end(a);
    std::fputc('\n', g_log);
}
[[noreturn]] void fatal(const char* fmt, ...) {
    char b[512];
    va_list a; va_start(a, fmt); std::vsnprintf(b, sizeof b, fmt, a); va_end(a);
    std::fprintf(stderr, "fs_emu: UB, by %s: %s\n", who(), b);
    if (g_log) { std::fprintf(g_log, "UB, by %s: %s\n", who(), b); std::fflush(g_log); }
    std::exit(2);
}
void master_only(const char* call) {
    if (g_caller != Caller::MASTER) fatal("%s is the master tile's alone (FS-API 5.1, 5.3, 5.4, 6)", call);
}

// ------------------------------------------------------------------ handing out and freeing pages (4.2)
void discard(uint32_t p) {
    g_stats.discards++;
    if (g_tracing) g_stats.discards_while_tracing++;
    g_incache[p] = 0;
    setbit(g_mbc, p, false);
}
// A page leaves the free map: discarded first if it may be cached; then it must be new (23.6.8).
void take(uint32_t p) {
    if (bit(g_taken, p)) fatal("page %u taken twice", p);
    if (bit(g_mbc, p)) discard(p);
    if (g_incache[p]) fatal("page %u handed out with lines in the cache and no discard (23.6.8)", p);
    setbit(g_taken, p, true);
    g_stats.pages_taken++;
}
void free_page(uint32_t p, bool cow) {
    if (!bit(g_taken, p) || p == 0) fatal("page %u freed but not taken", p);
    setbit(g_taken, p, false);
    if (cow) setbit(g_mbc, p, true);
    g_stats.pages_freed++;
}
uint32_t free_count() {
    uint32_t n = 0;
    for (uint32_t p = 0; p < g_pages; p++) n += !bit(g_taken, p);
    return n;
}
// The file system's own page (a directory's, a streamed file's): a free page not in the "may be cached" map if there
// is one, so that nothing is discarded while the Tracer runs; else any free page, discarded. NONE if none.
uint32_t alloc_own() {
    uint32_t fallback = NONE;
    for (uint32_t k = 0; k < g_pages; k++) {
        const uint32_t p = (g_cursor + k) % g_pages;
        if (p == 0 || bit(g_taken, p)) continue;
        if (bit(g_mbc, p)) { if (fallback == NONE) fallback = p; continue; }
        take(p); g_cursor = p + 1; return p * PAGE_UNITS;
    }
    if (fallback == NONE) return NONE;
    take(fallback); return fallback * PAGE_UNITS;
}

// ------------------------------------------------------------------ trees (23.5)
uint32_t digit(uint64_t pg, uint32_t level, uint32_t depth) { return uint32_t(pg >> (10 * (depth - 1 - level))) & 1023u; }
uint32_t depth_for(uint64_t pg) { return pg == 0 ? 0 : pg < 1024 ? 1 : pg < (1u << 20) ? 2 : 3; }  // to hold page pg

// How a tree's entries are read and written: directly in LPDDR (a directory, a file being read, a walk), or through a
// writer's buffer (4.6), which holds one line of each level and writes it back when another is needed or at a flush.
struct Entries {
    Slot* slot = nullptr;              // nullptr: directly
    uint32_t get(uint32_t level, uint32_t page, uint32_t i) {
        if (slot) {
            IndexLine& l = slot->lines[level];
            if (l.page == page && l.line == i / 32) return get32(l.bytes + 4 * (i % 32));
        }
        uint8_t b[LINE_BYTES];
        line_read(page + i / 32, b);
        return get32(b + 4 * (i % 32));
    }
    void set(uint32_t level, uint32_t page, uint32_t i, uint32_t e) {
        if (!slot) {
            uint8_t b[LINE_BYTES];
            line_read(page + i / 32, b); put32(b + 4 * (i % 32), e); line_write(page + i / 32, b);
            return;
        }
        IndexLine& l = slot->lines[level];
        if (l.page != page || l.line != i / 32) {
            if (l.page != NONE && l.dirty) line_write(l.page + l.line, l.bytes);
            l.page = page; l.line = i / 32; l.dirty = false;
            line_read(page + l.line, l.bytes);
        }
        put32(l.bytes + 4 * (i % 32), e);
        l.dirty = true;
    }
};
void flush_lines(Slot& s) {
    for (IndexLine& l : s.lines) {
        if (l.page != NONE && l.dirty) line_write(l.page + l.line, l.bytes);
        l.dirty = false;
    }
}
void forget_lines(Slot& s) { for (IndexLine& l : s.lines) { l.page = NONE; l.dirty = false; } }

// A new index page, all holes, written whole (a new page may be: 23.6.8); entry 0 first0 if not NONE.
uint32_t new_index_page(uint32_t first0) {
    const uint32_t a = alloc_own();
    if (a == NONE) return NONE;
    uint8_t b[PAGE_BYTES];
    std::memset(b, 0xFF, sizeof b);
    if (first0 != NONE) put32(b, first0);
    page_write(a, b);
    return a;
}
// Page pg of a streamed file or directory, to be written with data (a page, or zeros): the tree grown to reach it, the
// index pages on its path made, the data page made if it is a hole, the data written, and only then the entry that
// names it (the data is in LPDDR before an entry names it). The root and depth may change. NONE: no room.
uint32_t put_page(Entries& en, uint32_t& root, uint32_t& depth, uint64_t pg, const uint8_t* data) {
    const uint32_t need = depth_for(pg);
    if (root == NONE) depth = need;
    else while (depth < need) {                                     // grow: a new root above, the old its entry 0
        const uint32_t r = new_index_page(root | OWNED);
        if (r == NONE) return NONE;
        root = r; depth++;
        if (en.slot) { flush_lines(*en.slot); forget_lines(*en.slot); }   // the levels move down a place
    }
    static const uint8_t zeros[PAGE_BYTES] = {};
    if (depth == 0) {
        if (root == NONE) { root = alloc_own(); if (root == NONE) return NONE; }
        page_write(root, data ? data : zeros);
        return root;
    }
    if (root == NONE) { root = new_index_page(NONE); if (root == NONE) return NONE; }
    uint32_t page = root;
    for (uint32_t L = 0; L < depth; L++) {
        const uint32_t i = digit(pg, L, depth);
        const uint32_t e = en.get(L, page, i);
        if (L + 1 < depth) {
            if (e == NONE) {
                const uint32_t n = new_index_page(NONE);
                if (n == NONE) return NONE;
                en.set(L, page, i, n | OWNED);
                page = n;
            } else page = e & ~OWNED;
        } else {
            if (e == NONE) {
                const uint32_t d = alloc_own();
                if (d == NONE) return NONE;
                page_write(d, data ? data : zeros);
                en.set(L, page, i, d | OWNED);
                return d;
            }
            page_write(e & ~OWNED, data ? data : zeros);
            return e & ~OWNED;
        }
    }
    return NONE;
}
// Page pg's data page, or NONE for a hole or past the tree.
uint32_t find_page(Entries& en, uint32_t root, uint32_t depth, uint64_t pg) {
    if (root == NONE) return NONE;
    if (depth == 0) return pg == 0 ? root : NONE;
    if (pg >> (10 * depth)) return NONE;
    uint32_t page = root;
    for (uint32_t L = 0; L < depth; L++) {
        const uint32_t e = en.get(L, page, digit(pg, L, depth));
        if (e == NONE) return NONE;
        page = e & ~OWNED;
    }
    return page;
}
// The walk (4.4): the root and every page an owned entry reaches, index pages read whole, data pages never read.
void walk(uint32_t root, uint32_t depth, const std::function<void(uint32_t)>& fn) {
    if (root == NONE) return;
    fn(root);
    std::function<void(uint32_t, uint32_t)> rec = [&](uint32_t page, uint32_t level) {
        uint8_t b[PAGE_BYTES];
        page_read(page, b);
        for (uint32_t i = 0; i < 1024; i++) {
            const uint32_t e = get32(b + 4 * i);
            if (e == NONE || !(e & OWNED)) continue;
            fn(e & ~OWNED);
            if (level + 1 < depth) rec(e & ~OWNED, level + 1);
        }
    };
    if (depth > 0) rec(root, 0);
}

// ------------------------------------------------------------------ directory entries (4.3) and the superblock (4.4)
// An entry, as bytes: 0-95 name; 96 length u64; 104 root u32; 108 type u32; 112 count u32; 116 source u32; 120 flags u32
// ([0] frozen, [1] COW, [3:2] depth); 124 reserved.
constexpr uint32_t T_FREE = 0, T_DIR = 1, T_FILE = 2;
constexpr uint32_t E_FROZEN = 1, E_COW = 2;
struct Ent {
    char name[96]; uint64_t length; uint32_t root, type, count, source, flags;
    uint32_t depth() const { return (flags >> 2) & 3; }
};
Ent ent_decode(const uint8_t* b) {
    Ent e; std::memcpy(e.name, b, 96);
    e.length = get64(b + 96); e.root = get32(b + 104); e.type = get32(b + 108);
    e.count = get32(b + 112); e.source = get32(b + 116); e.flags = get32(b + 120);
    return e;
}
void ent_encode(const Ent& e, uint8_t* b) {
    std::memset(b, 0, LINE_BYTES);
    std::memcpy(b, e.name, 96);
    put64(b + 96, e.length); put32(b + 104, e.root); put32(b + 108, e.type);
    put32(b + 112, e.count); put32(b + 116, e.source); put32(b + 120, e.flags);
}
Ent ent_read(uint32_t a) { uint8_t b[LINE_BYTES]; line_read(a, b); return ent_decode(b); }
void ent_write(uint32_t a, const Ent& e) { uint8_t b[LINE_BYTES]; ent_encode(e, b); line_write(a, b); }
Ent ent_new(const char* name, uint32_t type) {
    Ent e; std::memset(&e, 0, sizeof e);
    std::strncpy(e.name, name, 95);
    e.root = NONE; e.type = type; e.source = NONE;
    return e;
}
void set_depth(Ent& e, uint32_t d) { e.flags = (e.flags & ~0xCu) | d << 2; }

constexpr uint32_t ROOT_ENTRY = 1;     // the root directory's entry: page 0, line 1
const char MAGIC[8] = {'m', 'e', 's', 'h', 'f', 's', '0', '1'};

// A directory's entry slot idx, as an address in units; NONE past its length.
uint32_t dir_slot(const Ent& d, uint32_t idx) {
    if (uint64_t(idx) * LINE_BYTES >= d.length) return NONE;
    Entries en;
    const uint32_t pg = find_page(en, d.root, d.depth(), idx / 32);
    return pg == NONE ? NONE : pg + idx % 32;
}

// ------------------------------------------------------------------ paths
int split(const char* path, std::vector<std::string>& parts) {
    if (!path || path[0] != '/') return FS_EINVAL;
    const size_t n = std::strlen(path);
    if (n > 1023) return FS_ENAMETOOLONG;
    if (n == 1) return 0;
    if (path[n - 1] == '/') return FS_EINVAL;
    std::string cur;
    for (size_t i = 1; i <= n; i++) {
        if (i == n || path[i] == '/') {
            if (cur.empty() || cur == "." || cur == "..") return FS_EINVAL;   // not normalized
            if (cur.size() > 95) return FS_ENAMETOOLONG;
            parts.push_back(cur); cur.clear();
        } else cur += path[i];
    }
    return parts.size() > 10 ? FS_ENAMETOOLONG : 0;
}
// The entry named name in directory d: its address, or NONE.
uint32_t dir_find(const Ent& d, const std::string& name) {
    for (uint32_t i = 0;; i++) {
        const uint32_t a = dir_slot(d, i);
        if (a == NONE) return NONE;
        const Ent e = ent_read(a);
        if (e.type != T_FREE && name == e.name) return a;
    }
}
// Resolves a path: the entry's address, or NONE with *parent the directory it would go in and *last its name.
int resolve(const char* path, uint32_t* addr, uint32_t* parent = nullptr, std::string* last = nullptr) {
    std::vector<std::string> parts;
    if (int r = split(path, parts)) return r;
    uint32_t a = ROOT_ENTRY, par = NONE;
    for (size_t k = 0; k < parts.size(); k++) {
        const Ent d = ent_read(a);
        if (d.type != T_DIR) return FS_ENOTDIR;
        par = a;
        a = dir_find(d, parts[k]);
        if (a == NONE) {
            if (k + 1 < parts.size()) return FS_ENOENT;
            if (parent) *parent = par;
            if (last) *last = parts[k];
            *addr = NONE;
            return 0;
        }
    }
    if (parent) *parent = par;
    if (last && !parts.empty()) *last = parts.back();
    *addr = a;
    return 0;
}
// A new entry in directory dir_a: a free slot if the count says there is one, else a slot appended. Its address, or
// NONE for no room. The directory's count is raised.
uint32_t dir_add(uint32_t dir_a, const Ent& e) {
    Ent d = ent_read(dir_a);
    uint32_t a = NONE;
    if (d.count < d.length / LINE_BYTES) {
        for (uint32_t i = 0; a == NONE; i++) {
            const uint32_t s = dir_slot(d, i);
            if (s == NONE) fatal("a directory's count is below its free slots");
            if (ent_read(s).type == T_FREE) a = s;
        }
    } else {
        const uint32_t idx = uint32_t(d.length / LINE_BYTES);
        if (idx % 32 == 0) {                                        // a new page of entries, all free
            Entries en; uint32_t root = d.root, depth = d.depth();
            const uint32_t pg = put_page(en, root, depth, idx / 32, nullptr);
            if (pg == NONE) return NONE;
            d.root = root; set_depth(d, depth);
            a = pg;
        } else a = dir_slot(Ent{d}, idx - 1) + 1;
        d.length += LINE_BYTES;
    }
    ent_write(a, e);
    d.count++;
    ent_write(dir_a, d);
    return a;
}
void dir_drop(uint32_t parent, uint32_t a) {
    Ent free_e; std::memset(&free_e, 0, sizeof free_e);
    ent_write(a, free_e);
    Ent d = ent_read(parent);
    d.count--;
    ent_write(parent, d);
}

// ------------------------------------------------------------------ open files
int fd_find_entry(uint32_t entry, bool writers_only) {
    for (int fd = 0; fd < MAX_FDS; fd++) {
        const Fd f = fd_get(fd);
        if (f.inuse && f.entry == entry && (!writers_only || (f.mode & (FS_APPEND | FS_WRITE)))) return fd;
    }
    return -1;
}
bool fd_ok(int fd) { return fd >= 0 && fd < MAX_FDS && fd_get(fd).inuse; }
// A writer's data page out to LPDDR, if it holds changes.
int write_out(Fd& f, Slot& s) {
    if (!s.data_dirty) return 0;
    Entries en{&s};
    uint32_t root = root_unit(f.root_pg), depth = f.depth;
    if (put_page(en, root, depth, s.data_pg, s.data) == NONE) return FS_ENOSPC;
    f.root_pg = pgno(root); f.depth = depth;
    s.data_dirty = false;
    return 0;
}
// fs_flush's work: the data page and the index lines to LPDDR, then the entry's root, depth and length.
int flush(int fd) {
    Fd f = fd_get(fd);
    if (!(f.mode & (FS_APPEND | FS_WRITE))) return 0;
    Slot& s = g_slots[f.slot];
    if (int r = write_out(f, s)) return r;
    flush_lines(s);
    Ent e = ent_read(f.entry);
    e.root = root_unit(f.root_pg); e.length = f.length; set_depth(e, f.depth);
    ent_write(f.entry, e);
    fd_put(fd, f);
    return 0;
}
// A streamed file's pages back, by the walk; a COW file's are marked "may be cached".
void free_tree(const Ent& e) {
    std::vector<uint32_t> pages;
    walk(e.root, e.depth(), [&](uint32_t a) { pages.push_back(a); });
    for (uint32_t a : pages) free_page(pgno(a), e.flags & E_COW);
}
#define LIVE() do { if (g_shutdown) { logf("%s = FS_ESHUTDOWN", __func__); return FS_ESHUTDOWN; } } while (0)

}  // namespace

// ================================================================== the emulation's hooks (fs_emu.h)
void set_caller(Caller c, int tile) { g_caller = c; g_tile = tile; }
void set_log(FILE* f) { g_log = f; }
void note(const char* fmt, ...) {
    if (!g_log) return;
    std::fputs("# ", g_log);
    va_list a; va_start(a, fmt); std::vfprintf(g_log, fmt, a); va_end(a);
    std::fputc('\n', g_log);
}
uint8_t* page(uint32_t unit) { return lp(unit); }
uint32_t lpddr_pages() { return g_pages; }
void cache_touch(uint32_t unit) { g_incache[pgno(unit)] = 1; }
void set_tracing(bool on) { g_tracing = on; }
const Stats& stats() { return g_stats; }
void reset_stats() { g_stats = Stats{}; }

void power_on(uint32_t pages, bool hard) {
    if (hard) {
        if (pages % 128 || pages < 256 || pages > (1u << 20)) fatal("LPDDR of %u pages", pages);
        g_pages = pages;
        g_lpddr.resize(size_t(pages) * PAGE_BYTES);           // its old contents stay: nothing is zeroed
    }
    // the reset: the scratchpad's state and the cache (23.6.10) empty
    g_incache.assign(g_pages, 0);
    g_taken.assign(g_pages / 32, 0); g_mbc.assign(g_pages / 32, 0);
    std::memset(g_fdtab, 0, sizeof g_fdtab);
    for (Slot& s : g_slots) { s.used = false; forget_lines(s); }
    for (Reservation& r : g_res) { r.used = false; r.pages.clear(); }
    g_shutdown = false; g_cursor = 1;
    setbit(g_taken, 0, true);
    const Caller c = g_caller; g_caller = Caller::MASTER;
    if (hard) {                                                     // the format (3.1)
        uint8_t b[PAGE_BYTES] = {};
        std::memcpy(b, MAGIC, 8); put32(b + 8, 1); put32(b + 12, pages);
        ent_encode(ent_new("/", T_DIR), b + LINE_BYTES);
        page_write(0, b);
        logf("format: %u pages", pages);
    } else {                                                        // the mount: the maps rebuilt by the walk
        if (std::memcmp(lp(0), MAGIC, 8) != 0) fatal("mount: no superblock");
        uint32_t files = 0;
        std::function<void(uint32_t)> visit = [&](uint32_t a) {
            const Ent e = ent_read(a);
            walk(e.root, e.depth(), [&](uint32_t u) {
                if (bit(g_taken, pgno(u))) fatal("mount: page %u reached twice", pgno(u));
                setbit(g_taken, pgno(u), true);
                if (e.flags & E_COW) setbit(g_mbc, pgno(u), true);
            });
            files++;
            if (e.type == T_DIR)
                for (uint32_t i = 0;; i++) {
                    const uint32_t s = dir_slot(e, i);
                    if (s == NONE) break;
                    if (ent_read(s).type != T_FREE) visit(s);
                }
        };
        visit(ROOT_ENTRY);
        logf("mount: %u entries, %u pages free", files, free_count());
    }
    g_caller = c;
}

void check() {
    for (int fd = 0; fd < MAX_FDS; fd++) {
        const Fd f = fd_get(fd);
        if (f.inuse && (f.mode & (FS_APPEND | FS_WRITE))) fatal("check() with fd %d open for writing", fd);
    }
    std::vector<uint8_t> seen(g_pages, 0);
    seen[0] = 1;
    std::function<void(uint32_t)> visit = [&](uint32_t a) {
        const Ent e = ent_read(a);
        walk(e.root, e.depth(), [&](uint32_t u) {
            const uint32_t p = pgno(u);
            if (seen[p]) fatal("check: page %u has two owners (or is page 0)", p);
            seen[p] = 1;
            if (!bit(g_taken, p)) fatal("check: page %u is in a file and free", p);
        });
        uint32_t live = 0;
        if (e.type == T_DIR)
            for (uint32_t i = 0;; i++) {
                const uint32_t s = dir_slot(e, i);
                if (s == NONE) break;
                const Ent c = ent_read(s);
                if (c.type == T_FREE) continue;
                live++;
                visit(s);
            }
        if (e.type == T_DIR && live != e.count) fatal("check: %s's count %u, %u entries live", e.name, e.count, live);
    };
    visit(ROOT_ENTRY);
    for (const Reservation& r : g_res)
        for (uint32_t u : r.pages) { if (seen[pgno(u)]) fatal("check: reserved page in a file"); seen[pgno(u)] = 1; }
    for (uint32_t p = 0; p < g_pages; p++) {
        if (bool(seen[p]) != bit(g_taken, p)) fatal("check: page %u is %s in the map, %s by the walk", p,
                                                    bit(g_taken, p) ? "taken" : "free", seen[p] ? "used" : "unused");
        if (g_incache[p] && !seen[p] && !bit(g_mbc, p)) fatal("check: free page %u has lines in the cache and is not in the \"may be cached\" map", p);
    }
    // a file's count is the copies whose source it is
    std::vector<std::pair<uint32_t, uint32_t>> counts;  // (entry, count)
    std::vector<uint32_t> sources;
    std::function<void(uint32_t)> gather = [&](uint32_t a) {
        const Ent e = ent_read(a);
        if (e.type == T_FILE) { counts.push_back({a, e.count}); if (e.source != NONE) sources.push_back(e.source); }
        if (e.type == T_DIR)
            for (uint32_t i = 0;; i++) {
                const uint32_t s = dir_slot(e, i);
                if (s == NONE) break;
                if (ent_read(s).type != T_FREE) gather(s);
            }
    };
    gather(ROOT_ENTRY);
    for (auto& c : counts) {
        uint32_t n = 0;
        for (uint32_t s : sources) n += s == c.first;
        if (n != c.second) fatal("check: entry %u counts %u copies, %u found", c.first, c.second, n);
    }
}

}  // namespace fsemu

using namespace fsemu;

// ================================================================== the API (fs.h)
extern "C" {

// ---- pages (5.1)
int fs_reserve_pages(u32 n, u32* r) {
    LIVE(); master_only("fs_reserve_pages");
    int slot = -1;
    for (int i = 0; i < MAX_RES; i++) if (!g_res[i].used) { slot = i; break; }
    if (slot < 0) fatal("more than %d reservations", MAX_RES);
    if (free_count() < n) { logf("fs_reserve_pages(%u) = FS_ENOSPC", n); return FS_ENOSPC; }
    const uint64_t d0 = g_stats.discards;
    Reservation& res = g_res[slot];
    res.used = true; res.pages.clear();
    // the pages chosen now, and discarded now if they need it (4.2): the pages that may be cached first, while the
    // Tracer is idle, so that the clean pages are left for the file system's own pages taken while it runs (the logs)
    for (int pass = 0; pass < 2 && res.pages.size() < n; pass++)
        for (uint32_t k = 0; k < g_pages && res.pages.size() < n; k++) {
            const uint32_t p = (g_cursor + k) % g_pages;
            if (p == 0 || bit(g_taken, p) || (pass == 0 && !bit(g_mbc, p))) continue;
            take(p);
            res.pages.push_back(p * PAGE_UNITS);
        }
    *r = u32(slot);
    logf("fs_reserve_pages(%u) = 0, r %u (%llu discarded)", n, *r, (unsigned long long)(g_stats.discards - d0));
    return 0;
}
int fs_acquire_pages(u32 r, u32* pages, u32 k) {
    LIVE(); master_only("fs_acquire_pages");
    if (r >= MAX_RES || !g_res[r].used || k > 128) { logf("fs_acquire_pages(%u, %u) = FS_EINVAL", r, k); return FS_EINVAL; }
    std::vector<uint32_t>& v = g_res[r].pages;
    const u32 n = k < v.size() ? k : u32(v.size());
    for (u32 i = 0; i < n; i++) { pages[i] = v.back(); v.pop_back(); }
    logf("fs_acquire_pages(%u, %u) = %u%s", r, k, n, n ? "" : "");
    return int(n);
}
int fs_release_pages(const u32* pages, u32 k) {
    LIVE(); master_only("fs_release_pages");
    for (u32 i = 0; i < k; i++) free_page(pgno(pages[i]), true);    // they may have been a COW file's
    logf("fs_release_pages(%u) = 0", k);
    return 0;
}
int fs_unreserve(u32 r) {
    LIVE(); master_only("fs_unreserve");
    if (r >= MAX_RES || !g_res[r].used) { logf("fs_unreserve(%u) = FS_EINVAL", r); return FS_EINVAL; }
    const size_t n = g_res[r].pages.size();
    for (uint32_t u : g_res[r].pages) free_page(pgno(u), false);    // discarded already, never used
    g_res[r].pages.clear(); g_res[r].used = false;
    logf("fs_unreserve(%u) = 0, %zu pages back", r, n);
    return 0;
}

// ---- streamed files (5.2)
int fs_open(const char* path, u32 flags) {
    LIVE();
    const bool writes = flags & (FS_APPEND | FS_WRITE);
    if (!flags || (flags & ~15u) || ((flags & FS_READ) && writes) || ((flags & FS_TRUNC) && !(flags & FS_APPEND))) {
        logf("fs_open(\"%s\", 0x%x) = FS_EINVAL", path, flags); return FS_EINVAL;
    }
    uint32_t a, parent; std::string last;
    int r = resolve(path, &a, &parent, &last);
    if (r == 0 && a == NONE && !writes) r = FS_ENOENT;
    if (r == 0 && a == NONE) {                                      // created
        if (ent_read(parent).type != T_DIR) r = FS_ENOTDIR;
        else if ((a = dir_add(parent, ent_new(last.c_str(), T_FILE))) == NONE) r = FS_ENOSPC;
    }
    Ent e{};
    if (r == 0) {
        e = ent_read(a);
        if (e.type == T_DIR) r = FS_EISDIR;
        else if (writes && (e.flags & (E_COW | E_FROZEN))) r = FS_EROFS;
        else if (!writes && (e.flags & E_COW) && !(e.flags & E_FROZEN)) r = FS_EBUSY;   // the Tracer's copy (2.4)
        else if (fd_find_entry(a, true) >= 0 || (writes && fd_find_entry(a, false) >= 0)) r = FS_EBUSY;
    }
    int fd = -1, slot = 0;
    if (r == 0) {
        for (int i = 0; i < MAX_FDS && fd < 0; i++) if (!fd_get(i).inuse) fd = i;
        if (fd < 0) r = FS_EBUSY;
    }
    if (r == 0 && writes) {
        slot = -1;
        for (int i = 0; i < MAX_SLOTS && slot < 0; i++) if (!g_slots[i].used) slot = i;
        if (slot < 0) r = FS_EBUSY;
    }
    if (r) { logf("fs_open(\"%s\", 0x%x) = %d", path, flags, r); return r; }
    if (flags & FS_TRUNC) {
        free_tree(e);
        e.root = NONE; e.length = 0; set_depth(e, 0);
        ent_write(a, e);
    }
    Fd f{};
    f.root_pg = e.root == NONE ? 0 : pgno(e.root); f.depth = e.depth(); f.mode = flags & 7u;
    f.cow = (e.flags & E_COW) ? 1 : 0; f.inuse = 1; f.slot = u32(slot);
    f.length = f.cow ? u32(e.length / PAGE_BYTES) : u32(e.length);
    f.entry = a; f.pos = 0;
    fd_put(fd, f);
    if (writes) { Slot& s = g_slots[slot]; s.used = true; s.data_pg = NONE; s.data_dirty = false; forget_lines(s); }
    logf("fs_open(\"%s\", 0x%x) = %d", path, flags, fd);
    return fd;
}
long fs_read(int fd, void* buf, long n) {
    LIVE();
    if (!fd_ok(fd) || !(fd_get(fd).mode & FS_READ) || n < 0) { logf("fs_read(%d, %ld) = FS_EBADF", fd, n); return FS_EBADF; }
    Fd f = fd_get(fd);
    const uint64_t len = f.cow ? uint64_t(f.length) * PAGE_BYTES : f.length;
    long got = 0;
    Entries en;
    uint8_t pg[PAGE_BYTES];
    while (got < n && f.pos < len) {
        const uint64_t p = f.pos / PAGE_BYTES, off = f.pos % PAGE_BYTES;
        const uint32_t d = find_page(en, root_unit(f.root_pg), f.depth, p);
        if (d == NONE) std::memset(pg, 0, sizeof pg); else page_read(d, pg);
        uint64_t c = PAGE_BYTES - off;
        if (c > uint64_t(n - got)) c = uint64_t(n - got);
        if (c > len - f.pos) c = len - f.pos;
        std::memcpy(static_cast<uint8_t*>(buf) + got, pg + off, c);
        got += long(c); f.pos += u32(c);
    }
    fd_put(fd, f);
    logf("fs_read(%d, %ld) = %ld", fd, n, got);
    return got;
}
long fs_write(int fd, const void* buf, long n) {
    LIVE();
    if (!fd_ok(fd) || !(fd_get(fd).mode & FS_APPEND) || n < 0) { logf("fs_write(%d, %ld) = FS_EBADF", fd, n); return FS_EBADF; }
    Fd f = fd_get(fd);
    Slot& s = g_slots[f.slot];
    long done = 0;
    while (done < n) {
        if (uint64_t(f.length) + 1 > 0xFFFFFFFFull) { fd_put(fd, f); logf("fs_write(%d, %ld) = FS_ENOSPC", fd, n); return FS_ENOSPC; }
        const uint32_t p = f.length / PAGE_BYTES, off = f.length % PAGE_BYTES;
        if (s.data_pg != p) {                                       // the page being filled changes
            if (int r = write_out(f, s)) { fd_put(fd, f); logf("fs_write(%d, %ld) = %d", fd, n, r); return r; }
            s.data_pg = p;
            Entries en{&s};
            const uint32_t d = off ? find_page(en, root_unit(f.root_pg), f.depth, p) : NONE;
            if (d == NONE) std::memset(s.data, 0, PAGE_BYTES); else page_read(d, s.data);   // a page flushed part full
        }
        uint32_t c = PAGE_BYTES - off;
        if (c > uint64_t(n - done)) c = u32(n - done);
        std::memcpy(s.data + off, static_cast<const uint8_t*>(buf) + done, c);
        s.data_dirty = true;
        f.length += c; done += c;
        if (f.length % PAGE_BYTES == 0)                             // full: out now
            if (int r = write_out(f, s)) { fd_put(fd, f); logf("fs_write(%d, %ld) = %d", fd, n, r); return r; }
    }
    fd_put(fd, f);
    logf("fs_write(%d, %ld) = %ld", fd, n, done);
    return done;
}
int fs_flush(int fd) {
    LIVE();
    if (!fd_ok(fd)) { logf("fs_flush(%d) = FS_EBADF", fd); return FS_EBADF; }
    const int r = flush(fd);
    logf("fs_flush(%d) = %d", fd, r);
    return r;
}
int fs_close(int fd) {
    LIVE();
    if (!fd_ok(fd)) { logf("fs_close(%d) = FS_EBADF", fd); return FS_EBADF; }
    const int r = flush(fd);
    const Fd f = fd_get(fd);
    if (f.mode & (FS_APPEND | FS_WRITE)) g_slots[f.slot].used = false;
    std::memset(g_fdtab + fd * FD_BYTES, 0, FD_BYTES);
    logf("fs_close(%d) = %d", fd, r);
    return r;
}
int fs_read_page(int fd, u64 pg, void* buf) {
    LIVE();
    if (!fd_ok(fd) || !(fd_get(fd).mode & FS_READ)) { logf("fs_read_page(%d, %llu) = FS_EBADF", fd, (unsigned long long)pg); return FS_EBADF; }
    const Fd f = fd_get(fd);
    Entries en;
    const uint32_t d = find_page(en, root_unit(f.root_pg), f.depth, pg);
    if (d == NONE) std::memset(buf, 0, PAGE_BYTES);
    else { uint8_t b[PAGE_BYTES]; page_read(d, b); std::memcpy(buf, b, PAGE_BYTES); }
    logf("fs_read_page(%d, %llu) = 0%s", fd, (unsigned long long)pg, d == NONE ? " (a hole)" : "");
    return 0;
}
int fs_write_page(int fd, u64 pg, const void* buf) {
    LIVE();
    if (!fd_ok(fd) || !(fd_get(fd).mode & FS_WRITE)) { logf("fs_write_page(%d, %llu) = FS_EBADF", fd, (unsigned long long)pg); return FS_EBADF; }
    Fd f = fd_get(fd);
    if ((pg + 1) * PAGE_BYTES > 0xFFFFFFFFull) { logf("fs_write_page(%d, %llu) = FS_EINVAL", fd, (unsigned long long)pg); return FS_EINVAL; }
    Slot& s = g_slots[f.slot];
    Entries en{&s};
    uint32_t root = root_unit(f.root_pg), depth = f.depth;
    uint8_t b[PAGE_BYTES]; std::memcpy(b, buf, PAGE_BYTES);
    if (put_page(en, root, depth, pg, b) == NONE) { logf("fs_write_page(%d, %llu) = FS_ENOSPC", fd, (unsigned long long)pg); return FS_ENOSPC; }
    f.root_pg = pgno(root); f.depth = depth;
    if ((pg + 1) * PAGE_BYTES > f.length) f.length = u32((pg + 1) * PAGE_BYTES);
    fd_put(fd, f);
    logf("fs_write_page(%d, %llu) = 0", fd, (unsigned long long)pg);
    return 0;
}
int fs_fprintf(int fd, const char* fmt, ...) {
    char b[1024];
    va_list a; va_start(a, fmt); const int n = std::vsnprintf(b, sizeof b, fmt, a); va_end(a);
    const long r = fs_write(fd, b, n < int(sizeof b) ? n : int(sizeof b) - 1);
    return r < 0 ? int(r) : int(r);
}
int fs_mkdir(const char* path) {
    LIVE();
    uint32_t a, parent; std::string last;
    int r = resolve(path, &a, &parent, &last);
    if (r == 0 && a != NONE) r = FS_EEXIST;
    if (r == 0 && dir_add(parent, ent_new(last.c_str(), T_DIR)) == NONE) r = FS_ENOSPC;
    logf("fs_mkdir(\"%s\") = %d", path, r);
    return r;
}
int fs_remove(const char* path) {
    LIVE();
    uint32_t a, parent;
    int r = resolve(path, &a, &parent);
    if (r == 0 && a == NONE) r = FS_ENOENT;
    if (r == 0 && a == ROOT_ENTRY) r = FS_EBUSY;
    Ent e{};
    if (r == 0) {
        e = ent_read(a);
        if (fd_find_entry(a, false) >= 0) r = FS_EBUSY;
        else if (e.count) r = e.type == T_DIR ? FS_ENOTEMPTY : FS_EBUSY;   // live entries; copies
    }
    if (r) { logf("fs_remove(\"%s\") = %d", path, r); return r; }
    const uint64_t f0 = g_stats.pages_freed;
    free_tree(e);
    if (e.source != NONE) { Ent s = ent_read(e.source); s.count--; ent_write(e.source, s); }
    dir_drop(parent, a);
    logf("fs_remove(\"%s\") = 0, %llu pages freed%s", path, (unsigned long long)(g_stats.pages_freed - f0),
         (e.flags & E_COW) ? ", may be cached" : "");
    return 0;
}
int fs_list(const char* path, u32* pos, struct fs_dirent* out) {
    LIVE();
    uint32_t a;
    int r = resolve(path, &a);
    if (r == 0 && a == NONE) r = FS_ENOENT;
    Ent d{};
    if (r == 0 && (d = ent_read(a)).type != T_DIR) r = FS_ENOTDIR;
    if (r) { logf("fs_list(\"%s\", %u) = %d", path, *pos, r); return r; }
    const u32 from = *pos;
    for (;; (*pos)++) {
        const uint32_t s = dir_slot(d, *pos);
        if (s == NONE) { logf("fs_list(\"%s\", %u) = 0", path, from); return 0; }
        const Ent e = ent_read(s);
        if (e.type == T_FREE) continue;
        std::memcpy(out->name, e.name, 96);
        out->type = e.type;
        out->flags = ((e.flags & E_FROZEN) ? FS_F_FROZEN : 0) | ((e.flags & E_COW) ? FS_F_COW | FS_F_SPARSE : 0);
        out->length = e.length;
        (*pos)++;
        logf("fs_list(\"%s\", %u) = 1, \"%s\"", path, from, out->name);
        return 1;
    }
}
int fs_stat(const char* path, struct fs_stat* st) {
    LIVE();
    uint32_t a;
    int r = resolve(path, &a);
    if (r == 0 && a == NONE) r = FS_ENOENT;
    if (r) { logf("fs_stat(\"%s\") = %d", path, r); return r; }
    const Ent e = ent_read(a);
    uint64_t pages = 0;
    walk(e.root, e.depth(), [&](uint32_t) { pages++; });
    st->type = e.type;
    st->flags = ((e.flags & E_FROZEN) ? FS_F_FROZEN : 0) | ((e.flags & E_COW) ? FS_F_COW | FS_F_SPARSE : 0);
    st->length = e.length; st->pages = pages; st->copies = e.type == T_FILE ? e.count : 0; st->depth = e.depth();
    logf("fs_stat(\"%s\") = 0: length %llu, %llu pages, depth %u, %u copies", path, (unsigned long long)e.length,
         (unsigned long long)pages, st->depth, st->copies);
    return 0;
}
int fs_df(u64* total, u64* free) {
    LIVE();
    *total = g_pages; *free = free_count();
    logf("fs_df() = 0: %llu total, %llu free", (unsigned long long)*total, (unsigned long long)*free);
    return 0;
}

// ---- COW files (5.3)
namespace {
uint32_t cow_depth(u64 pages) { return pages <= 1024 ? 1 : pages <= (1u << 20) ? 2 : 3; }   // >= 1, as v1.37's depth_of
int cow_make(const char* call, const char* path, u64 pages, u32 r, uint32_t source, u32* root, u32* depth) {
    uint32_t a, parent; std::string last;
    int rc = resolve(path, &a, &parent, &last);
    if (rc == 0 && a != NONE) rc = FS_EEXIST;
    if (rc == 0 && (r >= MAX_RES || !g_res[r].used || g_res[r].pages.empty())) rc = FS_ENOSPC;
    if (rc) return rc;
    Ent e = ent_new(last.c_str(), T_FILE);
    e.root = g_res[r].pages.back(); g_res[r].pages.pop_back();     // the root: not yet made (5.3)
    e.length = pages * PAGE_BYTES; e.flags = E_COW; set_depth(e, cow_depth(pages)); e.source = source;
    if (dir_add(parent, e) == NONE) { g_res[r].pages.push_back(e.root); return FS_ENOSPC; }
    *root = e.root; *depth = e.depth();
    (void)call;
    return 0;
}
}  // namespace
int cow_create(const char* path, u64 length_pages, u32 r, u32* root, u32* depth) {
    LIVE(); master_only("cow_create");
    int rc = (length_pages == 0 || length_pages > (1ull << 23)) ? FS_EINVAL : cow_make("cow_create", path, length_pages, r, NONE, root, depth);
    if (rc) logf("cow_create(\"%s\", %llu, r %u) = %d", path, (unsigned long long)length_pages, r, rc);
    else logf("cow_create(\"%s\", %llu, r %u) = 0: root 0x%x, depth %u", path, (unsigned long long)length_pages, r, *root, *depth);
    return rc;
}
int cow_copy(const char* src, const char* dst, u32 r, u32* root, u32* depth) {
    LIVE(); master_only("cow_copy");
    uint32_t sa;
    int rc = resolve(src, &sa);
    if (rc == 0 && sa == NONE) rc = FS_ENOENT;
    Ent s{};
    if (rc == 0) {
        s = ent_read(sa);
        if (s.type != T_FILE || !(s.flags & E_COW) || !(s.flags & E_FROZEN)) rc = FS_EINVAL;   // a frozen COW file
    }
    if (rc == 0) rc = cow_make("cow_copy", dst, s.length / PAGE_BYTES, r, sa, root, depth);
    if (rc == 0) { s = ent_read(sa); s.count++; ent_write(sa, s); }
    if (rc) logf("cow_copy(\"%s\", \"%s\", r %u) = %d", src, dst, r, rc);
    else logf("cow_copy(\"%s\", \"%s\", r %u) = 0: root 0x%x, depth %u; source root 0x%x", src, dst, r, *root, *depth, s.root);
    return rc;
}
int cow_freeze(const char* path) {
    LIVE(); master_only("cow_freeze");
    uint32_t a;
    int rc = resolve(path, &a);
    if (rc == 0 && a == NONE) rc = FS_ENOENT;
    if (rc == 0) {
        Ent e = ent_read(a);
        if (!(e.flags & E_COW)) rc = FS_EINVAL;
        else { e.flags |= E_FROZEN; ent_write(a, e); }
    }
    logf("cow_freeze(\"%s\") = %d", path, rc);
    return rc;
}
int cow_root(const char* path, u32* root, u32* depth) {
    LIVE(); master_only("cow_root");
    uint32_t a;
    int rc = resolve(path, &a);
    if (rc == 0 && a == NONE) rc = FS_ENOENT;
    if (rc == 0) {
        const Ent e = ent_read(a);
        if (!(e.flags & E_COW)) rc = FS_EINVAL;                    // a streamed file is never walked through the cache
        else { *root = e.root; *depth = e.depth(); }
    }
    if (rc) logf("cow_root(\"%s\") = %d", path, rc);
    else logf("cow_root(\"%s\") = 0: root 0x%x, depth %u", path, *root, *depth);
    return rc;
}

// ---- the file system as a whole (5.4)
int fs_shutdown(void) {
    LIVE(); master_only("fs_shutdown");
    int open = 0;
    for (int fd = 0; fd < MAX_FDS; fd++) {
        if (!fd_get(fd).inuse) continue;
        flush(fd);
        const Fd f = fd_get(fd);
        if (f.mode & (FS_APPEND | FS_WRITE)) g_slots[f.slot].used = false;
        std::memset(g_fdtab + fd * FD_BYTES, 0, FD_BYTES);
        open++;
    }
    g_shutdown = true;
    logf("fs_shutdown() = 0, %d files closed", open);
    return 0;
}

}  // extern "C"
