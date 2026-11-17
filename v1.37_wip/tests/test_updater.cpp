// test_updater: the Updater black box alone, with no firmware (v1.37; src/units/updater.h), from
// ../v1.4_wip/planning/UPDATER-INTERFACE.md and HOST-FRAMES.md: its registers and their reset, both queues' credits,
// the update words raw and LRU-coded and carried on across runs, the root's commands, copy-on-write by the owned bits
// at every level, the pages taken from the free page queue in order and counted, the Tracer's copy, its errors and its
// statistics. The expectations are a model written apart from it: a file as a map of words, the pages a write takes
// counted from sets (`Model`). Then the fixtures: gold's frame files of the repository's guests (tests/updater/), each
// image's SHA-256 against gold's reference.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "sha256.h"
#include "updater_bench.h"
#include "units/net.h"

namespace {

using units::Updater;
using units::tracer::FileStore;
using updater_bench::Bench;

// ---- a file as the tests expect it: its words, and what it owns
struct Model {
    int depth = 1;
    std::map<uint64_t, uint64_t> words;                 // by 64-bit-word index
    std::set<std::pair<int, uint64_t>> owned;           // (level 1..depth, the page number's top digits): a page the file owns
    Model copy() const { Model m; m.depth = depth; m.words = words; return m; }   // a copy owns nothing but its root
    // Writes a word; returns the pages the file takes for it.
    unsigned write(uint64_t index, uint64_t v) {
        words[index] = v;
        unsigned taken = 0;
        for (int level = 1; level <= depth; level++) taken += owned.insert({level, (index >> 9) >> (10 * (depth - level))}).second;
        return taken;
    }
    unsigned apply(const std::vector<uint64_t>& u) {
        unsigned taken = 0;
        for (size_t i = 0; i < u.size();) {
            const uint64_t start = u[i] & 0xFFFFFFFFu, n = u[i] >> 32;
            for (uint64_t k = 0; k < n; k++) taken += write(start + k, u[i + 1 + k]);
            i += 1 + n;
        }
        return taken;
    }
    std::vector<uint8_t> image() const {
        std::vector<uint8_t> out;
        for (auto it = words.begin(); it != words.end();) {
            const uint64_t page = it->first >> 9;
            uint64_t pg[512] = {};
            bool any = false;
            for (; it != words.end() && it->first >> 9 == page; ++it) { pg[it->first & 511] = it->second; any |= it->second != 0; }
            if (!any) continue;
            for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t((page * 4096) >> (8 * b)));
            for (uint64_t w : pg) for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t(w >> (8 * b)));
        }
        return out;
    }
};

// ---- a frame of update words, raw or LRU-coded (HOST-FRAMES.md), as a one-frame file
struct BitWriter {
    std::vector<uint8_t> bytes;
    size_t bits = 0;
    void put(uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; i++, bits++) {
            if (bits / 8 == bytes.size()) bytes.push_back(0);
            if (v >> i & 1) bytes[bits / 8] |= uint8_t(1u << (bits % 8));
        }
    }
};
frames::File frame_of(const std::vector<uint64_t>& u, bool lru) {
    BitWriter w;
    if (!lru) for (uint64_t x : u) w.put(x, 64);
    else {
        uint64_t table[32] = {};
        for (uint64_t x : u) {
            int hit = -1;
            for (int i = 0; i < 32 && hit < 0; i++) if (table[i] == x) hit = i;
            if (hit >= 0) { w.put(1, 1); w.put(uint64_t(hit), 5); }
            else { w.put(0, 1); if (x >> 32) { w.put(1, 1); w.put(x, 64); } else { w.put(0, 1); w.put(x, 32); } }
            for (int k = hit >= 0 ? hit : 31; k > 0; k--) table[k] = table[k - 1];
            table[0] = x;
        }
    }
    while (w.bytes.size() % 16) w.bytes.push_back(0);
    frames::File f;
    f.bytes.assign(16, 0);
    f.bytes[0] = lru ? 1 : 0;
    f.bytes[8] = 9;
    const uint32_t length = uint32_t(w.bytes.size() / 16);
    for (unsigned b = 0; b < 4; b++) f.bytes[12 + b] = uint8_t(length >> (8 * b));
    f.bytes.insert(f.bytes.end(), w.bytes.begin(), w.bytes.end());
    std::string err;
    if (!f.parse(err)) FATAL("test_updater: frame_of: %s", err.c_str());
    return f;
}

