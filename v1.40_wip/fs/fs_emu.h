// fs_emu.h: the C++ emulation's own hooks, beside fs.h. Not part of the API: what a test, the Updater's stand-in and
// the Tracer's stand-in need to drive the emulated file system and to look inside it.
//
// The emulation is one memory tile's file system, entirely in software, over an emulated LPDDR. The structures it keeps
// are FS-API.md's, laid out byte for byte: the superblock in page 0 (4.4), directories as files of 128-byte entries
// (4.3), the two page maps of 128 pages to a 128-bit word (4.1), the fd table of 512 entries of 16 bytes (4.6).
//
// A breach of a rule a caller keeps (a call the caller may not make, a page reused before its discard) is undefined
// behaviour (GUIDELINES section 2): the emulation stops and names it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "fs.h"

namespace fsemu {

constexpr uint32_t PAGE_BYTES = 4096, LINE_BYTES = 128, PAGE_UNITS = 32;
constexpr uint32_t NONE = 0xFFFFFFFFu, OWNED = 0x80000000u;

// Who is calling: the callers of FS-API.md section 6.
enum class Caller { MASTER, TRACER, TILE, SHELL };
void set_caller(Caller c, int tile = 0);

// Power: a hard reset formats, a soft reset mounts (3.1). LPDDR (lpddr_pages, a multiple of 128) keeps its contents
// across a soft reset; the memory tile's scratchpad does not.
void power_on(uint32_t lpddr_pages, bool hard);

// The call log: every call, its caller, its arguments and its result, a line each. nullptr: none.
void set_log(FILE* f);
void note(const char* fmt, ...);      // a line of the test's own into the log

// LPDDR, for the stand-ins: a page by its address in units.
uint8_t* page(uint32_t unit_address);
uint32_t lpddr_pages();

// The cache, as far as the rules need it (23.6.8): which pages may have lines in it. The Updater's stand-in marks the
// index pages it updates; the Tracer's marks every page it reads or writes. A discard clears a page. A page handed out
// with lines in the cache is the breach the rules forbid, and stops the emulation.
void cache_touch(uint32_t unit_address);
void set_tracing(bool on);            // while on, a discard is counted as one made during tracing

struct Stats {
    uint64_t discards = 0, discards_while_tracing = 0;
    // LPDDR: only 2 KiB (half) and 4 KiB (page) transfers (FS-DATA-PATH 2); the memory tile's cache (6)
    uint64_t lpddr_page_writes = 0, lpddr_half_writes = 0, lpddr_page_reads = 0, lpddr_half_reads = 0;
    uint64_t cache_hits = 0, cache_misses = 0, hint_hits = 0;   // hint: a reader's fd named its buffer (FS-API 4.6)
    uint64_t name_hash_compares = 0, dir_entry_reads = 0;      // path look-ups: in data memory; from the SRAM
    uint64_t page_requests = 0;                                 // the memory tile's stash refilled from the tracer tile
    uint64_t pages_taken = 0, pages_freed = 0;
};
const Stats& stats();
void reset_stats();

// The invariants, checked whole: every page in use has exactly one owner, found by walking every file from the root;
// the taken map is the walk's pages, the pools' prepared batches' and page 0, and nothing else; every page with lines in the
// cache is in the "may be cached" map or taken. Only with no file open for writing. Stops the emulation on a breach.
void check();

// What the last mount found in the superblock: whether the file system had been shut down cleanly (FS-API 4.4).
bool last_mount_clean();

}  // namespace fsemu
