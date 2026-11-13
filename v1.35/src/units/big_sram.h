// big_sram.h: the big SRAM (sim/ref/meshsim/src/BigSRAM.java; AUDIT-M2 3.4).
//
// 16 areas of 32 K rows x 128 bits (chunk address: bits 20-17 the area, 16-2 the row, 1-0 the chunk in the row). Each
// area serves the head of its own request queue (depth `area_queue_depth`) once a cycle, in arrival order: a write stores
// its enabled chunks at once; a read puts its row on the area's return Link(sram_read_latency). Arrivals each cycle: the
// write Hold first, then the read Hold, each only if its area has room by its count before the cycle's pop (13a,
// `fifo_count_before_pop`; the write counted in first, so a read to the same area sees it), else the request stays in its
// Hold and the producer's next put is refused (PORTS.md 5). Write combining (when configured): a write into the same row
// as its area's last queued write, with disjoint chunks, merges into it. A chunk at or past `mem_chunks` reads 0 and a
// write to it is dropped.
//
// THE CONTENTS. One zero-filled block per tile, mem_chunks 32-bit words, allocated once at configuration by its owner
// (`SramBlocks`: anonymous mmap; the OS backs only the pages touched), never in the loop (D6; owner, 2026-11-08). The
// fingerprint's sum (region 3, AUDIT 6.2) is kept incrementally at every store when `running_sum` (the default; D31:
// off in timing runs, where data_sum() scans the block if a fingerprint is ever asked for).
//
// PORTS
//   in   wr      HoldIn, 3 words: the write engine's request {chunk | chunks << 21, chunks 0-1, chunks 2-3}
//        rd      HoldIn, 1 word:  the read queue's request {chunk | chunks << 21 | token << 24 | part << 40}
//                (declared only with `reads`: a tile without the read side)
//   out  ret[a]  LinkOut, 3 words, Link(sram_read_latency): {token | part << 16, row chunks 0-1, row chunks 2-3}
//                (declared only with `reads`)
#pragma once

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "base/fatal.h"
#include "check.h"
#include "sim.h"
#include "units/memsum.h"
#include "units/s1.h"

namespace units {

struct SramConfig {
    uint32_t mem_chunks = 1u << 20;
    unsigned area_queue_depth = 8;
    bool write_combining = false;
    bool fifo_count_before_pop = true;
    // Keep the contents' fingerprint sum at every store (D31). Off in timing runs, where no fingerprint is taken: the
    // store is then a plain store, and data_sum() scans the block when asked.
    bool running_sum = true;
};

// The units' big-SRAM blocks: one anonymous mapping per unit (tiles 0-63, C&C 1-3 as units 64-66: M3), made at
// configuration, unmapped by the destructor.
class SramBlocks {
public:
    static constexpr unsigned kUnits = 67;
    SramBlocks() = default;
    ~SramBlocks() { for (unsigned k = 0; k < kUnits; k++) if (b_[k]) munmap(b_[k], size_t(n_[k]) * 4); }
    SramBlocks(const SramBlocks&) = delete;
    SramBlocks& operator=(const SramBlocks&) = delete;
    // Tile k's block, mapped at its first call (configuration); fatal if a later call names another size.
    uint32_t* block(unsigned k, uint32_t mem_chunks) {
        if (k >= kUnits) FATAL("big SRAM blocks: unit %u", k);
        if (b_[k]) {
            if (n_[k] != mem_chunks) FATAL("big SRAM blocks: tile %u mapped with %u chunks, asked for %u", k, n_[k], mem_chunks);
            return b_[k];
        }
        void* p = mmap(nullptr, size_t(mem_chunks) * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) FATAL("big SRAM blocks: mmap of %u chunks for tile %u failed", mem_chunks, k);
        b_[k] = static_cast<uint32_t*>(p); n_[k] = mem_chunks;
        return b_[k];
    }
    size_t allocated_bytes() const { size_t s = 0; for (unsigned k = 0; k < kUnits; k++) s += size_t(n_[k]) * 4; return s; }
    // Resident pages (mincore), for the footprint: what the run touched of what is allocated.
    size_t touched_bytes() const {
        const size_t pg = size_t(sysconf(_SC_PAGESIZE));
        static unsigned char vec[(size_t(1) << 23) / 4096];       // one block (8 MB at most) at 4 KB pages; between runs only
        size_t s = 0;
        for (unsigned k = 0; k < kUnits; k++) {
            if (!b_[k]) continue;
            const size_t bytes = size_t(n_[k]) * 4, pages = (bytes + pg - 1) / pg;
            if (pages > sizeof vec || mincore(b_[k], bytes, vec) != 0) FATAL("big SRAM blocks: mincore on tile %u", k);
            for (size_t i = 0; i < pages; i++) s += (vec[i] & 1) ? pg : 0;
        }
        return s;
    }
private:
    uint32_t* b_[kUnits] = {};
    uint32_t n_[kUnits] = {};
};

struct BigSram {
    static constexpr unsigned AREAS = 16, ROWS = 32768, AREA_CHUNKS = ROWS * 4, MAX_DEPTH = 16;