// Random update words for a file of this depth: runs in address order over a few pages, some whole pages, some half
// pages, some single words.
std::vector<uint64_t> random_updates(test::Rng& rng, int depth, unsigned runs) {
    std::vector<uint64_t> u;
    // (a run's start is a 32-bit word index, so a file's first 2^23 pages are all the update words reach)
    const uint64_t pages = depth < 3 ? uint64_t(1) << (10 * depth) : uint64_t(1) << 23;
    uint64_t at = 0;
    for (unsigned r = 0; r < runs; r++) {
        const unsigned kind = unsigned(rng.below(8));
        uint64_t start, n;
        const uint64_t page = (at >> 9) + 1 + (rng.below(4) == 0 ? rng.below(pages / 8) : rng.below(3));
        if (page + 3 >= pages) break;
        if (kind == 0) { start = page << 9; n = 512 * (1 + rng.below(2)); }           // whole pages
        else if (kind == 1) { start = (page << 9) + 256 * rng.below(2); n = 256; }    // a half page
        else if (kind == 2) { start = (page << 9) + 200 + rng.below(100); n = 300 + rng.below(400); }   // across a boundary
        else { start = (page << 9) + rng.below(512); n = 1 + rng.below(12); }         // a few words
        u.push_back(start | n << 32);
        for (uint64_t k = 0; k < n; k++) u.push_back(rng.below(3) == 0 ? rng.below(5) : rng.below(2) ? rng.next() : rng.next() & 0xFFFFFFFFu);
        at = start + n;
    }
    return u;
}

uint32_t load(Bench& b, int w) { uint32_t v = 0; b.u.word(b.t, w, v); return v; }

}  // namespace

// ================================================================================================ the registers
TEST(updater_reset) {
    Bench b;
    CHECK_EQ(b.u.params(), 5);
    CHECK(!b.u.running(b.t));
    CHECK_EQ(b.u.errors(b.t), 0);
    for (int w = 32; w <= 37; w++) CHECK_EQ(load(b, w), 0u);
    for (int w = 48; w <= 53; w++) CHECK_EQ(load(b, w), 0u);
    uint32_t v;
    for (int w : {38, 47, 54, 64, 127}) CHECK(!b.u.word(b.t, w, v));       // unmapped
    CHECK_EQ(b.u.write_word(b.t, 48, 1), 0);                               // block 3 is read-only
    CHECK_EQ(b.u.write_word(b.t, 38, 1), -1);
    // both credits are full at the reset: two counts each (depth 2)
    const uint32_t credits = 1u << Updater::EV_SEGMENT_CREDIT | 1u << Updater::EV_FREE_PAGE_CREDIT;
    CHECK_EQ(b.u.ev.value(b.t + 1), credits);
    b.u.ev.consume(credits);
    CHECK_EQ(b.u.ev.value(b.t + 1), credits);
    b.u.ev.consume(credits);
    CHECK_EQ(b.u.ev.value(b.t + 1), 0u);
    // RUN_CONFIG holds its seven bits
    b.u.write_word(b.t, Updater::W_RUN_CONFIG, 0xFFFFFFFF);
    CHECK_EQ(load(b, Updater::W_RUN_CONFIG), 0x7Fu);
}

