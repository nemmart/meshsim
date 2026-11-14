// test_tracer_rules.cpp: the Tracer's rules that v1.36 took from the RTL (Snarky One), each against spec 17.9
// (`spec/jolt/tracer.md`), on the rig (tests/tracer_rig.h), whose controller is the RTL bench's. Two families:
//
//  - THE BEHAVIOUR v1.36 CHANGED (the inventory's A10.1-A10.10): the events' and errors' bit numbers, a failing
//    write-back, the fill port's two channels, the write-back buffer, EXPANSION_OFFSET's latch, the jump LRU's key, the
//    record's spare bits, each log's own end, the events after a stop, the reset. Each test fails on v1.35's behaviour:
//    tests/tracer_teeth.sh plants it in the Tracer and requires the test named there to fail.
//  - THE TIMING (17.9.7, the inventory's B6), as closed forms in the fill port's read latencies, Lm for the guest's
//    file and Lp for the map's, each checked at two latencies. Where the spec gives no number the RTL's source does:
//      a run's first row          7 cycles after START's store, 6 after its effect (rtl/blocks/tracer_unit/README.md,
//                                 "The rows and their schedule"; tracer_execution_test.rs, what_a_transfer_costs)
//      a redirect                 the next row 5 cycles past its schedule (the same); 4 to pc 0, which is fetched
//                                 from nowhere (tracer_instruction.veryl, issue_padding)
//      the NOP policy             + 2 (replayer_fetch.veryl reads the template's first ROM word for its rd0, then ROM
//                                 word 1: rtl/blocks/replayer_unit/README.md, "the shared NOP row of a ROM template,
//                                 + 2 cycles"; the Tracer's README says "a cycle")
//      a core-fed instruction     + 1 before its first row, the cycle its decode-table entry is read and it is posted
//                                 (README, "Its stall attribution": the cause is what kept the row's reads from the
//                                 cycle before, and the wait is the core's only from the post on)
//      an instruction miss        Lm + 3 (tracer_fetch.veryl: REQUEST; tracer_fill.veryl's request register; the
//                                 response; READ_LINE; against a hit's READ in the look-up's next cycle)
//      a data miss                Lm + 5 (tracer_data.veryl: VICTIM, MISS, REQUEST or EVICT; the request register;
//                                 the response; READ_LINE; against a hit's ACCESS in the look-up's next cycle)
//      a probe of n lines         n (Lp + 4) + 2 (tracer_jump_lookup.veryl: LOOKUP, HOME; a line: READ, the request
//                                 register, the response, tracer_memory.veryl's register of it, DECIDE; the answer's
//                                 register), against an LRU hit's answer in the look-up's next cycle
//      the rows' end              the control's is the cycle after the last row's completion (tracer_unit.veryl
//                                 registers row_done); the choice two cycles after it (tracer_output.veryl, "in the
//                                 cycle after rows_end's register"); state_ready from the cycle after the choice
//      done                       the second cycle after the DMA's last pop (peripheral_dma.veryl: its busy falls
//                                 after the cycle that follows its last entry; tracer_run.veryl's work_end)
// Run from v1.36/.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "harness.h"
#include "tracer_rig.h"

using namespace tracer_rig;

