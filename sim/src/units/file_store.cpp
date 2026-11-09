// file_store.cpp: FileStore (file_store.h), ported from `ref`'s FileStore.java (WORKER14).
#include "units/file_store.h"

#include <cstring>

#include "base/fatal.h"

namespace units {
namespace tracer {

FileStore::FileStore(int32_t first) : first_(first), next_free_(first) {
    if (first % PAGE_UNITS != 0 || first < 0) FATAL("FileStore: unaligned first page address 0x%x", unsigned(first));
}

int32_t FileStore::alloc(bool index) {
    const int64_t a = next_free_;
    next_free_ += PAGE_UNITS;
    if (next_free_ > int64_t(0x7FFFFFFF) + 1) FATAL("FileStore: out of 31-bit addresses");
    words_.resize(words_.size() + PAGE_WORDS, index ? ~uint64_t(0) : 0);  // an index page: every entry 0xFFFFFFFF
    return int32_t(a);
}

uint64_t* FileStore::page(int32_t a) {
    const int64_t k = (int64_t(a) - first_) / PAGE_UNITS;
    if (a < first_ || (a - first_) % PAGE_UNITS != 0 || int64_t(a) >= next_free_)
        FATAL("FileStore: no page at 0x%x", unsigned(a));
    return words_.data() + k * PAGE_WORDS;
}
const uint64_t* FileStore::page(int32_t a) const { return const_cast<FileStore*>(this)->page(a); }

int FileStore::depth_for(int64_t bytes) {
    for (int d = 0; d <= 3; d++)
        if (bytes <= (int64_t(4096) << (10 * d))) return d;
    FATAL("FileStore: too long: %lld", (long long)bytes);
}

FileStore::File FileStore::create(int depth) {
    if (depth < 0 || depth > 3) FATAL("FileStore: depth %d", depth);
    File f;
    f.root = alloc(depth > 0);
    f.depth = depth;
    return f;
}

void FileStore::put_page(File f, int64_t pg, const uint64_t* data) {
    if (f.depth == 0) {
        if (pg != 0) FATAL("FileStore: depth 0 has one page");
        std::memcpy(page(f.root), data, PAGE_WORDS * sizeof(uint64_t));
        return;
    }
    if (pg < 0 || pg >= (int64_t(1) << (10 * f.depth))) FATAL("FileStore: page %lld beyond depth %d", (long long)pg, f.depth);
    int32_t a = f.root;
    for (int lv = 0; lv < f.depth; lv++) {
        const int i = digit(pg, lv, f.depth);
        uint32_t e = entry_of(page(a), i);
        if (e == NONE) {
            e = uint32_t(alloc(lv < f.depth - 1)) | OWNED;  // allocate first: the vector may move
            set_entry(page(a), i, e);
        } else if ((e & OWNED) == 0) {
            FATAL("FileStore: put_page through a shared entry: use make_private on a copy");
        }
        a = addr(e);
    }
    std::memcpy(page(a), data, PAGE_WORDS * sizeof(uint64_t));
}

FileStore::File FileStore::cow_open(File src) {
    if (src.depth == 0) FATAL("FileStore: cow_open of a depth-0 file: nothing to share");
    const int32_t r = alloc(true);
    const uint64_t* from = page(src.root);
    uint64_t* to = page(r);
    for (int i = 0; i < 1024; i++) {
        const uint32_t e = entry_of(from, i);
        set_entry(to, i, e == NONE ? NONE : e & ~OWNED);
    }
    File f;
    f.root = r;
    f.depth = src.depth;
    return f;
}

void FileStore::make_private(File f, int64_t pg) {
    int32_t a = f.root;
    for (int lv = 0; lv < f.depth; lv++) {
        const int i = digit(pg, lv, f.depth);
        uint32_t e = entry_of(page(a), i);
        if (e == NONE) FATAL("FileStore: pre-copying a hole at page %lld", (long long)pg);
        if ((e & OWNED) == 0) {
            const bool index = lv < f.depth - 1;
            const int32_t c = alloc(index);
            const uint64_t* src = page(addr(e));
            uint64_t* dst = page(c);
            if (index) {
                for (int k = 0; k < 1024; k++) {
                    const uint32_t x = entry_of(src, k);
                    set_entry(dst, k, x == NONE ? NONE : x & ~OWNED);
                }
            } else {
                std::memcpy(dst, src, PAGE_WORDS * sizeof(uint64_t));
            }
            e = uint32_t(c) | OWNED;
            set_entry(page(a), i, e);
        }
        a = addr(e);
    }
}

int FileStore::set_ownership(File f, const std::vector<int64_t>& written) {
    if (f.depth == 0) FATAL("FileStore: set_ownership of a depth-0 file");
    clear_owned(f.root, f.depth);
    int created = 0;
    for (int64_t pg : written) {
        int32_t a = f.root;
        for (int lv = 0; lv < f.depth; lv++) {
            const int i = digit(pg, lv, f.depth);
            uint32_t e = entry_of(page(a), i);
            if (e == NONE) {
                e = uint32_t(alloc(lv < f.depth - 1));
                if (lv == f.depth - 1) created++;
            }
            e |= OWNED;
            set_entry(page(a), i, e);
            a = addr(e);
        }
    }
    return created;
}

void FileStore::clear_owned(int32_t a, int levels) {
    for (int i = 0; i < 1024; i++) {
        const uint32_t e = entry_of(page(a), i);
        if (e == NONE) continue;
        set_entry(page(a), i, e & ~OWNED);
        if (levels > 1) clear_owned(addr(e), levels - 1);
    }
}

void FileStore::for_each_page(File f, const std::function<void(int64_t, const uint64_t*)>& fn) const {
    for_each(f.root, f.depth, 0, fn);
}
void FileStore::for_each(int32_t a, int levels, int64_t prefix,
                         const std::function<void(int64_t, const uint64_t*)>& fn) const {
    if (levels == 0) {
        fn(prefix, page(a));
        return;
    }
    for (int i = 0; i < 1024; i++) {
        const uint32_t e = entry_of(page(a), i);
        if (e != NONE) for_each(addr(e), levels - 1, (prefix << 10) | i, fn);
    }
}

void FileStore::walk(int32_t root, int depth, int64_t line, Walk& w) const {
    w.n_lines = 0;
    w.index_lines = 0;
    w.data_line = -1;
    if (line < 0 || line >= (int64_t(1) << (5 + 10 * depth))) {
        w.status = ST_BEYOND;
        return;
    }
    const int64_t pg = line >> 5;
    int32_t a = root;
    bool owned = true;
    for (int lv = 0; lv < depth; lv++) {
        const int i = digit(pg, lv, depth);
        w.lines[w.n_lines++] = a + (i >> 5);  // the index line holding entry i (32 entries a line)
        w.index_lines++;
        const uint32_t e = entry_of(page(a), i);
        if (e == NONE) {
            w.status = ST_HOLE;
            return;
        }
        if ((e & OWNED) == 0) owned = false;
        a = addr(e);
    }
    w.data_line = a + int32_t(line & 31);
    w.lines[w.n_lines++] = w.data_line;
    w.status = owned ? ST_OK : ST_NOT_OWNED;
}

void FileStore::read_line(int32_t line_addr, uint64_t* out) const {
    std::memcpy(out, page(line_addr & ~31) + 16 * (line_addr & 31), 16 * sizeof(uint64_t));
}
void FileStore::write_line(int32_t line_addr, const uint64_t* in) {
    std::memcpy(page(line_addr & ~31) + 16 * (line_addr & 31), in, 16 * sizeof(uint64_t));
}

uint64_t FileStore::load64(File f, int64_t off) const {
    Walk w;
    walk(f.root, f.depth, int64_t(uint64_t(off) >> 7), w);
    if (w.data_line < 0) return 0;
    return page(w.data_line & ~31)[16 * (w.data_line & 31) + int((uint64_t(off) >> 3) & 15)];
}

}  // namespace tracer
}  // namespace units
