// seed.cpp: a test's file system, made through the API itself (NOTES 1.2, owner 2026-11-20). Each line of a .seed
// file is a few calls, made as the caller who makes such a thing on the chip:
//
//   cap <pages>                              power_on(pages, hard): a format. Without it, 16,384 pages
//   dir <path>                               fs_mkdir, as the master tile
//   file <path> [nonl]                       a streamed file, as a compute tile: the lines up to a line ".", each
//                                            with its \n (nonl: none after the last). An existing file is appended to
//   gen <path> <bytes> lines|bytes|char:<c>  the same, generated: "line N\n" from 1, bytes 0-255, or one byte
//   program <stem> <length_pages> <pages>    a stored program (FS-API 3.2): <stem>.pages, those pages written, and
//                                            <stem>.map, 4 pages; both frozen
//   cow <path> <length_pages> <pages> [frozen]   a COW file as the master tile makes one: cow_create, a run, cow_commit
//   copy <src> <dst> <pages> [frozen]        cow_copy of a frozen COW file, a run writing <pages>, cow_commit
//   proof <wd> <stem> <tiles>                a working directory as a proof that failed in software leaves it
//                                            (FS-API 3.3, 3.4, 3.6): map, cycle0, start, final (frozen), tracer
//                                            (never frozen), logs/ with each tile's three run logs and its own log,
//                                            held open through the segment; every tile writes all its logs
//   open <path> r|w                          the file left open by a compute tile, to read or to write
//   reset                                    power_on(pages, soft): a mount
//
// <pages> is a list like 0,1,1030, or - for none. Blank lines and lines from # are ignored.
//
// The Updater-driving code is copied from v1.40_wip/fs/tests/workflows.cpp at commit 66c2936 (static there, and
// v1.40_wip/fs/ is not refactored): feed() and run() from its feed and run, program from store_program, proof from
// run_proof, log_byte and log_size as they are there. Each copy is marked "from workflows.cpp".
#include "seed.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../fs/fs.h"
#include "../../fs/fs_emu.h"
#include "../../fs/stand_ins.h"

using namespace fsemu;

namespace {

std::string g_where;
uint32_t g_cap = 16384;
bool g_powered = false;
Updater g_updater;
std::map<std::string, std::vector<uint64_t>> g_programs;    // a stem's pages

[[noreturn]] void fail(const std::string& what) {
    std::fprintf(stderr, "seed: %s: %s\n", g_where.c_str(), what.c_str());
    std::exit(3);
}
void ok(int rc, const char* call) {
    if (rc < 0) fail(std::string(call) + " = " + std::to_string(rc));
}
void power() {
    if (!g_powered) { power_on(g_cap, true); g_powered = true; }
}

std::vector<uint64_t> page_list(const std::string& s) {
    std::vector<uint64_t> v;
    if (s == "-") return v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) v.push_back(std::strtoull(x.c_str(), nullptr, 0));
    return v;
}
uint32_t depth_for(uint64_t pages) { return pages <= 1024 ? 1 : pages <= (1u << 20) ? 2 : 3; }

void write_file(const std::string& path, const std::string& data) {
    set_caller(Caller::TILE, 0);
    const int fd = fs_open(path.c_str(), FS_APPEND);
    ok(fd, "fs_open");
    for (size_t at = 0; at < data.size();) {
        const size_t n = std::min<size_t>(65536, data.size() - at);
        if (fs_write(fd, data.data() + at, long(n)) != long(n)) fail("fs_write");
        at += n;
    }
    ok(fs_close(fd), "fs_close");
}

// ---- from workflows.cpp: feed and run
void feed(uint64_t n) {
    while (n) {
        const u32 k = u32(std::min<uint64_t>(128, n));
        u32 b[128];
        const int got = fs_acquire_cow_pages(b, k);
        if (got != int(k)) fail("fs_acquire_cow_pages");
        for (int i = 0; i < got; i++) g_updater.push(b[i]);
        n -= u32(got);
    }
}
void run(const Run& c, const std::vector<uint64_t>& words) {
    feed(c.page_count);
    if (g_updater.run(c, words) != 0 || g_updater.queued() != 0) fail("an Updater run's pages");
}

