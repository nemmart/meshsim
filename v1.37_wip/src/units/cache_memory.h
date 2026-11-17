// cache_memory.h: the cache memory (../v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md section 1: "plain memories, the
// kind a memory macro gives"): the tag memory and the data memory of the 512 KB cache, 8 ways by 512 sets of 128-byte
// lines (CACHE-INTERFACE.md section 6). updater_wip, WORKER5.
//
// EACH IS A MEMORY WITH TWO PORTS, one read and one write (a choice for the hardware designer: one port would cost the
// controller a cycle on each read-modify-write). In a cycle t:
//   - read(t, row): the row as it stands at the start of t is at the memory's output, q(), from t + 1 until the next
//     read; a write in the same cycle does not change what this read returns;
//   - write(t, row, ...): lands at the edge that ends t;
//   - at most one read and one write a cycle each: a second is FATAL (the controller's bug);
//   - clock(): the edge. The owner calls it once a cycle, after its own edge's reads and writes.
//
// THE TAG MEMORY: 512 rows, a set each. A row: for each of the 8 ways a valid bit, a dirty bit and a 22-bit tag (the
// line address's bits [30:9]: the set is bits [8:0]); and the set's replacement order, an age of 3 bits for each way,
// 0 the most recently used, the ages a permutation of 0 to 7.
// THE DATA MEMORY: 4,096 rows of 1,024 bits, row set * 8 + way; a write has 8 lane enables of 128 bits.
#pragma once

#include <cstdint>
#include <cstring>

#include "base/fatal.h"

namespace units {
namespace cache {

constexpr unsigned SETS = 512, WAYS = 8, LINE_WORDS = 16, LANES = 8;

struct TagRow {
    uint32_t tag[WAYS] = {};
    uint8_t valid = 0, dirty = 0;          // a bit for each way
    uint8_t age[WAYS] = {0, 1, 2, 3, 4, 5, 6, 7};
    bool is_valid(unsigned w) const { return valid >> w & 1; }
    bool is_dirty(unsigned w) const { return dirty >> w & 1; }
    // way w becomes the most recently used
    void touch(unsigned w) {
        const uint8_t a = age[w];
        for (unsigned k = 0; k < WAYS; k++) if (age[k] < a) age[k]++;
        age[w] = 0;
    }
};

class TagMemory {
public:
    TagMemory() { reset(); }
    void reset() {
        for (TagRow& r : rows_) r = TagRow{};
        q_ = q_next_ = TagRow{};
        read_t_ = write_t_ = INT64_MIN;
        pending_ = false;
    }
    void read(int64_t t, unsigned set) {
        if (read_t_ == t) FATAL("cache tag memory: two reads in cycle %lld", (long long)t);
        read_t_ = t; q_next_ = rows_[set]; reads++;
    }
    void write(int64_t t, unsigned set, const TagRow& r) {
        if (write_t_ == t) FATAL("cache tag memory: two writes in cycle %lld", (long long)t);
        write_t_ = t; pending_ = true; pending_set_ = set; pending_row_ = r; writes++;
    }
    const TagRow& q() const { return q_; }
    void clock(int64_t t) {
        if (read_t_ == t) q_ = q_next_;
        if (pending_) { rows_[pending_set_] = pending_row_; pending_ = false; }
    }
    bool written(int64_t t) const { return write_t_ == t; }
    // back doors, for checks and tests only: not ports
    const TagRow& peek(unsigned set) const { return rows_[set]; }
    TagRow& poke(unsigned set) { return rows_[set]; }
    uint64_t reads = 0, writes = 0;

private:
    TagRow rows_[SETS];
    TagRow q_, q_next_, pending_row_;
    unsigned pending_set_ = 0;
    bool pending_ = false;
    int64_t read_t_, write_t_;
};

class DataMemory {
public:
    DataMemory() : rows_(new uint64_t[SETS * WAYS * LINE_WORDS]()) { reset(); }
    ~DataMemory() { delete[] rows_; }
    DataMemory(const DataMemory&) = delete;
    DataMemory& operator=(const DataMemory&) = delete;
    void reset() {
        std::memset(q_, 0, sizeof q_);
        read_t_ = write_t_ = INT64_MIN;
        pending_ = false;
    }
    static unsigned row(unsigned set, unsigned way) { return set * WAYS + way; }
    void read(int64_t t, unsigned r) {
        if (read_t_ == t) FATAL("cache data memory: two reads in cycle %lld", (long long)t);
        read_t_ = t; std::memcpy(q_next_, rows_ + size_t(r) * LINE_WORDS, sizeof q_next_); reads++;
    }
    // lanes: bit k enables 128-bit lane k, words 2k and 2k + 1
    void write(int64_t t, unsigned r, const uint64_t* line, unsigned lanes = 0xFF) {
        if (write_t_ == t) FATAL("cache data memory: two writes in cycle %lld", (long long)t);
        write_t_ = t; pending_ = true; pending_row_ = r; pending_lanes_ = lanes;
        std::memcpy(pending_line_, line, sizeof pending_line_); writes++;
    }
    const uint64_t* q() const { return q_; }
    void clock(int64_t t) {
        if (read_t_ == t) std::memcpy(q_, q_next_, sizeof q_);
        if (pending_) {
            uint64_t* d = rows_ + size_t(pending_row_) * LINE_WORDS;
            for (unsigned k = 0; k < LANES; k++)
                if (pending_lanes_ >> k & 1) { d[2 * k] = pending_line_[2 * k]; d[2 * k + 1] = pending_line_[2 * k + 1]; }
            pending_ = false;
        }
    }
    bool read_at(int64_t t) const { return read_t_ == t; }
    bool written(int64_t t) const { return write_t_ == t; }
    const uint64_t* peek(unsigned r) const { return rows_ + size_t(r) * LINE_WORDS; }   // checks and tests only
    uint64_t reads = 0, writes = 0;

private:
    uint64_t* rows_;
    uint64_t q_[LINE_WORDS], q_next_[LINE_WORDS], pending_line_[LINE_WORDS];
    unsigned pending_row_ = 0, pending_lanes_ = 0;
    bool pending_ = false;
    int64_t read_t_, write_t_;
};

}  // namespace cache
}  // namespace units
