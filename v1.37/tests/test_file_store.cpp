// test_file_store: the file store's pages by address (v1.37; src/units/file_store.h): a page placed where it is told,
// whole pages read and written, placed pages beside the ones the store hands out itself, and a tree built by hand on
// placed pages walked as any other. (The store's files, statuses and the memory system's timing are suite_memsys'.)
#include <cstring>
#include <vector>

#include "harness.h"
#include "units/file_store.h"

using units::tracer::FileStore;

TEST(file_store_page_addresses) {
    const int64_t limit = int64_t(1) << 25;
    CHECK(FileStore::page_address(0, limit));
    CHECK(FileStore::page_address(32, limit));
    CHECK(FileStore::page_address(limit - 32, limit));
    CHECK(!FileStore::page_address(limit, limit));
    CHECK(!FileStore::page_address(33, limit));
    CHECK(!FileStore::page_address(-32, limit));
}

TEST(file_store_places_a_page_anywhere) {
    FileStore fs;
    CHECK_EQ(fs.pages_allocated(), size_t(0));
    const int32_t far = int32_t((int64_t(1) << 25) - 32), near = 64, top = 0x7FFFFFE0;
    uint64_t pg[512], back[512];
    CHECK(!fs.has_page(far));
    CHECK(!fs.read_page(far, back));                         // never written: zeros, and said so
    for (uint64_t w : back) CHECK_EQ(w, 0u);
    for (int i = 0; i < 512; i++) pg[i] = uint64_t(i) * 0x0101010101010101ull + 7;
    fs.write_page(far, pg);
    fs.write_page(near, pg);
    fs.write_page(top, pg);                                  // the last page a 31-bit address names
    CHECK(fs.has_page(far) && fs.has_page(near) && fs.has_page(top));
    CHECK(!fs.has_page(far - 32) && !fs.has_page(near + 32) && !fs.has_page(96));
    CHECK_EQ(fs.pages_allocated(), size_t(3));
    CHECK(fs.read_page(far, back));
    CHECK(std::memcmp(pg, back, sizeof pg) == 0);
    uint64_t line[16];
    fs.read_line(near + 5, line);                            // a line of a placed page
    CHECK(std::memcmp(line, pg + 16 * 5, sizeof line) == 0);
    uint64_t* p = fs.page_at(128);                           // made of zeros
    CHECK(fs.has_page(128));
    for (int i = 0; i < 512; i++) CHECK_EQ(p[i], 0u);
    CHECK(fs.page_at(128) == p);                             // and found again, where it was
    std::vector<int32_t> order;
    fs.for_each_stored([&](int32_t a, const uint64_t*) { order.push_back(a); });
    CHECK(order == (std::vector<int32_t>{near, 128, far, top}));
}

// A page never moves: a pointer to one holds while thousands more are placed.
TEST(file_store_pages_never_move) {
    FileStore fs;
    uint64_t* first = fs.page_at(32);
    first[3] = 0xABCD;
    for (int k = 2; k < 5000; k++) fs.page_at(32 * k * 7);
    CHECK(fs.page_at(32) == first);
    CHECK_EQ(first[3], 0xABCDu);
    CHECK_EQ(fs.pages_allocated(), size_t(4999));
}

// The store's own addresses are what they were (from `first`, 32 apart), and pass over a page that was placed.
TEST(file_store_hands_out_around_placed_pages) {
    FileStore fs;
    fs.page_at(64);                                          // where the store's second page would go
    const FileStore::File f = fs.create(1);
    CHECK_EQ(f.root, 32);
    uint64_t pg[512] = {1, 2, 3};
    fs.put_page(f, 0, pg);                                   // a data page: 64 is taken, so 96
    CHECK_EQ(FileStore::addr(FileStore::entry_of(fs.raw_page(32), 0)), 96);
    CHECK_EQ(fs.next_free(), int64_t(128));
    CHECK_EQ(fs.load64(f, 8), 2u);
    FileStore plain;                                         // with nothing placed: exactly as before
    const FileStore::File g = plain.create(1);
    plain.put_page(g, 0, pg);
    CHECK_EQ(g.root, 32);
    CHECK_EQ(FileStore::addr(FileStore::entry_of(plain.raw_page(32), 0)), 64);
}

// A tree built by hand on placed pages, as the Updater builds one: the walk's statuses and lines.
TEST(file_store_walks_a_placed_tree) {
    FileStore fs;
    const int32_t root = 32 * 1000, index = 32 * 77, data = 32 * 500000, shared = 32 * 9;
    uint64_t pg[512];
    std::memset(pg, 0xFF, sizeof pg);
    FileStore::set_entry(pg, 3, uint32_t(index) | FileStore::OWNED);
    fs.write_page(root, pg);
    std::memset(pg, 0xFF, sizeof pg);
    FileStore::set_entry(pg, 1000, uint32_t(data) | FileStore::OWNED);
    FileStore::set_entry(pg, 1001, uint32_t(shared));       // not owned
    fs.write_page(index, pg);
    for (int i = 0; i < 512; i++) pg[i] = 0x1000 + uint64_t(i);
    fs.write_page(data, pg);
    fs.write_page(shared, pg);
    FileStore::Walk w;
    const int64_t page = 3 * 1024 + 1000;
    fs.walk(root, 2, page * 32 + 4, w);
    CHECK_EQ(w.status, int(FileStore::ST_OK));
    CHECK_EQ(w.data_line, data + 4);
    CHECK_EQ(w.n_lines, 3);
    CHECK_EQ(w.lines[0], root + (3 >> 5));
    CHECK_EQ(w.lines[1], index + (1000 >> 5));
    fs.walk(root, 2, (page + 1) * 32, w);
    CHECK_EQ(w.status, int(FileStore::ST_NOT_OWNED));
    fs.walk(root, 2, (page + 2) * 32, w);
    CHECK_EQ(w.status, int(FileStore::ST_HOLE));
    fs.walk(root, 2, 5 * 1024 * 32, w);
    CHECK_EQ(w.status, int(FileStore::ST_HOLE));
    FileStore::File f; f.root = root; f.depth = 2;
    CHECK_EQ(fs.load64(f, page * 4096 + 8 * 17), 0x1011u);
}

// The fingerprint's sum is kept at whole-page writes as at line writes.
TEST(file_store_sum_tracks_page_writes) {
    FileStore fs;
    uint64_t pg[512];
    for (int i = 0; i < 512; i++) pg[i] = uint64_t(i % 7);
    fs.write_page(320, pg);
    fs.track_sum(true);
    CHECK_EQ(fs.files_sum(), fs.scan_sum());
    for (int i = 0; i < 512; i++) pg[i] = uint64_t(i) << 20;
    fs.write_page(320, pg);
    fs.write_page(640, pg);
    CHECK_EQ(fs.files_sum(), fs.scan_sum());
}

DEATH_TEST(file_store_no_page_off_a_boundary, "no page can be at 0x21") {
    FileStore fs;
    fs.page_at(33);
}