    // ---- ports ----
    sim::HoldIn wr, rd;
    sim::LinkOut ret[AREAS];

    // ---- configuration ----
    SramConfig cfg;
    bool reads = false;
    uint32_t* data = nullptr;          // mem_chunks words (SramBlocks)

    // ---- state: the area queues ----
    struct Entry { uint32_t row; uint8_t write, mask, part, pad; uint32_t token; uint32_t d[4]; };
    Entry q[AREAS][MAX_DEPTH] = {};
    uint8_t head[AREAS] = {}, count[AREAS] = {};
    uint32_t busy = 0;                 // areas with a non-empty queue (bit a)
    MemSum sum{FP_SRAM};

    // ---- statistics ----
    uint64_t reads_served = 0, writes_served = 0, combined = 0, read_stall_cycles = 0, write_stall_cycles = 0;
    // (the harness's Chip::new_phase; the agreed form, 2026-11-12): a new disjoint-buffer phase.
    // (invariant 2; BigSRAM.newPhase, mark): every chunk read and every chunk written in a phase is marked; a chunk
    // both read and written in one phase is a violation, its DISJOINT fact recorded once a phase (ref reports the
    // phase's first, once a tile and phase). Phases: the harness's Chip::new_phase, every GO and every counters-done latch
    // (ref's TileControl.endCycle(t): sram.newPhase at both, so two at once when both: the block's `phase_out` Wire, here
    // `go_seen`, read at this unit's step at t + 1 before it serves; the F6 added the latch).
    // SIM_CHECKS only (the bits, two per chunk, allocated at build); elsewhere new_phase does nothing.
    sim::WireIn go_seen;               // TileCtl's phase_out (tile_ctl.h), declared under SIM_CHECKS only (`go_wire`)
    void new_phase() {
#ifdef SIM_CHECKS
        if (rd_bits) { std::memset(rd_bits.get(), 0, bit_words * 8); std::memset(wr_bits.get(), 0, bit_words * 8); }
        phase++; phase_flagged = false;
#endif
    }
#ifdef SIM_CHECKS
    std::unique_ptr<uint64_t[]> rd_bits, wr_bits;
    uint32_t bit_words = 0, phase = 0;
    uint64_t disjoint_violations = 0;  // ref's BigSRAM.disjointViolations (MemTest reads it); check builds only
    S1Tile* s1 = nullptr;              // S1 (s1.h): the write's ordering point and its store
    bool phase_flagged = false;
    int ck_tile = -1;                  // the checker's tile (add_g1); -1 records nothing
    sim::Word go_last = 0;
    bool go_wire = false;
    void mark(uint64_t t, uint64_t* mine, const uint64_t* other, uint32_t c, unsigned did) {
        if (c >= cfg.mem_chunks) return;
        mine[c >> 6] |= 1ull << (c & 63);
        if (!(other[c >> 6] >> (c & 63) & 1)) return;
        disjoint_violations++;                                    // ref's disjointViolations: every such access
        if (!phase_flagged) {
            phase_flagged = true;
            if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::DISJOINT, c, did, did ^ 1, phase);
        }
    }
#endif
#ifdef SIM_CHECKS
    // (P4; approved 2026-11-12; SIM_CHECKS only, the timing builds unchanged): ref's per-area reads, writes, combined
    // and queuePeak (the count after an enqueue), updated where BigSRAM updates them, for MemTest. Statistics.
    uint64_t reads_a[AREAS] = {}, writes_a[AREAS] = {}, combined_a[AREAS] = {};
    uint32_t queue_peak[AREAS] = {};
#endif

