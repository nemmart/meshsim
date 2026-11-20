// stand_ins.cpp: see stand_ins.h.
#include "stand_ins.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "fs_emu.h"

namespace fsemu {
namespace {

[[noreturn]] void stop(const char* what) { std::fprintf(stderr, "stand-in: %s\n", what); std::exit(3); }
uint32_t entry(uint32_t page_unit, uint32_t i) { uint32_t v; std::memcpy(&v, page(page_unit) + 4 * i, 4); return v; }
uint32_t digit(uint64_t pg, uint32_t level, uint32_t depth) { return uint32_t(pg >> (10 * (depth - 1 - level))) & 1023u; }

}  // namespace

uint32_t Updater::take() {
    if (page_count_ == 0) stop("Updater: out_of_pages (17.10.5)");
    if (queue_.empty()) stop("Updater: the free page queue is empty (the run would wait)");
    const uint32_t p = queue_.front();
    queue_.pop_front();
    page_count_--;
    return p;
}

// An update (23.6.3) changes one entry of an index page, through the cache: its line is then in it. A look-up reads
// the lines on the path, so they are in it too.
static void update(uint32_t page_unit, uint32_t i, uint32_t e) {
    std::memcpy(page(page_unit) + 4 * i, &e, 4);
    cache_touch(page_unit);
}

uint32_t Updater::own(uint32_t root, uint32_t depth, uint64_t pg) {
    if (depth == 0) return root;
    if (pg >> (10 * depth)) stop("Updater: beyond (17.10.5)");
    uint32_t at = root;
    for (uint32_t L = 0; L < depth; L++) {
        cache_touch(at);                                             // the look-up's line
        const uint32_t i = digit(pg, L, depth), e = entry(at, i);
        const bool bottom = L + 1 == depth;
        if (e != NONE && (e & OWNED)) { at = e & ~OWNED; continue; }
        const uint32_t n = take();                                   // a hole, or a page not the file's own
        uint8_t* np = page(n);
        if (e == NONE) std::memset(np, bottom ? 0x00 : 0xFF, PAGE_BYTES);
        else {
            std::memcpy(np, page(e & ~OWNED), PAGE_BYTES);
            if (!bottom)                                             // a copied index page owns nothing yet
                for (uint32_t k = 0; k < 1024; k++) {
                    uint32_t v; std::memcpy(&v, np + 4 * k, 4);
                    if (v != NONE) { v &= ~OWNED; std::memcpy(np + 4 * k, &v, 4); }
                }
        }
        update(at, i, n | OWNED);                                    // the page is in LPDDR before the entry names it
        at = n;
    }
    return at;
}

uint64_t Updater::run(const Run& r, const std::vector<uint64_t>& words) {
    page_count_ = r.page_count;
    // the check state: the root made (17.10.5); a root is a new page, written whole
    auto make_root = [&](uint32_t dst) {
        uint8_t* d = page(dst);
        if (r.root == 1) std::memset(d, r.depth ? 0xFF : 0x00, PAGE_BYTES);
        else {
            std::memcpy(d, page(r.source_root), PAGE_BYTES);
            if (r.depth)
                for (uint32_t k = 0; k < 1024; k++) {
                    uint32_t v; std::memcpy(&v, d + 4 * k, 4);
                    if (v != NONE) { v &= ~OWNED; std::memcpy(d + 4 * k, &v, 4); }
                }
        }
    };
    if (r.root == 1 || r.root == 2) {
        make_root(r.image_root);
        if (r.tracer_copy && r.root == 2) make_root(r.tracer_root);
    }
    // the words: a header word (start, count) and its values
    for (size_t k = 0; k < words.size();) {
        const uint64_t h = words[k++];
        const uint64_t start = h & 0xFFFFFFFFu, count = h >> 32;
        for (uint64_t v = 0; v < count; v++) {
            if (k >= words.size()) stop("Updater: the words end inside a run of values");
            const uint64_t w = start + v, pg = w / 512;
            if (r.tracer_copy) own(r.tracer_root, r.depth, pg);     // the copy gets the page as it stands, first
            const uint32_t d = own(r.image_root, r.depth, pg);
            std::memcpy(page(d) + 8 * (w % 512), &words[k++], 8);  // the data page: on the Updater's own port
        }
    }
    return page_count_;
}

void tracer_read(uint32_t root, uint32_t depth, uint64_t pg) {
    uint32_t at = root;
    for (uint32_t L = 0; L < depth; L++) {
        cache_touch(at);
        const uint32_t e = entry(at, digit(pg, L, depth));
        if (e == NONE) return;                                       // a hole reads as zeros
        at = e & ~OWNED;
    }
    cache_touch(at);
}

bool tracer_write(uint32_t root, uint32_t depth, uint64_t pg, uint32_t word, uint64_t value) {
    uint32_t at = root;
    for (uint32_t L = 0; L < depth; L++) {
        cache_touch(at);
        const uint32_t e = entry(at, digit(pg, L, depth));
        if (e == NONE || !(e & OWNED)) return false;                 // the Tracer adds no pages
        at = e & ~OWNED;
    }
    cache_touch(at);                                                 // dirty in the cache; here, in LPDDR at once
    std::memcpy(page(at) + 8 * (word % 512), &value, 8);
    return true;
}

uint64_t pages_for(uint32_t depth, const std::vector<uint64_t>& pages) {
    std::set<uint64_t> data(pages.begin(), pages.end());
    uint64_t n = data.size();
    for (uint32_t L = 1; L < depth; L++) {
        std::set<uint64_t> idx;
        for (uint64_t p : data) idx.insert(p >> (10 * (depth - L)));
        n += idx.size();
    }
    return depth == 0 ? 0 : n;
}

std::vector<uint64_t> words_for(const std::vector<uint64_t>& pages, uint64_t seed) {
    std::vector<uint64_t> w;
    for (uint64_t p : pages) {
        w.push_back((p * 512) | (uint64_t(512) << 32));
        for (uint64_t i = 0; i < 512; i++) w.push_back(seed * 1000003u + p * 512 + i + 1);
    }
    return w;
}

}  // namespace fsemu