// ================================================================================================ the queues
TEST(updater_queues_and_credits) {
    Bench b;
    const int size = int(units::lw::SIZE_128);
    // the segment queue: 512 entries, 8 KB (owner)
    for (unsigned i = 0; i < 512; i++) b.u.accept(b.t, 64, Updater::PART_SEGMENT << 20 | 0x40, size, i, 0);   // the rest of the address is ignored
    CHECK_EQ(b.u.segment_entries(), 512u);
    CHECK_EQ(b.u.errors(b.t), 0);
    b.u.accept(b.t, 64, Updater::PART_SEGMENT << 20, size, 999, 0);        // the 513th: a push into a full queue, dropped
    CHECK_EQ(b.u.segment_entries(), 512u);
    CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_QUEUE_OVERRUN));
    for (unsigned i = 0; i < 256; i++) b.u.accept(b.t, 64, Updater::PART_FREE_PAGES << 20, size, 32 * (i + 1), 0);
    b.u.accept(b.t, 64, Updater::PART_FREE_PAGES << 20, size, 32 * 300, 0);
    CHECK_EQ(b.u.free_page_entries(), 256u);
    // a run that reads 1,024 raw words out of the 512 entries: a credit after 256 entries, another after 512
    const uint32_t credits = 1u << Updater::EV_SEGMENT_CREDIT;
    b.u.ev.consume(credits); b.u.ev.consume(credits);
    const int32_t root = 32 * 400;
    b.u.write_word(b.t, Updater::W_RUN_CONFIG, Bench::config(false, false, false, 1, Updater::ROOT_EMPTY));
    b.u.write_word(b.t, Updater::W_WORD_COUNT, 1024);
    b.u.write_word(b.t, Updater::W_PAGE_COUNT, 10);
    b.u.write_word(b.t, Updater::W_IMAGE_ROOT, uint32_t(root));
    b.u.run(b.t);
    unsigned first_credit_at = 0, second_credit_at = 0;
    for (unsigned c = 0; c < 20000 && b.u.running(b.t); c++) {
        b.u.step(b.t++);
        const unsigned left = b.u.segment_entries();
        if (!first_credit_at && b.u.ev.count_of(Updater::EV_SEGMENT_CREDIT) == 1) first_credit_at = 512 - left;
        if (!second_credit_at && b.u.ev.count_of(Updater::EV_SEGMENT_CREDIT) == 2) second_credit_at = 512 - left;
    }
    CHECK(!b.u.running(b.t));
    CHECK_EQ(first_credit_at, 256u);
    CHECK_EQ(second_credit_at, 512u);
    CHECK_EQ(b.u.segment_entries(), 0u);
}

// ================================================================================================ update words
// Random runs of values, raw and LRU-coded, at depths 1 to 3, into an empty file: the image, the pages taken, and
// PAGE_COUNT 0 with exactly the model's count queued.
TEST(updater_words_raw_and_lru) {
    test::Rng rng(11);
    for (int depth = 1; depth <= 3; depth++)
        for (int lru = 0; lru < 2; lru++)
            for (int round = 0; round < 6; round++) {
                const std::vector<uint64_t> u = random_updates(rng, depth, 40);
                Model m; m.depth = depth;
                const unsigned pages = m.apply(u);
                const frames::File f = frame_of(u, lru);
                CHECK(frames::update_words(f, f.frame[0], u.size()) == u);
                Bench b;
                const int32_t root = b.new_page();
                b.stream(f, f.frame[0]);
                CHECK(b.run({Bench::config(lru, false, false, depth, Updater::ROOT_EMPTY), uint32_t(u.size()), pages, root, 0, 0}));
                CHECK_EQ(b.accounts[0].pages_left, 0u);
                CHECK_EQ(b.u.errors(b.t), 0);
                CHECK_EQ(b.u.unwritten_reads, 0u);
                CHECK(updater_bench::image(b.store, root, depth) == m.image());
                CHECK_EQ(b.store.pages_allocated(), size_t(pages) + 1);           // the pages taken, and the root
                // the pages are taken in the queue's order: every one given out is in the store
                for (int32_t a : b.pages_given) CHECK(b.store.has_page(a));
            }
}

