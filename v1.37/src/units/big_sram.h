// big_sram.h: the scratchpad (machine-spec 15; the RTL's rtl/blocks/scratchpad).
//
// `mem_chunks` 32-bit words (SCRATCHPAD_BYTES / 4: a power of two, 512 bytes to 8 MiB) in banks of
// min(SCRATCHPAD_BYTES, MAX_BANK_BYTES = 512 KiB), each contiguous and aligned to its size. A bank does one access a
// cycle, a read or a write, of one 128-bit row (word address: bits 1-0 the word in the row).
//   - THE WRITE (`wr`, from the write engine, the only writer) is done in the cycle it is presented: it wins its bank,
//     never waits, and there is no queue (12.4, 15). It is in place for every read taken in a later cycle.
//   - THE READ (`rd`, from the read arbiter in the inject unit) is the one the arbiter took the cycle before it reaches
//     this unit: the arbiter takes a read only if no write meets it at its bank (14.1; the write engine tells it its
//     write's cycle: write_engine.h's `wr_note`), so the scratchpad never chooses. Its row goes back on `ret`, a
//     Link(READ_LATENCY - 1): the row is at the inject unit READ_LATENCY cycles after the take, whatever its bank, so
//     reads return in the order taken (15).
//     In the cycle the read reaches this unit it is served BEFORE that cycle's write: a read taken in cycle T sees the
//     writes presented up to T - 1 (the RTL's accesses of one cycle reach the banks' SRAMs at one edge).
//   - A read that meets a write at its bank is the arbiter's error: the RTL's row is undefined; fatal here.
// A word at or past `mem_chunks` reads 0 and a write to it is dropped (UB, 15: the RTL's row aliases; flagged upstream).
//
// THE CONTENTS. One zero-filled block per tile, mem_chunks 32-bit words, allocated once at configuration by its owner
// (`SramBlocks`: anonymous mmap; the OS backs only the pages touched), never in the loop (D6). The fingerprint's sum is
// kept incrementally at every store when `running_sum` (the default; D31: off in timing runs, where data_sum() scans the
// block if a fingerprint is ever asked for).
//
// PORTS
//   in   wr      HoldIn, 3 words: the write engine's write {word address | words << 21, words 0-1, words 2-3}, presented
//                in the cycle it is visible; always taken (never refused)
//        rd      HoldIn, 1 word:  the arbiter's read {word address | words << 21} (words 0: a read of nothing, which
//                returns zeros in its turn); declared only with `reads`
//   out  ret     LinkOut, 2 words, Link(READ_LATENCY - 1): the row {words 0-1, words 2-3}; declared only with `reads`
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

// 15, 19: MAX_BANK_BYTES = 512 KiB, in 32-bit words; a scratchpad smaller than a bank is one bank.
constexpr uint32_t kMaxBankWords = 1u << 17;
inline uint32_t scratchpad_bank_words(uint32_t mem_chunks) { return mem_chunks < kMaxBankWords ? mem_chunks : kMaxBankWords; }
inline uint32_t scratchpad_bank(uint32_t mem_chunks, uint32_t word) { return (word & (mem_chunks - 1)) / scratchpad_bank_words(mem_chunks); }
// SCRATCHPAD_BYTES is a power of two from 512 bytes to 8 MiB (5.1, 19): no partial bank.
inline bool scratchpad_size_ok(uint32_t mem_chunks) { return mem_chunks >= 128 && mem_chunks <= 1u << 21 && (mem_chunks & (mem_chunks - 1)) == 0; }

struct SramConfig {
    uint32_t mem_chunks = 1u << 20;
    // Keep the contents' fingerprint sum at every store (D31). Off in timing runs, where no fingerprint is taken: the
    // store is then a plain store, and data_sum() scans the block when asked.
    bool running_sum = true;
};

