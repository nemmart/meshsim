// standin_cache.h: the stand-in cache of CACHE-INTERFACE.md section 8 ("CI"), on the Updater's look-up port (updater_wip,
// WORKER6; NOTES section 8). The simplest correct cache is none: a functional stand-in that walks the file store's pages
// as LPDDR has them and, for an update, writes the new entry into them at once. **A test piece:** WORKER5's cache takes
// its place when the two controllers are joined.
//
//   - one request at a time; port 1's `ready` a register, 0 from the cycle after a handshake until the cycle after its
//     reply; the reply on the bus for one cycle, `client` 1;
//   - its replies are CI section 2's tables, word for word;
//   - timing: `line` cycles for each index line it reads and `write` for an update's write (80 and 80, CI's suggestion),
//     or, with a seed, each from 1 to 200 at random and `ready` 0 now and then;
//   - injected replies, for the error tests: the nth update (or look-up) answered with a given status, writing nothing;
//   - its checks, counted for a test to fail on: an update whose entry names a page with a write still in flight on the
//     AXI port (the data must be in LPDDR before the index points to it: CI section 3), and an index page read that was
//     never written.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

#include "test_lpddr.h"
#include "units/lookup_port.h"
#include "units/updater_top.h"

namespace test_pieces {

namespace lookup = units::lookup;

struct StandInCache final : units::updctl::LookupDev {
    FileStore& store;
    const TestLpddr* lpddr = nullptr;              // for the data-before-index check (null: not checked)
    unsigned line_cycles = 80, write_cycles = 80;
    bool random = false;
    // an injected reply: the nth request of `op` (from 1) answered `status`, nothing written
    struct Inject { uint8_t op = lookup::UPDATE; unsigned nth = 0; uint8_t status = lookup::LPDDR_ERROR; };
    Inject inject;
    // the harness's observer of each update applied (root, depth, page, level, the new entry)
    std::function<void(uint32_t, unsigned, uint32_t, unsigned, uint32_t)> on_update;

    // ---- counts
    uint64_t lookups = 0, updates = 0, discards = 0, lines_read = 0, early = 0, unwritten = 0, busy_cycles = 0;
    std::string early_why;
    uint64_t replies_by[2][8][4] = {};             // [look up 0, update 1][status][level]: what the tests reached

    StandInCache(FileStore& s, uint64_t seed = 0) : store(s), random(seed != 0), rnd(seed ? seed : 1) {}

    bool ready() const override { return rdy; }
    lookup::Reply reply() const override { return rp; }
    bool quiet() const override { return !busy && !rp.valid && rdy; }
    uint64_t violations() const override { return early + unwritten; }

    void edge(int64_t, const lookup::Request& r) override {
        if (busy || rp.valid) busy_cycles++;
        rp.valid = false;
        bool replied = false;
        if (busy && --left == 0) { rp = pending; rp.valid = true; busy = false; replied = true; }
        if (rdy && r.valid) {
            unsigned lines = 0;
            bool wrote = false;
            pending = answer(r, lines, wrote);
            if (r.op <= lookup::UPDATE) replies_by[r.op][pending.status & 7][pending.level & 3]++;
            pending.client = lookup::UPDATER;
            unsigned cost = 0;
            for (unsigned k = 0; k < lines; k++) cost += random ? unsigned(1 + rnd.below(200)) : line_cycles;
            if (wrote) cost += random ? unsigned(1 + rnd.below(200)) : write_cycles;
            left = cost ? cost : 1;
            busy = true;
        }
        rdy = !busy && !replied && (!random || rnd.below(8) != 0);
    }

private:
    Rand rnd;
    bool rdy = true, busy = false;
    unsigned left = 0;
    lookup::Reply rp, pending;
    unsigned seen[3] = {};

    uint32_t entry_at(uint32_t page_unit, unsigned i) {
        if (!store.has_page(int32_t(page_unit))) { unwritten++; return 0; }
        return FileStore::entry_of(store.raw_page(int32_t(page_unit)), int(i));
    }
    static unsigned digit(uint32_t pg, unsigned level, unsigned depth) { return (pg >> (10 * (depth - 1 - level))) & 1023; }

    lookup::Reply answer(const lookup::Request& r, unsigned& lines, bool& wrote) {
        lookup::Reply a;
        if (r.op > lookup::DISCARD) FATAL("stand-in cache: a request of op %u (reserved)", unsigned(r.op));
        const unsigned n = ++seen[r.op];
        if (r.op == lookup::DISCARD) { discards++; return a; }               // ok, 0, 0: it holds nothing
        if (r.op == lookup::LOOK_UP) lookups++; else updates++;
        if (inject.nth && inject.op == r.op && inject.nth == n) { a.status = inject.status; a.level = r.level; return a; }
        const unsigned depth = r.depth;
        const bool beyond = depth == 0 || (depth < 3 && r.page >= (1u << (10 * depth))) || (r.op == lookup::UPDATE && r.level >= depth);
        if (beyond) { a.status = lookup::BEYOND; return a; }
        const unsigned stop_at = r.op == lookup::UPDATE ? r.level : depth;   // the levels checked above the target
        uint32_t at = r.root;
        for (unsigned level = 0; level < stop_at; level++) {
            lines++; lines_read++;
            const uint32_t e = entry_at(at, digit(r.page, level, depth));
            if (e == lookup::NO_PAGE) { a.status = lookup::HOLE; a.level = uint8_t(level); a.entry = e; return a; }
            if (!(e & lookup::OWNED)) { a.status = lookup::NOT_OWNED; a.level = uint8_t(level); a.entry = e; return a; }
            if (r.op == lookup::LOOK_UP && level == depth - 1) { a.status = lookup::OK; a.level = uint8_t(level); a.entry = e; return a; }
            at = e & lookup::ADDRESS;
        }
        // an update: the entry at r.level in the page `at`, the path above it the file's own
        lines++; lines_read++;
        const unsigned i = digit(r.page, r.level, depth);
        const uint32_t old = entry_at(at, i);
        if (lpddr && r.entry != lookup::NO_PAGE && lpddr->write_in_flight(r.entry & lookup::ADDRESS)) {
            early++;
            if (early_why.empty()) early_why = "an update naming page " + std::to_string(r.entry & lookup::ADDRESS) + " before its write's response";
        }
        uint64_t* pg = store.page_at(int32_t(at));
        FileStore::set_entry(pg, int(i), r.entry);
        wrote = true;
        if (on_update) on_update(r.root, depth, r.page, r.level, r.entry);
        a.status = lookup::OK; a.level = r.level; a.entry = old;
        return a;
    }
};

}  // namespace test_pieces