// One stream applied as three runs, cut anywhere (at an odd word, inside a run of values), with `continue`: the same
// image as one run.
TEST(updater_continue) {
    test::Rng rng(12);
    for (int lru = 0; lru < 2; lru++)
        for (int round = 0; round < 12; round++) {
            const std::vector<uint64_t> u = random_updates(rng, 2, 30);
            Model m; m.depth = 2;
            const unsigned pages = m.apply(u);
            const frames::File f = frame_of(u, lru);
            Bench b;
            const int32_t root = b.new_page();
            b.stream(f, f.frame[0]);
            const uint32_t n = uint32_t(u.size()), c1 = uint32_t(rng.below(n)), c2 = c1 + uint32_t(rng.below(n - c1 + 1));
            // every page to the first run that takes any; the next runs take what the last left in the queue (UI: a
            // run's end touches nothing), so their PAGE_COUNT is what the last left
            CHECK(b.run({Bench::config(lru, false, false, 2, Updater::ROOT_EMPTY), c1, pages, root, 0, 0}));
            uint32_t left = b.accounts.back().pages_left;
            CHECK(b.run({Bench::config(lru, true, false, 2, Updater::ROOT_EXISTS), c2 - c1, left, root, 0, 0}));
            left = b.accounts.back().pages_left;
            CHECK(b.run({Bench::config(lru, true, false, 2, Updater::ROOT_EXISTS), n - c2, left, root, 0, 0}));
            CHECK_EQ(b.accounts.back().pages_left, 0u);
            CHECK_EQ(b.u.errors(b.t), 0);
            CHECK(updater_bench::image(b.store, root, 2) == m.image());
        }
}

// Without `continue` a run starts a new frame: the LRU table and the bits left over are cleared.
TEST(updater_new_frame_clears_the_decoder) {
    const std::vector<uint64_t> u1 = {uint64_t(5) | uint64_t(3) << 32, 0x1111111122222222ull, 7, 7};
    const std::vector<uint64_t> u2 = {uint64_t(600) | uint64_t(2) << 32, 0x1111111122222222ull, 0};
    Model m; m.depth = 1; unsigned pages = m.apply(u1); pages += m.apply(u2);
    const frames::File f1 = frame_of(u1, true), f2 = frame_of(u2, true);   // f2's codes are against an empty table
    Bench b;
    const int32_t root = b.new_page();
    b.stream(f1, f1.frame[0]);
    CHECK(b.run({Bench::config(true, false, false, 1, Updater::ROOT_EMPTY), uint32_t(u1.size()), pages, root, 0, 0}));
    b.stream(f2, f2.frame[0]);
    CHECK(b.run({Bench::config(true, false, false, 1, Updater::ROOT_EXISTS), uint32_t(u2.size()), b.accounts.back().pages_left, root, 0, 0}));
    CHECK_EQ(b.accounts.back().pages_left, 0u);
    CHECK(updater_bench::image(b.store, root, 1) == m.image());
}