// A COW file's root and depth: held with FS_ROOT just long enough to read them (FS-API 5.3), as the master tile does.
int root_of(const std::string& path, u32* root, u32* depth) {
    const int fd = fs_open(path.c_str(), FS_ROOT);
    if (fd < 0) return fd;
    const int r = cow_root(fd, root, depth);
    fs_close(fd);
    return r;
}

// a COW file made empty (root 1) or a copy (root 2), with pages written, committed, frozen if asked
void make_cow(const std::string& path, const std::string& src, uint64_t length, const std::vector<uint64_t>& pages,
              bool frozen, uint64_t seed) {
    set_caller(Caller::MASTER);
    u32 root, depth, sroot = 0, sdepth;
    uint32_t d;
    if (src.empty()) d = depth_for(length);
    else { ok(root_of(src, &sroot, &sdepth), "cow_root"); d = sdepth; }
    const uint64_t n = pages_for(d, pages);
    ok(fs_reserve_pages(u32(n + 1)), "fs_reserve_pages");
    if (src.empty()) ok(cow_create(path.c_str(), length, &root, &depth), "cow_create");
    else ok(cow_copy(src.c_str(), path.c_str(), &root, &depth), "cow_copy");
    Run c; c.root = src.empty() ? 1 : 2; c.depth = depth; c.image_root = root; c.source_root = sroot; c.page_count = n;
    run(c, words_for(pages, seed));
    ok(cow_commit(path.c_str()), "cow_commit");
    if (frozen) ok(cow_freeze(path.c_str()), "cow_freeze");
    ok(fs_unreserve(), "fs_unreserve");
}

// ---- from workflows.cpp: log_byte, log_size, run_proof (a failure in software; every tile writes its logs)
const char* KIND[3] = {"code", "read", "write"};
uint8_t log_byte(uint32_t tile, uint32_t kind, uint64_t i) { return uint8_t(tile * 7 + kind * 13 + i * 31 + (i >> 9)); }
uint64_t log_size(uint32_t t, uint32_t k) { return 3000 + 9000 * ((t * 3 + k) % 5); }

