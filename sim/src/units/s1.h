// s1.h (WORKER26, P2): item S1's measurement, invariant 24 (sim/ref/meshsim/src/S1.java; spec 4.8, 4.10). For every write
// handed to the write engine, the cycles from the director's acceptance to the write being complete (stored in its big-SRAM
// area, or written back by PTR's or HIST's read-modify-write), against K; a write in its area's queue, or a PTR or HIST
// write-back, later than K + 4 (the controller's least reaction) is flagged (an S1_LATE fact, Chip's S1 slot).
//
// ref keeps a HashMap and ArrayDeques per tile; here every structure is fixed and sized at configuration (D24), one
// S1Tile per tile, shared by the tile's director, write engine, big SRAM, table store, HIST store and core (one thread
// domain: a tile is never split). Order within a cycle needs nothing more: each side reads only what another unit wrote
// in an earlier cycle (a Fifo, a Hold, a Link of at least 1 between them), except the core's HIST loads, which read the
// HIST store's published write-back (a RegFile at a lower level). SIM_CHECKS only (add_g1 makes them); `on` is ref's
// Config.measureS1 (the harness turns it off).
#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

#include "base/fatal.h"
#include "check.h"

namespace units {

struct S1Tile {
    static constexpr unsigned REACTION = 4;                 // S1.REACTION
    static constexpr unsigned ACC = 1024, MAP = 4096, POOL = 512, IDX = 256;
    bool on = true;
    int tile = 0;
    unsigned K = 8;
    // ---- the director's acceptances, in the write engine's input order (ref's acceptQ) ----
    uint64_t acc_q[ACC] = {};
    uint32_t acc_h = 0, acc_n = 0;
    void accept(uint64_t t) {
        if (acc_n == ACC) FATAL("S1 (tile %d): more than %u acceptances waiting for the write engine", tile, ACC);
        acc_q[(acc_h + acc_n++) % ACC] = t;
    }
    int64_t take_accept() {                                  // -1: none (ref's poll of an empty queue)
        if (!acc_n) return -1;
        const uint64_t t = acc_q[acc_h]; acc_h = (acc_h + 1) % ACC; acc_n--;
        return int64_t(t);
    }
    // ---- chunk -> acceptance until big SRAM stores it (ref's sramAcc; a put replaces), open addressing ----
    uint32_t key[MAP] = {};                                  // chunk + 1; 0 empty
    uint64_t val[MAP] = {};
    uint32_t map_n = 0;
    static uint32_t hash(uint32_t c) { return (c * 0x9E3779B1u) >> 20; }   // 12 bits: MAP = 4096
    // ref's tile runs big SRAM before the write engine, so a write's put of t follows big SRAM's removes of t: a put is
    // staged with its cycle and applied at the next use of the table in a later cycle (at most 4 chunks a cycle).
    struct Staged { uint32_t chunk; uint64_t acc, t; };
    Staged staged[8] = {};
    uint32_t staged_n = 0;
    void settle(uint64_t now) {
        uint32_t k = 0;
        for (uint32_t i = 0; i < staged_n; i++) {
            if (staged[i].t < now) apply_put(staged[i].chunk, staged[i].acc);
            else staged[k++] = staged[i];
        }
        staged_n = k;
    }
    void sram_put(uint64_t t, uint32_t chunk, uint64_t acc) {   // the write engine's, at t
        settle(t);
        if (staged_n == 8) FATAL("S1 (tile %d): more than 8 big-SRAM chunks put in one cycle", tile);
        staged[staged_n++] = Staged{chunk, acc, t};
    }
    void apply_put(uint32_t chunk, uint64_t acc) {
        uint32_t i = hash(chunk);
        while (key[i] && key[i] != chunk + 1) i = (i + 1) % MAP;
        if (!key[i]) { if (++map_n > MAP * 3 / 4) FATAL("S1 (tile %d): more than %u big-SRAM writes awaiting their store", tile, MAP * 3 / 4); key[i] = chunk + 1; }
        val[i] = acc;
    }
    bool sram_get(uint64_t now, uint32_t chunk, uint64_t& acc) {     // big SRAM's, at now
        settle(now);
        for (uint32_t i = hash(chunk); key[i]; i = (i + 1) % MAP) if (key[i] == chunk + 1) { acc = val[i]; return true; }
        return false;
    }
    bool sram_remove(uint64_t now, uint32_t chunk, uint64_t& acc) {   // big SRAM's, at now; backward-shift deletion
        settle(now);
        uint32_t i = hash(chunk);
        while (key[i] && key[i] != chunk + 1) i = (i + 1) % MAP;
        if (!key[i]) return false;
        acc = val[i]; map_n--;
        for (uint32_t j = (i + 1) % MAP; key[j]; j = (j + 1) % MAP) {
            const uint32_t h = hash(key[j] - 1);
            if ((j > i && (h <= i || h > j)) || (j < i && h <= i && h > j)) { key[i] = key[j]; val[i] = val[j]; i = j; }
        }
        key[i] = 0;
        return true;
    }
    // ---- PTR's and HIST's acceptances per table index, awaiting their write-back (ref's ptrAcc, histAcc) ----
    struct Node { uint64_t acc; int32_t next; };
    Node pool[POOL] = {};
    int32_t free_head = -1;
    int32_t head[2][IDX], tail[2][IDX];
    S1Tile() {
        for (unsigned k = 0; k < POOL; k++) pool[k].next = k + 1 < POOL ? int32_t(k + 1) : -1;
        free_head = 0;
        for (auto& r : head) for (auto& x : r) x = -1;
        for (auto& r : tail) for (auto& x : r) x = -1;
    }
    void push(unsigned table, uint32_t idx, uint64_t acc) {  // table 0 PTR, 1 HIST
        if (free_head < 0) FATAL("S1 (tile %d): more than %u PTR and HIST updates awaiting their write-back", tile, POOL);
        const int32_t k = free_head; free_head = pool[k].next;
        pool[k] = Node{acc, -1};
        idx &= IDX - 1;
        if (tail[table][idx] >= 0) pool[tail[table][idx]].next = k; else head[table][idx] = k;
        tail[table][idx] = k;
    }
    bool pop(unsigned table, uint32_t idx, uint64_t& acc) {
        idx &= IDX - 1;
        const int32_t k = head[table][idx];
        if (k < 0) return false;
        acc = pool[k].acc;
        head[table][idx] = pool[k].next;
        if (head[table][idx] < 0) tail[table][idx] = -1;
        pool[k].next = free_head; free_head = k;
        return true;
    }
    // ---- the measurement (S1's fields, per tile; the harness sums them) ----
    uint64_t sram_max = 0, sram_n = 0, sram_order_max = 0, ptr_max = 0, ptr_n = 0, hist_max = 0, hist_n = 0, late = 0;
    uint64_t hist_load_min = UINT64_MAX, hist_loads = 0;
    // big SRAM's ordering point: the write in its area's queue (BigSRAM.s1Ordered)
    void ordered(uint64_t t, uint32_t chunk, uint64_t lat) {
        if (lat > sram_order_max) sram_order_max = lat;
        if (lat > K + REACTION) { late++; sim::check::record(t, -1, sim::check::Kind::S1_LATE, 0, uint64_t(tile), chunk, lat); }
    }
    void stored(uint64_t lat) { sram_n++; if (lat > sram_max) sram_max = lat; }
    // PTR's or HIST's write-back of index idx at t (TableStore, HistStore)
    void written_back(unsigned table, uint64_t t, uint32_t idx) {
        uint64_t acc;
        if (!pop(table, idx, acc)) return;
        const uint64_t lat = t - acc;
        if (table == 0) { ptr_n++; if (lat > ptr_max) ptr_max = lat; } else { hist_n++; if (lat > hist_max) hist_max = lat; }
        if (lat > K + REACTION) { late++; sim::check::record(t, -1, sim::check::Kind::S1_LATE, 1 + table, uint64_t(tile), idx, lat); }
    }
    void hist_load(int64_t since) { if (since >= 0 && since < 100) { hist_loads++; if (uint64_t(since) < hist_load_min) hist_load_min = uint64_t(since); } }
};

// S1.report(K) over the tiles (the configuration regime: the harness's, after a run).
inline std::string s1_report(const S1Tile* const* t, unsigned n, unsigned K) {
    S1Tile s;
    for (unsigned i = 0; i < n; i++) {
        const S1Tile& x = *t[i];
        s.sram_max = std::max(s.sram_max, x.sram_max); s.sram_n += x.sram_n; s.sram_order_max = std::max(s.sram_order_max, x.sram_order_max);
        s.ptr_max = std::max(s.ptr_max, x.ptr_max); s.ptr_n += x.ptr_n; s.hist_max = std::max(s.hist_max, x.hist_max); s.hist_n += x.hist_n;
        s.late += x.late; s.hist_load_min = std::min(s.hist_load_min, x.hist_load_min); s.hist_loads += x.hist_loads;
    }
    char b[512];
    const int64_t worst = int64_t(std::max(s.sram_max, std::max(s.ptr_max, s.hist_max)));
    std::snprintf(b, sizeof b, "S1: accept -> complete, the largest: big SRAM %" PRIu64 " (%" PRIu64 " writes; in its area's queue by %" PRIu64
                  "), PTR %" PRIu64 " (%" PRIu64 " updates), HIST %" PRIu64 " (%" PRIu64 " updates); K = %u: margin %" PRId64
                  " before the controller's reaction (+%u)", s.sram_max, s.sram_n, s.sram_order_max, s.ptr_max, s.ptr_n, s.hist_max,
                  s.hist_n, K, int64_t(K) - worst, S1Tile::REACTION);
    std::string r = b;
    if (s.late > 0) r += "; " + std::to_string(s.late) + " LATE";
    if (s.hist_loads > 0) {
        std::snprintf(b, sizeof b, "; the table-window case: %" PRIu64 " HIST loads soon after an update, the nearest %" PRIu64 " cycles after its write-back",
                      s.hist_loads, s.hist_load_min);
        r += b;
    }
    return r;
}

}  // namespace units