// ================================================================================================ copies
// A copy shares its source's pages: writing it leaves the source as it was, takes a page at each level it does not own,
// and a page written twice is taken once.
TEST(updater_copy_on_write) {
    test::Rng rng(13);
    for (int depth = 1; depth <= 3; depth++) {
        const std::vector<uint64_t> u1 = random_updates(rng, depth, 50), u2 = random_updates(rng, depth, 50), u3 = random_updates(rng, depth, 20);
        Model base; base.depth = depth;
        const unsigned p1 = base.apply(u1);
        Model copy = base.copy();
        const unsigned p2 = copy.apply(u2);
        const unsigned p3 = copy.apply(u3);                                   // again over the copy: fewer pages
        Bench b;
        const int32_t root = b.new_page();
        const frames::File f1 = frame_of(u1, false), f2 = frame_of(u2, true), f3 = frame_of(u3, false);
        b.stream(f1, f1.frame[0]);
        CHECK(b.run({Bench::config(false, false, false, depth, Updater::ROOT_EMPTY), uint32_t(u1.size()), p1, root, 0, 0}));
        const int32_t root2 = b.new_page();
        b.stream(f2, f2.frame[0]);
        CHECK(b.run({Bench::config(true, false, false, depth, Updater::ROOT_COPY), uint32_t(u2.size()), p2, root2, 0, root}));
        CHECK_EQ(b.accounts.back().pages_left, 0u);
        b.stream(f3, f3.frame[0]);
        CHECK(b.run({Bench::config(false, false, false, depth, Updater::ROOT_EXISTS), uint32_t(u3.size()), p3, root2, 0, 0}));
        CHECK_EQ(b.accounts.back().pages_left, 0u);
        CHECK_EQ(b.u.errors(b.t), 0);
        CHECK_EQ(b.u.unwritten_reads, 0u);
        CHECK(updater_bench::image(b.store, root, depth) == base.image());    // the source, untouched
        CHECK(updater_bench::image(b.store, root2, depth) == copy.image());
        CHECK_EQ(b.store.pages_allocated(), size_t(p1 + p2 + p3) + 2);
    }
}

// tracer_copy: the Tracer's copy holds the source's contents, and owns exactly the pages the run writes (a walk of it
// is `ok` there and `not_owned` or a hole elsewhere); the run takes twice the pages.
TEST(updater_tracer_copy) {
    test::Rng rng(14);
    for (int depth = 1; depth <= 3; depth++) {
        const std::vector<uint64_t> u1 = random_updates(rng, depth, 60), u2 = random_updates(rng, depth, 25);
        Model start; start.depth = depth;
        const unsigned p1 = start.apply(u1);
        Model fin = start.copy();
        const unsigned p2 = fin.apply(u2);
        Bench b;
        const int32_t root = b.new_page();
        const frames::File f1 = frame_of(u1, false), f2 = frame_of(u2, false);
        b.stream(f1, f1.frame[0]);
        CHECK(b.run({Bench::config(false, false, false, depth, Updater::ROOT_EMPTY), uint32_t(u1.size()), p1, root, 0, 0}));
        const int32_t final_root = b.new_page(), tracer_root = b.new_page();
        b.stream(f2, f2.frame[0]);
        CHECK(b.run({Bench::config(false, false, true, depth, Updater::ROOT_COPY), uint32_t(u2.size()), 2 * p2, final_root, tracer_root, root}));
        CHECK_EQ(b.accounts.back().pages_left, 0u);
        CHECK_EQ(b.u.errors(b.t), 0);
        CHECK_EQ(b.u.unwritten_reads, 0u);
        CHECK(updater_bench::image(b.store, root, depth) == start.image());
        CHECK(updater_bench::image(b.store, tracer_root, depth) == start.image());
        CHECK(updater_bench::image(b.store, final_root, depth) == fin.image());
        std::set<uint64_t> written;
        for (const auto& o : fin.owned) if (o.first == depth) written.insert(o.second);
        FileStore::Walk w;
        for (uint64_t page : written) {
            b.store.walk(tracer_root, depth, int64_t(page << 5), w);
            CHECK_EQ(w.status, int(FileStore::ST_OK));
        }
        std::set<uint64_t> others;
        for (const auto& o : start.owned) if (o.first == depth && !written.count(o.second)) others.insert(o.second);
        for (uint64_t page : others) {
            b.store.walk(tracer_root, depth, int64_t(page << 5), w);
            CHECK_EQ(w.status, int(FileStore::ST_NOT_OWNED));
            b.store.walk(root, depth, int64_t(page << 5), w);
            CHECK_EQ(w.status, int(FileStore::ST_OK));
        }
    }
}

