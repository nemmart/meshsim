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
// JAVA'S ITERATION ORDER SETS THE PAGE ADDRESSES. `ref`'s rig keeps the guest's pages in a HashMap<Long, long[]> (filled
// by poke's computeIfAbsent and by put) and the pages to pre-copy in a HashSet<Long>, and build_image hands them to
// FileStore in those collections' iteration order. FileStore allocates pages in the order it is handed them, so that
// order decides every page's address, and the addresses decide the shared cache's sets (MemSystem: 512 sets by line
// address), so its conflicts, evictions and every timing that follows. A rig that put the pages in another order (by
// key, or by insertion) would build a different, equally valid machine whose timings need not be ref's.
// `JavaLongHashMap` reproduces java.util.HashMap's order for Long keys operation by operation: Long.hashCode and
// HashMap.hash's spread; put appending a new key to its bin and growing the table after inserting; computeIfAbsent
// prepending it and growing before; each one's treeifyBin threshold (which resizes a table under 64 bins); a resize
// splitting each bin in order. A bin that is treeified (9 keys in one bin of a table of 64 or more, e.g. 11 or more
// consecutive pages of one 64 KB block) iterates in an order that follows its red-black tree: not modelled, so the order
// is refused (FATAL), never guessed. No TracerTest guest gets near it (6 pages at most).
// Checked against Java itself on 49 cases, 3 of them treeified (tests/tracersuites/hashorder.txt, JavaHashOrder.java;
// test_tracer_rig). TracerTest's own guests show why it matters: in rig() the data page (computeIfAbsent second) comes
// before the code page.
#pragma once

#include <cstdint>
#include <cstdio>
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

// ================================================================ the call recorder (TracerConf's format)
// The Tracer as the rig and the suite see it: every call TracerConf.Rec records (tests/ref/TracerConf.java) goes through
// here, and with a log open each is written as Rec writes it, so the rig's calls can be compared with `ref`'s rig's
// (tests/tracerconf/cases.log.gz; test_tracer_rig). Hiding, not overriding: the Tracer's own code is untouched, and with
// no log a call costs one test. Ids are given at construction, as Rec's (probes included). Lines: "N id 66 lines",
// store dumps ("F id k first next" and a "P"/"Z" line per page), "c id t0 n sig" (consecutive equal cycles run
// together; no observation, unlike Rec's), and "U id": the suite is about to run a UB check in a forked child, whose
// calls (and the one that stops) are in `ref`'s log and not here. Fixed buffers only: nothing allocates while recording
// (the window loop runs under the allocation hook).
class RecTracer : public Tracer {
public:
    explicit RecTracer(const FileStore* fs);
    ~RecTracer();
    RecTracer(const RecTracer&) = delete;
    RecTracer& operator=(const RecTracer&) = delete;

    // The log (null: off), for every RecTracer made after it is set; ids from 0 again.
    static void open_log(std::FILE* f);
    static void close_log();
    void mark_ub();  // before a UB check on this Tracer runs in a child

    // ---- the recorded calls (Rec's list)
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void run(int64_t t);
    int write_word(int64_t t, int w, uint32_t v);
    bool word(int64_t t, int w, uint32_t& v);
    bool running(int64_t t);
    int errors(int64_t t);
    void sync(int64_t t);
    bool rows_ended(int64_t t);
    void out_start(int64_t t, int q, int64_t words);
    bool out_ready(int64_t t, int q);
    void out_take(int64_t t, int q, uint64_t w[2]);
    void step(int64_t t);  // ref's runCycle
    struct Ev {
        RecTracer& r;
        uint32_t value(int64_t t);
        uint32_t consume(uint32_t bits);
    };
    Ev events() { return Ev{*this}; }

private:
    static std::FILE* log_;
    static int next_id_;
    const FileStore* fs_;
    const int id_;
    bool rec_ = false, started_ = false;
    int64_t dumped_next_ = -1;
    int dumps_ = 0;
    static constexpr int TOKS = 64, TOKLEN = 112, SIGLEN = 8192;
    int64_t call_t_[TOKS] = {};
    char call_tok_[TOKS][TOKLEN] = {};
    int n_tok_ = 0;
    bool g_open_ = false;
    char g_sig_[SIGLEN] = {};
    int64_t g_t0_ = 0, g_n_ = 0;
    char sig_[SIGLEN] = {};

    void start();
    int dump();
    void call(int64_t t, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void app(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void make_sig(int64_t anchor);
    int64_t anchor() const;
    void flush_group();
    void end_cycle(int64_t t);
    void flush_partial();
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
    FileStore fs;
    RecTracer tr{&fs};  // a Tracer(66), its calls recorded when a log is open
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
    // The image: pages (in their HashMap's order), the code pages shared, `owned` (a HashSet's keys, added in the order
    // given) pre-copied in the copy, in that set's order.
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
