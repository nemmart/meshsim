// tracer_rig.h: the Tracer's rig for the suites and tests (v1.36): a hand-built guest in a FileStore (an image owning
// every page; the Tracer's copy by cow_open with the data pages pre-copied, section 22), a jump-target map built by
// 17.9.4's hash rule, the universal tables, and THE RTL BENCH'S CONTROLLER (tests/tracer_block.h's Bench, Snarky One's
// tracer_acceptance_test's bench rule for rule): one register access a cycle, the control block's and the DMA's words,
// WRITEs one a cycle through the write port, the peripheral DMA's timing. So a guest's cycles here are what the RTL's
// bench would count on the same guest, given the same memory system on the fill port: fixed read latencies (1 by
// default, as the RTL's tests), or the simulator's own model (`fill(0, 0)`, 17.9.14).
//
// The rig keeps v1.3's TracerRig's builders (an assembler, Pages, the image and the map) and its page order:
//
// JAVA'S ITERATION ORDER SETS THE PAGE ADDRESSES. v1.3's rig kept the guest's pages in a HashMap<Long, long[]> (filled
// by poke's computeIfAbsent and by put) and the pages to pre-copy in a HashSet<Long>, and build_image hands them to
// FileStore in those collections' iteration order. FileStore allocates pages in the order it is handed them, so that
// order decides every page's address, and the addresses decide the memory system's model's sets (MemSystem: 512 sets by
// line address), so its conflicts, evictions and every timing that follows. `JavaLongHashMap` reproduces
// java.util.HashMap's order for Long keys operation by operation: Long.hashCode and
// HashMap.hash's spread; put appending a new key to its bin and growing the table after inserting; computeIfAbsent
// prepending it and growing before; each one's treeifyBin threshold (which resizes a table under 64 bins); a resize
// splitting each bin in order. A bin that is treeified (9 keys in one bin of a table of 64 or more, e.g. 11 or more
// consecutive pages of one 64 KB block) iterates in an order that follows its red-black tree: not modelled, so the order
// is refused (FATAL), never guessed. No suite's guest gets near it (6 pages at most).
// Checked against Java itself on 49 cases, 3 of them treeified (tests/tracersuites/hashorder.txt, JavaHashOrder.java;
// test_tracer_rig).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "tracer_block.h"
#include "units/file_store.h"
#include "units/mem_system.h"
#include "units/replay_tables.h"
#include "units/stream_word.h"
#include "units/tracer.h"

