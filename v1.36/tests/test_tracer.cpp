// test_tracer.cpp: the Tracer's interface to its slot (tracer.h): the landing at t + 2, the settling rule, the static
// word map; its wake rule (`must_step`); and what a test of state compares, `observe()` and the canonical
// `fingerprint()`.
//
// v1.36: the Tracer is a model of the RTL's (Snarky One's rtl/blocks/tracer_unit), checked against the RTL itself
// (tracer_block's references, tests/tracerblock/) and against spec 17.9 (suite_tracer, test_tracer_rules). The replay
// of the Java reference Tracer's call logs that was here (tests/tracerconf/*.log.gz, the v1.35 hardware's, cycle for
// cycle) is retired with that hardware.
// Run from v1.36/.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "harness.h"
#include "statefp.h"
#include "tracer_rig.h"
#include "units/tracer.h"

using namespace tracer_rig;

namespace {

constexpr int64_t CODE = 0x80000000LL, DATA = 0x80001000LL;
constexpr uint32_t CSR = 0x305090f3;   // CSRRW x1, mtvec, x1: core-fed
using RigP = std::unique_ptr<Rig>;

// A guest with every kind of work: a core-fed CSR, a loop of stores and loads over 3 lines and its taken branches (a
// probe, then LRU hits), a DIV with its advice, the end and padding.
Prog guest() {
    Prog p(CODE);
    p.w({CSR, addi(1, 0, 3), sd(1, 2, 0), ld(3, 2, 0), addi(2, 2, 128), addi(1, 1, -1), bne(1, 0, -16),
         addi(5, 0, 100), addi(6, 0, 7), div(7, 5, 6), END});
    return p;
}
void build(Rig& g, const Prog& p) {
    Pages pages;
    pages.poke(CODE - g.lowest, p.bytes());
    pages.put(DATA - g.lowest, std::vector<uint64_t>(512, 0));
    g.build_image(pages, {DATA - g.lowest});
    if (p.parcels.size() > 2) g.build_map({{p.pc_of(2), g.bases(p)[2]}});   // the guests' one target, their third instruction
    else g.build_map({});
    g.load_tables();
}
void start(Rig& g, const Prog& p) {
    uint64_t regs[128] = {};
    regs[2] = uint64_t(DATA);
    g.load_record(CODE, int32_t(g.bases(p)[0]), 0, regs, {14});
}
uint64_t fp_of(const Tracer& tr, int unit, int64_t c) {
    fp::FpOut o;
    tr.fingerprint(unit, o, c);
    return o.done();
}

// Every cycle's state, as a list: observe()'s fields hashed, and the fingerprint's units 0-2.
struct Trace {
    Rig* g;
    bool fingerprints;
    std::vector<uint64_t> v;
    static void hook(void* ctx, int64_t n) {
        Trace& t = *static_cast<Trace*>(ctx);
        int64_t obs[Tracer::OBS_N];
        t.g->tr.observe(obs);
        uint64_t h = 17;
        for (int64_t x : obs) h = h * 31 + uint64_t(x);
        t.v.push_back(h);
        if (t.fingerprints)
            for (int u = 0; u < 3; u++) t.v.push_back(fp_of(t.g->tr, u, n));
    }
};
// The guest's whole life on a rig: the tables, a segment, three runs (the second cut inside the DIV, the third into
// padding), idle cycles between them, a new segment and a run of it, idle cycles after.
struct Life {
    std::vector<uint64_t> trace, out;
    int64_t cycles = 0, skipped = 0;
    bool ok = true;
};
// Something in flight while the Tracer is idle: a guest whose last row evicts a dirty line, whose write-back the
// memory system answers `error` 3,000 cycles after its request, long after the run's end (17.9.9, "while idle"). Only a
// step takes the response.
void idle_error(Life& l, bool model, bool lazy, bool fingerprints) {
    Prog q(CODE);
    q.w({addi(1, 0, 128), sd(1, 2, 0), addi(2, 2, 128), addi(1, 1, -1), bne(1, 0, -12), sd(2, 2, 0)});
    Rig h;
    if (model) h.fill(0, 0);
    h.bench.skip_idle_steps = lazy;
    Trace t{&h, fingerprints, {}};
    h.bench.on_cycle = Trace::hook;
    h.bench.on_cycle_ctx = &t;
    Pages pages;
    pages.poke(CODE - h.lowest, q.bytes());
    std::vector<int64_t> owned;
    for (int k = 0; k < 5; k++) {
        pages.put(DATA - h.lowest + 4096 * k, std::vector<uint64_t>(512, 0));
        owned.push_back(DATA - h.lowest + 4096 * k);
    }
    h.build_image(pages, owned);
    h.build_map({{q.pc_of(1), h.bases(q)[1]}});
    h.load_tables();
    uint64_t regs[128] = {};
    regs[2] = uint64_t(DATA);
    h.load_record(CODE, int32_t(h.bases(q)[0]), 0, regs, {});
    h.ms.fail_write_line = (DATA - h.lowest) / 128;
    h.ms.fail_write_delay = 3000;
    WindowOut o;
    l.ok = h.window(1 + 128 * 4 + 1, 400000, o) && l.ok && h.last.errors == 0 && h.last.status == 0;
    for (int k = 0; k < 3200; k++) h.step();
    l.ok = l.ok && h.errors() == 0x200 && h.tr.n_errors_logged == 1;   // the response came, the Tracer idle
    l.out.push_back(uint64_t(h.tr.error_log[0].t));
    l.out.push_back(uint64_t(h.t));
    for (int u = 0; u < Tracer::FP_UNITS; u++) l.out.push_back(fp_of(h.tr, u, h.t - 1));
    l.trace.insert(l.trace.end(), t.v.begin(), t.v.end());
    l.skipped += h.bench.steps_skipped;
}

Life live(bool model, bool lazy, bool fingerprints) {
    const Prog p = guest();
    Life l;
    Rig g;
    const int rows = g.expansion_length(CSR) + 1 + 3 * 5 + 2 + g.expansion_length(div(7, 5, 6)) + 1;
    if (model) g.fill(0, 0);
    g.bench.skip_idle_steps = lazy;
    Trace t{&g, fingerprints, {}};
    g.bench.on_cycle = Trace::hook;
    g.bench.on_cycle_ctx = &t;
    build(g, p);
    for (int k = 0; k < 40; k++) g.step();
    start(g, p);
    const uint32_t runs[3] = {uint32_t(rows - 6), 3, 9};
    for (uint32_t r : runs) {
        WindowOut o;
        l.ok = g.window(r | Tracer::RC_COMP_CODE, 200000, o) && l.ok;
        for (const std::vector<uint64_t>* s : {&o.record, &o.code, &o.rlog, &o.wlog}) l.out.insert(l.out.end(), s->begin(), s->end());
        l.out.push_back(uint64_t(g.last.done_load - g.last.start_store));
        for (int k = 0; k < 25; k++) g.step();
    }
    l.ok = l.ok && g.reg(7) == 14 && g.tr.rec[0] == 0;   // the guest ran to its end
    start(g, p);
    WindowOut o;
    l.ok = g.window(uint32_t(rows), 200000, o) && l.ok;
    l.out.insert(l.out.end(), o.record.begin(), o.record.end());
    for (int k = 0; k < 300; k++) g.step();
    for (int u = 0; u < Tracer::FP_UNITS; u++) l.out.push_back(fp_of(g.tr, u, g.t - 1));
    l.trace = std::move(t.v);
    l.cycles = g.t;
    l.skipped = g.bench.steps_skipped;
    idle_error(l, model, lazy, fingerprints);
    return l;
}

}  // namespace

