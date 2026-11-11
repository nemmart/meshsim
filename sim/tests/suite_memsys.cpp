// suite_memsys.cpp (WORKER21; P7a, inventory C21): v1.3's MemorySystemTest (sim/ref/meshsim/test/MemorySystemTest.java)
// on the C++ FileStore and MemSystem, every assertion in Java's order, on W20's suite.h. Its output must equal v1.3's
// log of run_tests.sh's step `memsys` (tests/tracersuites/memsys.log, from oracle/ref/tracersuites.sh).
//
// The Tracer's black-box memory system (WORKER22, B1; spec 4.21.5, 7; DECISIONS-v1.3 sections 20-22; SPEC-QUESTIONS Q1,
// Q2, Q12, Q13):
//  - FileStore: depths, holes reading zero, cow_open and path copying, the four statuses (ok only with every entry on the
//    path owned, Q1; beyond the depth's capacity, Q2), a dense file of depth 1;
//  - the shared cache's timing: a cold depth-2 read (three misses), warm reads, an index line shared by 32 pages, the set
//    mapping and LRU replacement, the free-or-clean victim policy with its early evictions, the write buffer draining one
//    line per 80 cycles and a read miss waiting for it to empty, a write-allocate without an LPDDR read, cold().
// Usage: suite_memsys (from sim/)
#include <cstdint>
#include <vector>

#include "suite.h"
#include "units/file_store.h"
#include "units/mem_system.h"

using units::tracer::FileStore;
using units::tracer::MemSystem;