std::vector<uint64_t> uniq(std::vector<uint64_t> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

void proof(const std::string& wd, const std::string& stem, uint32_t tiles) {
    auto it = g_programs.find(stem);
    if (it == g_programs.end() || it->second.empty()) fail("no such program, or one with no pages");
    const std::vector<uint64_t>& pp = it->second;
    // four segments, s = 1: segment k writes the program's page k (cycled); frame 6 its last page; the table page 0
    std::vector<std::vector<uint64_t>> seg;
    for (size_t k = 0; k < 4; k++) seg.push_back({pp[k % pp.size()]});
    const std::vector<uint64_t> data = {pp.back()}, delta = {0};
    const std::vector<uint64_t> before = seg[0], mine = seg[1], after = uniq({seg[1][0], seg[2][0], seg[3][0]});

    set_caller(Caller::MASTER);
    ok(fs_mkdir(wd.c_str()), "fs_mkdir");
    const std::string logs = wd + "/logs";
    ok(fs_mkdir(logs.c_str()), "fs_mkdir");
    const std::string img = stem + ".pages", base = stem + ".map";
    struct fs_stat st;
    ok(fs_stat(img.c_str(), &st), "fs_stat");
    const uint32_t d = st.depth;
    ok(fs_stat(base.c_str(), &st), "fs_stat");
    const uint32_t md = st.depth;
    const uint64_t n_map = pages_for(md, delta), n_c0 = pages_for(d, data), n_start = pages_for(d, before),
                   n_tracer = pages_for(d, mine), n_final = pages_for(d, after);
    const uint64_t total = n_map + n_c0 + n_start + 2 * n_tracer + (n_final - n_tracer) + 5;
    u32 root, depth, src, sd;
    ok(fs_reserve_pages(u32(total)), "fs_reserve_pages");
    const std::string map = wd + "/map", c0 = wd + "/cycle0", start = wd + "/start", fin = wd + "/final",
                      tr = wd + "/tracer";
    ok(root_of(base, &src, &sd), "cow_root");
    ok(cow_copy(base.c_str(), map.c_str(), &root, &depth), "cow_copy");
    Run c; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_map;
    run(c, words_for(delta, 5));
    ok(cow_commit(map.c_str()), "cow_commit"); ok(cow_freeze(map.c_str()), "cow_freeze");
    ok(root_of(img, &src, &sd), "cow_root");
    ok(cow_copy(img.c_str(), c0.c_str(), &root, &depth), "cow_copy");
    c = Run{}; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_c0;
    run(c, words_for(data, 6));
    ok(cow_commit(c0.c_str()), "cow_commit"); ok(cow_freeze(c0.c_str()), "cow_freeze");
    ok(root_of(c0, &src, &sd), "cow_root");
    ok(cow_copy(c0.c_str(), start.c_str(), &root, &depth), "cow_copy");
    c = Run{}; c.root = 2; c.depth = depth; c.image_root = root; c.source_root = src; c.page_count = n_start;
    run(c, words_for(before, 90));
    ok(cow_commit(start.c_str()), "cow_commit"); ok(cow_freeze(start.c_str()), "cow_freeze");
    ok(root_of(start, &src, &sd), "cow_root");
    u32 froot, troot;
    ok(cow_copy(start.c_str(), fin.c_str(), &froot, &depth), "cow_copy");
    ok(cow_copy(start.c_str(), tr.c_str(), &troot, &depth), "cow_copy");
    c = Run{}; c.root = 2; c.depth = depth; c.tracer_copy = true; c.image_root = froot; c.tracer_root = troot;
    c.source_root = src; c.page_count = 2 * n_tracer;
    run(c, words_for(mine, 91));
    ok(cow_commit(fin.c_str()), "cow_commit"); ok(cow_commit(tr.c_str()), "cow_commit");
    std::vector<uint64_t> rest;
    for (uint64_t p : after) if (std::find(mine.begin(), mine.end(), p) == mine.end()) rest.push_back(p);
    c = Run{}; c.root = 0; c.depth = depth; c.image_root = froot; c.page_count = n_final - n_tracer;
    run(c, words_for(rest, 92));
    u32 croot, cdepth;
    ok(root_of(tr, &croot, &cdepth), "cow_root");
    set_tracing(true);
    for (uint64_t pg : mine) if (!tracer_write(croot, cdepth, pg, 7, 0xC0FFEE)) fail("tracer_write");
    // the logs: each tile's own, open through the segment; the run logs, three at a time, by the tracer tile
    // the logs: each tile's own, open through the segment (128 writers: FS-API 4.6); the run logs, three at a time,
    // by the tracer tile
    std::vector<int> fd(tiles);
    for (uint32_t t = 0; t < tiles; t++) {
        set_caller(Caller::TILE, int(t));
        fd[t] = fs_open((logs + "/tile" + std::to_string(t) + ".log").c_str(), FS_APPEND);
        ok(fd[t], "fs_open");
        fs_fprintf(fd[t], "tile %u: segment 1 starts\n", t);
    }
    const uint32_t fail_at = tiles / 2;
    for (uint32_t t = 0; t < tiles; t++) {
        set_caller(Caller::TRACER);
        for (uint32_t k = 0; k < 3; k++) {
            const int f = fs_open((logs + "/tile" + std::to_string(t) + "." + KIND[k]).c_str(), FS_APPEND);
            ok(f, "fs_open");
            const uint64_t n = log_size(t, k);
            std::vector<uint8_t> b(n);
            for (uint64_t i = 0; i < n; i++) b[i] = log_byte(t, k, i);
            if (fs_write(f, b.data(), long(n)) != long(n)) fail("fs_write");
            ok(fs_close(f), "fs_close");
        }
        set_caller(Caller::TILE, int(t));
        fs_fprintf(fd[t], "tile %u: window %u replayed\n", t, t);
        if (t == fail_at) fs_fprintf(fd[t], "tile %u: check failed at row 12345\n", t);
    }
    set_tracing(false);
    for (uint32_t t = 0; t < tiles; t++) {
        set_caller(Caller::TILE, int(t));
        fs_fprintf(fd[t], "tile %u: done\n", t);
        ok(fs_close(fd[t]), "fs_close");
    }
    set_caller(Caller::MASTER);
    ok(cow_freeze(fin.c_str()), "cow_freeze");
    ok(fs_unreserve(), "fs_unreserve");
}

}  // namespace