// The units' scratchpad blocks: one anonymous mapping per unit (tiles 0-63, the special tiles as units 64-66), made at
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
        if (k >= kUnits) FATAL("scratchpad blocks: unit %u", k);
        if (b_[k]) {
            if (n_[k] != mem_chunks) FATAL("scratchpad blocks: tile %u mapped with %u words, asked for %u", k, n_[k], mem_chunks);
            return b_[k];
        }
        void* p = mmap(nullptr, size_t(mem_chunks) * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) FATAL("scratchpad blocks: mmap of %u words for tile %u failed", mem_chunks, k);
        b_[k] = static_cast<uint32_t*>(p); n_[k] = mem_chunks;
        return b_[k];
    }
    size_t allocated_bytes() const { size_t s = 0; for (unsigned k = 0; k < kUnits; k++) s += size_t(n_[k]) * 4; return s; }
    // Resident pages (mincore), for the footprint: what the run touched of what is allocated.
    size_t touched_bytes() const {
        const size_t pg = size_t(sysconf(_SC_PAGESIZE));
        static unsigned char vec[(size_t(1) << 23) / 4096];       // one block (8 MiB at most) at 4 KiB pages; between runs only
        size_t s = 0;
        for (unsigned k = 0; k < kUnits; k++) {
            if (!b_[k]) continue;
            const size_t bytes = size_t(n_[k]) * 4, pages = (bytes + pg - 1) / pg;
            if (pages > sizeof vec || mincore(b_[k], bytes, vec) != 0) FATAL("scratchpad blocks: mincore on tile %u", k);
            for (size_t i = 0; i < pages; i++) s += (vec[i] & 1) ? pg : 0;
        }
        return s;
    }
private:
    uint32_t* b_[kUnits] = {};
    uint32_t n_[kUnits] = {};
};

struct BigSram {
    static constexpr unsigned MAX_BANKS = 16;

    // ---- ports ----
    sim::HoldIn wr, rd;
    sim::LinkOut ret;

    // ---- configuration ----
    SramConfig cfg;
    bool reads = false;
    uint32_t* data = nullptr;          // mem_chunks words (SramBlocks)
    MemSum sum{FP_SRAM};

    // ---- state: none but the contents (no queue: 15) ----
    uint64_t last_write = 0;           // the cycle of the last write, and its bank (statistics; the checks below)
    uint32_t last_write_bank = 0;

    // ---- statistics ----
    uint64_t reads_served = 0, writes_served = 0;
    uint64_t reads_b[MAX_BANKS] = {}, writes_b[MAX_BANKS] = {};
    // (invariant 2): every word read and every word written in a phase is marked; a word both read and written in one
    // phase is a violation, its DISJOINT fact recorded once a phase. Phases: the harness's Chip::new_phase. SIM_CHECKS
    // only (the bits, two per word, allocated at build); elsewhere new_phase does nothing.
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
    uint64_t disjoint_violations = 0;
    S1Tile* s1 = nullptr;              // S1 (s1.h): the write's ordering point and its store
    bool phase_flagged = false;
    int ck_tile = -1;                  // the checker's tile (add_g1); -1 records nothing
    sim::Word go_last = 0;
    bool go_wire = false;
    void mark(uint64_t t, uint64_t* mine, const uint64_t* other, uint32_t c, unsigned did) {
        if (c >= cfg.mem_chunks) return;
        mine[c >> 6] |= 1ull << (c & 63);
        if (!(other[c >> 6] >> (c & 63) & 1)) return;
        disjoint_violations++;
        if (!phase_flagged) {
            phase_flagged = true;
            if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::DISJOINT, c, did, did ^ 1, phase);
        }
    }