namespace {

std::vector<uint64_t> page(int64_t seed) {
    std::vector<uint64_t> p(512);
    for (int i = 0; i < 512; i++) p[size_t(i)] = uint64_t(seed * 1000003LL + i);
    return p;
}
// Java's fs.walk(...) returns its Walk; here walk fills one.
FileStore::Walk& walk(const FileStore& fs, int32_t root, int depth, int64_t line, FileStore::Walk& w) {
    fs.walk(root, depth, line, w);
    return w;
}
int64_t I(uint64_t v) { return int64_t(v); }

void statuses() {
    suite::group("FileStore: statuses");
    FileStore fs;
    FileStore::File img = fs.create(2);
    // pages 0, 1 (same leaf index page), 5000 (another leaf: 5000 >> 10 = 4), and 1<<20 - 1 (the last of depth 2)
    const int64_t pgs[] = {0, 1, 5000, (int64_t(1) << 20) - 1};
    for (int64_t p : pgs) fs.put_page(img, p, page(p + 7).data());
    FileStore::Walk w;
    suite::eq(FileStore::ST_OK, walk(fs, img.root, 2, 0, w).status, "the image owns every path");
    suite::eq(3, w.n_lines, "a depth-2 walk reads two index lines and the data line");
    suite::eq(2, w.index_lines, "two of them index lines");
    suite::eq(FileStore::ST_HOLE, walk(fs, img.root, 2, 2 * 32, w).status, "page 2 is a hole (an entry of the existing leaf)");
    suite::eq(FileStore::ST_HOLE, walk(fs, img.root, 2, 3000LL * 32, w).status,
              "page 3000's leaf index page is a hole (stops a level up)");
    suite::eq(1, w.n_lines, "a hole at the root's entry: one index line read");
    suite::eq(0, I(fs.load64(img, 2 * 4096 + 8)), "a hole reads as zero");
    suite::eq(I(page(5007)[17]), I(fs.load64(img, 5000LL * 4096 + 17 * 8)), "the doubleword at an offset");
    suite::eq(FileStore::ST_BEYOND, walk(fs, img.root, 2, int64_t(1) << 25, w).status,
              "beyond the depth's capacity (Q2): status 3");
    suite::eq(0, w.n_lines, "status 3 reads nothing");
    suite::eq(FileStore::ST_OK, walk(fs, img.root, 2, (int64_t(1) << 25) - 1, w).status, "the capacity's last line");

    suite::group("FileStore: cow_open and path copying (Q1)");
    FileStore::File cp = fs.cow_open(img);
    fs.make_private(cp, 1);  // the segment writes page 1
    suite::eq(FileStore::ST_OK, walk(fs, cp.root, 2, 1 * 32 + 5, w).status,
              "a pre-copied page: every entry on its path owned");
    suite::eq(FileStore::ST_NOT_OWNED, walk(fs, cp.root, 2, 0, w).status,
              "page 0, beside it in the now-private leaf: not owned");
    suite::eq(FileStore::ST_NOT_OWNED, walk(fs, cp.root, 2, 5000LL * 32, w).status,
              "page 5000, under a shared leaf whose entry reads owned = 1 in the source: not owned (Q1: the path, not the leaf)");
    suite::eq(FileStore::ST_HOLE, walk(fs, cp.root, 2, 2 * 32, w).status, "a hole stays a hole in the copy");
    suite::eq(I(page(8)[3]), I(fs.load64(cp, 4096 + 24)), "the copy reads the same data");
    uint64_t line[16];
    for (auto& x : line) x = 0x5A5A;
    walk(fs, cp.root, 2, 32, w);
    fs.write_line(w.data_line, line);
    suite::eq(0x5A5A, I(fs.load64(cp, 4096)), "a write to the copy's private page");
    suite::eq(I(page(8)[0]), I(fs.load64(img, 4096)), "leaves the source unchanged");
    suite::throws([&] { fs.make_private(cp, 2); }, "pre-copying a hole is refused");

    suite::group("FileStore: a dense depth-1 file and depth 0");
    FileStore::File map = fs.create(FileStore::depth_for(int64_t(4) << 20));
    suite::eq(1, map.depth, "4 MB is depth 1");
    for (int p = 0; p < 1024; p++) fs.put_page(map, p, page(p).data());
    suite::eq(FileStore::ST_OK, walk(fs, map.root, 1, 1023LL * 32 + 31, w).status, "the last line");
    suite::eq(FileStore::ST_BEYOND, walk(fs, map.root, 1, 1024LL * 32, w).status, "one past it: beyond (Q2)");
    suite::eq(2, w.n_lines + 2, "nothing read beyond");
    FileStore::File d0 = fs.create(0);
    fs.put_page(d0, 0, page(9).data());
    suite::eq(FileStore::ST_OK, walk(fs, d0.root, 0, 31, w).status, "depth 0: the page itself");
    suite::eq(1, w.n_lines, "depth 0: the data line only");
    suite::eq(FileStore::ST_BEYOND, walk(fs, d0.root, 0, 32, w).status, "depth 0 holds 32 lines");
    suite::eq(0, FileStore::depth_for(4096), "4 KB is depth 0");
    suite::eq(2, FileStore::depth_for(int64_t(4) << 30), "4 GB is depth 2");
    suite::eq(3, FileStore::depth_for((int64_t(4) << 30) + 1), "above 4 GB is depth 3");
}

void timing() {
    suite::group("the shared cache: timing");
    FileStore fs;
    FileStore::File img = fs.create(2);
    for (int64_t p = 0; p < 64; p++) fs.put_page(img, p, page(p).data());
    MemSystem ms(&fs);
    MemSystem::Response r;
    ms.read(100, img.root, 2, 0, r);
    suite::eq(100 + 3 * 80, r.ready_at, "cold: two index lines and the data line, each from LPDDR");
    suite::eq(I(page(0)[0]), I(r.data[0]), "the line's data");
    suite::eq(FileStore::ST_OK, r.status, "status ok");
    suite::check(!ms.idle(r.ready_at - 1), "one request outstanding: busy until its data");
    suite::throws([&] { MemSystem::Response r2; ms.read(r.ready_at - 1, img.root, 2, 1, r2); },
                  "a second request while busy is refused");
    int64_t t = r.ready_at;
    ms.read(t, img.root, 2, 0, r);
    suite::eq(t + 3 * 6, r.ready_at, "warm: three hits");
    t = r.ready_at;
    ms.read(t, img.root, 2, 1, r);
    suite::eq(t + 6 + 6 + 80, r.ready_at, "the next line of the page: index lines hit, the data line misses");
    t = r.ready_at;
    ms.read(t, img.root, 2, 31LL * 32, r);
    suite::eq(t + 6 + 6 + 80, r.ready_at, "page 31: the same leaf index line as page 0 (32 entries a line)");
    t = r.ready_at;
    ms.read(t, img.root, 2, 40LL * 32, r);
    suite::eq(t + 6 + 80 + 80, r.ready_at, "page 40: the next leaf index line (pages 32-63), a miss");
    suite::eq(160 + 12 + 12 + 12 + 86, ms.walk_cycles, "the walk's cycles");
    ms.read(r.ready_at, img.root, 2, 3LL * 32, r);
    suite::eq(FileStore::ST_OK, r.status, "page 3 ok");
    ms.read(r.ready_at, img.root, 2, 70LL * 32, r);
    suite::eq(FileStore::ST_HOLE, r.status, "page 70: a hole");
    suite::eq(0, I(r.data[5]), "reads zeros");
    const int64_t b0 = r.ready_at;
    ms.read(b0, img.root, 2, int64_t(1) << 25, r);
    suite::eq(FileStore::ST_BEYOND, r.status, "beyond: status 3");
    suite::eq(b0 + 1, r.ready_at, "answered at once");
    ms.cold();
    ms.read(1000, img.root, 2, 0, r);
    suite::eq(1000 + 240, r.ready_at, "cold() empties the cache");
    suite::eq(1, ms.reads, "and clears the statistics");
}

void policy() {
    suite::group("the shared cache: the free-or-clean victim policy and the write buffer (Q12, Q13)");
    // a depth-0 file per line address is awkward; use one depth-2 file and pick data lines in one set: line addresses
    // a = s (mod 512). Pages are allocated 32 units apart, so data lines of page p at unit P+l; choose lines of pages
    // whose units are 512 apart (16 pages), same l.
    FileStore fs(32 * 1024);  // pages from unit 32768 (set 0 aligned)
    FileStore::File img = fs.create(2);  // root at 32768, the leaf next, then data pages
    const int pages_n = 16 * 12;
    for (int64_t p = 0; p < pages_n; p++) fs.put_page(img, p, page(p).data());
    MemSystem ms(&fs);
    MemSystem::Response r;
    FileStore::Walk w;
    // the data line 3 of page p: its set
    const int set0 = walk(fs, img.root, 2, 3, w).data_line & 511;
    int in_set[12] = {};
    int n = 0;
    for (int64_t p = 0; p < pages_n && n < 12; p++)
        if ((walk(fs, img.root, 2, p * 32 + 3, w).data_line & 511) == set0) in_set[n++] = int(p);
    suite::eq(12, n, "twelve data lines in one set");
    int64_t t = 0;
    uint64_t line[16] = {};
    // write 8 lines into the set (write-allocates: their index lines read first)
    for (int k = 0; k < 8; k++) {
        t = ms.read(t, img.root, 2, in_set[k] * 32LL + 3, r).ready_at;
        t = ms.write(t, img.root, 2, in_set[k] * 32LL + 3, line);
    }
    suite::eq(0, ms.write_allocates, "a write after a read of the line hits it in place");
    suite::eq(1, ms.early_evictions, "the set full with its LRU line dirty: evicted at once (the 8th write ages line 0)");
    const int64_t before = ms.data_accesses - ms.data_hits;
    t = ms.read(t, img.root, 2, in_set[8] * 32LL + 3, r).ready_at;
    suite::eq(before + 1, ms.data_accesses - ms.data_hits, "a ninth line misses and fills the free way (no dirty victim)");
    suite::eq(2, ms.early_evictions, "the set full again with its LRU line dirty: that one evicted too");
    suite::check(ms.read_waits >= 1, "the read miss waited for the write buffer to empty");
    // a write-allocate: a line never read, written whole
    const int64_t wa = ms.write_allocates;
    const int64_t t0 = t;
    t = ms.write(t, img.root, 2, in_set[9] * 32LL + 3, line);
    suite::eq(wa + 1, ms.write_allocates, "a Tracer write-back missing the cache allocates without an LPDDR read (Q13)");
    suite::eq(t0 + 6 + 6 + 6, t, "its index lines hit and its data line costs a hit");
    // the buffer drains one line per 80 cycles; a read miss waits only until it is empty
    MemSystem m2(&fs);
    int64_t u = 0;
    for (int k = 0; k < 8; k++) {
        u = m2.read(u, img.root, 2, in_set[k] * 32LL + 3, r).ready_at;
        u = m2.write(u, img.root, 2, in_set[k] * 32LL + 3, line);
    }
    const int64_t wait0 = m2.read_wait_cycles;
    const int64_t u1 = m2.read(u, img.root, 2, in_set[10] * 32LL + 3, r).ready_at;
    suite::check(m2.read_wait_cycles - wait0 <= 80, "one buffered line: the wait is at most one drain (80)");
    suite::eq(u1, r.ready_at, "the response's cycle");
}

}  // namespace

int main() {
    statuses();
    timing();
    policy();
    return suite::done("MemorySystemTest");
}