std::vector<std::string> g_seed_open;   // the paths left open, for main.cpp
bool g_seed_writer = false;

void seed_run(const char* file) {
    if (!file) { power(); return; }
    FILE* f = std::fopen(file, "r");
    if (!f) { std::fprintf(stderr, "seed: cannot open %s\n", file); std::exit(3); }
    std::vector<std::string> lines;
    char b[8192];
    while (std::fgets(b, sizeof b, f)) {
        std::string s(b);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        lines.push_back(s);
    }
    std::fclose(f);
    for (size_t i = 0; i < lines.size(); i++) {
        g_where = std::string(file) + ":" + std::to_string(i + 1);
        std::stringstream ss(lines[i]);
        std::vector<std::string> w;
        std::string x;
        while (ss >> x) w.push_back(x);
        if (w.empty() || w[0][0] == '#') continue;
        const std::string& k = w[0];
        auto need = [&](size_t n) { if (w.size() < n) fail("too few words"); };
        if (k == "cap") {
            need(2);
            if (g_powered) fail("cap after the file system is made");
            g_cap = uint32_t(std::strtoul(w[1].c_str(), nullptr, 0));
            power();
            continue;
        }
        power();
        if (k == "dir") {
            need(2); set_caller(Caller::MASTER); ok(fs_mkdir(w[1].c_str()), "fs_mkdir");
        } else if (k == "file") {
            need(2);
            std::string data;
            size_t j = i + 1;
            for (; j < lines.size() && lines[j] != "."; j++) data += lines[j] + "\n";
            if (j == lines.size()) fail("no line \".\"");
            if (w.size() > 2 && w[2] == "nonl" && !data.empty()) data.pop_back();
            write_file(w[1], data);
            i = j;
        } else if (k == "gen") {
            need(4);
            const uint64_t n = std::strtoull(w[2].c_str(), nullptr, 0);
            std::string data;
            if (w[3] == "lines") {
                for (uint64_t ln = 1; data.size() < n; ln++) data += "line " + std::to_string(ln) + "\n";
                data.resize(n);
            } else if (w[3] == "bytes") {
                for (uint64_t j = 0; j < n; j++) data += char(j & 255);
            } else if (w[3].compare(0, 5, "char:") == 0 && w[3].size() == 6) {
                data.assign(n, w[3][5]);
            } else fail("gen: lines, bytes or char:<c>");
            write_file(w[1], data);
        } else if (k == "program") {
            need(4);
            const std::vector<uint64_t> p = page_list(w[3]);
            const uint64_t len = std::strtoull(w[2].c_str(), nullptr, 0);
            make_cow(w[1] + ".pages", "", len, p, true, 1);
            make_cow(w[1] + ".map", "", 4, {0, 1, 2, 3}, true, 2);
            g_programs[w[1]] = p;
        } else if (k == "cow") {
            need(4);
            make_cow(w[1], "", std::strtoull(w[2].c_str(), nullptr, 0), page_list(w[3]), w.size() > 4 && w[4] == "frozen", 3);
        } else if (k == "copy") {
            need(4);
            make_cow(w[2], w[1], 0, page_list(w[3]), w.size() > 4 && w[4] == "frozen", 4);
        } else if (k == "proof") {
            need(4);
            proof(w[1], w[2], uint32_t(std::strtoul(w[3].c_str(), nullptr, 0)));
        } else if (k == "open") {
            need(3);
            set_caller(Caller::TILE, 60);
            const int fd = fs_open(w[1].c_str(), w[2] == "w" ? FS_APPEND : FS_READ);
            ok(fd, "fs_open");
            g_seed_open.push_back(w[1]);
            if (w[2] == "w") g_seed_writer = true;
        } else if (k == "reset") {
            power_on(g_cap, false);
            g_seed_open.clear();
            g_seed_writer = false;
        } else {
            fail("unknown line");
        }
    }
}
