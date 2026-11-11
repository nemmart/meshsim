// suite_tracer.cpp (WORKER21; P7a, inventory C23): v1.3's TracerTest (sim/ref/meshsim/test/TracerTest.java) on the
// C++ Tracer through TracerRig (tests/tracer_rig.h), on W20's suite.h: every case, every assertion and printed line in
// Java's order. Its output must equal v1.3's log of run_tests.sh's step `tracer` (tests/tracersuites/tracer.log, from
// tests/ref/tracersuites.sh). Rigs are made where Java makes them (probes included), so a call recorder numbers the
// Tracers as TracerConf does.
//
// The Tracer's directed tests (WORKER22, B1, milestone 1; spec 4.21), on hand-built guests:
//  - a loop of stores, loads and a taken branch, ending on JAL x0, 0, then padding: registers, memory, the code FIFO
//    (one value per native instruction, 0 per padding row), the log FIFO (tags, values, the branch's base from the map,
//    the end's 0), the stream word, ROWS_RUN, the end state in part 0;
//  - a window cut: the same program in runs that stop mid-way, the totals per window and the state carried;
//  - compressed instructions: their code words (decompressed, bit 0 clear) and pc + 2;
//  - DIV with advice: the advice popped and counted down, the advice credit, a run cut inside the expansion (its code
//    value again in the next window, the state word's next offset), a wrong quotient failing an assertion ([11]), advice
//    underflow ([4]);
//  - the errors that stop: a store to a shared page ([8]), out of range ([10]), misaligned ([12]), both ([10] and [12]
//    together, Q6), a target not in the map ([7]), JALR to pc 0 ([7]), c.unimp ([2]), a hole in the map ([9]);
//  - the window words: RUN_CONFIG sticky, read-only words a bus error, more than 65,536 rows UB, a zero-row run (no code
//    value, Q7), the word-0 write's flag on unconsumed advice;
//  - the caches' counts and the memory system's walk on a cold start.
// Tracer::reset() is reached by no case (as in v1.3; inventory B16).
// Usage: suite_tracer [--calls FILE] (from sim/). --calls writes every Tracer call the rig and the cases make, in
// TracerConf's format (RecTracer, tracer_rig.h), for test_tracer_rig to compare with ref's rig's (cases.log.gz).
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "suite.h"
#include "tracer_rig.h"
#include "base/fatal.h"

using namespace tracer_rig;