namespace {

constexpr int64_t CODE = 0x80000000LL, DATA = 0x80001000LL;
constexpr uint32_t NOP = 0x00000013, EBREAK = 0x00100073;
constexpr uint32_t CSR = 0x305090f3;   // CSRRW x1, mtvec, x1: a core-fed template of class 2, 3 words
using RigP = std::unique_ptr<Rig>;
using Targets = std::vector<std::pair<int64_t, int64_t>>;

// A rig with p at CODE (shared, so not writable), `data_pages` owned pages of zeros from DATA, the map holding the
// instructions `targets` names (by index), and the fill port at (lm, lp).
RigP rig(const Prog& p, const std::vector<int>& targets, int data_pages = 1, int lm = 1, int lp = 1,
         const Targets& more_targets = {}, int map_k = 5) {
    RigP g(new Rig());
    g->fill(lm, lp);
    g->map_k = map_k;
    Pages pages;
    pages.poke(CODE - g->lowest, p.bytes());
    std::vector<int64_t> owned;
    for (int k = 0; k < data_pages; k++) {
        const int64_t o = DATA - g->lowest + 4096LL * k;
        pages.put(o, std::vector<uint64_t>(512, 0));
        owned.push_back(o);
    }
    g->build_image(pages, owned);
    const std::vector<int64_t> base = g->bases(p);
    Targets t = more_targets;
    for (int k : targets) t.push_back({p.pc_of(k), base[size_t(k)]});
    g->build_map(t);
    g->load_tables();
    return g;
}
// The segment's start at the program's first instruction, x2 = x3 = DATA.
void start(Rig& g, const Prog& p, const std::vector<uint64_t>& advice = {}) {
    uint64_t regs[128] = {};
    regs[2] = regs[3] = uint64_t(DATA);
    g.load_record(CODE, int32_t(g.bases(p)[0]), 0, regs, advice);
}
int64_t stat(Rig& g, int k) {
    uint32_t v = 0;
    g.tr.word(g.t, Tracer::W_STAT + k, v);
    return int64_t(v);
}
// The line of the guest's file that holds address a.
int64_t line_of(const Rig& g, int64_t a) { return (a - g.lowest) / 128; }
// A doubleword of the guest's file as the memory holds it now.
uint64_t memory_at(Rig& g, int64_t a) {
    FileStore::Walk w;
    g.fs.walk(g.copy.root, g.copy.depth, line_of(g, a), w);
    if (w.data_line < 0) return ~uint64_t(0);
    uint64_t d[16];
    g.fs.read_line(w.data_line, d);
    return d[((a - g.lowest) % 128) / 8];
}

// Every row's start and completion cycles.
struct RowLog {
    std::vector<int64_t> start, done;
    explicit RowLog(Rig& g) {   // room for a window: the Tracer's cycle allocates nothing (the rig's guard)
        start.reserve(1 << 16); done.reserve(1 << 16);
        g.tr.on_row = hook; g.tr.on_row_ctx = this;
    }
    static void hook(void* ctx, int64_t t, bool s) {
        RowLog& r = *static_cast<RowLog*>(ctx);
        (s ? r.start : r.done).push_back(t);
    }
    std::vector<int64_t> gaps() const {
        std::vector<int64_t> g;
        for (size_t k = 1; k < start.size(); k++) g.push_back(start[k] - start[k - 1]);
        return g;
    }
};

// The two latency pairs every timing test runs at: (Lm, Lp).
constexpr int LAT[2][2] = {{1, 1}, {7, 3}};

#define CHECK_VEC(got, ...)                                                                              \
    do {                                                                                                 \
        const std::vector<int64_t> want_ = {__VA_ARGS__};                                                \
        const std::vector<int64_t> got_ = (got);                                                         \
        if (got_ != want_) {                                                                             \
            std::string s_;                                                                              \
            for (int64_t v_ : got_) s_ += " " + std::to_string(v_);                                      \
            ::test::fail(__FILE__, __LINE__, "CHECK_VEC(%s): got%s, want %s", #got, s_.c_str(), #__VA_ARGS__); \
            return;                                                                                      \
        }                                                                                                \
    } while (0)

}  // namespace

// ================================================================ the timing (17.9.7, B6)

// A run's first instruction, a control transfer, a branch not taken (the RTL's what_a_transfer_costs, here through the
// memory side): ADDI, JAL (taken, a probe of one line), ADDI, BEQ (not taken), ADDI, the end.
TEST(b6_a_redirect_costs_5_and_the_first_row_comes_7_after_start) {
    for (const auto& lat : LAT) {
        const int lm = lat[0], lp = lat[1];
        Prog p(CODE);
        p.w({addi(20, 0, 1), jal(0, 8), EBREAK, addi(20, 20, 1), beq(20, 0, 8), addi(20, 20, 1), END});
        RigP gp = rig(p, {3}, 1, lm, lp);
        Rig& g = *gp;
        start(g, p);
        RowLog rows(g);
        WindowOut o;
        CHECK(g.window(6, 10000, o));
        CHECK_EQ(g.reg(20), uint64_t(3));
        const int64_t probe = (lp + 4) + 2;
        CHECK_EQ(rows.start[0] - g.last.start_store, int64_t(7 + (lm + 3)));   // the first row, and its line's fill
        CHECK_VEC(rows.gaps(), 4, 4 + probe + 5, 4, 4 + 5, 4);
        CHECK_EQ(stat(g, Tracer::ST_STALL_FETCH), int64_t(6 + (lm + 3) + 5 + 5));
        CHECK_EQ(stat(g, Tracer::ST_STALL_MAP), probe);
        CHECK_EQ(stat(g, Tracer::ST_STALL_DATA), int64_t(0));
        // a row takes its 4 cycles, the transfer's and the branch's too: the stall is after them
        for (size_t k = 0; k < rows.done.size(); k++)
            CHECK_EQ(rows.done[k] - rows.start[k], int64_t(k == 1 ? 3 + probe : 3));
    }
}

// Straight-line code keeps its schedule; the shared NOP row (ADDI x0) starts two cycles late.
TEST(b6_the_nop_policy_costs_two_cycles) {
    for (const auto& lat : LAT) {
        Prog p(CODE);
        p.w({addi(5, 0, 1), NOP, addi(5, 5, 1), NOP, NOP, addi(5, 5, 1), mul(6, 5, 5), addi(5, 5, 1), END});
        RigP gp = rig(p, {}, 1, lat[0], lat[1]);
        Rig& g = *gp;
        start(g, p);
        RowLog rows(g);
        WindowOut o;
        CHECK(g.window(9, 10000, o));
        CHECK_EQ(g.reg(6), uint64_t(9));
        // 4 a row, 6 into a NOP row; the M row (MUL) takes 6
        CHECK_VEC(rows.gaps(), 6, 4, 6, 6, 4, 4, 6, 4);
        CHECK_EQ(stat(g, Tracer::ST_STALL_FETCH), int64_t(6 + (lat[0] + 3) + 3 * 2));
        CHECK_EQ(rows.done[6] - rows.start[6], int64_t(5));   // the M row's 6 cycles
    }
}

// A core-fed instruction: one stall cycle in STAT_STALL_FETCH before its first row, the cycle its decode-table entry
// is read and it is posted; from the post the wait is the core's, in no counter (17.9.8); its rows and its successor
// keep the schedule (its last word is popped rows ahead). The core here is the RTL bench's: a post in cycle n is
// seen by the EVENTS load of n + 1, answered by the loads of words 35 and 34, and its first word's WRITE is in
// n + 4 (build-scratch/rtl-ref/HOWTO.md, "A run", steps 2 to 5): 3 cycles of wait.
TEST(b6_a_core_fed_instruction_costs_a_fetch_cycle_and_the_cores_wait) {
    for (const auto& lat : LAT) {
        Prog p(CODE);
        p.w({addi(5, 0, 1), CSR, addi(5, 5, 1), addi(5, 5, 1), END});
        RigP gp = rig(p, {}, 1, lat[0], lat[1]);
        Rig& g = *gp;
        start(g, p);
        RowLog rows(g);
        WindowOut o;
        const int n = 1 + g.expansion_length(CSR) + 3;
        CHECK_EQ(n, 7);
        CHECK(g.window(uint32_t(n), 10000, o));
        CHECK_EQ(g.tr.new_expansions, int64_t(1));
        const int64_t fetch = stat(g, Tracer::ST_STALL_FETCH), core = g.tr.stalls[Tracer::S_EXP];
        CHECK_EQ(fetch, int64_t(6 + (lat[0] + 3) + 1));
        CHECK_EQ(core, int64_t(3));
        CHECK_EQ(g.tr.stalls[Tracer::S_ADVICE], int64_t(0));
        // the cycles add up (17.9.8): the schedules, the counted stalls, the wait on the core
        CHECK_EQ(g.last.last_row_done - g.last.start_store, 4 * int64_t(n) + fetch + core);
        CHECK_VEC(rows.gaps(), 4 + 1 + 3, 4, 4, 4, 4, 4);
    }
}

// An instruction line's fill: straight-line code into a second line (32 instructions of 4 bytes a line), and a
// transfer into a third.
TEST(b6_an_instruction_miss_costs_the_latency_and_3) {
    for (const auto& lat : LAT) {
        const int lm = lat[0], lp = lat[1];
        Prog p(CODE);
        for (int k = 0; k < 40; k++) p.w({addi(5, 5, 1)});
        p.w({jal(0, 4 * 30)});               // instruction 40, to instruction 70, in the third line
        for (int k = 0; k < 29; k++) p.w({EBREAK});
        p.w({addi(5, 5, 1), END});           // instructions 70 and 71
        RigP gp = rig(p, {70}, 1, lm, lp);
        Rig& g = *gp;
        start(g, p);
        RowLog rows(g);
        WindowOut o;
        CHECK(g.window(43, 10000, o));
        CHECK_EQ(g.reg(5), uint64_t(41));
        CHECK_EQ(stat(g, Tracer::ST_MISS_I), int64_t(3));
        const int64_t miss = lm + 3, probe = (lp + 4) + 2;
        const std::vector<int64_t> gaps = rows.gaps();
        for (int k = 0; k < 40; k++) CHECK_EQ(gaps[size_t(k)], int64_t(k == 31 ? 4 + miss : 4));   // into the second line
        CHECK_EQ(gaps[40], 4 + probe + 5 + miss);   // the transfer: its probe, its redirect, its target's line
        CHECK_EQ(stat(g, Tracer::ST_STALL_FETCH), 6 + miss + miss + 5 + miss);
        CHECK_EQ(stat(g, Tracer::ST_STALL_MAP), probe);
        CHECK_EQ(g.ms.reads, int64_t(4));   // need, not speculation (17.9.5): three instruction lines, one map line
    }
}

// A data line's fill, with no victim and with a clean one; a hit costs nothing.
TEST(b6_a_data_miss_costs_the_latency_and_5) {
    for (const auto& lat : LAT) {
        const int lm = lat[0];
        Prog p(CODE);
        // loads of lines 0, 0 (a hit), 1 and 2, then a store into line 3
        p.w({ld(5, 2, 0), ld(6, 2, 8), ld(7, 2, 128), addi(8, 0, 9), ld(9, 2, 256), sd(8, 2, 384), END});
        RigP gp = rig(p, {}, 1, lm, lat[1]);
        Rig& g = *gp;
        start(g, p);
        RowLog rows(g);
        WindowOut o;
        CHECK(g.window(7, 10000, o));
        const int64_t miss = lm + 5;
        CHECK_VEC(rows.gaps(), 4 + miss, 4, 4 + miss, 4, 4 + miss, 4 + miss);
        CHECK_EQ(stat(g, Tracer::ST_STALL_DATA), 4 * miss);
        CHECK_EQ(stat(g, Tracer::ST_MISS_D), int64_t(4));
        CHECK_EQ(stat(g, Tracer::ST_STALL_FETCH), int64_t(6 + (lm + 3)));
    }
}

// A probe of one, two and three lines: the target's home line full of other keys, by the host's rule (17.9.4).
TEST(b6_a_probe_costs_2_and_each_line_the_latency_and_4) {
    for (const auto& lat : LAT) {
        const int lm = lat[0], lp = lat[1];
        for (int lines = 1; lines <= 3; lines++) {
            Prog p(CODE);
            p.w({jal(0, 8), EBREAK, addi(5, 0, 7), END});
            // 2 map lines' worth of keys whose home line is the target's: 16 fill it, 16 more the next line probed
            Rig probe;
            const int k = 2;
            const auto home = [&](int64_t pc) {
                return int64_t((uint64_t(uint32_t(pc - probe.lowest)) * uint64_t(probe.map_mul) & 0xFFFFFFFFull) >> (32 - k));
            };
            const int64_t target = p.pc_of(2);
            Targets others;
            for (int64_t pc = CODE + 0x10000; int(others.size()) < 16 * (lines - 1); pc += 4)
                if (home(pc) == home(target)) others.push_back({pc, 1000 + (pc & 0xFFF)});
            RigP gp = rig(p, {2}, 1, lm, lp, others, k);
            Rig& g = *gp;
            start(g, p);
            RowLog rows(g);
            WindowOut o;
            CHECK(g.window(3, 10000, o));
            CHECK_EQ(g.reg(5), uint64_t(7));
            const int64_t cost = lines * (lp + 4) + 2;
            CHECK_EQ(stat(g, Tracer::ST_STALL_MAP), cost);
            CHECK_EQ(stat(g, Tracer::ST_PROBES), int64_t(1));
            CHECK_EQ(g.tr.probe_reads, int64_t(lines));
            CHECK_VEC(rows.gaps(), 4 + cost + 5, 4);
            CHECK_EQ(stat(g, Tracer::ST_STALL_FETCH), int64_t(6 + (lm + 3) + 5));
        }
    }
}

// The rows' end to state_ready, and done after the DMA's last pop.
struct LastPop {
    Rig* g;
    int64_t packets = 0, at = -1;
    static void hook(void* ctx, int64_t n) {
        LastPop& l = *static_cast<LastPop*>(ctx);
        if (l.g->bench.dma_packets != l.packets) { l.packets = l.g->bench.dma_packets; l.at = n; }
    }
};
TEST(b6_state_ready_after_the_rows_end_and_done_after_the_last_pop) {
    for (int rows_n : {0, 6}) {
        Prog p(CODE);
        p.w({addi(5, 0, 1), sd(5, 2, 0), ld(6, 2, 0), addi(5, 5, 1), addi(5, 5, 1), END});
        RigP gp = rig(p, {});
        Rig& g = *gp;
        start(g, p);
        LastPop pop{&g};
        g.bench.on_cycle = LastPop::hook;
        g.bench.on_cycle_ctx = &pop;
        WindowOut o;
        CHECK(g.window(uint32_t(rows_n), 10000, o));
        // the last row completes at c: the control's rows' end c + 1 (the cycle after START's with no rows), the choice
        // c + 3, state_ready up from c + 4, where the load of that cycle reads it
        const int64_t rows_end = rows_n == 0 ? g.last.start_store + 1 : g.last.last_row_done + 1;
        CHECK_EQ(g.tr.rows_end_at, rows_end);
        CHECK_EQ(g.last.state_ready_load, rows_end + 3);
        // the length words read the stream word's counts from the choice on (17.9.1): the bench loads them after
        CHECK_EQ(g.last.lengths[0], uint32_t(o.code.size() / 2));
        CHECK_EQ(g.last.lengths[0], uint32_t((rows_n + 3) / 4));
        // done: the DMA's last pop at q, its busy through q + 1, done posted in q + 2, read by the load of q + 3
        CHECK_EQ(pop.packets, int64_t(68 + o.code.size() / 2 + o.rlog.size() / 2 + o.wlog.size() / 2));
        CHECK_EQ(g.tr.drained_at, pop.at + 2);
        CHECK_EQ(g.last.done_load, pop.at + 3);
    }
}

// ================================================================ the behaviour v1.36 changed (A10)
namespace {

// EVENTS as the Tracer's control holds it (dma_done apart, which is the DMA's), ORed over every cycle.
struct Seen {
    Rig* g;
    uint32_t any = 0;
    static void hook(void* ctx, int64_t n) {
        Seen& s = *static_cast<Seen*>(ctx);
        s.g->tr.sync(n + 1);
        s.any |= s.g->tr.events().value(n + 1);
    }
};
// The events a run of `rows` rows of p shows, the two credits apart.
uint32_t events_of(const Prog& p, int rows, bool* ok) {
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    Seen seen{&g};
    g.bench.on_cycle = Seen::hook;
    g.bench.on_cycle_ctx = &seen;
    WindowOut o;
    *ok = g.window(uint32_t(rows), 10000, o);
    return seen.any & ~0x0Cu;
}
// The record's 68 words written through the write port, with no load of EVENTS: the state write's credits stay.
void write_record(Rig& g, const Prog& p) {
    uint64_t regs[128] = {}, rec[2 * Tracer::REC_WORDS];
    regs[2] = regs[3] = uint64_t(DATA);
    g.record_words(CODE, int32_t(g.bases(p)[0]), 0, regs, 0, rec);
    for (uint32_t w = 0; w < Tracer::REC_WORDS; w++) g.port_add(16 * w, rec[2 * w], rec[2 * w + 1]);
    g.drain_port();
}
// A transfer of `count` entries of output queue q, started by hand: the DMA's three stores (17.4), as the bench's.
void transfer(Rig& g, int q, uint32_t count) {
    g.bench.store(Bench::B_DMA, Bench::D_HDR, uint32_t(1 + q) << 2 | 5u << 8 | count << 15 | uint32_t(q) << 27);
    g.bench.store(Bench::B_DMA, Bench::D_DEST_ADDR, 0x04000000u | uint32_t(q) << 20);
    g.bench.store(Bench::B_DMA, Bench::D_START, 1);
}
void idle(Rig& g, int64_t cycles) { for (int64_t k = 0; k < cycles; k++) g.step(); }
void idle_until(Rig& g, int64_t cycle) { while (g.t < cycle) g.step(); }

// `lines` stores, one a line from DATA, each of its countdown value (the first line holds `lines`), then `tail`.
// x2 is left at DATA + 128 * lines; x3 stays DATA.
Prog stores_then(int lines, std::initializer_list<uint32_t> tail) {
    Prog p(CODE);
    p.w({addi(1, 0, lines), sd(1, 2, 0), addi(2, 2, 128), addi(1, 1, -1), bne(1, 0, -12)});
    p.w(tail);
    return p;
}
// One store (77 into line 0), then loads of lines 1 to `lines` - 1: one dirty line and clean ones. Then `tail`.
// x2 is left at DATA + 128 * (lines - 1).
Prog one_store_then_loads(int lines, std::initializer_list<uint32_t> tail) {
    Prog p(CODE);
    p.w({addi(1, 0, 77), sd(1, 2, 0), addi(6, 0, lines - 1), addi(2, 2, 128), ld(7, 2, 0), addi(6, 6, -1), bne(6, 0, -12)});
    p.w(tail);
    return p;
}

}  // namespace

// A10.1. EVENTS' bits, by 17.9.1's table, as literal numbers: 0 done, 1 dma_done, 2 advice_credit, 3
// expansion_credit, 4 new_expansion, 5 state_ready, 6 code_ready, 7 read_log_ready, 8 write_log_ready.
TEST(a10_1_events_bits_are_packed_from_bit_0) {
    bool ok = false;
    // the streams a run has decide its _ready events: none; code; code and the read log; code and the write log
    Prog code(CODE), load(CODE), store(CODE), csr(CODE);
    code.w({addi(5, 0, 1), addi(5, 5, 1), addi(5, 5, 1)});
    load.w({ld(5, 2, 0), addi(5, 5, 1), addi(5, 5, 1)});
    store.w({sd(5, 2, 0), addi(5, 5, 1), addi(5, 5, 1)});
    csr.w({CSR, addi(5, 5, 1), addi(5, 5, 1)});
    CHECK_EQ(events_of(code, 0, &ok), 0x21u);    // done [0], state_ready [5]
    CHECK(ok);
    CHECK_EQ(events_of(code, 3, &ok), 0x61u);    // and code_ready [6]
    CHECK(ok);
    CHECK_EQ(events_of(load, 3, &ok), 0xE1u);    // and read_log_ready [7]
    CHECK(ok);
    CHECK_EQ(events_of(store, 3, &ok), 0x161u);  // write_log_ready [8], no read log
    CHECK(ok);
    CHECK_EQ(events_of(csr, 5, &ok), 0x71u);     // new_expansion [4]
    CHECK(ok);

    // the credits [2] and [3], filled to 2 by the state write; dma_done [1] and done [0] through the control's words
    RigP gp = rig(code, {});
    Rig& g = *gp;
    write_record(g, code);
    CHECK_EQ(g.events_peek(), 0x0Cu);
    CHECK_EQ(g.events(), 0x0Cu);         // a consuming load takes one of each (EVENTS_MASK 0x1F)
    CHECK_EQ(g.events_peek(), 0x0Cu);    // filled to 2: still set
    CHECK_EQ(g.events(), 0x0Cu);
    CHECK_EQ(g.events_peek(), 0u);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);   // RUN_CONFIG 0: a zero-row run
    idle(g, 5);
    CHECK_EQ(g.events_peek(), 0x20u);
    transfer(g, 0, 67);
    idle(g, 72);
    CHECK_EQ(g.events_peek(), 0x22u);    // dma_done; state_ready still up: word 67 is not read
    CHECK_EQ(g.status(), 1u);            // busy: the run has not ended
    CHECK_EQ(g.events(), 0x22u);
    transfer(g, 0, 1);
    idle(g, 6);
    CHECK_EQ(g.events_peek(), 0x03u);    // done and dma_done; state_ready down
    CHECK_EQ(g.status(), 0u);
}

