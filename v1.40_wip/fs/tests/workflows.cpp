// workflows.cpp: the flows of FS-API.md section 3, as call sequences against the emulated file system. Each writes
// its call log to tests/out/NN-name.log: every call, its caller, its arguments and its result, in order, with the
// test's own notes (#) between. The emulation's invariants are checked after each (fs_emu.h, check()).
//
//   1 power-on: a format, an empty file system, a mount
//   2 storing two programs (3.2)
//   3 a proof that succeeds (3.3, 3.4, 3.5): the five COW files, the run logs, the tiles' logs, the cleanup
//   4 a proof that fails in software (3.6): everything kept
//   5 a hardware error in a compute tile (3.7, 3.8): fs_shutdown, a soft reset, a mount; then an error in the file
//     system's own path, with no shutdown: what was flushed survives
//   6 the shell after the soft reset (3.8, SHELL-SPEC section 9), as call sequences
//   7 removing the working directories (3.9): leaves first, every page back
//   8 a second proof on freed pages (4.2): the discards made at fs_reserve_pages, none while the Tracer runs
//   9 many writers at once (4.6: 128 buffers), and a file system full: fs_write's FS_ENOSPC keeps the pages stored
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../fs.h"
#include "../fs_emu.h"
#include "../stand_ins.h"

using namespace fsemu;