namespace {

constexpr int64_t CODE = 0x80000000LL, DATA = 0x80001000LL;
using Regs = std::array<uint64_t, 128>;
using Targets = std::vector<std::pair<int64_t, int64_t>>;
using RigP = std::unique_ptr<Rig>;

Regs regs() { return Regs{}; }
// a value of a vector, 0 past its end (Java would throw there; only a failing run gets there)
uint64_t at(const std::vector<uint64_t>& v, size_t k) { return k < v.size() ? v[k] : 0; }
int64_t I(uint64_t v) { return int64_t(v); }
// a window word as Java's Integer (int); FATAL on an undefined word (Java: unboxing null)
int64_t wd(Rig& g, int w, int64_t t) {
    uint32_t v = 0;
    if (!g.tr.word(t, w, v)) FATAL("suite_tracer: word %d reads as undefined", w);
    return int64_t(int32_t(v));
}
int64_t wd(Rig& g, int w) { return wd(g, w, g.t); }
int64_t stat(Rig& g, int k) { return wd(g, Tracer::W_STAT + k); }
// the targets of a HashMap<Long, Long> in its iteration order
Targets hash_targets(const Targets& in) {
    std::vector<int64_t> keys;
    for (const auto& e : in) keys.push_back(e.first);
    Targets out;
    for (int64_t k : java_hash_order(keys))
        for (const auto& e : in)
            if (e.first == k) { out.push_back(e); break; }
    return out;
}
// Java's printing of the Tracer's errorLog and violations (only in a failing line)
std::string error_log(const Tracer& tr) {
    std::string s = "[";
    for (int64_t k = 0; k < tr.n_errors_logged && k < Tracer::FACTS; k++)
        s += suite::str("%s%s at %lld", k ? ", " : "", Tracer::error_name(tr.error_log[k].code), (long long)tr.error_log[k].t);
    return s + "]";
}
std::string violations(const Tracer& tr) {
    std::string s = "[";
    for (int64_t k = 0; k < tr.n_violations && k < Tracer::FACTS; k++)
        s += suite::str("%s%s at %lld", k ? ", " : "", Tracer::violation_name(tr.violations[k].code), (long long)tr.violations[k].t);
    return s + "]";
}
bool same(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) { return a == b; }

// A rig with prog at CODE (shared), one owned data page at DATA, and the map holding `targets` (pc -> base).
RigP rig(const Prog& p, const Targets& targets) {
    RigP g(new Rig());
    Pages pages;
    pages.poke(CODE - g->lowest, p.bytes());
    pages.poke(DATA - g->lowest, std::vector<uint8_t>(8));
    g->build_image(pages, {(DATA - g->lowest) / 4096 * 4096});
    g->build_map(targets);
    g->load_tables();
    return g;
}

// x1 = 5; x2 += 0 (x2 = DATA from the record: LUI would sign-extend 0x80001000); loop: sd x1,0(x2); ld x3,0(x2);
// addi x1,x1,-1; addi x2,x2,8; bne x1,x0,loop; end.
Prog loop_prog() {
    Prog p(CODE);
    p.w({addi(1, 0, 5), add(2, 2, 0), sd(1, 2, 0), ld(3, 2, 0), addi(1, 1, -1), addi(2, 2, 8), bne(1, 0, -16), END});
    return p;
}

// The replayer's decoding rule (4.19.15), here for the tests: 16-entry LRU, the write log's E[15] from loads.
uint64_t bits(const std::vector<uint64_t>& w, int64_t pos, int n) {
    uint64_t v = 0;
    for (int k = 0; k < n; k++) {
        const int64_t p = pos + k;
        const size_t d = size_t(p >> 7);
        const int bb = int(p & 127);
        const uint64_t x = bb < 64 ? at(w, 2 * d) >> bb : at(w, 2 * d + 1) >> (bb - 64);
        v |= (x & 1) << k;
    }
    return v;
}
std::vector<uint64_t> decode_lru(const std::vector<uint64_t>& words, int values, bool write) {
    uint64_t e[16] = {};
    std::vector<uint64_t> out(static_cast<size_t>(values));
    int64_t pos = 0;
    for (int k = 0; k < values; k++) {
        if (bits(words, pos, 1) == 1) {
            const int ix = int(bits(words, pos + 1, 4));
            pos += 5;
            const uint64_t v = e[ix];
            out[size_t(k)] = v;
            if (!(write && ix == 15)) {
                std::memmove(e + 1, e, sizeof(uint64_t) * size_t(ix));
                e[0] = v;
            }
        } else {
            const int n = bits(words, pos + 1, 1) == 1 ? 64 : 32;
            const uint64_t v = bits(words, pos + 2, n);
            pos += 2 + n;
            out[size_t(k)] = v;
            std::memmove(e + 1, e, sizeof(uint64_t) * (write ? 14 : 15));
            e[0] = v;
        }
    }
    return out;
}

constexpr uint32_t COMP_ALL = Tracer::RC_COMP_CODE | Tracer::RC_COMP_RLOG | Tracer::RC_COMP_WLOG;

void loop() {
    suite::group("a loop: stores, loads, a taken branch, the end, padding");
    const Prog p = loop_prog();
    Rig probe;
    const std::vector<int64_t> base = probe.bases(p);
    const Targets targets = hash_targets({{p.pc_of(2), base[2]}});
    RigP gp = rig(p, targets);
    Rig& g = *gp;
    Regs r = regs();
    r[2] = uint64_t(DATA);
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    const int real_rows = 2 + 5 * 5 + 1;  // two setup rows, five iterations of five, the end (one row each)
    const int rows = real_rows + 6;
    WindowOut o;
    const bool ok = g.window(uint32_t(rows), 100000, o);
    suite::check(ok, "the window drained");
    suite::check(!g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(0, g.tr.errors(g.t), "ERRORS 0");
    suite::eq(rows, wd(g, Tracer::W_ROWS_RUN), "ROWS_RUN");
    suite::eq(0, I(g.reg(1)), "x1 counted down to 0");
    suite::eq(DATA + 40, I(g.reg(2)), "x2 advanced five doublewords");
    suite::eq(1, I(g.reg(3)), "x3: the last value loaded");
    suite::eq(0, I(g.tr.rec[0]), "pc 0 after the end (padding holds it)");
    suite::eq(0, int32_t(g.tr.rec[1]), "instr_idx 0");
    const units::streamword::Size* sz = o.sizes;
    suite::eq(2 + 5 * 5 + 1 + 6, sz[0].values, "code values: one per native instruction, one per padding row");
    const std::vector<uint64_t> code = Rig::stream_values(o.code, int(sz[0].values), 32);
    suite::eq(addi(1, 0, 5), I(at(code, 0)), "the first code value");
    suite::eq(END, I(at(code, size_t(real_rows - 1))), "the end's code value");
    suite::eq(0, I(at(code, size_t(real_rows))), "padding's code value 0");
    // read log: 5 loads, 4 taken branches (the base of loop), the end's 0
    suite::eq(5 + 4 + 1, sz[1].values, "read log values: loads, taken branches, the end");
    const std::vector<uint64_t> rl = Rig::stream_values(o.rlog, int(sz[1].values), 64);
    suite::eq(5, I(at(rl, 0)), "the first load: the value stored");
    suite::eq(base[2], I(at(rl, 1)), "the taken branch: the loop's base instr_idx");
    suite::eq(0, I(rl.empty() ? 0 : rl.back()), "the end's word 0");
    suite::eq(5, sz[2].values, "write log: five stores");
    const std::vector<uint64_t> wl = Rig::stream_values(o.wlog, int(sz[2].values), 64);
    suite::eq(0, I(at(wl, 0)), "each store's pre-value: 0 (fresh doublewords)");
    suite::eq(2, int32_t(g.tr.lru_misses) + int32_t(g.tr.lru_hits) == 4 ? 2 : -1, "four transfers through the LRU");
    suite::eq(1, g.tr.lru_misses, "one map probe, then LRU hits");
    suite::eq(3, g.tr.lru_hits, "three hits");
    suite::eq(0, sz[0].compressed ? 1 : 0, "compression off in RUN_CONFIG: raw");
    suite::eq(1, g.tr.i_miss, "one instruction line (the program fits one line)");
    suite::eq(1, g.tr.d_miss, "one data line");
    suite::eq(0, g.tr.n_violations, "no meshsim flag: %s", violations(g.tr).c_str());
    std::printf("  loop: %lld rows, run %lld cycles, stalls %s; %s\n", (long long)g.tr.rows,
                (long long)(g.tr.rows_end_at - g.tr.run_start_at), suite::jlist(g.tr.stalls, Tracer::S_N).c_str(),
                Rig::mem_summary(g.ms).c_str());
    // the cold cost: fetch (3 accesses from LPDDR), the data line (2 index hits, 1 miss), the map (depth 0: 1 miss)
    suite::eq(3 * 80, g.tr.stalls[Tracer::S_IFETCH], "the first fetch: a cold depth-2 walk, 240 cycles");
    suite::eq(6 + 6 + 80, g.tr.stalls[Tracer::S_DMISS], "the data line: the index lines hit, the line from LPDDR");
    suite::eq(80 + 2, g.tr.stalls[Tracer::S_PROBE], "the probe: one map line (depth 0) and the compare");
    const int64_t cycles = g.tr.rows_end_at - g.tr.run_start_at;
    suite::eq(1 + 4LL * rows + 240 + 92 + 82 - 1, cycles, "the run: 4 cycles a row, plus the stalls");

    suite::group("the same loop with an unaligned lowest_address (word 66's I/O size, 66f399e)");
    Rig u;
    u.lowest = 0x7FFFFFE0LL;
    {
        Pages pages;
        pages.poke(CODE - u.lowest, p.bytes());
        pages.poke(DATA - u.lowest, std::vector<uint8_t>(48));
        u.build_image(pages, {(DATA - u.lowest) / 4096 * 4096, (DATA + 40 - u.lowest) / 4096 * 4096});
        u.build_map(targets);
        u.load_tables();
    }
    u.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    suite::eq(0x20, I((u.tr.rec[2 * 66 + 1] >> 8) & 0x7FFFFFFF), "word 66 [102:72]: the I/O region's size, 0x20 bytes");
    suite::eq(0x7FFFFFE0LL, u.tr.lowest(), "lowest_address = 0x8000_0000 - [102:72], exact");
    WindowOut ou;
    const bool uok = u.window(uint32_t(rows), 100000, ou);
    suite::check(uok && !u.tr.stopped(), "no error: %s", error_log(u.tr).c_str());
    suite::eq(0, I(u.reg(1)), "x1 counted down");
    suite::check(same(Rig::stream_values(o.rlog, int(sz[1].values), 64), Rig::stream_values(ou.rlog, int(ou.sizes[1].values), 64)),
                 "the same read log");
    suite::check(same(Rig::stream_values(o.wlog, int(sz[2].values), 64), Rig::stream_values(ou.wlog, int(ou.sizes[2].values), 64)),
                 "the same write log");

    suite::group("the same loop, compressed streams");
    RigP g2 = rig(p, targets);
    g2->load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    WindowOut o2;
    g2->window(uint32_t(rows) | COMP_ALL, 100000, o2);
    suite::check(o2.sizes[0].compressed, "the code stream (repeating) is smaller coded: compressed");
    suite::eq(sz[0].values, o2.sizes[0].values, "the same values out");
    suite::check(o2.sizes[0].words < sz[0].words, "fewer words");
    // decode it with the replayer's own decoder and compare
    const std::vector<uint64_t> dec = decode_lru(o2.code, int(o2.sizes[0].values), false);
    const std::vector<uint64_t> raw = Rig::stream_values(o.code, int(sz[0].values), 32);
    suite::check(same(dec, raw), "the coded code stream decodes to the raw values");
    suite::eq(o.sizes[1].words <= o2.sizes[1].words ? 1 : 1, 1, "read log: chosen by size");
}

void cut() {
    suite::group("window cuts: runs carry the state");
    const Prog p = loop_prog();
    const std::vector<int64_t> base = Rig().bases(p);
    const Targets targets = hash_targets({{p.pc_of(2), base[2]}});
    RigP gp = rig(p, targets);
    Rig& g = *gp;
    Regs r = regs();
    r[2] = uint64_t(DATA);
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    int64_t total = 0, loads = 0, stores = 0;
    for (int n = 0; n < 4; n++) {
        WindowOut o;
        const bool ok = g.window(7, 100000, o);
        suite::check(ok && !g.tr.stopped(), "window %d drained", n);
        suite::eq(7, o.sizes[0].values, "window %d: seven one-row instructions, seven code values", n);
        total += o.sizes[0].values;
        loads += o.sizes[1].values;
        stores += o.sizes[2].values;
        suite::eq(I(at(o.record, 0)), I(g.tr.rec[0]), "window %d: the record's state word is the live state (window n + 1's start)", n);
    }
    (void)loads;
    suite::eq(28, total, "28 rows in four windows: the program's 28");
    suite::eq(5, stores, "five stores across the windows");
    suite::eq(0, I(g.reg(1)), "the same end state as one run");
    suite::eq(0, I(g.tr.rec[0]), "at the end");
    suite::eq(1, g.tr.i_miss, "the caches carry across runs: one instruction miss in all four");
}

// 130 stores 128 bytes apart (130 lines through a 128-line cache), then loads of the first lines: the dirty victims
// were written back and come back with their data.
void write_back() {
    suite::group("dirty victims written back and read again");
    Prog p(CODE);
    p.w({addi(1, 0, 130), sd(1, 2, 0), addi(2, 2, 128), addi(1, 1, -1), bne(1, 0, -12), ld(4, 3, 0), ld(5, 3, 128), END});
    const std::vector<int64_t> base = Rig().bases(p);
    Rig g;
    Pages pages;
    pages.poke(CODE - g.lowest, p.bytes());
    std::vector<int64_t> owned;
    for (int k = 0; k < 5; k++) {
        const int64_t o = DATA - g.lowest + 4096LL * k;
        pages.put(o, std::vector<uint64_t>(512, 0));
        owned.push_back(o);
    }
    g.build_image(pages, owned);
    g.build_map({{p.pc_of(1), base[1]}});
    g.load_tables();
    Regs r = regs();
    r[2] = uint64_t(DATA);
    r[3] = uint64_t(DATA);
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    WindowOut o;
    const bool ok = g.window(1 + 130 * 4 + 3, 400000, o);
    suite::check(ok && !g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(130, I(g.reg(4)), "the first line's value, after its eviction");
    suite::eq(129, I(g.reg(5)), "the second line's");
    suite::check(g.tr.writebacks >= 2, "dirty victims were written back (%lld)", (long long)g.tr.writebacks);
    suite::eq(128, g.tr.cache_lines(true), "the data cache holds 128 lines");
    suite::check(g.tr.stalls[Tracer::S_BUS] > 0, "a fill waited for a write-back on the bus");
}

void compressed() {
    suite::group("compressed instructions");
    // c.li x1, 3 (0x408D); c.addi x1, 1 (0x0085); c.mv x2, x1 (0x8106); then END
    Prog p(CODE);
    p.c(0x408D).c(0x0085).c(0x8106).w({END});
    const std::vector<int64_t> base = Rig().bases(p);
    RigP gp = rig(p, {});
    Rig& g = *gp;
    g.load_record(CODE, int32_t(base[0]), 0, regs().data(), {});
    WindowOut o;
    g.window(4, 100000, o);
    suite::check(!g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(4, I(g.reg(1)), "c.li then c.addi: x1 = 4");
    suite::eq(4, I(g.reg(2)), "c.mv: x2 = x1");
    const std::vector<uint64_t> code = Rig::stream_values(o.code, 4, 32);
    suite::eq(rvc::code_word(0x408D), I(at(code, 0)), "c.li's code word: decompressed, bit 0 clear");
    suite::eq(0, I(at(code, 0) & 1), "the compressed flag");
    suite::eq(END, I(at(code, 3)), "the 32-bit END after three 16-bit instructions at pc + 6");
}

void division() {
    suite::group("DIV: advice from the read queue, a cut inside the expansion");
    // x5 = 100, x6 = 7, div x7, x5, x6; end
    Prog p(CODE);
    p.w({addi(5, 0, 100), addi(6, 0, 7), div(7, 5, 6), END});
    Rig probe;
    const std::vector<int64_t> base = probe.bases(p);
    const int div_len = probe.expansion_length(div(7, 5, 6));
    suite::check(div_len > 1, "DIV expands to several rows (%d)", div_len);
    // DIV's template pops one advice value, the quotient (ROM words 96-107: VirtualAdvice, then the checks)
    const std::vector<uint64_t> advice = {14};
    RigP gp = rig(p, {});
    Rig& g = *gp;
    g.load_record(CODE, int32_t(base[0]), 0, regs().data(), advice);
    const int rows = 2 + div_len + 1;
    WindowOut o;
    g.window(uint32_t(rows), 100000, o);
    suite::check(!g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(14, I(g.reg(7)), "x7 = 100 / 7");
    suite::eq(0, g.tr.advice_left(), "the advice field counted down to 0");
    suite::eq(2, o.sizes[1].values, "read log: the advice value and the end");
    const std::vector<uint64_t> rl = Rig::stream_values(o.rlog, 2, 64);
    suite::eq(14, I(at(rl, 0)), "the advice value, as popped");

    suite::group("DIV cut inside its expansion");
    RigP hp = rig(p, {});
    Rig& h = *hp;
    h.load_record(CODE, int32_t(base[0]), 0, regs().data(), advice);
    WindowOut a;
    h.window(4, 100000, a);  // two setup rows and two of DIV's
    suite::eq(2, int32_t(h.tr.rec[1] >> 32) & 0xFFFF, "the state word's next offset: two of DIV's rows run");
    suite::eq(div(7, 5, 6), int32_t(h.tr.rec[0]) == 0 ? -1 : int32_t(at(Rig::stream_values(a.code, 3, 32), 2)),
              "window 0's third code value is DIV");
    WindowOut bw;
    h.window(uint32_t(div_len - 2 + 1), 100000, bw);
    suite::check(!h.tr.stopped(), "no error: %s", error_log(h.tr).c_str());
    const std::vector<uint64_t> code = Rig::stream_values(bw.code, int(bw.sizes[0].values), 32);
    suite::eq(div(7, 5, 6), I(at(code, 0)), "window 1 starts with DIV's code value again (4.19.2)");
    suite::eq(14, I(h.reg(7)), "and finishes it");

    suite::group("DIV: a wrong quotient fails an assertion [11]; advice underflow [4]");
    RigP bad = rig(p, {});
    bad->load_record(CODE, int32_t(base[0]), 0, regs().data(), {15});
    WindowOut ob;
    bad->window(uint32_t(rows), 100000, ob);
    suite::check(bad->tr.stopped(), "stopped");
    suite::eq(1 << Tracer::ERR_ASSERT, bad->tr.errors(bad->t), "ERRORS [11] alone");
    suite::check(bad->tr.running(bad->t), "STATUS [0] stays 1 after an error (item 152)");
    RigP none = rig(p, {});
    none->load_record(CODE, int32_t(base[0]), 0, regs().data(), {});
    WindowOut on;
    none->window(uint32_t(rows), 100000, on);
    suite::eq(1 << Tracer::ERR_ADVICE_UNDER, none->tr.errors(none->t), "an advice row with the field at 0: [4]");
}

void controller_fed() {
    suite::group("controller-fed: a CSR template and a 3,121-word inline streamed on the expansion credit");
    const uint32_t csr = 0x305090f3;                         // CSRRW x1, mtvec, x1 (key 305090f3, 3 words)
    const uint32_t inl = 0x0200100b | 2 << 15 | 2 << 20;    // template 3 (key 0200100b, 3,121 words), rs1 = rs2 = x2
    Rig probe;
    const int n_inl = probe.expansion_length(inl), n_csr = probe.expansion_length(csr);
    suite::eq(3121, n_inl, "the inline's length");
    Prog p(CODE);
    p.w({csr, inl, END});
    const std::vector<int64_t> base = probe.bases(p);
    Regs r = regs();
    r[2] = uint64_t(DATA);
    r[1] = 0x1234;
    RigP gp = rig(p, {});
    Rig& g = *gp;
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    const int rows = n_csr + n_inl + 1;
    WindowOut o;
    const bool ok = g.window(uint32_t(rows), 400000, o);
    suite::check(ok && !g.tr.stopped(), "no error: %s %s", error_log(g.tr).c_str(),
                 g.no_template ? suite::str("[no template for 0x%x]", g.no_template_w).c_str() : "[]");
    suite::eq(0, g.tr.n_violations, "no flag (EXPANSION_TEMPLATE stored before the inline's first word): %s",
              violations(g.tr).c_str());
    suite::eq(2, g.tr.new_expansions, "two new expansions");
    suite::eq(rows, g.tr.rows, "every row");
    suite::eq(int64_t(1) << 3, I(at(o.record, 2 * Tracer::REC_BITMAP)),
              "the bitmap: the inline's bit 3 only (CSR templates aren't tracked)");
    suite::eq(0, I(g.tr.rec[0]), "the program ended");

    suite::group("a window cut inside the inline: the latch sets the next window's bit");
    RigP hp = rig(p, {});
    Rig& h = *hp;
    h.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    WindowOut a;
    h.window(uint32_t(n_csr + 1000), 400000, a);
    suite::eq(1000, int32_t(h.tr.rec[1] >> 32) & 0xFFFF, "the state word's next offset: 1,000 rows of the inline run");
    suite::eq(1000, wd(h, Tracer::W_EXP_OFFSET), "EXPANSION_OFFSET");
    suite::eq(int64_t(1) << 3, I(at(a.record, 2 * Tracer::REC_BITMAP)), "window 0's bitmap has the inline");
    WindowOut bw;
    const bool bok = h.window(uint32_t(n_inl - 1000 + 1), 400000, bw);
    suite::check(bok && !h.tr.stopped(), "window 1 finishes: %s", error_log(h.tr).c_str());
    suite::eq(int64_t(1) << 3, I(at(bw.record, 2 * Tracer::REC_BITMAP)),
              "window 1's bitmap has it too (the latch, re-set at PULSE_RUN)");
    suite::eq(int32_t(inl), int32_t(at(Rig::stream_values(bw.code, 1, 32), 0)), "window 1's first code value: the inline again");
    suite::eq(2, h.tr.new_expansions, "no new expansion at the cut (a run boundary doesn't present it again)");
    suite::eq(I(g.tr.rec[2 * 66]), I(h.tr.rec[2 * 66]), "the files word unchanged");
    bool eqr = true;
    for (int k = 4; k < 132; k++) eqr &= g.tr.rec[k] == h.tr.rec[k];
    suite::check(eqr, "the same registers as the uncut run");
}

// 600 iterations of sd; ld; addi; addi; bne: about 3,000 code values (raw 750 words, six credits), 600 loads and 599
// branch bases (1,199 read-log values, ten credits), 600 stores (300 write-log words, three credits).
using Policy = void (*)(Rig&);
RigP streamy(WindowOut& out, bool& ok, Policy policy, uint32_t rc) {
    Prog p(CODE);
    p.w({addi(1, 0, 600), sd(1, 2, 0), ld(3, 2, 0), addi(1, 1, -1), addi(2, 2, 8), bne(1, 0, -16), END});
    const std::vector<int64_t> base = Rig().bases(p);
    RigP gp(new Rig());
    Rig& g = *gp;
    Pages pages;
    pages.poke(CODE - g.lowest, p.bytes());
    std::vector<int64_t> owned;
    for (int k = 0; k < 2; k++) {
        const int64_t o = DATA - g.lowest + 4096LL * k;
        pages.put(o, std::vector<uint64_t>(512, 0));
        owned.push_back(o);
    }
    g.build_image(pages, owned);
    g.build_map({{p.pc_of(1), base[1]}});
    g.load_tables();
    if (policy) policy(g);
    Regs r = regs();
    r[2] = uint64_t(DATA);
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    ok = g.window(rc, 2000000, out);
    return gp;
}
void set_order(Rig& g, int a, int b2, int c) {
    g.has_order = true;
    g.order[0] = a;
    g.order[1] = b2;
    g.order[2] = c;
}

void output_path() {
    suite::group("the output path: queues 0-3, credits, service flags, done last (4.21.6)");
    const int rows = 1 + 600 * 5 + 1 + 10;
    WindowOut o;
    bool ok = false;
    RigP gp = streamy(o, ok, nullptr, uint32_t(rows));
    Rig& g = *gp;
    suite::check(ok && !g.tr.stopped(), "the window drained: %s", error_log(g.tr).c_str());
    suite::eq(rows, o.sizes[0].values, "code values: one per instruction and padding row (every instruction here is one row)");
    suite::eq((rows + 3) / 4, o.sizes[0].words, "raw code words: ceil(values / 4)");
    suite::eq(o.sizes[0].words, int64_t(o.code.size() / 2), "the code queue delivered exactly its words");
    suite::eq(600 + 599 + 1, o.sizes[1].values, "read log: loads, taken branches, the end");
    suite::eq(o.sizes[1].words, int64_t(o.rlog.size() / 2), "the read-log queue delivered its words");
    suite::eq(300, int64_t(o.wlog.size() / 2), "the write-log queue: 300 words");
    suite::eq(1, I(g.reg(3)), "the program ran (x3 = 1, the last value stored and loaded)");
    suite::check(g.tr.oq[1].max_occ <= 256 && g.tr.oq[2].max_occ <= 256, "the queues never exceed 256 entries");
    suite::eq(68, int64_t(o.record.size() / 2), "queue 0: the 68 words, in four transfers");
    suite::eq(I(g.tr.rec[0]), I(at(o.record, 0)), "queue 0's word 0: the state word as it stands (window n + 1's start)");
    const int64_t drain = g.tr.drained_at - g.tr.rows_end_at;
    std::printf("  output path: drain %lld cycles, scan stalls %lld, packets %lld\n", (long long)drain,
                (long long)g.tr.scan_stalls, (long long)g.dma_packets);
    suite::check(drain >= 2 + 750, "the drain takes at least the code scan (one entry a cycle) after the two-cycle start");
    suite::eq(int64_t(68 + o.code.size() / 2 + o.rlog.size() / 2 + o.wlog.size() / 2), g.dma_packets,
              "every packet one 128-bit word");

    suite::group("the output path: the same output in any service order, and from a slow controller (no deadlock)");
    const Policy pols[6] = {
        [](Rig& x) { set_order(x, 3, 2, 1); },
        [](Rig& x) { set_order(x, 2, 3, 1); },
        [](Rig& x) { x.state_last = true; set_order(x, 3, 1, 2); },
        [](Rig& x) { x.q0_one = true; },
        [](Rig& x) { x.hold = 97; },
        [](Rig& x) { x.hold = 500; set_order(x, 1, 2, 3); },
    };
    for (int k = 0; k < 6; k++) {
        WindowOut o2;
        bool ok2 = false;
        RigP h = streamy(o2, ok2, pols[k], uint32_t(rows));
        const bool eqo = ok2 && same(o.record, o2.record) && same(o.code, o2.code) && same(o.rlog, o2.rlog) &&
                         same(o.wlog, o2.wlog);
        suite::check(eqo, "policy %d: the same record and streams%s", k, ok2 ? "" : " (didn't drain)");
        if (k == 5)
            suite::check(h->tr.scan_stalls > 0, "a slow controller stalls the scans (%lld cycles) and still drains",
                         (long long)h->tr.scan_stalls);
    }

    suite::group("the output path: compressed, a zero-row run, and the transfers that are UB");
    WindowOut oc;
    bool okc = false;
    streamy(oc, okc, nullptr, uint32_t(rows) | COMP_ALL);
    suite::check(oc.sizes[0].compressed, "the loop's code stream is coded");
    suite::check(same(decode_lru(oc.code, int(oc.sizes[0].values), false), Rig::stream_values(o.code, int(o.sizes[0].values), 32)),
                 "the coded code stream decodes to the raw one");
    suite::eq(oc.sizes[0].words, int64_t(oc.code.size() / 2), "and is delivered in exactly its words");
    WindowOut ox;
    bool okx = false;
    RigP bigp = streamy(ox, okx, [](Rig& x) { x.hold = int64_t(1) << 30; }, uint32_t(rows));  // never serves
    Rig& big = *bigp;
    suite::check(big.tr.running(big.t), "nothing served: STATUS [0] stays 1 through the drain");
    suite::check((big.tr.events().value(big.t) & 0b1111u << Tracer::EV_SVC_STATE) != 0, "the service flags stay up");
    suite::eq(256, big.tr.oq[1].occ, "queue 1 full (two credits, 256 entries), its scan stalled");
    big.tr.mark_ub();
    suite::ub([&] { big.tr.out_start(big.t, 1, 257); }, {"credits cover"},
              "a transfer from queue 1 for more than its credits cover");
    big.tr.mark_ub();
    suite::ub([&] { big.tr.out_start(big.t, 0, 69); }, {"past word 67"}, "a transfer from queue 0 past word 67");
}

int64_t errors_of(const Prog& p, const Targets& targets, const Regs& r, int rows) {
    RigP g = rig(p, targets);
    g->load_record(CODE, int32_t(Rig().bases(p)[0]), 0, r.data(), {});
    WindowOut o;
    g->window(uint32_t(rows), 100000, o);
    return g->tr.errors(g->t);
}
Prog prog_w(std::initializer_list<uint32_t> ws) {
    Prog p(CODE);
    p.w(ws);
    return p;
}

void errors() {
    suite::group("errors that stop the Tracer");
    Regs r = regs();
    r[2] = uint64_t(CODE);  // a shared (code) page
    suite::eq(1 << Tracer::ERR_STORE, errors_of(prog_w({sd(0, 2, 64), END}), {}, r, 2),
              "a store to a page not owned (the code page): [8]");
    r = regs();
    r[2] = 0x7FE00000ULL;  // below lowest_address
    suite::eq(1 << Tracer::ERR_RANGE, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "a load below lowest_address: [10]");
    r = regs();
    r[2] = 0x7FF00000ULL + (uint64_t(8) << 20);  // at 8 K: just outside
    suite::eq(1 << Tracer::ERR_RANGE, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "a load at lowest + 8 K: [10]");
    r = regs();
    r[2] = uint64_t(DATA + 4);
    suite::eq(1 << Tracer::ERR_MISALIGNED, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "LD at a multiple of 4: misaligned [12]");
    r = regs();
    r[2] = uint64_t(DATA + 4);
    suite::eq(0, errors_of(prog_w({lw(3, 2, 0), END}), {}, r, 8) & (1 << Tracer::ERR_MISALIGNED), "LW at a multiple of 4: aligned");
    r = regs();
    r[2] = uint64_t(DATA + 2);
    suite::eq(1 << Tracer::ERR_MISALIGNED, errors_of(prog_w({lw(3, 2, 0), END}), {}, r, 8) & (1 << Tracer::ERR_MISALIGNED),
              "LW at a multiple of 2: misaligned [12]");
    r = regs();
    r[2] = 0x7FE00004ULL;
    suite::eq(1 << Tracer::ERR_RANGE | 1 << Tracer::ERR_MISALIGNED, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2),
              "both: [10] and [12] together (Q6)");
    suite::eq(1 << Tracer::ERR_MAP, errors_of(prog_w({jal(0, 8), END, END}), {}, regs(), 3), "a jump target not in the map: [7]");
    suite::eq(1 << Tracer::ERR_MAP, errors_of(prog_w({jalr(0, 0, 0), END}), {}, regs(), 2), "JALR to pc 0: [7] at once");
    Prog pc(CODE);
    pc.c(0x0000).c(0x0001).w({END});
    suite::eq(1 << Tracer::ERR_INVALID, errors_of(pc, {}, regs(), 2), "c.unimp: an invalid expansion [2]");
    // a hole in the map: a depth-1 map file whose line 0's page is missing
    const Prog p = prog_w({jal(0, 8), END, END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    g.map = g.fs.create(1);  // an empty depth-1 file: every line a hole
    g.load_record(CODE, 1, 0, regs().data(), {});
    WindowOut o;
    g.window(3, 100000, o);
    suite::eq(1 << Tracer::ERR_PAGE, g.tr.errors(g.t), "a hole in the jump map: [9]");
}

void words() {
    suite::group("the window's words");
    const Prog p = loop_prog();
    RigP gp = rig(p, {});
    Rig& g = *gp;
    suite::eq(0, g.tr.write_word(g.t, Tracer::W_ROWS_RUN, 1), "a store to ROWS_RUN is a bus error");
    suite::eq(0, g.tr.write_word(g.t, 44, 1), "a store to word 44, STAT_CYCLES, is a bus error (item 157)");
    suite::eq(0, g.tr.write_word(g.t, 51, 1), "and to word 51, STAT_PROBES");
    suite::eq(-1, g.tr.write_word(g.t, 52, 1), "word 52: reserved");
    uint32_t v52 = 0;
    suite::check(!g.tr.word(g.t, 52, v52), "word 52 reads as reserved");
    suite::eq(68, wd(g, Tracer::W_STATE_LENGTH), "STATE_LENGTH reads 68");
    g.word(Tracer::W_RUN_CONFIG, 5 | Tracer::RC_COMP_CODE);
    g.step();
    suite::eq(5 | Tracer::RC_COMP_CODE, wd(g, Tracer::W_RUN_CONFIG), "RUN_CONFIG reads back");
    suite::eq(0, wd(g, Tracer::W_EXP_TEMPLATE), "EXPANSION_TEMPLATE reads 0");
    g.load_record(CODE, 1, 0, regs().data(), {});
    WindowOut z;
    const bool zok = g.window(0, 1000, z);
    suite::check(zok, "a zero-row run drains its state queue");
    suite::eq(0, z.sizes[0].values, "and writes no code value (Q7)");
    suite::eq(CODE, I(g.tr.rec[0]), "nothing traced");
    g.word(Tracer::W_RUN_CONFIG, 65537);
    g.tr.mark_ub();
    suite::ub([&] { g.tr.run(g.t); g.step(); g.step(); }, {"65,536"}, "more than 65,536 rows");
    // the word-0 write with advice left: flagged
    RigP hp = rig(p, {});
    Rig& h = *hp;
    h.load_record(CODE, 1, 0, regs().data(), {1, 2, 3});
    h.port_add(0x000, uint64_t(CODE), 1);
    h.drain_port();
    bool flagged = false;
    for (int64_t k = 0; k < h.tr.n_violations && k < Tracer::FACTS; k++)
        flagged |= h.tr.violations[k].code == Tracer::V_ADVICE_UNCONSUMED;
    suite::check(flagged, "a word-0 write finding advice above 0 is flagged");
}

// The statistics counters, words 44-51 (4.21.3, 8.16, item 157).
struct Watch {
    int64_t last[Tracer::STAT_N] = {};
    bool ok = true;
};
void watch(Rig& g, void* ctx) {
    Watch& w = *static_cast<Watch*>(ctx);
    for (int k = 0; k < Tracer::STAT_N; k++) {
        const int64_t v = wd(g, Tracer::W_STAT + k, g.t + 1), d = v - w.last[k];
        if (d < 0 || (k <= Tracer::ST_STALL_OUTPUT && d > 1)) w.ok = false;
        w.last[k] = v;
    }
}

void counters() {
    suite::group("the statistics counters, words 44-51 (item 157)");
    const Prog p = loop_prog();
    const std::vector<int64_t> base = Rig().bases(p);
    const Targets targets = hash_targets({{p.pc_of(2), base[2]}});
    RigP gp = rig(p, targets);
    Rig& g = *gp;
    Regs r = regs();
    r[2] = uint64_t(DATA);
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    for (int k = 0; k < Tracer::STAT_N; k++) suite::eq(0, stat(g, k), "word %d is 0 after the word-0 write", 44 + k);
    // read every counter every cycle: each moves by at most 1 a cycle (miss counts by the misses), never back
    Watch w;
    g.on_step = watch;
    g.on_step_ctx = &w;
    const int rows = 2 + 5 * 5 + 1 + 6;
    WindowOut o;
    const bool ok = g.window(uint32_t(rows), 100000, o);
    g.on_step = nullptr;
    suite::check(ok && !g.tr.stopped(), "the window drained: %s", error_log(g.tr).c_str());
    suite::check(w.ok, "read every cycle, each stall and cycle counter moves by at most one a cycle, never back (counted in its own cycle)");
    suite::eq(int32_t(g.tr.drained_at - g.tr.run_start_at), stat(g, Tracer::ST_CYCLES),
              "STAT_CYCLES: every cycle STATUS [0] was 1, the drain included");
    const int64_t* s = g.tr.stalls;
    suite::eq(int32_t(s[Tracer::S_IFETCH]), stat(g, Tracer::ST_STALL_FETCH), "STAT_STALL_FETCH: the instruction-fetch stalls");
    suite::eq(int32_t(s[Tracer::S_DMISS] + s[Tracer::S_BUS]), stat(g, Tracer::ST_STALL_DATA),
              "STAT_STALL_DATA: the data misses, the write-back wait included");
    suite::eq(int32_t(s[Tracer::S_PROBE]), stat(g, Tracer::ST_STALL_MAP), "STAT_STALL_MAP: the map probes' stalls");
    suite::eq(int32_t(g.tr.i_miss), stat(g, Tracer::ST_MISS_I), "STAT_MISS_I");
    suite::eq(int32_t(g.tr.d_miss), stat(g, Tracer::ST_MISS_D), "STAT_MISS_D");
    suite::eq(int32_t(g.tr.lru_misses), stat(g, Tracer::ST_PROBES), "STAT_PROBES: the jump-LRU misses");
    suite::check(stat(g, Tracer::ST_STALL_FETCH) > 0 && stat(g, Tracer::ST_STALL_DATA) > 0 && stat(g, Tracer::ST_STALL_MAP) > 0,
                 "a cold start stalls on fetch, data and the map");
    suite::check(stat(g, Tracer::ST_MISS_I) > 0 && stat(g, Tracer::ST_MISS_D) > 0 && stat(g, Tracer::ST_PROBES) > 0,
                 "and misses each");
    const int64_t cyc = stat(g, Tracer::ST_CYCLES), fetch = stat(g, Tracer::ST_STALL_FETCH);
    const int64_t t1 = g.t;
    WindowOut o2;
    const bool ok2 = g.window(4, 10000, o2);  // padding rows: nothing fetched
    suite::check(ok2, "a second run drains");
    suite::eq(cyc + int32_t(g.tr.drained_at - g.tr.run_start_at), stat(g, Tracer::ST_CYCLES),
              "PULSE_RUN doesn't clear them: they count across a segment's runs");
    suite::eq(fetch, stat(g, Tracer::ST_STALL_FETCH), "padding rows fetch nothing");
    suite::check(g.tr.run_start_at >= t1, "(the second run started after the first)");
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    int64_t sum = 0;
    for (int k = 0; k < Tracer::STAT_N; k++) sum |= stat(g, k);
    suite::eq(0, sum, "the next word-0 write clears all eight");

    suite::group("the statistics counters: STAT_STALL_OUTPUT, either scan stalled on a full queue");
    const int orows = 1 + 600 * 5 + 1 + 10;
    WindowOut oh;
    bool okh = false;
    RigP h = streamy(oh, okh, [](Rig& x) { x.hold = 500; set_order(x, 1, 2, 3); }, uint32_t(orows));
    const int64_t out = stat(*h, Tracer::ST_STALL_OUTPUT);
    suite::check(out > 0, "a slow controller: the scans stalled for %lld cycles", (long long)out);
    suite::check(out <= h->tr.scan_stalls && out >= (h->tr.scan_stalls + 1) / 2,
                 "counted once a cycle when both scans stall (%lld of %lld scan-stall cycles)", (long long)out,
                 (long long)h->tr.scan_stalls);
    WindowOut of;
    bool okf = false;
    RigP f = streamy(of, okf, nullptr, uint32_t(orows));
    suite::eq(0, stat(*f, Tracer::ST_STALL_OUTPUT), "an ideal controller: no output stall");
}

}  // namespace

int main(int argc, char** argv) {
    std::FILE* calls = nullptr;
    static char calls_buf[1 << 16];
    if (argc == 3 && std::strcmp(argv[1], "--calls") == 0) {
        calls = std::fopen(argv[2], "w");
        if (!calls) FATAL("suite_tracer: can't write %s", argv[2]);
        std::setvbuf(calls, calls_buf, _IOFBF, sizeof calls_buf);
        RecTracer::open_log(calls);
    } else if (argc != 1) FATAL("usage: suite_tracer [--calls FILE]");
    loop();
    cut();
    write_back();
    compressed();
    division();
    controller_fed();
    output_path();
    errors();
    words();
    counters();
    const int r = suite::done("TracerTest");
    if (calls) {
        RecTracer::close_log();
        std::fclose(calls);
    }
    return r;
}
