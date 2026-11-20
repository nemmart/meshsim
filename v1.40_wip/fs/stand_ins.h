// stand_ins.h: the Updater (17.10) and the Tracer (17.9) as far as the file system sees them, over the emulation's
// LPDDR (fs_emu.h). Neither is the hardware's model: the Updater applies raw update words (29.3; no LRU code) with the
// copy-on-write of 17.10.5, and the Tracer reads and writes lines of its files. Both mark the pages whose lines the
// cache would hold, so that the emulation can check the rules of 23.6.8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace fsemu {

// One run (17.10.1): RUN_CONFIG.root 0 the file exists, 1 an empty root at image_root, 2 a copy of source_root's root;
// with tracer_copy, the Tracer's copy at tracer_root is made the same way and given each page as it stands before the
// image changes it.
struct Run {
    int root = 0;
    uint32_t depth = 1;
    bool tracer_copy = false;
    uint32_t image_root = 0, tracer_root = 0, source_root = 0;
    uint64_t page_count = 0;
};

class Updater {
public:
    // The free page queue: a page pushed by the master tile, as v1.37's feed_pages does.
    void push(uint32_t unit_address) { queue_.push_back(unit_address); }
    std::size_t queued() const { return queue_.size(); }
    // A run over the update words given (header words and values, 29.3). Returns PAGE_COUNT at its done.
    uint64_t run(const Run& r, const std::vector<uint64_t>& words);

private:
    uint32_t take();
    uint32_t own(uint32_t root, uint32_t depth, uint64_t pg);   // page pg made the file's own; its data page
    std::deque<uint32_t> queue_;
    uint64_t page_count_ = 0;
};

// The Tracer: a line of a file read, or a 64-bit word written, through the cache. A write needs every entry on the
// path owned (23.6.4: anything else is answered `error`); it returns false then.
void tracer_read(uint32_t root, uint32_t depth, uint64_t pg);
bool tracer_write(uint32_t root, uint32_t depth, uint64_t pg, uint32_t word, uint64_t value);

// The host's arithmetic (29.7): the pages a run takes to write `pages` into a file of depth `depth` that owns only
// its root: the data pages and the index pages on their paths, not the root.
uint64_t pages_for(uint32_t depth, const std::vector<uint64_t>& pages);

// Update words for whole pages: each page in `pages` written with 512 values from `seed`.
std::vector<uint64_t> words_for(const std::vector<uint64_t>& pages, uint64_t seed);

}  // namespace fsemu