// A run of WORD_COUNT 0 makes the root and ends.
TEST(updater_root_only) {
    Bench b;
    const int32_t root = b.new_page(), copy = b.new_page();
    CHECK(b.run({Bench::config(false, false, false, 2, Updater::ROOT_EMPTY), 0, 0, root, 0, 0}));
    CHECK(b.store.has_page(root));
    CHECK_EQ(FileStore::entry_of(b.store.raw_page(root), 0), FileStore::NONE);
    CHECK_EQ(FileStore::entry_of(b.store.raw_page(root), 1023), FileStore::NONE);
    CHECK(b.run({Bench::config(false, false, false, 2, Updater::ROOT_COPY), 0, 0, copy, 0, root}));
    CHECK(b.store.has_page(copy));
    CHECK_EQ(b.store.pages_allocated(), size_t(2));
    CHECK_EQ(b.accounts[0].pages_written, 1u);
    CHECK_EQ(b.accounts[1].pages_read, 1u);
    CHECK_EQ(b.accounts[1].pages_written, 1u);
}

// ================================================================================================ errors
TEST(updater_out_of_pages) {
    const std::vector<uint64_t> u = {uint64_t(0) | uint64_t(2) << 32, 1, 2, uint64_t(5000) | uint64_t(1) << 32, 3};
    const frames::File f = frame_of(u, false);
    Bench b;
    const int32_t root = b.new_page();
    b.stream(f, f.frame[0]);
    CHECK(!b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), uint32_t(u.size()), 1, root, 0, 0}));   // two pages are needed
    CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_OUT_OF_PAGES));
    CHECK(b.u.running(b.t));                                               // the run stopped: busy stays, no done
    CHECK_EQ(b.u.ev.count_of(Updater::EV_DONE), 0u);
}
TEST(updater_beyond) {
    const std::vector<uint64_t> u = {uint64_t(1024 * 512) | uint64_t(1) << 32, 1};   // depth 1 reaches 1,024 pages
    const frames::File f = frame_of(u, false);
    Bench b;
    const int32_t root = b.new_page();
    b.stream(f, f.frame[0]);
    CHECK(!b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), uint32_t(u.size()), 4, root, 0, 0}));
    CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_BEYOND));
    Bench b2;                                                              // the last word the depth reaches is not beyond
    const std::vector<uint64_t> u2 = {uint64_t(1024 * 512 - 1) | uint64_t(1) << 32, 1};
    const frames::File f2 = frame_of(u2, false);
    const int32_t root2 = b2.new_page();
    b2.stream(f2, f2.frame[0]);
    CHECK(b2.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), uint32_t(u2.size()), 1, root2, 0, 0}));
    CHECK_EQ(b2.u.errors(b2.t), 0);
}
TEST(updater_lpddr_error) {
    {   // a root off a page boundary
        Bench b;
        CHECK(!b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), 0, 0, 33, 0, 0}));
        CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_LPDDR));
    }
    {   // a root past LPDDR's end
        Bench b;
        CHECK(!b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), 0, 0, int32_t(b.u.lpddr_units), 0, 0}));
        CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_LPDDR));
    }
    {   // a free page that is no page
        const std::vector<uint64_t> u = {uint64_t(0) | uint64_t(1) << 32, 1};
        const frames::File f = frame_of(u, false);
        Bench b;
        const int32_t root = b.new_page();
        b.next_page = 0x7FFFFF;                                            // the bench's next page: past LPDDR
        b.stream(f, f.frame[0]);
        CHECK(!b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), uint32_t(u.size()), 1, root, 0, 0}));
        CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_LPDDR));
    }
}