// Two Tracers given the same calls are in the same state at the end of every cycle: observe() and the fingerprint's
// units agree cycle by cycle, with the RTL tests' memory system and with the simulator's model.
TEST(the_same_calls_give_equal_fingerprints_every_cycle) {
    for (bool model : {false, true}) {
        const Life a = live(model, false, true), b = live(model, false, true);
        CHECK(a.ok && b.ok);
        CHECK(a.cycles > 1500);
        CHECK_EQ(a.cycles, b.cycles);
        CHECK(a.trace == b.trace);
        CHECK(a.out == b.out);
    }
}

// The wake rule (tracer.h, must_step): a Tracer stepped only in the cycles it asks for is, at the end of every cycle,
// the Tracer stepped in all of them: observe() at every cycle, the output, the runs' cycles, the final fingerprints.
TEST(a_tracer_stepped_only_on_its_wake_rule_is_the_same) {
    for (bool model : {false, true}) {
        const Life every = live(model, false, false), lazy = live(model, true, false);
        CHECK(every.ok && lazy.ok);
        CHECK_EQ(every.skipped, int64_t(0));
        CHECK(lazy.skipped > 300);           // the idle cycles between the runs and after the last
        CHECK_EQ(every.cycles, lazy.cycles);
        CHECK(every.out == lazy.out);
        CHECK(every.trace == lazy.trace);
    }
}

