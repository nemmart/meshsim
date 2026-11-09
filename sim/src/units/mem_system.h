// mem_system.h: the memory system as the Tracer sees it (spec 4.21.5, 7; DECISIONS-v1.3 sections 12, 20, 21; the
// owner's model for B1), ported from `ref`'s MemorySystem.java (WORKER14). A black box behind the fill bus, reading and
// writing one 128 B line of a file named by (root, depth, line); its storage is FileStore's trees, this is the timing.
//
// THE MODEL (`ref`'s, unchanged): no TLB, a walk's every index line an access, then the data line; the shared cache of
// 512 KB (8 ways x 512 sets, the set the LPDDR line address's low 9 bits, LRU, write-back) holding index and data lines
// alike, a hit 6 cycles and a miss 80; one request outstanding; the next victim of every set kept free or clean (a full
// set's dirty LRU line evicted at once into the write buffer); the write buffer of 4 lines draining one per 80 cycles,
// a read miss waiting until it is empty; a Tracer write-back allocating without an LPDDR read (Q13). The data is
// functional: reads return FileStore's lines and writes update them at once.
//
// SYNCHRONOUS, AS IN `ref`. The Tracer calls read and write directly and gets the cycle the data is ready; it owns its
// MemSystem (and FileStore), no ports (owner, 2026-11-09). Everything here is the cycle loop's: fixed arrays, no
// allocation. `ref`'s IllegalStateExceptions are FATAL.
#pragma once

#include <climits>
#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "units/file_store.h"

namespace units {
namespace tracer {

class MemSystem {
public:
    int hit = 6, miss = 80, wb_depth = 4, wb_drain = 80;
    static constexpr int SETS = 512, WAYS = 8, WB_RING = 64;

    FileStore* store;

    // ---- statistics (per segment: cleared by cold()) ----
    int64_t reads = 0, writes = 0, beyond = 0;
    int64_t index_accesses = 0, index_hits = 0, data_accesses = 0, data_hits = 0, lpddr_reads = 0;
    int64_t walk_cycles = 0, data_cycles = 0;  // cycles spent on index lines and on data lines
    int64_t early_evictions = 0, read_waits = 0, read_wait_cycles = 0, write_allocates = 0;
    // Per class of line (statistics only): 0 the memory file's index lines, 1 its data lines, 2 the map's index lines,
    // 3 the map's data lines; the map is the file whose root is map_root_for_stats.
    int64_t acc[4] = {}, hits[4] = {}, cycles[4] = {};
    int32_t map_root_for_stats = -1;

    // The result of a read: its line, status and the cycle its data arrives.
    struct Response {
        uint64_t data[16] = {};
        int status = 0;
        int64_t ready_at = 0;
    };

    explicit MemSystem(FileStore* s) : store(s) { cold(); }

    // Cold cache, empty write buffer, idle bus, statistics cleared: a segment's start.
    void cold() {
        for (int i = 0; i < SETS * WAYS; i++) {
            tag_[i] = -1;
            dirty_[i] = false;
            stamp_[i] = 0;
        }
        clock_ = 0;
        wb_head_ = 0;
        wb_count_ = 0;
        busy_until_ = INT64_MIN;
        reads = writes = beyond = 0;
        index_accesses = index_hits = data_accesses = data_hits = lpddr_reads = 0;
        walk_cycles = data_cycles = 0;
        early_evictions = read_waits = read_wait_cycles = write_allocates = 0;
        for (int c = 0; c < 4; c++) acc[c] = hits[c] = cycles[c] = 0;
    }

    // The bus is free at t (one request outstanding).
    bool idle(int64_t t) const { return t >= busy_until_; }
    // The first cycle the bus is free (the end of the request outstanding, or of a write-back).
    int64_t free_at() const { return busy_until_; }

    // A fill-bus read issued at t (the bus idle): walks (root, depth, line), times every access, and returns the line and
    // its status (4.21.5): 0 ok, 1 hole (zeros), 2 not owned, 3 beyond the depth's capacity (no access).
    Response& read(int64_t t, int32_t root, int depth, int64_t line, Response& r) {
        if (!idle(t))
            FATAL("MemSystem: a fill-bus read at %lld with a request outstanding until %lld", (long long)t,
                  (long long)busy_until_);
        reads++;
        store->walk(root, depth, line, walk_);
        int64_t now = t;
        const int cls0 = root == map_root_for_stats ? 2 : 0;
        for (int k = 0; k < walk_.n_lines; k++) {
            const bool idx = k < walk_.index_lines;
            const int64_t s = now;
            now = access(now, walk_.lines[k], false, idx, cls0 + (idx ? 0 : 1));
            if (idx) walk_cycles += now - s; else data_cycles += now - s;
        }
        if (walk_.status == FileStore::ST_BEYOND) {
            beyond++;
            now = t + 1;
        }
        r.status = walk_.status;
        if (walk_.data_line >= 0) store->read_line(walk_.data_line, r.data);
        else std::memset(r.data, 0, sizeof r.data);
        r.ready_at = now;
        busy_until_ = now;
        return r;
    }