DEATH_TEST(updater_start_while_busy_is_ub, "a START during a run") {
    Bench b;
    b.u.write_word(b.t, Updater::W_WORD_COUNT, 4);
    b.u.write_word(b.t, Updater::W_IMAGE_ROOT, 32);
    b.u.write_word(b.t, Updater::W_RUN_CONFIG, Bench::config(false, false, false, 1, Updater::ROOT_EMPTY));
    b.u.run(b.t);
    b.u.step(b.t++);
    b.u.run(b.t);
}
DEATH_TEST(updater_store_while_busy_is_ub, "a store to word 33 of block 2 during a run") {
    Bench b;
    b.u.write_word(b.t, Updater::W_WORD_COUNT, 4);
    b.u.write_word(b.t, Updater::W_IMAGE_ROOT, 32);
    b.u.write_word(b.t, Updater::W_RUN_CONFIG, Bench::config(false, false, false, 1, Updater::ROOT_EMPTY));
    b.u.run(b.t);
    b.u.write_word(b.t, Updater::W_WORD_COUNT, 5);
}
DEATH_TEST(updater_write_of_32_bits_is_ub, "another size than 128 bits") {
    Bench b;
    b.u.accept(b.t, 64, Updater::PART_SEGMENT << 20, int(units::lw::SIZE_32), 1, 0);
}
DEATH_TEST(updater_unmapped_part_is_ub, "part 3 of its region") {
    Bench b;
    b.u.accept(b.t, 64, 3u << 20, int(units::lw::SIZE_128), 1, 0);
}

// ================================================================================================ the statistics
// What is read, by UPDATER-INTERFACE.md's table: a whole page into a hole or over a shared page reads nothing; a few
// words into a shared page read the halves they touch, and the other half when the page is written to its new place; a
// half page over a shared page reads the other half; into an owned page a half is read when a word needs it.
TEST(updater_statistics) {
    std::vector<uint64_t> whole = {uint64_t(0) | uint64_t(1024) << 32};
    for (unsigned i = 0; i < 1024; i++) whole.push_back(i + 1);
    const frames::File f1 = frame_of(whole, false);
    Bench b;
    const int32_t root = b.new_page();
    b.stream(f1, f1.frame[0]);
    CHECK(b.run({Bench::config(false, false, false, 1, Updater::ROOT_EMPTY), uint32_t(whole.size()), 2, root, 0, 0}));
    CHECK_EQ(b.accounts[0].pages_read, 0u);
    CHECK_EQ(b.accounts[0].pages_written, 3u);                             // the root, and two pages
    // waited out: the root's write; for each page a look-up, its write and its entry
    CHECK_EQ(b.accounts[0].stall_memory, uint32_t(336 + 2 * (6 + 336 + 80)));
    // the cycles: the root's, one for each word, the last page's write and the end's, and what was waited out
    CHECK_EQ(b.accounts[0].cycles, uint32_t(1 + 1025 + 2) + b.accounts[0].stall_memory + b.accounts[0].stall_data);
    // a copy: three words in the first half of page 0; the second half of page 1 whole
    std::vector<uint64_t> part = {uint64_t(3) | uint64_t(3) << 32, 9, 9, 9, uint64_t(512 + 256) | uint64_t(256) << 32};
    for (unsigned i = 0; i < 256; i++) part.push_back(77);
    const frames::File f2 = frame_of(part, false);
    const int32_t copy = b.new_page();
    b.stream(f2, f2.frame[0]);
    CHECK(b.run({Bench::config(false, false, false, 1, Updater::ROOT_COPY), uint32_t(part.size()), 2, copy, 0, root}));
    // read: the source's root; page 0's first half (three words), then its second at the write; page 1's first half at the write
    CHECK_EQ(b.accounts[1].pages_read, 4u);
    CHECK_EQ(b.accounts[1].pages_written, 3u);
    CHECK_EQ(b.accounts[1].pages_left, 0u);
    // the copy again, now its own: one word of page 0's second half reads that half and writes it
    const std::vector<uint64_t> one = {uint64_t(300) | uint64_t(1) << 32, 5};
    const frames::File f3 = frame_of(one, false);
    b.stream(f3, f3.frame[0]);
    CHECK(b.run({Bench::config(false, false, false, 1, Updater::ROOT_EXISTS), uint32_t(one.size()), 0, copy, 0, 0}));
    CHECK_EQ(b.accounts[2].pages_read, 1u);
    CHECK_EQ(b.accounts[2].pages_written, 1u);
    CHECK_EQ(b.accounts[2].stall_memory, uint32_t(6 + 208 + 208));
    // the stalls: a run with its data and its pages still to come waits on each
    Bench c;
    const int32_t root_c = c.new_page();
    c.u.write_word(c.t, Updater::W_RUN_CONFIG, Bench::config(false, false, false, 1, Updater::ROOT_EMPTY));
    c.u.write_word(c.t, Updater::W_WORD_COUNT, 2);
    c.u.write_word(c.t, Updater::W_PAGE_COUNT, 1);
    c.u.write_word(c.t, Updater::W_IMAGE_ROOT, uint32_t(root_c));
    c.u.run(c.t);
    for (int i = 0; i < 400; i++) c.u.step(c.t++);                         // the root, then nothing to read
    CHECK_EQ(load(c, Updater::W_STAT_STALL_DATA), uint32_t(400 - 1 - 336));
    c.u.accept(c.t, 64, Updater::PART_SEGMENT << 20, int(units::lw::SIZE_128), uint64_t(0) | uint64_t(1) << 32, 42);
    for (int i = 0; i < 50; i++) c.u.step(c.t++);                          // the header word, the value, then no page
    CHECK_EQ(load(c, Updater::W_STAT_STALL_PAGES), 49u);
    CHECK(c.u.running(c.t));
    c.u.accept(c.t, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), 64, 0);
    for (int i = 0; i < 1000 && c.u.running(c.t); i++) c.u.step(c.t++);
    CHECK(!c.u.running(c.t));
    CHECK_EQ(load(c, Updater::W_PAGE_COUNT), 0u);
    CHECK_EQ(load(c, Updater::W_STAT_STALL_PAGES), 49u);
}

