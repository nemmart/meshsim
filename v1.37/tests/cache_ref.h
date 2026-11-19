// cache_ref.h: the stand-in of CACHE-INTERFACE.md section 8, the simplest correct cache: none at all. Each request is
// answered from LPDDR's current contents (a file store) as CI section 2's tables say, and an update writes its entry
// straight into LPDDR. The fill port's reads and writes are made straight on the store too (17.9.5's walk:
// FileStore::walk). It has no timing: test_cache answers the same requests with it, in each client's order, and
// requires every reply equal to the cache's. updater_wip, WORKER5.
#pragma once

#include <cstdint>
#include <cstring>

#include "units/file_store.h"
#include "units/lookup_port.h"

namespace cacheref {

namespace lookup = units::lookup;
using units::tracer::FileStore;

struct Answer {
    uint8_t status = 0, level = 0;
    uint32_t entry = 0;
    bool operator==(const Answer&) const = default;
};

struct Ref {
    FileStore* fs;
    explicit Ref(FileStore* s) : fs(s) {}

    uint32_t entry(uint32_t page, unsigned i) const {
        uint64_t pg[FileStore::PAGE_WORDS];
        fs->read_page(int32_t(page), pg);
        return FileStore::entry_of(pg, int(i));
    }
    void set_entry(uint32_t page, unsigned i, uint32_t e) {
        uint64_t pg[FileStore::PAGE_WORDS];
        fs->read_page(int32_t(page), pg);
        FileStore::set_entry(pg, int(i), e);
        fs->write_page(int32_t(page), pg);
    }
    static unsigned index(uint64_t page, unsigned depth, unsigned k) { return unsigned(page >> (10 * (depth - 1 - k))) & 1023; }

    Answer request(const lookup::Request& r) {
        const unsigned d = r.depth;
        if (r.op == lookup::DISCARD) return {lookup::OK, 0, 0};
        if (d == 0 || (uint64_t(r.page) >> (10 * d)) != 0 || (r.op == lookup::UPDATE && r.level >= d)) return {lookup::BEYOND, 0, 0};
        uint32_t cur = r.root;
        for (unsigned k = 0; k < d; k++) {
            const unsigned i = index(r.page, d, k);
            const uint32_t e = entry(cur, i);
            if (r.op == lookup::UPDATE && k == r.level) { set_entry(cur, i, r.entry); return {lookup::OK, uint8_t(k), e}; }
            if (e == lookup::NO_PAGE) return {lookup::HOLE, uint8_t(k), e};
            if (!(e & lookup::OWNED)) return {lookup::NOT_OWNED, uint8_t(k), e};
            if (k == d - 1) return {lookup::OK, uint8_t(k), e};
            cur = e & lookup::ADDRESS;
        }
        return {};
    }
    uint8_t fill_read(int32_t root, unsigned depth, int64_t line, uint64_t* out) const {
        FileStore::Walk w;
        fs->walk(root, int(depth), line, w);
        if (w.data_line >= 0) fs->read_line(w.data_line, out);
        else std::memset(out, 0, 128);
        return uint8_t(w.status);
    }
    uint8_t fill_write(int32_t root, unsigned depth, int64_t line, const uint64_t* data) {
        FileStore::Walk w;
        fs->walk(root, int(depth), line, w);
        if (w.status != FileStore::ST_OK) return 1;
        fs->write_line(w.data_line, data);
        return 0;
    }
};

}  // namespace cacheref