// A10.1. ERRORS' bits 0 and 9 (the others: the suite's "errors that stop the Tracer", by literal numbers).
TEST(a10_1_queue_overrun_is_errors_bit_0_and_the_tracer_runs_on) {
    Prog p(CODE);
    p.w({addi(5, 0, 1), addi(5, 5, 1), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    for (int k = 0; k < 257; k++) g.port_add(0x200000, uint64_t(k), 0);   // the read queue holds 256 entries
    g.drain_port();
    CHECK_EQ(g.errors(), 0x001u);
    CHECK(!g.tr.stopped());
    g.word(Tracer::W_RUN_CONFIG, 3);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    idle(g, 40);
    CHECK_EQ(g.events_peek() & 0x20u, 0x20u);   // the rows ran: state_ready
    CHECK_EQ(g.load_word(Tracer::W_ROWS_RUN), 3u);
    CHECK_EQ(g.errors(), 0x001u);
}

// A10.2. A write-back's response `error` (17.9.5, 17.9.9): ERRORS bit 9, the line is lost, the Tracer stops.
TEST(a10_2_a_write_back_error_sets_bit_9_and_stops_the_tracer) {
    const Prog p = stores_then(130, {ld(4, 3, 0), ld(5, 3, 128), END});
    RigP gp = rig(p, {1}, 5);
    Rig& g = *gp;
    start(g, p);
    g.ms.fail_write_line = line_of(g, DATA);   // the first victim's line
    PortLog port(g);
    WindowOut o;
    CHECK(!g.window(1 + 130 * 4 + 3, 100000, o));
    CHECK_EQ(g.last.errors, 0x200u);
    CHECK(g.tr.stopped());
    CHECK_EQ(g.last.status & 1u, 1u);                    // busy stays 1
    CHECK_EQ(port.writes(), 1);
    CHECK_EQ(port.write(0)->line, line_of(g, DATA));
    CHECK_EQ(port.write(0)->status, int(MemSystem::WRITE_ERROR));
    CHECK_EQ(memory_at(g, DATA), uint64_t(0));           // the line is lost: its 130 never reached the memory
    CHECK_EQ(memory_at(g, DATA + 128), uint64_t(0));     // and nothing more is written back
    CHECK(g.last.rows_run < 1 + 130 * 4);                // it traced no further: the loads never ran
    CHECK_EQ(g.reg(4), uint64_t(0));
    CHECK_EQ(g.last.events_peek & 0x1E1u, 0u);           // no done, no _ready
}

// A10.2, 17.9.9 "while idle": the response comes after the run's end; the next run stops at its START.
TEST(a10_2_a_write_back_error_while_idle_stops_the_next_run_at_its_start) {
    const Prog p = stores_then(128, {sd(2, 2, 0), END});
    RigP gp = rig(p, {1}, 5);
    Rig& g = *gp;
    start(g, p);
    PortLog port(g);
    WindowOut o;
    CHECK(g.window(1 + 128 * 4, 400000, o));   // 128 dirty lines, none evicted
    CHECK_EQ(port.writes(), 0);
    g.ms.fail_write_line = line_of(g, DATA);   // line 0's write-back answers `error`, 2,000 cycles after its request
    g.ms.fail_write_delay = 2000;
    CHECK(g.window(1, 400000, o));             // the 129th line: line 0 into the buffer; the run ends before its response
    CHECK_EQ(g.last.errors, 0u);
    CHECK_EQ(g.last.status, 0u);
    CHECK_EQ(port.writes(), 1);
    const int64_t response = port.write(0)->response_at;
    CHECK(response > g.t);                     // the write is still outstanding, the Tracer idle
    idle_until(g, response + 1);
    CHECK_EQ(g.errors(), 0x200u);
    CHECK_EQ(g.status(), 0u);                  // still idle
    const int64_t rows = g.tr.rows;
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    idle(g, 300);
    CHECK_EQ(g.status(), 1u);                  // busy rose, and stays
    CHECK_EQ(g.load_word(Tracer::W_ROWS_RUN), 0u);
    CHECK_EQ(g.tr.rows, rows);                 // the run traces nothing
    CHECK_EQ(g.events_peek(), 0u);             // no done, no state_ready
}

// A10.3. The fill port's two channels (17.9.5): a write-back's write outstanding while the next read is requested.
TEST(a10_3_a_read_and_a_write_outstanding_together) {
    const Prog p = one_store_then_loads(128, {ld(8, 2, 128), ld(9, 2, 256), END});
    RigP gp = rig(p, {3}, 5, 0, 0);            // the simulator's memory system
    Rig& g = *gp;
    start(g, p);
    PortLog port(g);
    WindowOut o;
    CHECK(g.window(3 + 127 * 4 + 3, 400000, o));
    CHECK_EQ(g.tr.errors(g.t), 0);
    // line 128's fill evicts line 0, dirty, into the buffer; line 129's victim is clean, so its read does not wait
    CHECK_EQ(port.writes(), 1);
    const MemSystem::PortCall* w = port.write(0);
    const MemSystem::PortCall* r = port.first(false, line_of(g, DATA) + 129);
    CHECK(w != nullptr && r != nullptr);
    CHECK_EQ(w->line, line_of(g, DATA));
    CHECK(w->t < r->t && r->t < w->response_at);   // the read requested while the write awaits its response
    CHECK(port.overlap());
    CHECK(port.one_of_each());                     // never two reads, never two writes
    CHECK(port.writes_wait_for_reads());           // the write was requested with no read outstanding
    CHECK_EQ(memory_at(g, DATA), uint64_t(77));    // and made
    CHECK_EQ(port.reads_of(g.copy.root, line_of(g, DATA)), 1);
}

// A10.4. The buffer's place is free again at its write's response: a second dirty victim waits for it, and its row
// with it.
TEST(a10_4_a_second_dirty_victim_waits_for_the_buffers_place) {
    const Prog p = stores_then(128, {sd(2, 2, 0), sd(2, 2, 128), END});
    RigP gp = rig(p, {1}, 5, 0, 0);
    Rig& g = *gp;
    start(g, p);
    PortLog port(g);
    RowLog rows(g);
    WindowOut o;
    CHECK(g.window(1 + 128 * 4 + 3, 400000, o));
    CHECK_EQ(port.writes(), 2);
    const MemSystem::PortCall* w0 = port.write(0);
    const MemSystem::PortCall* w1 = port.write(1);
    const MemSystem::PortCall* r129 = port.first(false, line_of(g, DATA) + 129);
    CHECK(r129 != nullptr);
    CHECK_EQ(w0->line, line_of(g, DATA));
    CHECK_EQ(w1->line, line_of(g, DATA) + 1);
    CHECK(r129->t > w0->response_at);              // line 129's fill waited for the place, so for the response
    CHECK(w1->t > w0->response_at);
    CHECK(port.one_of_each());
    CHECK(port.writes_wait_for_reads());
    CHECK_EQ(g.tr.take_backs, int64_t(0));
    // its row waited with it: the second store's row was in flight through the first write's response
    const size_t last = rows.start.size() - 2;     // the store into line 129
    CHECK(rows.start[last] < w0->response_at && rows.done[last] > w0->response_at);
    CHECK_EQ(memory_at(g, DATA), uint64_t(128));
    CHECK_EQ(memory_at(g, DATA + 128), uint64_t(127));
}

// A10.4. A miss on the buffer's line takes it back, dirty, with no read; its write, already requested, is still made
// and holds the place until its response. Here the taker's victim is clean, so it does not wait.
TEST(a10_4_a_miss_on_the_buffers_line_takes_it_back_with_no_read) {
    const Prog p = one_store_then_loads(128, {ld(8, 2, 128), ld(9, 3, 0), END});
    RigP gp = rig(p, {3}, 5, 0, 0);
    Rig& g = *gp;
    g.ms.hit = 60;                                 // a write of some 180 cycles
    start(g, p);
    PortLog port(g);
    RowLog rows(g);
    WindowOut o;
    CHECK(g.window(3 + 127 * 4 + 3, 400000, o));
    CHECK_EQ(g.reg(9), uint64_t(77));              // the line's data, from the buffer
    CHECK_EQ(g.tr.take_backs, int64_t(1));
    CHECK_EQ(port.reads_of(g.copy.root, line_of(g, DATA)), 1);   // the store's fill alone: no read for the take-back
    CHECK_EQ(port.writes(), 1);                    // the requested write is made
    const MemSystem::PortCall* w = port.write(0);
    CHECK_EQ(w->status, int(MemSystem::WRITE_OK));
    CHECK_EQ(memory_at(g, DATA), uint64_t(77));
    const size_t taker = rows.done.size() - 2;     // ld x9
    CHECK(rows.start[taker] > w->t && rows.done[taker] < w->response_at);   // taken back while the write was outstanding
    CHECK(port.one_of_each());
    // taken back dirty: it is the cache's one dirty line again
    CHECK_EQ(g.tr.write_back_dirty_untimed(), 1);
    CHECK_EQ(g.tr.write_back_dirty_untimed(), 0);
}

// A10.4. The same with a dirty victim: it needs the buffer's place, which the requested write holds until its
// response; then the victim goes into the buffer and the buffer's line into its place.
TEST(a10_4_a_take_back_with_a_dirty_victim_waits_for_the_write) {
    const Prog p = stores_then(128, {sd(2, 2, 0), ld(9, 3, 0), END});
    RigP gp = rig(p, {1}, 5, 0, 0);
    Rig& g = *gp;
    g.ms.hit = 60;
    start(g, p);
    PortLog port(g);
    RowLog rows(g);
    WindowOut o;
    CHECK(g.window(1 + 128 * 4 + 3, 400000, o));
    CHECK_EQ(g.reg(9), uint64_t(128));             // line 0's value, from the buffer
    CHECK_EQ(g.tr.take_backs, int64_t(1));
    CHECK_EQ(port.reads_of(g.copy.root, line_of(g, DATA)), 1);
    CHECK_EQ(port.writes(), 2);                    // line 0's requested write, then the victim's, line 1's
    const MemSystem::PortCall* w0 = port.write(0);
    const MemSystem::PortCall* w1 = port.write(1);
    CHECK_EQ(w0->line, line_of(g, DATA));
    CHECK_EQ(w1->line, line_of(g, DATA) + 1);
    CHECK(w1->t > w0->response_at);
    const size_t taker = rows.done.size() - 2;
    CHECK(rows.done[taker] > w0->response_at);     // the row waited for the place
    CHECK(port.one_of_each());
    CHECK_EQ(memory_at(g, DATA), uint64_t(128));
    CHECK_EQ(memory_at(g, DATA + 128), uint64_t(127));
}

// A10.4, 17.9.3. The state write drops every dirty line and writes nothing back; a write already requested is still
// made and holds the buffer's place until its response, into the next segment.
struct SlowWrite {   // the memory system slow from the moment its `reads`-th read is taken: the write after it is slow
    Rig* g;
    int64_t reads;
    static void hook(void* ctx, int64_t) {
        SlowWrite& s = *static_cast<SlowWrite*>(ctx);
        if (s.g->ms.reads >= s.reads) s.g->ms.hit = 20000;
    }
};
TEST(a10_4_the_state_write_keeps_a_requested_writes_place) {
    const Prog p = stores_then(128, {sd(2, 2, 0), END});
    RigP gp = rig(p, {1}, 5, 0, 0);
    Rig& g = *gp;
    start(g, p);
    PortLog port(g);
    SlowWrite slow{&g, 1 + 1 + 129};               // the instruction line, the map's, 129 data lines
    g.bench.on_cycle = SlowWrite::hook;
    g.bench.on_cycle_ctx = &slow;
    WindowOut o;
    CHECK(g.window(1 + 128 * 4 + 1, 400000, o));
    g.bench.on_cycle = nullptr;
    CHECK_EQ(port.writes(), 1);
    const MemSystem::PortCall* w0 = port.write(0);
    CHECK(w0->response_at > g.t + 30000);          // outstanding for a long time yet
    g.ms.hit = 6;
    // the next segment, the same guest from its start: the state write lands with the write outstanding
    start(g, p);
    CHECK(w0->response_at > g.t);
    CHECK_EQ(g.tr.cache_lines(true), 0);
    CHECK(g.window(1 + 128 * 4 + 1, 400000, o));
    CHECK_EQ(g.tr.errors(g.t), 0);
    // nothing written back at the state write: lines 1 to 128 were dirty and are dropped
    CHECK_EQ(port.writes(), 2);                    // the second is this segment's own first victim
    const MemSystem::PortCall* w1 = port.write(1);
    CHECK(w1->t > w0->response_at);                // which waited for the place the old write held
    CHECK(port.one_of_each());
    CHECK_EQ(memory_at(g, DATA), uint64_t(128));   // the old write was made
    CHECK_EQ(g.reg(1), uint64_t(0));
}

// A10.5. EXPANSION_OFFSET is the row offset at the post, held until the next post (17.9.1): a segment that starts
// inside a core-fed expansion presents it at its offset, and the rows after do not move the word.
TEST(a10_5_expansion_offset_is_latched_at_the_post) {
    Prog p(CODE);
    p.w({CSR, addi(5, 0, 1), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    uint64_t regs[128] = {};
    g.load_record(CODE, int32_t(g.bases(p)[0]) + 1, 1, regs, {});   // inside the CSR's expansion, at its row 1
    WindowOut o;
    const int n = g.expansion_length(CSR);
    CHECK(n >= 3);
    CHECK(g.window(uint32_t(n - 1 + 2), 10000, o));
    CHECK_EQ(g.last.post_offset.size(), size_t(1));
    CHECK_EQ(g.last.post_offset[0], 1u);                    // presented at its offset
    CHECK_EQ(g.last.post_opcode[0], CSR);
    CHECK_EQ(g.reg(5), uint64_t(1));                        // the rows went on: offsets 2, then 0 twice
    CHECK_EQ(g.load_word(Tracer::W_EXP_OFFSET), 1u);        // the word still reads the post's
    CHECK_EQ(g.load_word(Tracer::W_EXP_OPCODE), CSR);
}

// A10.6. A jump target whose offset from lowest_address is 2^32 or more is map_miss at once (17.9.4): no look-up, the
// LRU untouched, no probe, STAT_PROBES not counted. A JALR to pc 0 is one.
TEST(a10_6_a_jump_to_pc_0_is_a_map_miss_with_no_probe) {
    Prog p(CODE);
    p.w({jalr(0, 0, 0), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    PortLog port(g);
    WindowOut o;
    CHECK(!g.window(2, 10000, o));
    CHECK_EQ(g.last.errors, 0x008u);
    CHECK_EQ(g.last.stat[Tracer::ST_PROBES], 0u);
    CHECK_EQ(port.n(), 1);                                  // the instruction line's read alone: no map line
    CHECK_EQ(port.at(0).root, g.copy.root);
    CHECK_EQ(g.tr.lru_hits + g.tr.lru_misses, int64_t(0));
}
// ... and so is a target 2^32 past one the LRU holds: the key is the offset's 32 bits, tested for its size first.
TEST(a10_6_an_offset_of_2_to_the_32_or_more_never_hits_the_lru) {
    Prog p(CODE);
    p.w({jal(0, 8), EBREAK, addi(5, 0, 1), jalr(0, 10, 0), END});
    RigP gp = rig(p, {2});
    Rig& g = *gp;
    uint64_t regs[128] = {};
    regs[10] = uint64_t(p.pc_of(2)) + (uint64_t(1) << 32);   // the held target's offset + 2^32
    g.load_record(CODE, int32_t(g.bases(p)[0]), 0, regs, {});
    WindowOut o;
    CHECK(!g.window(5, 10000, o));
    CHECK_EQ(g.last.rows_run, 2u);                           // the JAL and the ADDI; the JALR stops
    CHECK_EQ(g.last.errors, 0x008u);
    CHECK_EQ(g.last.stat[Tracer::ST_PROBES], 1u);            // the JAL's probe alone
    CHECK_EQ(g.tr.probe_reads, int64_t(1));
    CHECK_EQ(g.tr.lru_hits, int64_t(0));
}

// A10.7. The record's spare bits are not held (17.9.2: "spare, 0"; the RTL's tracer_record.veryl and register file):
// output queue 0 reads them 0, and register 0 reads 0.
TEST(a10_7_spare_record_bits_read_0_through_output_queue_0) {
    Prog p(CODE);
    p.w({addi(5, 0, 1), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    uint64_t regs[128], rec[2 * Tracer::REC_WORDS];
    for (int k = 0; k < 128; k++) regs[k] = ~uint64_t(0) - uint64_t(k);
    g.record_words(CODE, 5, 0, regs, 0, rec);
    const uint64_t state_hi = rec[1], files_hi = rec[2 * Tracer::REC_FILES + 1];
    rec[1] |= uint64_t(0xFFFF) << 48;                        // the state word's [127:112]
    rec[3] |= uint64_t(7) << 61;                             // the stream word's [127:125]
    rec[2 * Tracer::REC_FILES + 1] |= ~uint64_t(0) << 39;    // the files word's [127:103]
    g.bench.segment(rec, {}, 0);
    WindowOut o;
    CHECK(g.window(0, 2000, o));
    CHECK_EQ(o.record[1], state_hi);
    CHECK_EQ(o.record[3] >> 61, uint64_t(0));
    CHECK_EQ(o.record[2 * Tracer::REC_FILES + 1], files_hi);
    CHECK_EQ(o.record[4], uint64_t(0));                      // register 0
    CHECK_EQ(o.record[5], regs[1]);                          // every held bit as written
    CHECK_EQ(o.record[4 + 127], regs[127]);
}

// A10.8. Each log ends on its own last value (17.9.6): the write log's one value comes first of 600 log words, and its
// stream ends, with its credit and its _ready, when the log scan has given it that value, not at the scan's end.
struct FirstReady {
    Rig* g;
    int64_t at[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    static void hook(void* ctx, int64_t n) {
        FirstReady& f = *static_cast<FirstReady*>(ctx);
        f.g->tr.sync(n + 1);
        const uint32_t v = f.g->tr.events().value(n + 1);
        for (int b = 0; b < 9; b++) if ((v >> b & 1) && f.at[b] < 0) f.at[b] = n + 1;
    }
};
TEST(a10_8_each_log_ends_on_its_own_last_value) {
    Prog p(CODE);
    p.w({addi(1, 0, 200), sd(1, 2, 0), ld(5, 2, 0), addi(1, 1, -1), bne(1, 0, -8), END});
    RigP gp = rig(p, {2});
    Rig& g = *gp;
    start(g, p);
    FirstReady first{&g};
    g.bench.on_cycle = FirstReady::hook;
    g.bench.on_cycle_ctx = &first;
    WindowOut o;
    CHECK(g.window(2 + 200 * 3 + 1, 100000, o));
    CHECK_EQ(o.sizes[2].values, 1u);
    CHECK_EQ(o.sizes[1].values, 400u);                       // 200 loads, 199 bases, the end's 0
    const int64_t end = g.tr.rows_end_at;
    // the log scan moves one of its 401 entries a cycle (17.9.7): the read log ends no earlier than that
    CHECK(g.tr.oq[2].ended_at >= end + 401);
    CHECK(first.at[7] >= end + 256);                         // read_log_ready: its first credit, 128 entries of two values
    // the write log's value is the log FIFO's first entry: its end, credit and _ready within a few cycles of the drain's start
    CHECK(g.tr.oq[3].ended_at <= end + 12);
    CHECK(first.at[8] > end && first.at[8] <= end + 12);
    CHECK_EQ(o.wlog.size(), size_t(2));
    CHECK_EQ(o.wlog[0], uint64_t(0));                        // the store's pre-value
}

// A10.9. After an error that stops the Tracer (17.9.9): no counted event is set, dma_done among them; the live events
// are down from the cycle after the error; counts already posted stay until consumed; later cycles still count in
// STAT_CYCLES; busy stays 1.
TEST(a10_9_after_a_stopping_error_no_event) {
    const Prog p = stores_then(128, {sd(2, 2, 0)});
    RigP gp = rig(p, {1}, 5);
    Rig& g = *gp;
    g.ms.fail_write_line = line_of(g, DATA);                 // the last row's write-back answers `error`, 900 cycles
    g.ms.fail_write_delay = 900;                             // after its request
    g.bench.store(Bench::B_CONTROL, Bench::C_EVENTS_MASK, 0x13);   // a consuming load leaves the credits
    write_record(g, p);
    g.word(Tracer::W_RUN_CONFIG, 1 + 128 * 4 + 1);
    PortLog port(g);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    uint32_t v = 0;
    for (int k = 0; k < 400000 && !(v & 0x20); k++) v = g.events_peek();
    CHECK_EQ(v & 0x20u, 0x20u);                              // the rows ended
    CHECK_EQ(port.writes(), 1);
    const int64_t error_at = port.write(0)->response_at;     // the cycle of the response, and of the error's pulse
    CHECK(error_at > g.t + 700);
    // before the stop: a transfer's dma_done is posted and consumed; the scans end; every _ready is up
    transfer(g, 0, 1);
    idle(g, 4);
    CHECK_EQ(g.events_peek() & 0x02u, 0x02u);
    CHECK_EQ(g.events() & 0x02u, 0x02u);
    idle_until(g, error_at - 30);
    CHECK_EQ(g.events_peek(), 0x1ECu);                       // the four _ready events and the two credits
    transfer(g, 0, 65);                                      // in progress at the error: it would end after it
    idle_until(g, error_at);
    CHECK_EQ(g.events_peek(), 0x1ECu);                       // the error's own cycle: still up
    CHECK_EQ(g.events_peek(), 0x00Cu);                       // the cycle after: the live events down, the counts stay
    CHECK_EQ(g.errors(), 0x200u);
    idle(g, 60);                                             // the transfer's 65 entries are sent meanwhile
    CHECK(!g.bench.dma_busy());
    CHECK_EQ(g.events_peek(), 0x00Cu);                       // no dma_done, no done
    CHECK_EQ(g.status(), 1u);                                // busy, the DMA idle
    const uint32_t c0 = g.load_word(44);
    idle(g, 10);
    CHECK_EQ(g.load_word(44), c0 + 11);   // the cycles after the stop count (17.9.8)
    CHECK_EQ(g.load_word(Tracer::W_ROWS_RUN), uint32_t(1 + 128 * 4 + 1));
    // the counts already posted stay until consumed
    g.bench.store(Bench::B_CONTROL, Bench::C_EVENTS_MASK, 0x1F);
    CHECK_EQ(g.events(), 0x00Cu);
    CHECK_EQ(g.events(), 0x00Cu);
    CHECK_EQ(g.events_peek(), 0u);
}

// A10.10. The reset (17.9.12): what it zeroes, what it keeps; then a segment runs as on a fresh Tracer.
TEST(a10_10_the_reset_zeroes_its_list_and_keeps_the_memories) {
    Prog p(CODE);
    p.w({CSR, addi(5, 0, 1), sd(5, 2, 0), ld(6, 2, 0), jal(0, 8), EBREAK, addi(7, 0, 9), END});
    RigP gp = rig(p, {6});
    Rig& g = *gp;
    start(g, p, {11, 12, 13});                               // three advice values, which no row takes
    WindowOut o;
    const int rows_n = g.expansion_length(CSR) + 6;
    CHECK(g.window(uint32_t(rows_n) | Tracer::RC_COMP_CODE, 10000, o));
    const int64_t first_row = g.last.first_row_start - g.last.start_store;
    uint32_t stat0[Tracer::STAT_N];
    for (int k = 0; k < Tracer::STAT_N; k++) stat0[k] = g.last.stat[k];
    // a state to reset: an error, a latched template and its bitmap bit, posted credits, a run in progress
    for (int k = 0; k < 257; k++) g.port_add(0x400000, 0, 0);   // the expansion queue overruns
    g.drain_port();
    g.word(Tracer::W_EXP_TEMPLATE, 0x80000000u | 5);
    g.step();
    CHECK_EQ(g.tr.rec[2 * Tracer::REC_BITMAP], uint64_t(1) << 5);
    g.word(Tracer::W_RUN_CONFIG, 40);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    idle(g, 9);
    CHECK_EQ(g.status(), 1u);
    CHECK_EQ(g.errors(), 0x001u);
    CHECK_EQ(g.load_word(Tracer::W_EXP_OPCODE), CSR);
    CHECK(g.load_word(Tracer::W_CODE_LENGTH) == 0 && stat0[Tracer::ST_CYCLES] != 0);
    uint64_t kept[2 * Tracer::REC_WORDS];
    for (int k = 0; k < 2 * Tracer::REC_WORDS; k++) kept[k] = g.tr.rec[k];
    CHECK(g.tr.cache_lines(false) > 0 && g.tr.cache_lines(true) > 0);

    g.bench.reset();
    CHECK_EQ(g.status(), 0u);                                // any run ended
    CHECK_EQ(g.events_peek(), 0u);                           // EVENTS, the credits among them
    CHECK_EQ(g.errors(), 0u);
    CHECK_EQ(g.bench.load(Bench::B_CONTROL, Bench::C_EVENTS_MASK), 0u);
    for (int w : {Tracer::W_RUN_CONFIG, Tracer::W_ROWS_RUN, Tracer::W_EXP_OFFSET, Tracer::W_EXP_OPCODE, Tracer::W_CODE_LENGTH,
                  Tracer::W_RLOG_LENGTH, Tracer::W_WLOG_LENGTH, Tracer::W_HASH_MUL, Tracer::W_HASH_STEP, Tracer::W_HASH_LINES})
        CHECK_EQ(g.load_word(w), 0u);
    for (int k = 0; k < Tracer::STAT_N; k++) CHECK_EQ(g.load_word(Tracer::W_STAT + k), 0u);
    CHECK_EQ(g.load_word(Tracer::W_STATE_LENGTH), 68u);      // a constant
    // the template bitmap zeroed; the state word, the stream word, the registers and the files word kept
    for (int k = 0; k < 2 * Tracer::REC_WORDS; k++)
        CHECK_EQ(g.tr.rec[k], k >= 2 * Tracer::REC_BITMAP ? uint64_t(0) : kept[k]);
    CHECK(kept[4 + 5] == 1 && kept[4 + 7] == 9 && kept[2 * Tracer::REC_FILES] != 0);
    // the decode table and the ROM kept
    for (int k = 0; k < 2048; k++) CHECK_EQ(g.tr.decode_table[k], g.tables.table[k]);
    for (int k = 0; k < 512; k++) CHECK(g.tr.rom_lo[k] == g.tables.rom_lo[k] && g.tr.rom_hi[k] == g.tables.rom_hi[k]);
    // every queue and both caches empty
    CHECK(g.tr.cache_lines(false) == 0 && g.tr.cache_lines(true) == 0);
    for (int q = 1; q <= 3; q++) CHECK_EQ(g.tr.oq[q].occ, 0);

    // the same segment again, after software has set up what the reset zeroed (the mask and the hash's constants): as
    // on a fresh Tracer, the caches and the jump LRU cold, the counters from 0, with no leftover template bit
    g.bench.store(Bench::B_CONTROL, Bench::C_EVENTS_MASK, Bench::EVENTS_COUNTED);
    g.word(Tracer::W_HASH_MUL, g.map_mul);
    g.word(Tracer::W_HASH_STEP, g.map_step);
    g.word(Tracer::W_HASH_LINES, uint32_t(g.map_k));
    const int64_t flags = g.tr.n_violations;
    start(g, p);
    CHECK_EQ(g.tr.advice_left(), 0u);                        // (the stream word kept the last segment's 3 until here)
    CHECK_EQ(g.tr.n_violations, flags);                      // a segment's start, not a rewrite within one: no flag
    CHECK(g.window(uint32_t(rows_n) | Tracer::RC_COMP_CODE, 10000, o));
    CHECK_EQ(g.reg(7), uint64_t(9));
    CHECK_EQ(g.last.first_row_start - g.last.start_store, first_row);
    for (int k = 0; k < Tracer::STAT_N; k++) CHECK_EQ(g.last.stat[k], stat0[k]);
    CHECK_EQ(g.last.stat[Tracer::ST_MISS_I], 1u);
    CHECK_EQ(g.last.stat[Tracer::ST_PROBES], 1u);
    CHECK_EQ(o.record[2 * Tracer::REC_BITMAP], uint64_t(0));
}

// A10.10. The statistics counters are zeroed by the state write (and the reset) alone (17.9.8): not by START, a
// part-0 write of another word, or a store to a word.
TEST(a10_10_the_state_write_alone_zeroes_the_counters) {
    Prog p(CODE);
    p.w({addi(5, 0, 1), ld(6, 2, 0), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    WindowOut o;
    CHECK(g.window(3, 10000, o));
    const uint32_t cycles = g.last.stat[Tracer::ST_CYCLES];
    CHECK(cycles > 0 && g.last.stat[Tracer::ST_MISS_I] == 1 && g.last.stat[Tracer::ST_MISS_D] == 1);
    g.port_add(16 * 5, 1, 2);                                // record word 5
    g.drain_port();
    g.word(Tracer::W_HASH_LINES, uint32_t(g.map_k));
    CHECK_EQ(g.load_word(44), cycles);
    CHECK(g.window(2, 10000, o));                            // START: they count on
    CHECK(g.last.stat[Tracer::ST_CYCLES] > cycles && g.last.stat[Tracer::ST_MISS_I] == 1);
    g.port_add(0, uint64_t(CODE), 1);                        // the state write
    g.drain_port();
    for (int k = 0; k < Tracer::STAT_N; k++) CHECK_EQ(g.load_word(Tracer::W_STAT + k), 0u);
}

// A10.10. START with no state write since the reset is UB (17.9.3).
DEATH_TEST(a10_10_start_with_no_state_write_is_ub, "no state write since the reset") {
    Prog p(CODE);
    p.w({addi(5, 0, 1), END});
    RigP gp = rig(p, {});
    gp->bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    idle(*gp, 2);
}
DEATH_TEST(a10_10_start_after_a_reset_with_no_state_write_is_ub, "no state write since the reset") {
    Prog p(CODE);
    p.w({addi(5, 0, 1), END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    WindowOut o;
    if (!g.window(2, 10000, o)) return;
    g.bench.reset();
    g.load_tables();
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    idle(g, 2);
}

// A consuming load of EVENTS in the cycle the Tracer posts the same counted event again leaves it set (the RTL's
// peripheral_control: one less, then one more; 5.4). Two core-fed instructions in a row: the first post is left
// unconsumed until the cycle of the second.
namespace {
// Runs to the second post; `consume_at` (or -1) is the cycle of a consuming load. Returns the second post's cycle and
// the EVENTS values peeked in the two cycles after it.
struct TwoPosts { int64_t second = -1; uint32_t after[2] = {0, 0}; uint32_t consumed = 0; };
TwoPosts two_posts(int64_t consume_at) {
    TwoPosts out;
    Prog p(CODE);
    p.w({CSR, CSR, END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    start(g, p);
    g.word(Tracer::W_RUN_CONFIG, 7);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    const int number = g.tables.lookup(CSR);
    const std::vector<uint64_t>& words = g.tables.words[size_t(number)];
    bool fed = false;
    for (int k = 0; k < 400 && out.second < 0; k++) {
        const int64_t n = g.t;
        const uint32_t v = n == consume_at ? g.events() : g.events_peek();
        if (n == consume_at) out.consumed = v;
        if ((v & 0x10) && !fed) {   // the first post: its words, rd0 in the first (17.8.13)
            for (size_t w = 0; 2 * w < words.size(); w++)
                g.port_add(0x400000, w == 0 ? (words[0] & ~(uint64_t(3) << 46)) | uint64_t(g.tables.rd0[size_t(number)]) << 46 : words[2 * w], words[2 * w + 1]);
            fed = true;
        }
        if (g.tr.new_expansions == 2) out.second = n;
    }
    for (uint32_t& v : out.after) {
        if (g.t == consume_at) v = out.consumed = g.events();
        else v = g.events_peek();
    }
    return out;
}
}  // namespace
TEST(a_consuming_load_in_the_cycle_of_a_post_leaves_the_event_set) {
    const TwoPosts plain = two_posts(-1);
    CHECK(plain.second > 0);
    CHECK_EQ(plain.after[0] & 0x10u, 0x10u);                 // unconsumed: set (the second post found it full)
    const TwoPosts met = two_posts(plain.second);            // the consuming load in the second post's own cycle
    CHECK_EQ(met.second, plain.second);
    CHECK_EQ(met.consumed & 0x10u, 0x10u);                   // it read, and took, the first post's count
    CHECK_EQ(met.after[0] & 0x10u, 0x10u);                   // one less, then one more: the second post's
    const TwoPosts early = two_posts(plain.second - 1);      // a cycle earlier: consumed, then posted again
    CHECK_EQ(early.after[0] & 0x10u, 0x10u);
    CHECK_EQ(early.after[1] & 0x10u, 0x10u);
    const TwoPosts late = two_posts(plain.second + 1);       // a cycle later: the load takes the one count there is
    CHECK_EQ(late.consumed & 0x10u, 0x10u);
    CHECK_EQ(late.after[1] & 0x10u, 0u);
}