    BigSram(sim::Ports& p, const SramConfig& c, uint32_t* block, bool with_reads, [[maybe_unused]] bool go_wire_ = false)
        : cfg(c), reads(with_reads), data(block) {
        if (!block) FATAL("big SRAM: no block");
        if (c.area_queue_depth < 1 || c.area_queue_depth > MAX_DEPTH) FATAL("big SRAM: area_queue_depth %u (1-%u)", c.area_queue_depth, MAX_DEPTH);
        if (c.mem_chunks < 1u << 16 || c.mem_chunks > 1u << 21 || c.mem_chunks % (1u << 16)) FATAL("big SRAM: mem_chunks %u (a multiple of 65,536 up to 2^21)", c.mem_chunks);
        p.in(wr, "wr", 3);
#ifdef SIM_CHECKS
        go_wire = go_wire_;
        if (go_wire) p.in(go_seen, "go_seen");
        bit_words = (c.mem_chunks + 63) / 64;
        rd_bits.reset(new uint64_t[bit_words]()); wr_bits.reset(new uint64_t[bit_words]());   // configuration: once
#endif
        if (reads) {
            p.in(rd, "rd", 1);
            for (unsigned a = 0; a < AREAS; a++) p.out(ret[a], "ret", 3);
        }
    }

    static unsigned area(uint32_t chunk) { return chunk >> 17 & 15; }
    static uint32_t row(uint32_t chunk) { return chunk >> 2 & (ROWS - 1); }
    static unsigned chunk_mask(uint32_t addr, unsigned n) { return ((1u << n) - 1) << (addr & 3) & 0xF; }
    static uint32_t chunk_of(uint64_t lo, uint64_t hi, int k) { return uint32_t((k < 2 ? lo : hi) >> (32 * (k & 1))); }

    uint32_t load(uint32_t c) const { return c < cfg.mem_chunks ? data[c] : 0; }
    void store(uint32_t c, uint32_t v) {
        if (c >= cfg.mem_chunks) return;
        if (cfg.running_sum) sum.write(c, data[c], v);
        data[c] = v;
    }
    // The contents' fingerprint sum (StateFp's sram.data): the running sum, or a scan of the block without it.
    uint64_t data_sum() const { return cfg.running_sum ? sum.sum : MemSum::scan(FP_SRAM, data, cfg.mem_chunks); }
    // Host access between cycles (SortTest's input load: BigSRAM.poke), keeping the sum.
    void poke(uint32_t c, uint32_t v) { store(c & 0x1FFFFF, v); }
    uint32_t peek(uint32_t c) const { return load(c & 0x1FFFFF); }
    void poke64(uint32_t c, uint64_t v) { poke(c, uint32_t(v)); poke(c + 1, uint32_t(v >> 32)); }
    uint64_t peek64(uint32_t c) const { return uint64_t(peek(c)) | uint64_t(peek(c + 1)) << 32; }