    // A fill-bus write issued at t (the bus idle) of a line the Tracer may write (status ok); returns its
    // acknowledgement's cycle.
    int64_t write(int64_t t, int32_t root, int depth, int64_t line, const uint64_t* data) {
        if (!idle(t))
            FATAL("MemSystem: a fill-bus write at %lld with a request outstanding until %lld", (long long)t,
                  (long long)busy_until_);
        writes++;
        store->walk(root, depth, line, walk_);
        if (walk_.status != FileStore::ST_OK)
            FATAL("MemSystem: a write-back of a line that isn't writable (status %d): only writable lines are dirty "
                  "(4.21.5)", walk_.status);
        int64_t now = t;
        const int cls0 = root == map_root_for_stats ? 2 : 0;
        for (int k = 0; k < walk_.n_lines; k++) {
            const bool idx = k < walk_.index_lines;
            const int64_t s = now;
            now = access(now, walk_.lines[k], !idx, idx, cls0 + (idx ? 0 : 1));
            if (idx) walk_cycles += now - s; else data_cycles += now - s;
        }
        store->write_line(walk_.data_line, data);
        busy_until_ = now;
        return now;
    }

private:
    // One access of LPDDR line a at t; returns the cycle it completes. write: a whole-line write (Q13).
    int64_t access(int64_t t, int32_t a, bool write, bool index, int cls) {
        const int64_t t0 = t;
        acc[cls]++;
        const int set = a & (SETS - 1), base = set * WAYS;
        int way = -1;
        if (index) index_accesses++; else data_accesses++;
        for (int w = 0; w < WAYS; w++)
            if (tag_[base + w] == a) {
                way = w;
                break;
            }
        int64_t done;
        if (way >= 0) {
            if (index) index_hits++; else data_hits++;
            hits[cls]++;
            done = t + hit;
        } else {
            way = victim(base);
            if (write) {
                done = t + hit;  // allocated without an LPDDR read (Q13)
                write_allocates++;
            } else {
                const int64_t e = wb_empty_at(t);
                if (e > t) {  // a read miss waits for an empty buffer (Q12)
                    read_waits++;
                    read_wait_cycles += e - t;
                    t = e;
                }
                done = t + miss;
                lpddr_reads++;
            }
            tag_[base + way] = a;
            dirty_[base + way] = false;
        }
        stamp_[base + way] = ++clock_;
        if (write) dirty_[base + way] = true;
        keep_victim_clean(base, done);
        cycles[cls] += done - t0;
        return done;
    }

    // A free way, else the LRU way (clean, by the policy).
    int victim(int base) const {
        int v = -1;
        for (int w = 0; w < WAYS; w++) {
            if (tag_[base + w] < 0) return w;
            if (v < 0 || stamp_[base + w] < stamp_[base + v]) v = w;
        }
        if (dirty_[base + v]) FATAL("MemSystem: a dirty victim: the policy keeps the next victim free or clean");
        return v;
    }

    // The policy (Q12): a full set whose LRU line is dirty evicts it at once, at t, into the write buffer.
    void keep_victim_clean(int base, int64_t t) {
        int v = -1;
        for (int w = 0; w < WAYS; w++) {
            if (tag_[base + w] < 0) return;  // a free way is the next victim
            if (v < 0 || stamp_[base + w] < stamp_[base + v]) v = w;
        }
        if (!dirty_[base + v]) return;
        early_evictions++;
        tag_[base + v] = -1;
        dirty_[base + v] = false;
        wb_push(t);
    }

    // ---- the write buffer: a FIFO draining one line per wb_drain ----
    void wb_retire(int64_t t) {
        while (wb_count_ > 0 && wb_done_[wb_head_] <= t) {
            wb_head_ = (wb_head_ + 1) % WB_RING;
            wb_count_--;
        }
    }
    void wb_push(int64_t t) {
        wb_retire(t);
        const int64_t last = wb_count_ == 0 ? INT64_MIN : wb_done_[(wb_head_ + wb_count_ - 1) % WB_RING];
        int64_t enter = t;
        if (wb_count_ >= wb_depth) {
            const int64_t d = wb_done_[(wb_head_ + wb_count_ - wb_depth) % WB_RING];
            enter = t > d ? t : d;
        }
        const int64_t done = (enter > last ? enter : last) + wb_drain;
        if (wb_count_ == WB_RING) FATAL("MemSystem: write-buffer ring overflow");
        wb_done_[(wb_head_ + wb_count_) % WB_RING] = done;
        wb_count_++;
    }
    int64_t wb_empty_at(int64_t t) {
        wb_retire(t);
        return wb_count_ == 0 ? t : wb_done_[(wb_head_ + wb_count_ - 1) % WB_RING];
    }

    int32_t tag_[SETS * WAYS];
    bool dirty_[SETS * WAYS];
    int64_t stamp_[SETS * WAYS];
    int64_t clock_ = 0;
    int64_t wb_done_[WB_RING];  // the write buffer's lines' drain completions, a ring (FIFO)
    int wb_head_ = 0, wb_count_ = 0;
    int64_t busy_until_ = INT64_MIN;  // one request outstanding: the bus is free from this cycle
    FileStore::Walk walk_;
};

}  // namespace tracer
}  // namespace units
