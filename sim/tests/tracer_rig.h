// tracer_rig.h (WORKER21; P7a, inventory C23): TracerRig in C++, ported from `ref`'s test/TracerRig.java (v1.3's
// WORKER22): a hand-built guest in a FileStore (an image owning every page; the Tracer's copy by cow_open with the data
// pages pre-copied, section 22), a jump-target map built by 4.21.4's hash rule, the universal tables, and an IDEAL
// CONTROLLER standing in for C&C 3's firmware: it lands every packet through the region port (one a cycle, expansion
// words first), answers new expansion (EXPANSION_TEMPLATE for an inline, then the template from EXPANSION_OFFSET, the N3
// rules as the replayer's driver), the expansion and advice credits, and takes each window by an ideal DMA at the state
// service. A real port (no generation): member for member, call for call, so the Tracer sees the calls `ref`'s rig makes.
//
// TWO REGIMES (D6). Building the guest, the image, the map and the record (the constructor, build_image, build_map,
// load_tables' queueing, load_record's queueing, collect) may use the STL. The per-cycle path (step, the window loop,
// drain_port) uses fixed rings and preallocated buffers only, and runs under the allocation hook.
//
// JAVA'S ITERATION ORDERS. `ref`'s rig iterates a HashMap of pages and a HashSet of owned pages when it builds the
// image, and the order decides where FileStore puts each page (so the caches' sets and every timing). `Pages` and
// `java_hash_order` reproduce java.util.HashMap's order for Long keys exactly (buckets by Long.hashCode spread, the
// table's growth, insertion order within a bin), and stop if a bin would be treeified (an order not reproduced; no
// TracerTest case gets near it).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "units/file_store.h"
#include "units/mem_system.h"
#include "units/replay_tables.h"
#include "units/stream_word.h"
#include "units/tracer.h"

namespace tracer_rig {

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
// The iteration order of a java.util.HashMap (or HashSet) of Long keys inserted in `keys`' order (repeats ignored).
std::vector<int64_t> java_hash_order(const std::vector<int64_t>& keys);

// The rig's pages: offset in .pages (a multiple of 4096) -> 512 words, as ref's HashMap<Long, long[]>.
struct Pages {
    std::vector<int64_t> keys;                 // insertion order
    std::vector<std::vector<uint64_t>> pages;  // by keys' index
    std::vector<uint64_t>& at(int64_t key);    // computeIfAbsent(key, new long[512])
    void put(int64_t key, std::vector<uint64_t> page);
    void poke(int64_t off, const std::vector<uint8_t>& b);
    void poke64(int64_t off, uint64_t v);
};

// ================================================================ the window's output
struct WindowOut {
    std::vector<uint64_t> record, code, rlog, wlog;  // {lo, hi} per 128-bit word, as the DMA delivered them
    units::streamword::Size sizes[3];
};

// ================================================================ the rig
class Rig {
public:
    static constexpr int SRC = 66;  // C&C 3
    Tracer tr{66};
    FileStore fs;
    MemSystem ms{&fs};
    const ReplayTables& tables;
    int64_t lowest = 0x7FF00000LL;
    int log2k = 20;
    int64_t t = 0;

    Rig();
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    ~Rig();

    // The tables, loaded once (ReplayTables::default_dir()).
    static const ReplayTables& shared_tables();

    // Each instruction's base instr_idx (section 18: 1 + the expansion lengths below it) by the universal tables.
    std::vector<int64_t> bases(const Prog& p) const;
    int expansion_length(uint32_t w) const;

    // ---- the images
    FileStore::File image, copy, map;
    int map_k = 5;
    uint32_t map_mul = 0x9E3779B1u, map_step = 0x00000B;  // 32 lines; the golden-ratio multiplier; a prime step
    // The image: pages, the code pages shared, `owned` (a HashSet's keys, in insertion order) pre-copied in the copy.
    void build_image(const Pages& pages, const std::vector<int64_t>& owned);
    // The jump map (4.21.4): 2^map_k lines of 16 (key u32, base u32) entries, by the probe's own rule; depth 0 here.
    // `targets`: pc -> base, in the order ref's Map iterates them.
    void build_map(const std::vector<std::pair<int64_t, int64_t>>& targets);

    // ---- the controller
    // The service order for the stream queues (has_order false: round-robin from queue 1); state_last serves queue 0
    // after them; q0_one sends queue 0 as one 68-word transfer; hold delays every service request (a slow C&C 3).
    int order[3] = {0, 0, 0};
    bool has_order = false, state_last = false, q0_one = false;
    int64_t hold = 0;
    int64_t dma_packets = 0;
    // Called after each cycle's step (TracerTest's counters: a window word read every cycle). Plain pointer, no STL.
    void (*on_step)(Rig&, void*) = nullptr;
    void* on_step_ctx = nullptr;

    void step();
    void drain_port();
    void load_tables();
    void word(int w, uint32_t v);
    // The segment's record, word 0 first: the state, the advice count, the registers, the files word, the bitmap 0.
    void load_record(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128], const std::vector<uint64_t>& advice);
    // One window: RUN_CONFIG, PULSE_RUN, the controller loop until run done (true, `out` filled), or the Tracer stops,
    // or max_cycles (false: ref's null).
    bool window(uint32_t run_config, int64_t max_cycles, WindowOut& out);
    // A region packet queued at the port (as ref's `port.add`), for a test's own landings.
    void port_add(uint32_t offset, uint64_t lo, uint64_t hi);

    // "no template for 0x..." lines (ref's `log`): the count and the last word.
    int no_template = 0;
    uint32_t no_template_w = 0;

    // ---- reading the output
    uint64_t reg(int r) const { return tr.rec[4 + r]; }
    static std::vector<uint64_t> stream_values(const std::vector<uint64_t>& words, int values, int bits);

    // MemorySystem.summary()'s line (W14's MemSystem has no summary; its counters are public).
    static std::string mem_summary(const MemSystem& m);

private:
    struct Pkt {
        uint32_t off;
        uint64_t lo, hi;
    };
    struct Ring {  // a fixed FIFO of packets (ref's ArrayDeque)
        Pkt* e = nullptr;
        int cap = 0, head = 0, n = 0;
        void push(const Pkt& p);
        Pkt pop();
    };
    Ring port_, port_exp_;

    // the ideal DMA (milestone 2): one transfer at a time, one 128-bit packet a cycle, as 4.17.9's
    int dma_q_ = -1;
    int64_t dma_left_ = 0;
    static constexpr int GOT_CAP = 1 << 16;  // 128-bit words per queue
    uint64_t* got_[4] = {};
    int got_n_[4] = {};
    void dma_start(int q, int64_t n);

    std::vector<uint64_t> advice_;
    int advice_next_ = 0;
    void send_advice();

    const uint64_t* exp_words_ = nullptr;  // null: ref's expWords == null
    int exp_len_ = 0, exp_next_ = 0;       // in 64-bit words
    uint64_t* exp_buf_ = nullptr;          // a template's copy with its offset word's rd0 changed (ref's clone)
    int exp_buf_cap_ = 0;
    uint64_t nop_words_[2] = {};
    void send_exp();

    void collect(WindowOut& o) const;
};

}  // namespace tracer_rig