    const Entry& at(unsigned a, unsigned k) const { return q[a][(head[a] + k) % cfg.area_queue_depth]; }

    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
#ifdef SIM_CHECKS
        if (go_wire) {                                                // the phases endCycle(t - 1) started
            const sim::Word g = c.read(go_seen);
            if (g != go_last) { go_last = g; for (unsigned k = 0; k < (g & 3); k++) new_phase(); }
        }
#endif
        uint8_t start[AREAS];
        for (unsigned a = 0; a < AREAS; a++) start[a] = count[a];
        // 1. every area serves its head (as at the start of the cycle)
        for (uint32_t m = busy; m; m &= m - 1) serve(c, unsigned(__builtin_ctz(m)), t);
        // 2. arrivals: the write first, then the read
        if (c.ready(wr)) {
            const sim::Word* w = c.peek(wr);
            const uint32_t addr = uint32_t(w[0] & 0x1FFFFF);
            const unsigned a = area(addr), n = unsigned(w[0] >> 21 & 7), mask = chunk_mask(addr, n);
            const bool merge = cfg.write_combining && can_combine(a, row(addr), mask);
            const bool room = cfg.fifo_count_before_pop ? start[a] < cfg.area_queue_depth : count[a] < cfg.area_queue_depth;
            if (merge || room) {
                if (cfg.fifo_count_before_pop && !merge) start[a]++;   // the write goes in first (13a)
                const uint64_t lo = w[1], hi = w[2];
                c.take(wr);
                if (merge) combine(a, mask, addr & 3, lo, hi);
                else enqueue(a, true, row(addr), mask, 0, 0, lo, hi);
#ifdef SIM_CHECKS
                if (s1 && s1->on) for (unsigned k = 0; k < n; k++) {   // S1's ordering point (BigSRAM.java:90)
                    uint64_t acc;
                    if (s1->sram_get(t, addr + k, acc)) s1->ordered(t, addr + k, t - acc);
                }
#endif
            } else write_stall_cycles++;
        }
        if (reads && c.ready(rd)) {
            const sim::Word r = c.peek(rd)[0];
            const uint32_t addr = uint32_t(r & 0x1FFFFF);
            const unsigned a = area(addr);
            const bool room = cfg.fifo_count_before_pop ? start[a] < cfg.area_queue_depth : count[a] < cfg.area_queue_depth;
            if (room) {
                c.take(rd);
                enqueue(a, false, row(addr), chunk_mask(addr, unsigned(r >> 21 & 7)), uint32_t(r >> 24 & 0xFFFF), uint8_t(r >> 40 & 1), 0, 0);
            } else read_stall_cycles++;
        }
        // A request left in its Hold waits for room (a queue pops every cycle it is non-empty).
        return busy || c.ready(wr) || (reads && c.ready(rd)) ? c.next() : sim::Wake::none();
    }

    void enqueue(unsigned a, bool w, uint32_t rw, unsigned mask, uint32_t token, uint8_t part, uint64_t lo, uint64_t hi) {
        Entry& e = q[a][(head[a] + count[a]) % cfg.area_queue_depth];
        e.row = rw; e.write = w; e.mask = uint8_t(mask); e.token = token; e.part = part;
        if (w) {
            const int o = __builtin_ctz(mask);
            for (int j = 0; j < 4; j++) e.d[j] = (mask >> j & 1) ? chunk_of(lo, hi, j - o) : 0;
        } else e.d[0] = e.d[1] = e.d[2] = e.d[3] = 0;
        count[a]++;
#ifdef SIM_CHECKS
        if (count[a] > queue_peak[a]) queue_peak[a] = count[a];   // BigSRAM.queuePeak
#endif
        busy |= 1u << a;
    }
    bool can_combine(unsigned a, uint32_t rw, unsigned mask) const {
        if (count[a] < 1) return false;
        const Entry& e = at(a, count[a] - 1u);
        return e.write && e.row == rw && (e.mask & mask) == 0;
    }
    void combine(unsigned a, unsigned mask, unsigned o, uint64_t lo, uint64_t hi) {
        Entry& e = q[a][(head[a] + count[a] - 1u) % cfg.area_queue_depth];
        for (int j = 0; j < 4; j++) if (mask >> j & 1) e.d[j] = chunk_of(lo, hi, j - int(o));
        e.mask = uint8_t(e.mask | mask);
        combined++;
#ifdef SIM_CHECKS
        combined_a[a]++;                                          // BigSRAM.combined[a]
#endif
    }
    void serve(sim::Ctx& c, unsigned a, [[maybe_unused]] uint64_t t) {
        const Entry& e = q[a][head[a]];
        const uint32_t base = a * AREA_CHUNKS + e.row * 4;
        if (e.write) {
            writes_served++;
#ifdef SIM_CHECKS
            writes_a[a]++;                                        // BigSRAM.writes[a]
#endif
            for (unsigned j = 0; j < 4; j++) if (e.mask >> j & 1) {
#ifdef SIM_CHECKS
                mark(t, wr_bits.get(), rd_bits.get(), base + j, 1);   // invariant 2
                uint64_t acc;
                if (s1 && s1->on && s1->sram_remove(t, base + j, acc)) s1->stored(t - acc);   // S1 (BigSRAM.java:138)
#endif
                store(base + j, e.d[j]);
            }
        } else {
            reads_served++;
#ifdef SIM_CHECKS
            reads_a[a]++;                                         // BigSRAM.reads[a]
#endif
#ifdef SIM_CHECKS
            for (unsigned j = 0; j < 4; j++) if (e.mask >> j & 1) mark(t, rd_bits.get(), wr_bits.get(), base + j, 0);
#endif
            const uint64_t lo = uint64_t(load(base)) | uint64_t(load(base + 1)) << 32;
            const uint64_t hi = uint64_t(load(base + 2)) | uint64_t(load(base + 3)) << 32;
            c.put(ret[a], uint64_t(e.token) | uint64_t(e.part) << 16, lo, hi);
        }
        head[a] = uint8_t((head[a] + 1) % cfg.area_queue_depth);
        if (--count[a] == 0) busy &= ~(1u << a);
    }

    uint64_t digest() const {
        uint64_t h = sim::hash_fields(head, count, busy, cfg.running_sum ? sum.sum : 0);   // contents: with the sum only
        for (unsigned a = 0; a < AREAS; a++)
            for (unsigned k = 0; k < count[a]; k++) {
                const Entry& e = at(a, k);
                h = sim::hash_mix(h, sim::hash_fields(e.row, e.write, e.mask, e.part, e.token, e.d));
            }
        return h;
    }
};

}  // namespace units