// The fingerprint moves when a register its stream holds moves (tracer.h, fingerprint: the registers, the record, the
// tables, the input queues, the run's state, the latch and the bitmap, the events' counts, the statistics; the caches
// in units 1 and 2).
TEST(the_fingerprint_moves_with_each_register) {
    Prog p(CODE);   // no core-fed instruction: nothing here answers a post
    p.w({addi(1, 0, 3), addi(1, 1, 1), sd(1, 2, 0), ld(3, 2, 0), jal(0, -8), END});
    Rig g;
    build(g, p);
    start(g, p);
    uint64_t last = fp_of(g.tr, 0, g.t - 1);
    int moved = 0;
    const auto moves = [&](const char* what) {
        const uint64_t now = fp_of(g.tr, 0, g.t - 1);
        if (now == last) { ::test::fail(__FILE__, __LINE__, "the fingerprint did not move with %s", what); return false; }
        last = now;
        moved++;
        return true;
    };
    g.word(Tracer::W_RUN_CONFIG, 5);                 g.step(); if (!moves("RUN_CONFIG")) return;
    g.word(Tracer::W_HASH_MUL, 12345);               g.step(); if (!moves("HASH_MUL")) return;
    g.word(Tracer::W_HASH_STEP, 77);                 g.step(); if (!moves("HASH_STEP")) return;
    g.word(Tracer::W_HASH_LINES, 3);                 g.step(); if (!moves("HASH_LINES")) return;
    g.word(Tracer::W_EXP_TEMPLATE, 0x80000000u | 9); g.step(); if (!moves("the template latch and bitmap")) return;
    g.port_add(16 * 7, 0x1234, 0x5678);              g.drain_port(); if (!moves("a record word")) return;
    g.port_add(0x1000 + 16 * 3, 1, 2);               g.drain_port(); if (!moves("a decode-table row")) return;
    g.port_add(0x2000 + 16 * 400, 3, 4);             g.drain_port(); if (!moves("a ROM word")) return;
    g.port_add(0x200000, 5, 6);                      g.drain_port(); if (!moves("a read-queue entry")) return;
    g.port_add(0x400000, 7, 8);                      g.drain_port(); if (!moves("an expansion-queue entry")) return;
    g.load_tables();                                 // the tables and the constants back
    if (!moves("the tables")) return;
    start(g, p);                                     // a state write: the queues emptied, the bitmap cleared
    if (!moves("the state write")) return;
    const uint64_t caches[2] = {fp_of(g.tr, 1, g.t - 1), fp_of(g.tr, 2, g.t - 1)};
    g.word(Tracer::W_RUN_CONFIG, 40);
    g.bench.store(Bench::B_CONTROL, Bench::C_START, 1);
    g.step();
    if (!moves("START")) return;
    uint64_t before = last;
    int cycles_moved = 0;
    for (int k = 0; k < 60; k++) {                   // the run: its pipeline's state, the statistics, each cycle
        g.step();
        const uint64_t now = fp_of(g.tr, 0, g.t - 1);
        cycles_moved += now != before;
        before = now;
    }
    CHECK_EQ(cycles_moved, 60);                      // STAT_CYCLES alone moves it every cycle of a run
    CHECK(fp_of(g.tr, 1, g.t - 1) != caches[0]);     // the instruction cache took a line
    CHECK(fp_of(g.tr, 2, g.t - 1) != caches[1]);     // and the data cache
    CHECK_EQ(moved, 13);
}