#endif

    BigSram(sim::Ports& p, const SramConfig& c, uint32_t* block, bool with_reads, [[maybe_unused]] bool go_wire_ = false)
        : cfg(c), reads(with_reads), data(block) {
        if (!block) FATAL("scratchpad: no block");
        if (!scratchpad_size_ok(c.mem_chunks))
            FATAL("scratchpad: %u words: SCRATCHPAD_BYTES is a power of two from 512 bytes to 8 MiB (machine-spec 5.1, 15)", c.mem_chunks);
        p.in(wr, "wr", 3);
#ifdef SIM_CHECKS
        go_wire = go_wire_;
        if (go_wire) p.in(go_seen, "go_seen");
        bit_words = (c.mem_chunks + 63) / 64;
        rd_bits.reset(new uint64_t[bit_words]()); wr_bits.reset(new uint64_t[bit_words]());   // configuration: once
#endif
        if (reads) { p.in(rd, "rd", 1); p.out(ret, "ret", 2); }
    }

    unsigned banks() const { return cfg.mem_chunks / scratchpad_bank_words(cfg.mem_chunks); }
    unsigned bank(uint32_t word) const { return scratchpad_bank(cfg.mem_chunks, word); }
    static uint32_t chunk_of(uint64_t lo, uint64_t hi, int k) { return uint32_t((k < 2 ? lo : hi) >> (32 * (k & 1))); }

    uint32_t load(uint32_t c) const { return c < cfg.mem_chunks ? data[c] : 0; }
    void store(uint32_t c, uint32_t v) {
        if (c >= cfg.mem_chunks) return;
        if (cfg.running_sum) sum.write(c, data[c], v);
        data[c] = v;
    }
    // The contents' fingerprint sum: the running sum, or a scan of the block without it.
    uint64_t data_sum() const { return cfg.running_sum ? sum.sum : MemSum::scan(FP_SRAM, data, cfg.mem_chunks); }
    // Host access between cycles (a test's input and its reading of the results), keeping the sum.
    void poke(uint32_t c, uint32_t v) { store(c & 0x1FFFFF, v); }
    uint32_t peek(uint32_t c) const { return load(c & 0x1FFFFF); }
    void poke64(uint32_t c, uint64_t v) { poke(c, uint32_t(v)); poke(c + 1, uint32_t(v >> 32)); }
    uint64_t peek64(uint32_t c) const { return uint64_t(peek(c)) | uint64_t(peek(c + 1)) << 32; }

    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
#ifdef SIM_CHECKS
        if (go_wire) {
            const sim::Word g = c.read(go_seen);
            if (g != go_last) { go_last = g; for (unsigned k = 0; k < (g & 3); k++) new_phase(); }
        }
#endif
        // 1. the read the arbiter took at t - 1: its row as the writes up to t - 1 left it
        if (reads && c.ready(rd)) {
            const sim::Word r = c.peek(rd)[0];
            const uint32_t addr = uint32_t(r & 0x1FFFFF);
            const unsigned n = unsigned(r >> 21 & 7);
            c.take(rd);
            if (n && last_write == t - 1 && last_write_bank == bank(addr))
                FATAL("scratchpad: cycle %llu: a read at word 0x%x taken in the cycle of a write to its bank %u: the read "
                      "arbiter takes no such read (machine-spec 14.1, 15)", (unsigned long long)(t - 1), addr, bank(addr));
            const uint32_t base = addr & ~3u;
            reads_served++; reads_b[bank(addr)]++;
#ifdef SIM_CHECKS
            for (unsigned j = 0; j < n; j++) mark(t, rd_bits.get(), wr_bits.get(), addr + j, 0);
#endif
            const uint64_t lo = n ? uint64_t(load(base)) | uint64_t(load(base + 1)) << 32 : 0;
            const uint64_t hi = n ? uint64_t(load(base + 2)) | uint64_t(load(base + 3)) << 32 : 0;
            c.put(ret, lo, hi);
        }
        // 2. this cycle's write: it wins its bank and is done at once
        if (c.ready(wr)) {
            const sim::Word* w = c.peek(wr);
            const uint32_t addr = uint32_t(w[0] & 0x1FFFFF);
            const unsigned n = unsigned(w[0] >> 21 & 7);
            const uint64_t lo = w[1], hi = w[2];
            c.take(wr);
            writes_served++; writes_b[bank(addr)]++;
            last_write = t; last_write_bank = bank(addr);
            for (unsigned j = 0; j < n; j++) {
#ifdef SIM_CHECKS
                mark(t, wr_bits.get(), rd_bits.get(), addr + j, 1);   // invariant 2
                uint64_t acc;
                if (s1 && s1->on && s1->sram_get(t, addr + j, acc)) s1->ordered(t, addr + j, t - acc);   // S1's ordering point
                if (s1 && s1->on && s1->sram_remove(t, addr + j, acc)) s1->stored(t - acc);            // and its store
#endif
                store(addr + j, chunk_of(lo, hi, int(j)));
            }
        }
        return sim::Wake::none();                             // every access arrives on a port
    }

    uint64_t digest() const { return cfg.running_sum ? sum.sum : 0; }   // contents: with the sum only
};

}  // namespace units