// ================================================================================================ gold's fixtures
namespace {

// gold's `frames-images` lines: "name  sha256  N pages[, T taken]".
std::map<std::string, std::string> gold_digests(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string name, digest;
        if (words >> name >> digest) out[name] = digest;
    }
    return out;
}
std::string sha(const std::vector<uint8_t>& bytes) { return test::sha256_hex(bytes.data(), bytes.size()); }

void fixture(const char* name) {
    frames::File file;
    std::string err;
    if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    for (unsigned s : {0u, 20u, 60u, 63u}) {
        const auto gold = gold_digests(std::string("tests/updater/") + name + "-s" + std::to_string(s) + ".images.txt");
        CHECK_EQ(gold.size(), size_t(6));
        Bench b;
        updater_bench::Proof p;
        if (!updater_bench::play(b, file, 64, s, p, err)) { ::test::fail(__FILE__, __LINE__, "%s, s %u: %s", name, s, err.c_str()); return; }
        CHECK(sha(p.program_image) == gold.at("program"));
        CHECK(sha(p.table_image) == gold.at("table"));
        CHECK(sha(p.cycle0_image) == gold.at("cycle0"));
        CHECK(sha(p.start_image) == gold.at("start-" + std::to_string(s)));
        CHECK(sha(p.tracer_image) == gold.at("tracer"));
        CHECK(sha(p.final_image) == gold.at("final"));
        for (const auto& a : b.accounts) CHECK_EQ(a.pages_left, 0u);        // exactly the pages the frames count
        CHECK_EQ(b.u.errors(b.t), 0);
        CHECK_EQ(b.u.unwritten_reads, 0u);
        CHECK_EQ(b.u.segment_entries() + b.u.free_page_entries(), 0u);
    }
}

}  // namespace

TEST(updater_gold_kin_raw) { fixture("kin-raw"); }
TEST(updater_gold_kin_lru) { fixture("kin-lru"); }
TEST(updater_gold_jumps_base) { fixture("jumps-base"); }
TEST(updater_gold_divs) { fixture("divs"); }