// observe()'s fields (tracer.h), on a finished run: where a conformance test reads the control's state, the
// statistics words and the events.
TEST(observe_reports_the_state_it_names) {
    const Prog p = guest();
    Rig g;
    build(g, p);
    start(g, p);
    WindowOut o;
    const int rows = 40;
    CHECK(g.window(uint32_t(rows), 100000, o));
    int64_t obs[Tracer::OBS_N];
    g.tr.observe(obs);
    CHECK_EQ(Tracer::OBS_N, 16 + 10 + 8 + 2 + 6 + 2 + 1 + 9);
    CHECK_EQ(obs[0], int64_t(0));                    // errors
    CHECK_EQ(obs[1], int64_t(0));                    // running
    CHECK_EQ(obs[2], int64_t(0));                    // stopped
    CHECK_EQ(obs[3], int64_t(rows));                 // ROWS_RUN
    CHECK_EQ(obs[4], int64_t(0));                    // the rows left
    CHECK_EQ(obs[14], int64_t(Tracer::REC_WORDS));   // output queue 0's pointer, past word 67
    CHECK_EQ(obs[16], int64_t(o.sizes[0].values));   // the code FIFO's entries
    CHECK_EQ(obs[18], int64_t(rows));                // rows
    for (int k = 0; k < Tracer::STAT_N; k++) CHECK_EQ(obs[26 + k], int64_t(g.last.stat[k]));
    CHECK_EQ(obs[26 + Tracer::STAT_N], g.tr.stalls[Tracer::S_ADVICE]);
    CHECK_EQ(obs[26 + Tracer::STAT_N + 1], g.tr.stalls[Tracer::S_EXP]);
    for (int b = 0; b < 9; b++) CHECK_EQ(obs[Tracer::OBS_N - 9 + b], int64_t(0));   // every event consumed
    // an error shows in field 0, the stop in field 2
    Prog bad(CODE);
    bad.w({jalr(0, 0, 0), END});
    Rig h;
    build(h, bad);
    start(h, bad);
    CHECK(!h.window(2, 10000, o));
    h.tr.observe(obs);
    CHECK_EQ(obs[0], int64_t(1 << 3));
    CHECK_EQ(obs[1], int64_t(1));
    CHECK_EQ(obs[2], int64_t(1));
}

// The slot's timing (the peripheral.h): accept(t) is called at level 0 of t + 3, possibly after the Tracer was settled
// through t + 2; the packet still lands at the end of t + 2. Settled through t + 3, it is a simulator bug.
TEST(late_accept_lands_at_t_plus_2) {
    Tracer early(66), late(66);
    const uint64_t lo = 0x1234, hi = 0x5678;
    early.accept(10, 74, 0x50, 2, lo, hi);  // record word 5 (x6's and x7's image)
    late.sync(12);                          // settled through 12: inputs before 12 applied
    late.accept(10, 74, 0x50, 2, lo, hi);   // the slot's call at level 0 of 13
    for (Tracer* t : {&early, &late}) {
        t->sync(12);
        CHECK_EQ(t->rec[10], uint64_t(0));  // not yet: it lands at the end of 12
        t->sync(13);
        CHECK_EQ(t->rec[10], lo);
        CHECK_EQ(t->rec[11], hi);
    }
    CHECK_EQ(late.violation_count(), uint64_t(0));
    CHECK_EQ(Tracer::DIGEST_N, 8);
}

DEATH_TEST(accept_after_its_landing_was_settled_is_fatal, "was settled") {
    Tracer tr(66);
    tr.sync(13);
    tr.accept(10, 74, 0x50, 2, 1, 2);
}

// writable(w) (the static map) agrees with write_word on every own word 32-127: 1 exactly where writable.
TEST(writable_agrees_with_write_word) {
    Tracer tr(66);
    unsigned n = 0;
    for (int w = 32; w < 128; w++) {
        const int r = tr.write_word(0, w, 0);
        CHECK_EQ(r == 1, tr.writable(w));
        n += tr.writable(w);
    }
    CHECK_EQ(n, 5u);   // RUN_CONFIG, HASH_MUL, HASH_STEP, HASH_LINES, EXP_TEMPLATE
}