namespace tracer_rig {

using tracer_block::Bench;
using tracer_block::RunOut;
using units::tracer::FileStore;
using units::tracer::MemSystem;
using units::tracer::ReplayTables;
using units::tracer::Tracer;

// ================================================================ an assembler (RV64IMAC encodings)
constexpr uint32_t r(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t i(int32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return (uint32_t(imm) & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t s(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t op) {
    return ((uint32_t(imm) >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (uint32_t(imm) & 31) << 7 | op;
}
constexpr uint32_t b(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    const uint32_t m = uint32_t(imm);
    return ((m >> 12) & 1) << 31 | ((m >> 5) & 63) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | ((m >> 1) & 15) << 8 |
           ((m >> 11) & 1) << 7 | 0x63;
}
constexpr uint32_t u(uint32_t imm20, uint32_t rd, uint32_t op) { return imm20 << 12 | rd << 7 | op; }
constexpr uint32_t j(int32_t imm, uint32_t rd) {
    const uint32_t m = uint32_t(imm);
    return ((m >> 20) & 1) << 31 | ((m >> 1) & 0x3FF) << 21 | ((m >> 11) & 1) << 20 | ((m >> 12) & 0xFF) << 12 | rd << 7 |
           0x6F;
}
constexpr uint32_t addi(uint32_t rd, uint32_t rs1, int32_t imm) { return i(imm, rs1, 0, rd, 0x13); }
constexpr uint32_t add(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r(0, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t mul(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r(1, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t div(uint32_t rd, uint32_t rs1, uint32_t rs2) { return r(1, rs2, rs1, 4, rd, 0x33); }
constexpr uint32_t lui(uint32_t rd, uint32_t imm20) { return u(imm20, rd, 0x37); }
constexpr uint32_t ld(uint32_t rd, uint32_t rs1, int32_t imm) { return i(imm, rs1, 3, rd, 0x03); }
constexpr uint32_t lw(uint32_t rd, uint32_t rs1, int32_t imm) { return i(imm, rs1, 2, rd, 0x03); }
constexpr uint32_t lbu(uint32_t rd, uint32_t rs1, int32_t imm) { return i(imm, rs1, 4, rd, 0x03); }
constexpr uint32_t sd(uint32_t rs2, uint32_t rs1, int32_t imm) { return s(imm, rs2, rs1, 3, 0x23); }
constexpr uint32_t sw(uint32_t rs2, uint32_t rs1, int32_t imm) { return s(imm, rs2, rs1, 2, 0x23); }
constexpr uint32_t sb(uint32_t rs2, uint32_t rs1, int32_t imm) { return s(imm, rs2, rs1, 0, 0x23); }
constexpr uint32_t bne(uint32_t rs1, uint32_t rs2, int32_t imm) { return b(imm, rs2, rs1, 1); }
constexpr uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t imm) { return b(imm, rs2, rs1, 0); }
constexpr uint32_t jal(uint32_t rd, int32_t imm) { return j(imm, rd); }
constexpr uint32_t jalr(uint32_t rd, uint32_t rs1, int32_t imm) { return i(imm, rs1, 0, rd, 0x67); }
constexpr uint32_t END = 0x0000006F;  // JAL x0, 0: the program's end

// A program: 16- and 32-bit parcels in order, from its pc.
struct Prog {
    int64_t base;
    std::vector<std::pair<uint32_t, int>> parcels;  // {bits, size 2 or 4}
    explicit Prog(int64_t b) : base(b) {}
    Prog& w(std::initializer_list<uint32_t> ws) {
        for (uint32_t x : ws) parcels.push_back({x, 4});
        return *this;
    }
    Prog& c(uint32_t h) {
        parcels.push_back({h & 0xFFFF, 2});
        return *this;
    }
    int64_t pc_of(int k) const;
    std::vector<uint8_t> bytes() const;
};

// ================================================================ Java's HashMap order for Long keys
// A java.util.HashMap<Long, V> (or HashSet<Long>: add is put) as far as its iteration order goes: the keys only.
class JavaLongHashMap {
public:
    void put(int64_t key);                 // HashMap.put, HashSet.add
    void compute_if_absent(int64_t key);   // HashMap.computeIfAbsent
    bool contains(int64_t key) const;
    std::vector<int64_t> order() const;    // keySet()'s iteration order (FATAL if a bin was treeified)
    bool treeified() const { return treeified_; }
    size_t size() const { return size_; }

private:
    std::vector<std::vector<int64_t>> tab_;  // empty: Java's table not yet allocated
    size_t size_ = 0, threshold_ = 0;
    bool treeified_ = false;
    static uint32_t hash(int64_t key);
    void resize();
    void treeify_bin(size_t bin);
};
// The iteration order of a HashSet<Long> after adding `keys` in order.
std::vector<int64_t> java_hash_order(const std::vector<int64_t>& keys);

// The rig's pages: offset in .pages (a multiple of 4096) -> 512 words, as ref's HashMap<Long, long[]>.
struct Pages {
    JavaLongHashMap map;                       // the keys, for the order
    std::vector<int64_t> keys;                 // storage: insertion order
    std::vector<std::vector<uint64_t>> pages;  // by keys' index
    std::vector<uint64_t>& at(int64_t key);    // computeIfAbsent(key, new long[512])
    void put(int64_t key, std::vector<uint64_t> page);  // put
    void poke(int64_t off, const std::vector<uint8_t>& b);
    void poke64(int64_t off, uint64_t v);
    const std::vector<uint64_t>& page(int64_t key) const;

private:
    std::vector<uint64_t>& slot(int64_t key);
};

// ================================================================ the window's output
struct WindowOut {
    std::vector<uint64_t> record, code, rlog, wlog;  // {lo, hi} per 128-bit word, as the DMA delivered them
    units::streamword::Size sizes[3];
};

// ================================================================ the rig
class Rig {
public:
    static constexpr int SRC = Bench::SRC;
    FileStore fs;
    Tracer tr{66};
    MemSystem ms{&fs};
    const ReplayTables& tables;
    Bench bench;         // the controller, the write port, the DMA: the RTL bench's
    int64_t& t;          // the cycle about to run (bench.cycle)
    int64_t lowest = 0x7FF00000LL;
    int log2k = 20;

    Rig();
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The tables, loaded once (ReplayTables::default_dir()).
    static const ReplayTables& shared_tables();

    // The fill port (tracer.h's fill_latency): fixed read latencies for the guest's file and the map's, 1 and 1 at
    // construction, as the RTL's tests' memory system; 0 and 0: the simulator's model of the memory system (`ms`).
    void fill(int memory, int map) { tr.fill_latency[0] = memory; tr.fill_latency[1] = map; }

    // Each instruction's base instr_idx (section 18: 1 + the expansion lengths below it) by the universal tables.
    std::vector<int64_t> bases(const Prog& p) const;
    int expansion_length(uint32_t w) const;

    // ---- the images
    FileStore::File image, copy, map;
    int map_k = 5;
    uint32_t map_mul = 0x9E3779B1u, map_step = 0x00000B;  // 32 lines; the golden-ratio multiplier; a prime step
    // The image: pages (in their HashMap's order), the code pages shared, `owned` (a HashSet's keys, added in the order
    // given) pre-copied in the copy, in that set's order.
    void build_image(const Pages& pages, const std::vector<int64_t>& owned);
    // The jump map (17.9.4): 2^map_k lines of 16 (key u32, base u32) entries, by the probe's own rule; depth 0 here.
    // `targets`: pc -> base, in the order given.
    void build_map(const std::vector<std::pair<int64_t, int64_t>>& targets);

    // ---- the controller: the bench's, a cycle each
    void step() { bench.idle_cycle(); }                 // a cycle with no access
    void drain_port() { bench.flush(); }                // until every queued WRITE has landed
    void load_tables() { bench.setup(map_mul, map_step, uint32_t(map_k)); }
    void word(int w, uint32_t v) { bench.store(Bench::B_TWO, w - 32, v); }     // a store to word w (32-51)
    uint32_t load_word(int w) { return bench.load(Bench::B_TWO, w - 32); }     // a load of word w
    uint32_t status() { return bench.load(Bench::B_CONTROL, Bench::C_STATUS); }
    uint32_t errors() { return bench.load(Bench::B_CONTROL, Bench::C_ERRORS); }
    uint32_t events_peek() { return bench.load(Bench::B_CONTROL, Bench::C_EVENTS_PEEK); }
    uint32_t events() { return bench.load(Bench::B_CONTROL, Bench::C_EVENTS); }  // a consuming load
    // The segment's record, word 0 first: the state, the advice count, the registers, the files word, the bitmap 0;
    // then the advice the state write's credits release (Bench::segment). RUN_CONFIG is left 0: window stores it.
    void load_record(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128], const std::vector<uint64_t>& advice);
    // The record's 136 words as load_record writes them.
    void record_words(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128], size_t advice_values,
                      uint64_t out[2 * Tracer::REC_WORDS]) const;
    // One window: RUN_CONFIG, START, the bench's loop until done (true, `out` filled), or the Tracer stops on an
    // error, or max_cycles (false). `last` is the bench's account of it.
    bool window(uint32_t run_config, int64_t max_cycles, WindowOut& out);
    RunOut last;
    // A WRITE queued at the write port, for a test's own landings.
    void port_add(uint32_t offset, uint64_t lo, uint64_t hi) { bench.write(offset, lo, hi); }

    // ---- reading the output
    uint64_t reg(int r) const { return tr.rec[4 + r]; }
    static std::vector<uint64_t> stream_values(const std::vector<uint64_t>& words, int values, int bits);

    // The MemSystem's counters in one line.
    static std::string mem_summary(const MemSystem& m);
};

// ================================================================ a monitor on the fill port
// Every request's handshake and response, in handshake order (MemSystem::port_log), while it lives.
struct PortLog {
    std::vector<MemSystem::PortCall> calls;
    MemSystem& ms;
    explicit PortLog(Rig& g, int cap = 1 << 14) : calls(size_t(cap)), ms(g.ms) {
        ms.port_log = calls.data();
        ms.port_log_cap = cap;
        ms.port_log_n = 0;
    }
    PortLog(const PortLog&) = delete;
    PortLog& operator=(const PortLog&) = delete;
    ~PortLog() { ms.port_log = nullptr; }
    int n() const { return ms.port_log_n < ms.port_log_cap ? ms.port_log_n : ms.port_log_cap; }
    const MemSystem::PortCall& at(int k) const { return calls[size_t(k)]; }
    int writes() const { int k = 0; for (int i = 0; i < n(); i++) k += at(i).write; return k; }
    int reads_of(int32_t root, int64_t line) const {
        int k = 0;
        for (int i = 0; i < n(); i++) k += !at(i).write && at(i).root == root && at(i).line == line;
        return k;
    }
    // the k-th write, or the first read or write of a line (null: none)
    const MemSystem::PortCall* write(int k) const {
        for (int i = 0; i < n(); i++) if (at(i).write && k-- == 0) return &at(i);
        return nullptr;
    }
    const MemSystem::PortCall* first(bool write, int64_t line) const {
        for (int i = 0; i < n(); i++) if (at(i).write == write && at(i).line == line) return &at(i);
        return nullptr;
    }
    // 17.9.5: at most one read outstanding, and at most one write
    bool one_of_each() const {
        int64_t read_until = INT64_MIN, write_until = INT64_MIN;
        for (int i = 0; i < n(); i++) {
            int64_t& until = at(i).write ? write_until : read_until;
            if (at(i).t <= until) return false;
            until = at(i).response_at;
        }
        return true;
    }
    // 17.9.5: a write is requested when no read is outstanding. tracer_fill.veryl: it is raised in the cycle after one
    // with no read presented, outstanding or granted, so no read's request is in the write's cycle or the cycle before,
    // and no read is outstanding in the cycle before.
    bool writes_wait_for_reads() const {
        for (int i = 0; i < n(); i++) {
            if (!at(i).write) continue;
            const int64_t w = at(i).t;
            for (int k = 0; k < n(); k++)
                if (!at(k).write && at(k).t <= w && w <= at(k).response_at + 1) return false;
        }
        return true;
    }
    // a read and a write outstanding together: a read's request while a write awaits its response, or the reverse
    bool overlap() const {
        for (int i = 0; i < n(); i++)
            for (int k = 0; k < n(); k++)
                if (at(i).write && !at(k).write && at(i).t < at(k).response_at && at(k).t < at(i).response_at) return true;
        return false;
    }
};

}  // namespace tracer_rig