static int failures = 0;
#define EXPECT(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #c); \
                                   note("FAILED: %s", #c); failures++; } } while (0)

static FILE* g_out = nullptr;
static void begin(const char* name) {
    if (g_out) std::fclose(g_out);
    const std::string p = std::string("out/") + name + ".log";
    g_out = std::fopen(p.c_str(), "w");
    set_log(g_out);
    reset_stats();
    std::printf("%s\n", name);
}
static void report() {
    const Stats& s = stats();
    note("stats: %llu pages taken, %llu freed, %llu discards (%llu while tracing); LPDDR %llu page writes, %llu half writes, %llu page reads, %llu half reads; the cache %llu hits, %llu misses; %llu reads by the fd's hint",
         (unsigned long long)s.pages_taken, (unsigned long long)s.pages_freed, (unsigned long long)s.discards,
         (unsigned long long)s.discards_while_tracing, (unsigned long long)s.lpddr_page_writes,
         (unsigned long long)s.lpddr_half_writes, (unsigned long long)s.lpddr_page_reads,
         (unsigned long long)s.lpddr_half_reads, (unsigned long long)s.cache_hits,
         (unsigned long long)s.cache_misses, (unsigned long long)s.hint_hits);
}
static u64 free_pages() { u64 t, f; set_caller(Caller::MASTER); fs_df(&t, &f); return f; }

// ------------------------------------------------------------------ the master tile's side
static Updater g_updater;

// The pages of a run into the free page queue, at most 128 a call, as the credits would allow (17.10.4).
static void feed(uint64_t n) {
    while (n) {
        const u32 k = u32(std::min<uint64_t>(128, n));
        u32 b[128];
        const int got = fs_acquire_pages(b, k);
        EXPECT(got == int(k));
        if (got <= 0) return;
        for (int i = 0; i < got; i++) g_updater.push(b[i]);
        n -= u32(got);
    }
}
static void run(const Run& c, const std::vector<uint64_t>& words, const char* what) {
    feed(c.page_count);
    note("Updater run (%s): root %d, depth %u, tracer_copy %d, IMAGE_ROOT 0x%x, TRACER_ROOT 0x%x, SOURCE_ROOT 0x%x, %zu words, PAGE_COUNT %llu",
         what, c.root, c.depth, int(c.tracer_copy), c.image_root, c.tracer_root, c.source_root, words.size(),
         (unsigned long long)c.page_count);
    const uint64_t left = g_updater.run(c, words);
    EXPECT(left == 0);
    EXPECT(g_updater.queued() == 0);
}
static std::vector<uint64_t> merge(const std::vector<std::vector<uint64_t>>& v, size_t from, size_t to) {
    std::vector<uint64_t> out;
    for (size_t k = from; k < to; k++) out.insert(out.end(), v[k].begin(), v[k].end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
static std::vector<uint64_t> concat(const std::vector<std::vector<uint64_t>>& v, size_t from, size_t to, uint64_t seed) {
    std::vector<uint64_t> w;
    for (size_t k = from; k < to; k++) { auto x = words_for(v[k], seed + k); w.insert(w.end(), x.begin(), x.end()); }
    return w;
}

struct Program {
    std::string dir, stem;              // stem: the full path without an extension
    uint64_t length_pages;              // the image's length
    std::vector<uint64_t> pages;        // the pages it has
    uint64_t map_pages;                 // the base table's length; every page sent
};

// 3.2: a program stored.
static void store_program(const Program& p) {
    set_caller(Caller::MASTER);
    note("--- storing %s", p.stem.c_str());
    int rc = fs_mkdir(p.dir.c_str());
    EXPECT(rc == 0 || rc == FS_EEXIST);
    const uint32_t d = p.length_pages <= 1024 ? 1 : 2, md = 1;
    std::vector<uint64_t> map_all;
    for (uint64_t i = 0; i < p.map_pages; i++) map_all.push_back(i);
    const uint64_t n_img = pages_for(d, p.pages), n_map = pages_for(md, map_all);
    u32 root, depth;
    EXPECT(fs_reserve_pages(u32(n_img + n_map + 2)) == 0);
    const std::string img = p.stem + ".pages", map = p.stem + ".map";
    EXPECT(cow_create(img.c_str(), p.length_pages, &root, &depth) == 0);
    EXPECT(depth == d);
    Run c; c.root = 1; c.depth = depth; c.image_root = root; c.page_count = n_img;
    run(c, words_for(p.pages, 1), "the program image, empty");
    EXPECT(cow_commit(img.c_str()) == 0);
    EXPECT(cow_freeze(img.c_str()) == 0);
    EXPECT(cow_create(map.c_str(), p.map_pages, &root, &depth) == 0);
    c = Run{}; c.root = 1; c.depth = depth; c.image_root = root; c.page_count = n_map;
    run(c, words_for(map_all, 2), "the base table, empty");
    EXPECT(cow_commit(map.c_str()) == 0);
    EXPECT(cow_freeze(map.c_str()) == 0);
    EXPECT(fs_unreserve() == 0);
}

enum class Ending { SUCCESS, SOFTWARE_FAILURE, HARDWARE_ERROR };
struct Proof {
    std::string wd;
    const Program* program;
    uint32_t N, s;
    std::vector<std::vector<uint64_t>> segments;   // each segment's written pages
    std::vector<uint64_t> data_pages;              // the data image's pages (frame 6)
    std::vector<uint64_t> map_delta;               // the proof's table: the pages that differ from the base
    uint32_t tiles;                                // compute tiles writing logs (64 on the chip)
    uint64_t big_log;                              // tile 0's code log, bytes (0: the default)
    Ending ending;
};

static uint8_t log_byte(uint32_t tile, uint32_t kind, uint64_t i) { return uint8_t(tile * 7 + kind * 13 + i * 31 + (i >> 9)); }
static const char* KIND[3] = {"code", "read", "write"};
static uint64_t log_size(const Proof& p, uint32_t t, uint32_t k) {
    if (t == 0 && k == 0 && p.big_log) return p.big_log;
    return 3000 + 9000 * ((t * 3 + k) % 5);        // 3 KB to 39 KB: from a part page to several
}

// 3.3 to 3.5: a proof, from its request to its reply, and its cleanup if it succeeds.
static void run_proof(const Proof& p) {
    set_caller(Caller::MASTER);
    note("=== a proof: %s, N %u, s %u", p.wd.c_str(), p.N, p.s);
    if (fs_mkdir(p.wd.c_str()) != 0) { EXPECT(false); return; }
    const std::string logs = p.wd + "/logs";
    EXPECT(fs_mkdir(logs.c_str()) == 0);
    struct fs_stat st;
    const std::string img = p.program->stem + ".pages", base = p.program->stem + ".map";
    EXPECT(fs_stat(img.c_str(), &st) == 0);
    const uint32_t d = st.depth;
    EXPECT(fs_stat(base.c_str(), &st) == 0);
    const uint32_t md = st.depth;
    // the host's counts (29.7)
    const auto before = merge(p.segments, 0, p.s), mine = merge(p.segments, p.s, p.s + 1), after_all = merge(p.segments, p.s, p.N);
    const uint64_t n_map = pages_for(md, p.map_delta), n_c0 = pages_for(d, p.data_pages), n_start = pages_for(d, before),
                   n_tracer = pages_for(d, mine), n_final = pages_for(d, after_all);
    const uint64_t total = n_map + n_c0 + n_start + 2 * n_tracer + (n_final - n_tracer) + 5;   // and five roots
    note("the host's counts: map %llu, cycle-0 %llu, start %llu, Tracer's copy %llu, final %llu; reserved %llu with the roots",
         (unsigned long long)n_map, (unsigned long long)n_c0, (unsigned long long)n_start, (unsigned long long)n_tracer,
         (unsigned long long)n_final, (unsigned long long)total);
    u32 root, depth, src, sd;
    if (fs_reserve_pages(u32(total)) != 0) { EXPECT(false); return; }
    const std::string map = p.wd + "/map", c0 = p.wd + "/cycle0", start = p.wd + "/start", fin = p.wd + "/final",
                      tr = p.wd + "/tracer";
    // the proof's table: a copy of the base, frame 5's pages over it
    EXPECT(cow_root(base.c_str(), &src, &sd) == 0);
    EXPECT(cow_copy(base.c_str(), map.c_str(), &root, &depth) == 0);
    Run c; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_map;
    run(c, words_for(p.map_delta, 5), "frame 5: the proof's table");
    EXPECT(cow_commit(map.c_str()) == 0);
    EXPECT(cow_freeze(map.c_str()) == 0);
    // the cycle-0 image: a copy of the program image, frame 6 over it
    EXPECT(cow_root(img.c_str(), &src, &sd) == 0);
    EXPECT(cow_copy(img.c_str(), c0.c_str(), &root, &depth) == 0);
    c = Run{}; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_c0;
    run(c, words_for(p.data_pages, 6), "frame 6: the data image");
    EXPECT(cow_commit(c0.c_str()) == 0);
    EXPECT(cow_freeze(c0.c_str()) == 0);
    // the start image: frame 9, the segments before s
    EXPECT(cow_root(c0.c_str(), &src, &sd) == 0);
    EXPECT(cow_copy(c0.c_str(), start.c_str(), &root, &depth) == 0);
    c = Run{}; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_start;
    run(c, concat(p.segments, 0, p.s, 90), "frame 9: the segments before s");
    EXPECT(cow_commit(start.c_str()) == 0);
    EXPECT(cow_freeze(start.c_str()) == 0);
    // the final image and the Tracer's copy: segment s with tracer_copy, then the rest
    EXPECT(cow_root(start.c_str(), &src, &sd) == 0);
    u32 froot, troot;
    EXPECT(cow_copy(start.c_str(), fin.c_str(), &froot, &depth) == 0);
    EXPECT(cow_copy(start.c_str(), tr.c_str(), &troot, &depth) == 0);
    c = Run{}; c.root = 2; c.depth = depth; c.tracer_copy = true; c.image_root = froot; c.tracer_root = troot;
    c.source_root = src; c.page_count = 2 * n_tracer;
    run(c, concat(p.segments, p.s, p.s + 1, 90), "frame 9: segment s, with the Tracer's copy");
    EXPECT(cow_commit(fin.c_str()) == 0);                                   // both roots made by that run
    EXPECT(cow_commit(tr.c_str()) == 0);
    c = Run{}; c.root = 0; c.depth = depth; c.image_root = froot; c.page_count = n_final - n_tracer;
    run(c, concat(p.segments, p.s + 1, p.N, 90), "frame 9: the segments after s");
    // the hand-over (30.5)
    u32 mroot, mdepth, croot, cdepth;
    EXPECT(cow_root(tr.c_str(), &croot, &cdepth) == 0);
    EXPECT(cow_root(map.c_str(), &mroot, &mdepth) == 0);
    note("hand-over: memory_root 0x%x depth %u, map_root 0x%x depth %u, to record s's word 66", croot, cdepth, mroot, mdepth);

    // the segment: the Tracer reads its files, writes its copy's own pages; the run logs; the tiles' logs
    set_tracing(true);
    for (uint64_t pg : p.program->pages) tracer_read(croot, cdepth, pg);
    for (uint64_t pg : p.map_delta) tracer_read(mroot, mdepth, pg);
    for (uint64_t pg : mine) EXPECT(tracer_write(croot, cdepth, pg, 7, 0xC0FFEE));
    if (!p.program->pages.empty() && std::find(mine.begin(), mine.end(), p.program->pages[0]) == mine.end())
        EXPECT(!tracer_write(croot, cdepth, p.program->pages[0], 7, 1));   // a page not its own: refused
    std::vector<int> tile_fd(p.tiles);
    for (uint32_t t = 0; t < p.tiles; t++) {
        set_caller(Caller::TILE, int(t));
        EXPECT(fs_chdir(logs.c_str()) == 0);                      // each tile sets its own (FS-API 4.7)
        const std::string f = "tile" + std::to_string(t) + ".log";   // relative: a short path in the request
        tile_fd[t] = fs_open(f.c_str(), FS_APPEND);
        EXPECT(tile_fd[t] >= 0);
        fs_fprintf(tile_fd[t], "tile %u: segment %u starts\n", t, p.s);
        if (t == 1) fs_flush(tile_fd[t]);
    }
    const uint32_t fail_at = p.tiles / 2;
    set_caller(Caller::TRACER);
    EXPECT(fs_chdir(logs.c_str()) == 0);
    for (uint32_t t = 0; t < p.tiles; t++) {
        set_caller(Caller::TRACER);
        for (uint32_t k = 0; k < 3; k++) {
            const std::string f = "tile" + std::to_string(t) + "." + KIND[k];
            const int fd = fs_open(f.c_str(), FS_APPEND);
            EXPECT(fd >= 0);
            const uint64_t n = log_size(p, t, k);
            std::vector<uint8_t> b(64 * 1024);
            for (uint64_t at = 0; at < n;) {
                const uint64_t c2 = std::min<uint64_t>(b.size(), n - at);
                for (uint64_t i = 0; i < c2; i++) b[i] = log_byte(t, k, at + i);
                EXPECT(fs_write(fd, b.data(), long(c2)) == long(c2));
                at += c2;
            }
            EXPECT(fs_close(fd) == 0);
        }
        set_caller(Caller::TILE, int(t));
        fs_fprintf(tile_fd[t], "tile %u: window %u replayed\n", t, t);
        if (p.ending == Ending::HARDWARE_ERROR && t == fail_at) {
            fs_fprintf(tile_fd[t], "tile %u: about to fail\n", t);
            set_tracing(false);
            set_caller(Caller::MASTER);
            note("tile %u raises a hardware error: the master tile's NMI handler closes out the file system", t);
            EXPECT(fs_shutdown() == 0);
            u64 tot, fr;
            EXPECT(fs_df(&tot, &fr) == FS_ESHUTDOWN);
            set_caller(Caller::TILE, int(t + 1));
            EXPECT(fs_fprintf(tile_fd[t + 1], "too late\n") == FS_ESHUTDOWN);
            note("the master tile parks; the outside world makes a soft reset");
            return;
        }
        if (p.ending == Ending::SOFTWARE_FAILURE && t == fail_at) {
            fs_fprintf(tile_fd[t], "tile %u: check failed at row 12345\n", t);
            break;
        }
    }
    set_tracing(false);
    for (uint32_t t = 0; t < p.tiles; t++) {
        set_caller(Caller::TILE, int(t));
        fs_fprintf(tile_fd[t], "tile %u: done\n", t);
        EXPECT(fs_close(tile_fd[t]) == 0);
        struct fs_stat ms;
        EXPECT(fs_stat("../map", &ms) == 0 && (ms.flags & FS_F_COW));    // ".." from the working directory
        EXPECT(fs_stat("./../logs/.", &ms) == 0 && ms.type == FS_T_DIR);
    }
    set_caller(Caller::MASTER);
    EXPECT(cow_freeze(fin.c_str()) == 0);                                   // the third run is done
    EXPECT(fs_unreserve() == 0);
    auto leave = [&] {                                                     // the tiles back to the root
        for (uint32_t t = 0; t < p.tiles; t++) { set_caller(Caller::TILE, int(t)); EXPECT(fs_chdir("/") == 0); }
        set_caller(Caller::TRACER); EXPECT(fs_chdir("/") == 0);
        set_caller(Caller::MASTER);
    };
    if (p.ending == Ending::SOFTWARE_FAILURE) { leave(); note("the reply: the proof failed; %s is kept", p.wd.c_str()); return; }
    note("the reply: the proof succeeded; the Tracer's copy and the logs removed (3.5)");
    EXPECT(fs_remove(tr.c_str()) == 0);
    struct fs_dirent e; u32 pos = 0;
    std::vector<std::string> names;
    while (fs_list(logs.c_str(), &pos, &e) == 1) names.push_back(e.name);
    for (auto& n : names) EXPECT(fs_remove((logs + "/" + n).c_str()) == 0);
    EXPECT(fs_remove(logs.c_str()) == FS_EBUSY);                          // still the tiles' working directory
    leave();
    EXPECT(fs_remove(logs.c_str()) == 0);
}

// ------------------------------------------------------------------ the shell's commands, as call sequences
static void sh(const char* line) { set_caller(Caller::SHELL); note("shell> %s", line); }
static void sh_ls(const char* path) {
    struct fs_dirent e; u32 pos = 0;
    while (fs_list(path, &pos, &e) == 1)
        note("  %c%c%c %12llu %s%s", e.type == FS_T_DIR ? 'd' : '-', (e.flags & FS_F_FROZEN) ? 'f' : '-',
             (e.flags & FS_F_SPARSE) ? 's' : '-', (unsigned long long)e.length, e.name, e.type == FS_T_DIR ? "/" : "");
}
static std::string sh_cat(const char* path) {
    std::string out;
    const int fd = fs_open(path, FS_READ);
    if (fd < 0) { note("  cat: %s: error %d", path, fd); return out; }
    char b[4096]; long n;
    while ((n = fs_read(fd, b, sizeof b)) > 0) out.append(b, size_t(n));
    fs_close(fd);
    return out;
}
static void sh_tail(const char* path, int lines) {
    struct fs_stat st;
    if (fs_stat(path, &st) != 0) return;
    const int fd = fs_open(path, FS_READ);
    std::string tail;
    for (int64_t pg = int64_t((st.length + 4095) / 4096) - 1; pg >= 0; pg--) {   // backward from the last page
        char b[4096];
        fs_read_page(fd, u64(pg), b);
        const uint64_t n = std::min<uint64_t>(4096, st.length - uint64_t(pg) * 4096);
        tail.insert(0, b, n);
        if (std::count(tail.begin(), tail.end(), '\n') > lines) break;
    }
    fs_close(fd);
    size_t at = tail.size();
    for (int k = 0; k <= lines && at > 0; k++) at = tail.rfind('\n', at - 2) == std::string::npos ? 0 : tail.rfind('\n', at - 2) + 1;
    note("  %s", tail.substr(at).c_str());
}

// ================================================================== the workflows
int main() {
    constexpr uint32_t LPDDR = 16384;                         // 64 MiB here; 2^20 pages on the chip

    begin("01-power-on");
    power_on(LPDDR, true);
    const u64 empty = free_pages();
    EXPECT(empty == LPDDR - 1);
    { struct fs_dirent e; u32 pos = 0; set_caller(Caller::MASTER); EXPECT(fs_list("/", &pos, &e) == 0); }
    power_on(LPDDR, false);
    EXPECT(free_pages() == empty);
    check(); report();

    begin("02-store-programs");
    Program fib{"/programs", "/programs/fib", 2048, {}, 4};   // depth 2: 2,048 pages of room
    for (uint64_t pg : {0ull, 1ull, 2ull, 3ull, 1030ull, 1031ull, 2040ull}) fib.pages.push_back(pg);
    Program sha{"/programs", "/programs/sha", 512, {0, 1, 2, 3, 4, 5, 300}, 2};
    store_program(fib);
    store_program(sha);
    set_caller(Caller::MASTER);
    EXPECT(fs_open("/programs/fib.pages", FS_APPEND) == FS_EROFS);          // a COW file is never written so
    u32 root, depth;
    EXPECT(fs_reserve_pages(4) == 0);
    EXPECT(cow_create("/programs/tmp", 10, &root, &depth) == 0);
    note("not yet committed: its entry's root is 0");
    EXPECT(cow_freeze("/programs/tmp") == FS_EBUSY);
    EXPECT(cow_root("/programs/tmp", &root, &depth) == FS_EBUSY);
    EXPECT(cow_copy("/programs/tmp", "/programs/tmp2", &root, &depth) == FS_EINVAL);   // the source not frozen
    EXPECT(fs_open("/programs/tmp", FS_READ) == FS_EBUSY);
    { struct fs_stat s2; EXPECT(fs_stat("/programs/tmp", &s2) == 0 && s2.pages == 0); }
    check();
    EXPECT(fs_remove("/programs/tmp") == 0);                                // its root to be freed with it
    EXPECT(fs_unreserve() == 0);
    struct fs_stat st;
    EXPECT(fs_stat("/programs/fib.pages", &st) == 0 && st.depth == 2 && (st.flags & FS_F_FROZEN));
    check(); report();

    begin("03-proof-succeeds");
    const u64 before3 = free_pages();
    Proof p3{"/proofs/p1", &fib, 4, 1, {{0, 1}, {1, 2, 1030}, {2, 3, 500}, {0, 2040, 700}}, {3, 1031}, {1}, 4,
             5u * 1024 * 1024 + 1000, Ending::SUCCESS};
    set_caller(Caller::MASTER);
    EXPECT(fs_mkdir("/proofs") == 0);
    run_proof(p3);
    EXPECT(stats().discards_while_tracing == 0);
    check();
    set_caller(Caller::MASTER);
    EXPECT(fs_stat("/proofs/p1/final", &st) == 0 && (st.flags & FS_F_FROZEN));
    note("kept until the host removes it: %s", "/proofs/p1 (the map, cycle0, start, final)");
    (void)before3;
    report();

    begin("04-proof-fails-in-software");
    Proof p4{"/proofs/p2", &fib, 4, 2, {{0}, {1, 1030}, {2, 3}, {2040}}, {3}, {}, 4, 0, Ending::SOFTWARE_FAILURE};
    run_proof(p4);
    check();
    sh("ls -l /proofs/p2/logs");
    sh_ls("/proofs/p2/logs");
    sh("cat /proofs/p2/logs/tile2.log");
    note("  %s", sh_cat("/proofs/p2/logs/tile2.log").c_str());
    report();

    begin("05-hardware-error");
    Proof p5{"/proofs/p3", &sha, 4, 0, {{0, 300}, {1}, {2}, {3}}, {4}, {}, 4, 0, Ending::HARDWARE_ERROR};
    run_proof(p5);
    power_on(LPDDR, false);                                     // the soft reset: the mount
    set_caller(Caller::SHELL);
    const std::string t2 = sh_cat("/proofs/p3/logs/tile2.log");
    EXPECT(t2.find("about to fail") != std::string::npos);    // fs_shutdown wrote it
    const std::string code1 = sh_cat("/proofs/p3/logs/tile1.code");
    bool ok = code1.size() == log_size(p5, 1, 0);
    for (size_t i = 0; ok && i < code1.size(); i++) ok = uint8_t(code1[i]) == log_byte(1, 0, i);
    EXPECT(ok);
    check();
    note("--- now an error in the file system's own path: no fs_shutdown");
    set_caller(Caller::TILE, 9);
    int fd = fs_open("/proofs/p3/logs/crash.log", FS_APPEND);
    std::vector<uint8_t> b(12000, 'x');
    for (size_t i = 0; i < 3000; i++) b[i] = 'a';
    EXPECT(fs_write(fd, b.data(), 3000) == 3000);
    EXPECT(fs_flush(fd) == 0);
    EXPECT(fs_write(fd, b.data() + 3000, 9000) == 9000);     // past two page boundaries, never flushed
    power_on(LPDDR, false);
    set_caller(Caller::SHELL);
    const std::string crash = sh_cat("/proofs/p3/logs/crash.log");
    EXPECT(crash == std::string(3000, 'a'));                  // what was flushed, and nothing else
    check();                                                  // the pages of the lost part are free again
    note("--- a soft reset between cow_create and cow_commit");
    set_caller(Caller::MASTER);
    EXPECT(fs_reserve_pages(3) == 0);
    EXPECT(cow_create("/proofs/p3/half", 100, &root, &depth) == 0);
    power_on(LPDDR, false);
    set_caller(Caller::MASTER);
    EXPECT(fs_stat("/proofs/p3/half", &st) == 0 && st.pages == 0);
    check();                                                  // the root to be and the reservation are free again
    EXPECT(fs_remove("/proofs/p3/half") == 0);
    check();
    report();

    begin("06-shell");
    sh("ls /"); sh_ls("/");
    sh("ls -l /proofs/p3"); sh_ls("/proofs/p3");
    sh("stat /proofs/p3/start");
    EXPECT(fs_stat("/proofs/p3/start", &st) == 0);
    note("  length %llu, pages %llu, depth %u, copies %u", (unsigned long long)st.length, (unsigned long long)st.pages, st.depth, st.copies);
    sh("tail -n 2 /proofs/p3/logs/tile0.log"); sh_tail("/proofs/p3/logs/tile0.log", 2);
    sh("hexdump -n 16 /proofs/p3/cycle0");
    fd = fs_open("/proofs/p3/cycle0", FS_READ);
    EXPECT(fd >= 0);
    { char x[16]; EXPECT(fs_read(fd, x, 16) == 16); fs_close(fd); }
    sh("grep -c . /proofs/p3/logs/tile1.code   (read in 64-byte calls)");
    {
        const uint64_t h0 = stats().hint_hits;
        const int g = fs_open("/proofs/p3/logs/tile1.code", FS_READ);
        char small[64]; long n2, total = 0;
        while ((n2 = fs_read(g, small, sizeof small)) > 0) total += n2;
        fs_close(g);
        EXPECT(total > 0);
        EXPECT(stats().hint_hits - h0 >= uint64_t(total / 64) - uint64_t(total / 2048) - 2);   // all but a half's first read
        note("  %ld bytes in 64-byte reads: %llu by the fd's hint", total, (unsigned long long)(stats().hint_hits - h0));
    }
    sh("cat /proofs/p3/tracer");
    EXPECT(fs_open("/proofs/p3/tracer", FS_READ) == FS_EBUSY);   // the Tracer's copy: never read directly
    sh("echo hello > /notes");
    fd = fs_open("/notes", FS_APPEND | FS_TRUNC);
    EXPECT(fd >= 0); fs_write(fd, "hello\n", 6); fs_close(fd);
    sh("echo more >> /notes");
    fd = fs_open("/notes", FS_APPEND);
    fs_write(fd, "more\n", 5); fs_close(fd);
    sh("cat /notes");
    EXPECT(sh_cat("/notes") == "hello\nmore\n");
    sh("echo again > /notes");
    fd = fs_open("/notes", FS_APPEND | FS_TRUNC); fs_write(fd, "again\n", 6); fs_close(fd);
    EXPECT(sh_cat("/notes") == "again\n");
    sh("echo x > /programs/fib.pages");
    EXPECT(fs_open("/programs/fib.pages", FS_APPEND | FS_TRUNC) == FS_EROFS);
    sh("rm /programs/fib.pages");
    EXPECT(fs_remove("/programs/fib.pages") == FS_EBUSY);      // it has copies
    sh("mkdir /tmp"); EXPECT(fs_mkdir("/tmp") == 0);
    sh("rmdir /proofs"); EXPECT(fs_remove("/proofs") == FS_ENOTEMPTY);
    sh("rm /notes"); EXPECT(fs_remove("/notes") == 0);
    sh("rmdir /tmp"); EXPECT(fs_remove("/tmp") == 0);
    sh("df");
    { u64 tot, fr; fs_df(&tot, &fr); note("  pages: %llu total, %llu used, %llu free", (unsigned long long)tot, (unsigned long long)(tot - fr), (unsigned long long)fr); }
    check(); report();

    begin("07-remove-working-directories");
    set_caller(Caller::MASTER);
    EXPECT(fs_remove("/proofs/p1/start") == FS_EBUSY);         // final is a copy of it: leaves first
    for (const char* wd : {"/proofs/p1", "/proofs/p2", "/proofs/p3"}) {
        note("--- removing %s, a copy before its source", wd);
        const std::string w = wd;
        // what is left: the logs (p2, p3), the Tracer's copy (p2, p3), the final, start, cycle-0 images, the table
        std::vector<std::string> logs;
        struct fs_dirent e; u32 pos = 0;
        while (fs_list((w + "/logs").c_str(), &pos, &e) == 1) logs.push_back(w + "/logs/" + e.name);
        for (auto& f : logs) EXPECT(fs_remove(f.c_str()) == 0);
        if (!logs.empty() || pos) EXPECT(fs_remove((w + "/logs").c_str()) == 0);
        for (const char* f : {"/tracer", "/final", "/start", "/cycle0", "/map"}) {
            const int rc = fs_remove((w + f).c_str());
            EXPECT(rc == 0 || rc == FS_ENOENT);
        }
        EXPECT(fs_remove(wd) == 0);
    }
    EXPECT(fs_remove("/proofs") == 0);
    EXPECT(fs_stat("/programs/fib.pages", &st) == 0 && st.copies == 0);
    EXPECT(fs_stat("/programs/fib.map", &st) == 0 && st.copies == 0);
    check(); report();

    begin("08-second-proof-on-freed-pages");
    power_on(1024, true);                                      // a small LPDDR, so that freed pages are taken again
    Program small{"/programs", "/programs/small", 1024, {}, 2};
    for (uint64_t pg = 0; pg < 200; pg++) small.pages.push_back(pg);
    store_program(small);
    set_caller(Caller::MASTER);
    EXPECT(fs_mkdir("/proofs") == 0);
    std::vector<std::vector<uint64_t>> segs(4);
    for (uint64_t pg = 0; pg < 400; pg++) segs[pg % 4].push_back(pg * 2);
    Proof a{"/proofs/a", &small, 4, 1, segs, {500, 501}, {0}, 2, 0, Ending::SUCCESS};
    run_proof(a);
    set_caller(Caller::MASTER);
    for (const char* f : {"/proofs/a/final", "/proofs/a/start", "/proofs/a/cycle0", "/proofs/a/map"}) EXPECT(fs_remove(f) == 0);
    EXPECT(fs_remove("/proofs/a") == 0);
    check();
    const uint64_t d0 = stats().discards;
    note("free now: %llu pages, of them may be cached: the next proof's reservation must take some",
         (unsigned long long)free_pages());
    Proof b2 = a; b2.wd = "/proofs/b";
    run_proof(b2);
    EXPECT(stats().discards > d0);                            // pages reused, each discarded first
    EXPECT(stats().discards_while_tracing == 0);              // ... at fs_reserve_pages, not while the Tracer ran
    check(); report();

    begin("09-many-writers-and-full");
    power_on(256, true);                                       // 1 MiB: easy to fill
    set_caller(Caller::MASTER);
    EXPECT(fs_mkdir("/logs") == 0);
    std::vector<int> fds;
    for (int t = 0; t < 128; t++) {                            // 64 tiles' logs, the tracer tile's three, and more
        set_caller(Caller::TILE, t);
        const std::string f = "/logs/w" + std::to_string(t);
        const int x = fs_open(f.c_str(), FS_APPEND);
        EXPECT(x >= 0);
        fds.push_back(x);
        fs_fprintf(x, "writer %d\n", t);
    }
    set_caller(Caller::TILE, 128);
    EXPECT(fs_open("/logs/w128", FS_APPEND) == FS_EBUSY);      // the 129th: every buffer in use
    for (int t = 0; t < 128; t++) { set_caller(Caller::TILE, t); EXPECT(fs_close(fds[t]) == 0); }
    check();
    note("--- the file system filled by one writer");
    set_caller(Caller::TILE, 0);
    fd = fs_open("/big", FS_APPEND);
    EXPECT(fd >= 0);
    std::vector<uint8_t> chunk(3000);
    uint64_t sent = 0;
    long w = 0;
    for (int k = 0; k < 10000 && w >= 0; k++) {
        for (size_t i = 0; i < chunk.size(); i++) chunk[i] = uint8_t((sent + i) * 7);
        w = fs_write(fd, chunk.data(), long(chunk.size()));
        if (w > 0) sent += uint64_t(w);
    }
    EXPECT(w == FS_ENOSPC);
    note("fs_write failed after %llu bytes accepted", (unsigned long long)sent);
    const int closed = fs_close(fd);
    EXPECT(closed == 0 || closed == FS_ENOSPC);
    EXPECT(fs_stat("/big", &st) == 0);
    EXPECT(st.length % 4096 == 0 && st.length > 0 && st.length <= sent);   // the pages stored, and no more
    note("kept: %llu bytes in %llu pages", (unsigned long long)st.length, (unsigned long long)st.pages);
    {
        const int r2 = fs_open("/big", FS_READ);
        std::vector<uint8_t> back(st.length);
        EXPECT(fs_read(r2, back.data(), long(back.size())) == long(back.size()));
        bool same = true;
        for (uint64_t i = 0; i < back.size() && same; i++) same = back[i] == uint8_t(i * 7);
        EXPECT(same);
        fs_close(r2);
    }
    check();                                                   // no page taken and in no file
    {   // a new file in a full file system: its entry fits in a directory page already there; its first byte does not
        const int m = fs_open("/logs/more", FS_APPEND);
        EXPECT(m >= 0);
        EXPECT(fs_write(m, "x", 1) == 1);                      // into the buffer
        EXPECT(fs_close(m) == FS_ENOSPC);                      // no page for it: the file keeps nothing
        EXPECT(fs_stat("/logs/more", &st) == 0 && st.length == 0);
        check();
    }
    set_caller(Caller::SHELL);
    EXPECT(fs_remove("/big") == 0);
    check(); report();

    if (g_out) std::fclose(g_out);
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
