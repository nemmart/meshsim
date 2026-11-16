// suite_tracer.cpp: the Tracer's directed suite (v1.3's TracerTest, ported), on the Tracer through the rig
// (tests/tracer_rig.h), whose controller is the RTL bench's (tests/tracer_block.h), on the suite.h. Its output must
// equal tests/tracersuites/tracer.log (run_tests.sh's step `tracer`).
//
// v1.36: the Tracer is the RTL's (Snarky One's rtl/blocks/tracer_unit), cycle for cycle. Each expectation here is
// spec 17.9's (`spec/jolt/tracer.md`); where the spec gives no number, the RTL's source or README, named in place.
// The fill port answers a read 1 cycle after its request (the RTL's tests' memory system) unless a case says
// otherwise, so the cycle counts are closed forms: L is that latency.
//   a row's schedule         4 cycles, 6 for an M row (17.9.7)
//   a run's first instruction: the first row starts 6 cycles after START's effect, 7 after START's store
//                            (rtl/blocks/tracer_unit/README.md "The rows and their schedule"; its
//                            tracer_execution_test.rs `what_a_transfer_costs`), all STAT_STALL_FETCH
//   a redirect               a row that sets its successor's pc: the next row starts 5 cycles past its schedule
//                            (17.9.7; the same README and test), STAT_STALL_FETCH; 4 when the new pc is 0, the
//                            program's end before padding, and a run's first row 5 cycles after START's effect when
//                            it is a padding row: pc 0 is fetched from nowhere, so the fetch's hit, one cycle of
//                            the 5 and of the 6, is not made (tracer_instruction.veryl, `issue_padding`)
//   an instruction miss      L + 3 cycles (tracer_fetch.veryl: REQUEST, the request's register, the response after L,
//                            READ_LINE, against a hit's READ in the look-up's next cycle), STAT_STALL_FETCH
//   a data miss              L + 5 cycles (tracer_data.veryl: VICTIM, MISS, REQUEST or EVICT, the request's register,
//                            the response after L, READ_LINE, against a hit's ACCESS in the look-up's next cycle),
//                            STAT_STALL_DATA
//   a probe of n lines       n * (L + 4) + 2 cycles (tracer_jump_lookup.veryl: the multiply, the home line, then for
//                            each line its request, the request's register, the response after L, the memory part's
//                            register of it, the compare; then the decision), STAT_STALL_MAP
//   the cycles add up        from START's effect to the last row's completion: the schedules + STAT_STALL_FETCH +
//                            STAT_STALL_DATA + STAT_STALL_MAP + the waits on the core (17.9.8)
//
// The cases, on hand-built guests:
//  - a loop of stores, loads and a taken branch, ending on JAL x0, 0, then padding: registers, memory, the code FIFO
//    (one value per native instruction, 0 per padding row), the log FIFO (tags, values, the branch's base from the map,
//    the end's 0), the stream word, ROWS_RUN, the end state in part 0, and its cycles;
//  - a window cut: the same program in runs that stop mid-way, the totals per window and the state carried;
//  - dirty victims through the write-back buffer and the fill port's write channel;
//  - compressed instructions: their code words (decompressed, bit 0 clear) and pc + 2;
//  - DIV with advice: the advice popped and counted down, a run cut inside the expansion (its code value again in the
//    next window, the state word's next offset), a wrong quotient failing an assertion, advice underflow;
//  - core-fed expansions: a CSR template and an inline, a cut inside the inline;
//  - the output path: the queues, the credits, the service orders, a slow controller, the transfers that are UB;
//  - the errors that stop, by 17.9.1's bit numbers;
//  - the window words; the statistics counters.
// Usage: suite_tracer (from v1.36/).
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
constexpr int L = 1;   // the fill port's read latency in these cases (Rig's default)
using Regs = std::array<uint64_t, 128>;
using Targets = std::vector<std::pair<int64_t, int64_t>>;
using RigP = std::unique_ptr<Rig>;

Regs regs() { return Regs{}; }
// a value of a vector, 0 past its end (only a failing run gets there)
uint64_t at(const std::vector<uint64_t>& v, size_t k) { return k < v.size() ? v[k] : 0; }
int64_t I(uint64_t v) { return int64_t(v); }
// a window word as a level read (the end of t - 1), signed; FATAL on an undefined word
int64_t wd(Rig& g, int w, int64_t t) {
    uint32_t v = 0;
    if (!g.tr.word(t, w, v)) FATAL("suite_tracer: word %d reads as undefined", w);
    return int64_t(int32_t(v));
}
int64_t wd(Rig& g, int w) { return wd(g, w, g.t); }
int64_t stat(Rig& g, int k) { return wd(g, Tracer::W_STAT + k); }
// the targets in the order v1.3's rig handed them to the map (a HashMap's)
Targets hash_targets(const Targets& in) {
    std::vector<int64_t> keys;
    for (const auto& e : in) keys.push_back(e.first);
    Targets out;
    for (int64_t k : java_hash_order(keys))
        for (const auto& e : in)
            if (e.first == k) { out.push_back(e); break; }
    return out;
}
// the Tracer's error log and flags (only in a failing line)
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

// The rows' schedules (17.9.7): 4 cycles a row, 6 for an M row.
int64_t schedules(const Tracer& tr, int64_t rows0 = 0, int64_t rows_m0 = 0) {
    return 4 * (tr.rows - rows0) + 2 * (tr.rows_m - rows_m0);
}
// 17.9.8, "the cycles add up", for the run just made on a rig whose statistics were `before` at its START: the cycles
// from START's effect (the cycle after its store) to the last row's completion.
struct Before {
    int64_t rows, rows_m, fetch, data, map, advice, exp;
    explicit Before(Rig& g)
        : rows(g.tr.rows), rows_m(g.tr.rows_m), fetch(stat(g, Tracer::ST_STALL_FETCH)), data(stat(g, Tracer::ST_STALL_DATA)),
          map(stat(g, Tracer::ST_STALL_MAP)), advice(g.tr.stalls[Tracer::S_ADVICE]), exp(g.tr.stalls[Tracer::S_EXP]) {}
};
void cycles_add_up(Rig& g, const Before& b, const char* what) {
    const int64_t counted = (stat(g, Tracer::ST_STALL_FETCH) - b.fetch) + (stat(g, Tracer::ST_STALL_DATA) - b.data) +
                            (stat(g, Tracer::ST_STALL_MAP) - b.map);
    const int64_t core = (g.tr.stalls[Tracer::S_ADVICE] - b.advice) + (g.tr.stalls[Tracer::S_EXP] - b.exp);
    suite::eq(schedules(g.tr, b.rows, b.rows_m) + counted + core, g.last.last_row_done - g.last.start_store,
              "%s: the cycles add up (17.9.8): the schedules, the three stall counters, the waits on the core", what);
}

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
// The loop's stall cycles (the header's closed forms), for a run of its 28 rows and padding from a cold start:
//   fetch: the run's first instruction, 6, and its line's fill, L + 3; five branches (four taken, one not), each a
//          redirect of 5; the end, to pc 0, 4
//   data:  the one data line's fill, L + 5 (the cache empty: no victim)
//   map:   the first taken branch's probe, one line: (L + 4) + 2; the other three hit the jump LRU
constexpr int64_t LOOP_FETCH = 6 + (L + 3) + 5 * 5 + 4, LOOP_DATA = L + 5, LOOP_MAP = (L + 4) + 2;

// The replayer's decoding rule (17.8.12), here for the tests: 16-entry LRU, the write log's E[15] from loads.
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
    const Before before(g);
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
    const int64_t counted[3] = {stat(g, Tracer::ST_STALL_FETCH), stat(g, Tracer::ST_STALL_DATA), stat(g, Tracer::ST_STALL_MAP)};
    std::printf("  loop: %lld rows, %lld cycles to the last row, stalls %s, waits on the core %s; %s\n", (long long)g.tr.rows,
                (long long)(g.last.last_row_done - g.last.start_store), suite::jlist(counted, 3).c_str(),
                suite::jlist(g.tr.stalls, Tracer::S_N).c_str(), Rig::mem_summary(g.ms).c_str());
    // the cold cost at fill latency L: one fill each of the instruction line, the data line and the map's line
    suite::eq(3, g.ms.reads, "the fill port: three reads, the instruction line, the data line, the map's line (need, 17.9.5)");
    suite::eq(0, g.ms.writes, "and no write: nothing evicted");
    suite::eq(7 + (L + 3), g.last.first_row_start - g.last.start_store,
              "the first row starts 7 cycles after START's store, and its line's fill later (L + 3)");
    suite::eq(LOOP_FETCH, counted[0], "STAT_STALL_FETCH: the run's first instruction 6, its fill L + 3, five redirects of 5, the end's 4");
    suite::eq(LOOP_DATA, counted[1], "STAT_STALL_DATA: the data line's fill, L + 5");
    suite::eq(LOOP_MAP, counted[2], "STAT_STALL_MAP: the probe of one line, (L + 4) + 2");
    suite::eq(0, g.tr.stalls[Tracer::S_ADVICE] + g.tr.stalls[Tracer::S_EXP], "no wait on the core");
    suite::eq(4LL * rows + LOOP_FETCH + LOOP_DATA + LOOP_MAP, g.last.last_row_done - g.last.start_store,
              "the run: 4 cycles a row, plus the stalls (17.9.8)");
    cycles_add_up(g, before, "the loop");
    // tracer_unit.veryl registers row_done: the control's rows' end is the cycle after the last row's completion
    suite::eq(g.last.last_row_done + 1, g.tr.rows_end_at, "the rows' end reaches the run the cycle after the last row completes");

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
    // 17.9.6's choice, for the read log: coded only if fewer 128-bit words than raw
    suite::check(o2.sizes[1].compressed ? o2.sizes[1].words < o.sizes[1].words : o2.sizes[1].words == o.sizes[1].words,
                 "read log: chosen by size");
    suite::check(same(o2.sizes[1].compressed ? decode_lru(o2.rlog, int(o2.sizes[1].values), false)
                                             : Rig::stream_values(o2.rlog, int(o2.sizes[1].values), 64),
                      Rig::stream_values(o.rlog, int(sz[1].values), 64)),
                 "and it holds the raw run's values");
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
        const Before before(g);
        const bool ok = g.window(7, 100000, o);
        suite::check(ok && !g.tr.stopped(), "window %d drained", n);
        suite::eq(7, o.sizes[0].values, "window %d: seven one-row instructions, seven code values", n);
        total += o.sizes[0].values;
        loads += o.sizes[1].values;
        stores += o.sizes[2].values;
        suite::eq(I(at(o.record, 0)), I(g.tr.rec[0]), "window %d: the record's state word is the live state (window n + 1's start)", n);
        cycles_add_up(g, before, suite::str("window %d", n).c_str());
    }
    (void)loads;
    suite::eq(28, total, "28 rows in four windows: the program's 28");
    suite::eq(5, stores, "five stores across the windows");
    suite::eq(0, I(g.reg(1)), "the same end state as one run");
    suite::eq(0, I(g.tr.rec[0]), "at the end");
    suite::eq(1, g.tr.i_miss, "the caches carry across runs: one instruction miss in all four");
    // the same stalls as the one run's, and each later run's first instruction: its 6 cycles, with a hit
    suite::eq(LOOP_FETCH - 5 - 4 + 3 * 6, stat(g, Tracer::ST_STALL_FETCH),
              "STAT_STALL_FETCH: the one run's, less the two redirects at a run's end (no row after them), and three more runs' first instructions");
    suite::eq(LOOP_DATA, stat(g, Tracer::ST_STALL_DATA), "STAT_STALL_DATA: the one data line's fill");
    suite::eq(LOOP_MAP, stat(g, Tracer::ST_STALL_MAP), "STAT_STALL_MAP: the one probe (the jump LRU carries across runs)");
}

// 130 stores 128 bytes apart (130 lines through a 128-line cache), then loads of the first lines: the dirty victims
// went through the write-back buffer and the fill port's write channel, and come back with their data.
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
    PortLog port(g);
    WindowOut o;
    const Before before(g);
    const bool ok = g.window(1 + 130 * 4 + 3, 400000, o);
    suite::check(ok && !g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(130, I(g.reg(4)), "the first line's value, after its eviction");
    suite::eq(129, I(g.reg(5)), "the second line's");
    // lines 128 and 129 evict lines 0 and 1, and the two loads' fills lines 2 and 3: four dirty victims
    suite::eq(4, g.tr.writebacks, "four dirty victims, each written back through the buffer");
    suite::eq(4, port.writes(), "four writes on the fill port");
    suite::eq(132, g.ms.reads - 2, "132 data fills (130 stores' lines and the two loads'), beside the instruction line and the map's");
    suite::eq(128, g.tr.cache_lines(true), "the data cache holds 128 lines");
    // the old bus wait (v1.35's S_BUS) is the buffer's rules now (17.9.5)
    suite::check(port.one_of_each(), "at most one read and one write outstanding");
    suite::check(port.writes_wait_for_reads(), "each write-back is requested only when no read is outstanding");
    bool own_line = true;
    for (int k = 0, w = 0; k < port.n(); k++) {
        const MemSystem::PortCall& c = port.at(k);
        if (!c.write) continue;
        own_line = own_line && c.line == (DATA - g.lowest) / 128 + w && c.status == MemSystem::WRITE_OK;
        w++;
    }
    suite::check(own_line, "the victims in LRU order, lines 0 to 3 of the data, each answered ok");
    suite::eq(0, g.tr.take_backs, "no line taken back: each write was answered before its line was needed again");
    suite::eq(132 * (L + 5), stat(g, Tracer::ST_STALL_DATA),
              "STAT_STALL_DATA: 132 fills of L + 5: no victim waited for the buffer's place");
    cycles_add_up(g, before, "the stores");
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
    // straight-line code keeps its schedule (17.9.7): 16-bit instructions too
    suite::eq(4 * 4 + 6 + (L + 3), g.last.last_row_done - g.last.start_store,
              "four rows of 4 cycles after the run's first instruction and its fill: no other stall");
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
    const Before before(g);
    g.window(uint32_t(rows), 100000, o);
    suite::check(!g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(14, I(g.reg(7)), "x7 = 100 / 7");
    suite::eq(0, g.tr.advice_left(), "the advice field counted down to 0");
    suite::eq(2, o.sizes[1].values, "read log: the advice value and the end");
    const std::vector<uint64_t> rl = Rig::stream_values(o.rlog, 2, 64);
    suite::eq(14, I(at(rl, 0)), "the advice value, as popped");
    cycles_add_up(g, before, "DIV");
    suite::eq(0, g.tr.stalls[Tracer::S_ADVICE], "the advice was in the read queue: no wait on the core");
    suite::eq(6 + (L + 3), stat(g, Tracer::ST_STALL_FETCH), "straight-line code: the run's first instruction and its fill alone");

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
    suite::eq(div(7, 5, 6), I(at(code, 0)), "window 1 starts with DIV's code value again (17.9.4)");
    suite::eq(14, I(h.reg(7)), "and finishes it");
    suite::eq(I(g.tr.rec[0]), I(h.tr.rec[0]), "the same end state as the uncut run");

    suite::group("DIV: a wrong quotient fails an assertion (ERRORS bit 7); advice underflow (bit 2)");
    RigP bad = rig(p, {});
    bad->load_record(CODE, int32_t(base[0]), 0, regs().data(), {15});
    WindowOut ob;
    bad->window(uint32_t(rows), 100000, ob);
    suite::check(bad->tr.stopped(), "stopped");
    suite::eq(1 << 7, bad->tr.errors(bad->t), "ERRORS bit 7, failed_assertion, alone");
    suite::check(bad->tr.running(bad->t), "STATUS.busy stays 1 after an error (17.9.9)");
    RigP none = rig(p, {});
    none->load_record(CODE, int32_t(base[0]), 0, regs().data(), {});
    WindowOut on;
    none->window(uint32_t(rows), 100000, on);
    suite::eq(1 << 2, none->tr.errors(none->t), "an advice row with the field at 0: bit 2, advice_underflow");
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
    const Before before(g);
    const bool ok = g.window(uint32_t(rows), 400000, o);
    suite::check(ok && !g.tr.stopped(), "no error: %s", error_log(g.tr).c_str());
    suite::eq(0, g.tr.n_violations, "no flag (EXPANSION_TEMPLATE stored before the inline's first word): %s",
              violations(g.tr).c_str());
    suite::eq(2, g.tr.new_expansions, "two new expansions");
    suite::eq(2, int64_t(g.last.post_opcode.size()), "each posted to the core once");
    suite::eq(int32_t(csr), int32_t(g.last.post_opcode.empty() ? 0 : g.last.post_opcode[0]), "EXPANSION_OPCODE at the first post: the CSR's code value");
    suite::eq(rows, g.tr.rows, "every row");
    suite::eq(int64_t(1) << 3, I(at(o.record, 2 * Tracer::REC_BITMAP)),
              "the bitmap: the inline's bit 3 only (CSR templates aren't tracked)");
    suite::eq(0, I(g.tr.rec[0]), "the program ended");
    cycles_add_up(g, before, "the core-fed run");
    suite::check(g.tr.stalls[Tracer::S_EXP] > 0, "the rows waited on the core for their words (%lld cycles, in no counter)",
                 (long long)g.tr.stalls[Tracer::S_EXP]);

    suite::group("a window cut inside the inline: the latch sets the next window's bit");
    RigP hp = rig(p, {});
    Rig& h = *hp;
    h.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    WindowOut a;
    h.window(uint32_t(n_csr + 1000), 400000, a);
    suite::eq(1000, int32_t(h.tr.rec[1] >> 32) & 0xFFFF, "the state word's next offset: 1,000 rows of the inline run");
    suite::eq(0, wd(h, Tracer::W_EXP_OFFSET),
              "EXPANSION_OFFSET: the row offset at which the inline was posted, 0, held until the next post (17.9.1)");
    suite::eq(int32_t(inl), wd(h, Tracer::W_EXP_OPCODE), "EXPANSION_OPCODE: the inline's code value, held");
    suite::eq(int64_t(1) << 3, I(at(a.record, 2 * Tracer::REC_BITMAP)), "window 0's bitmap has the inline");
    WindowOut bw;
    const bool bok = h.window(uint32_t(n_inl - 1000 + 1), 400000, bw);
    suite::check(bok && !h.tr.stopped(), "window 1 finishes: %s", error_log(h.tr).c_str());
    suite::eq(int64_t(1) << 3, I(at(bw.record, 2 * Tracer::REC_BITMAP)),
              "window 1's bitmap has it too (the latch, set again at START)");
    suite::eq(int32_t(inl), int32_t(at(Rig::stream_values(bw.code, 1, 32), 0)), "window 1's first code value: the inline again");
    suite::eq(2, h.tr.new_expansions, "no new expansion at the cut (a run boundary doesn't present it again)");
    suite::eq(0, wd(h, Tracer::W_EXP_OFFSET), "EXPANSION_OFFSET still the post's");
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
    g.bench.has_order = true;
    g.bench.order[0] = a;
    g.bench.order[1] = b2;
    g.bench.order[2] = c;
}

void output_path() {
    suite::group("the output path: queues 0-3, credits, service flags, done last (17.9.6)");
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
                (long long)g.tr.scan_stalls, (long long)g.bench.dma_packets);
    suite::check(drain >= rows, "the drain takes at least the code scan, one FIFO entry a cycle (17.9.7)");
    suite::eq(int64_t(68 + o.code.size() / 2 + o.rlog.size() / 2 + o.wlog.size() / 2), g.bench.dma_packets,
              "every packet one 128-bit word");
    // 17.9.6: done once the rows are traced, both scans ended, every queue empty and the DMA's last transfer ended
    suite::check(g.tr.drained_at >= g.tr.rows_end_at + 2, "done after the rows' end and the drain");
    suite::eq(1, g.last.status & 3 ? 0 : 1, "after done: STATUS.busy 0 and the DMA idle");
    suite::eq(0, g.last.events_peek, "after done: every event consumed, every _ready down");

    suite::group("the output path: the same output in any service order, and from a slow controller (no deadlock)");
    const Policy pols[6] = {
        [](Rig& x) { set_order(x, 3, 2, 1); },
        [](Rig& x) { set_order(x, 2, 3, 1); },
        [](Rig& x) { x.bench.state_last = true; set_order(x, 3, 1, 2); },
        [](Rig& x) { x.bench.q0_one = true; },
        [](Rig& x) { x.bench.hold = 97; },
        [](Rig& x) { x.bench.hold = 500; set_order(x, 1, 2, 3); },
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
    RigP bigp = streamy(ox, okx, [](Rig& x) { x.bench.hold = int64_t(1) << 30; }, uint32_t(rows));  // never serves
    Rig& big = *bigp;
    suite::check(!okx && big.tr.running(big.t), "nothing served: STATUS.busy stays 1 through the drain");
    suite::eq(0xF << 5, big.tr.events().value(big.t) & (0xFu << 5), "the four _ready events, bits 5 to 8, stay up");
    suite::eq(256, big.tr.oq[1].occ, "queue 1 full (two credits, 256 entries), its scan stalled");
    suite::eq(2, big.tr.oq[1].credits, "its two credits");
    suite::ub([&] { big.tr.out_start(big.t, 1, 257); }, {"credits cover"},
              "a transfer from queue 1 for more than its credits cover");
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

// ERRORS' bits, by 17.9.1's table: literal numbers.
void errors() {
    suite::group("errors that stop the Tracer");
    Regs r = regs();
    r[2] = uint64_t(CODE);  // a shared (code) page
    suite::eq(1 << 4, errors_of(prog_w({sd(0, 2, 64), END}), {}, r, 2),
              "a store to a page not owned (the code page): bit 4, store_not_writable");
    r = regs();
    r[2] = 0x7FE00000ULL;  // below lowest_address
    suite::eq(1 << 6, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "a load below lowest_address: bit 6, out_of_range");
    r = regs();
    r[2] = 0x7FF00000ULL + (uint64_t(8) << 20);  // at 8 K: just outside
    suite::eq(1 << 6, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "a load at lowest + 8 K: bit 6");
    r = regs();
    r[2] = uint64_t(DATA + 4);
    suite::eq(1 << 8, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "LD at a multiple of 4: bit 8, misaligned");
    r = regs();
    r[2] = uint64_t(DATA + 4);
    suite::eq(0, errors_of(prog_w({lw(3, 2, 0), END}), {}, r, 8), "LW at a multiple of 4: aligned, no error");
    r = regs();
    r[2] = uint64_t(DATA + 2);
    suite::eq(1 << 8, errors_of(prog_w({lw(3, 2, 0), END}), {}, r, 8), "LW at a multiple of 2: bit 8, misaligned");
    r = regs();
    r[2] = 0x7FE00004ULL;
    suite::eq(1 << 6 | 1 << 8, errors_of(prog_w({ld(3, 2, 0), END}), {}, r, 2), "both: bits 6 and 8 together (17.9.5)");
    suite::eq(1 << 3, errors_of(prog_w({jal(0, 8), END, END}), {}, regs(), 3), "a jump target not in the map: bit 3, map_miss");
    suite::eq(1 << 3, errors_of(prog_w({jalr(0, 0, 0), END}), {}, regs(), 2), "JALR to pc 0: bit 3 at once");
    Prog pc(CODE);
    pc.c(0x0000).c(0x0001).w({END});
    suite::eq(1 << 1, errors_of(pc, {}, regs(), 2), "c.unimp: bit 1, invalid_expansion");
    // a hole in the map: a depth-1 map file whose line 0's page is missing
    const Prog p = prog_w({jal(0, 8), END, END});
    RigP gp = rig(p, {});
    Rig& g = *gp;
    g.map = g.fs.create(1);  // an empty depth-1 file: every line a hole
    g.load_record(CODE, 1, 0, regs().data(), {});
    WindowOut o;
    g.window(3, 100000, o);
    suite::eq(1 << 5, g.tr.errors(g.t), "a hole in the jump map: bit 5, page_missing");
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
    suite::eq(0, z.sizes[0].values, "and writes no code value (17.9.3)");
    suite::eq(CODE, I(g.tr.rec[0]), "nothing traced");
    suite::eq(1, g.tr.rows_end_at - g.tr.run_start_at, "its rows end in the cycle after START's (17.9.3)");
    suite::eq(0, stat(g, Tracer::ST_STALL_FETCH) + stat(g, Tracer::ST_STALL_DATA) + stat(g, Tracer::ST_STALL_MAP),
              "with rows 0 no cycle is a stall cycle (17.9.8)");
    suite::eq(0, stat(g, Tracer::ST_MISS_I), "and nothing is fetched");
    g.word(Tracer::W_RUN_CONFIG, 65537);
    suite::ub([&] { g.bench.store(Bench::B_CONTROL, Bench::C_START, 1); g.step(); g.step(); }, {"65,536"}, "more than 65,536 rows");
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

// The statistics counters, words 44-51 (17.9.8).
struct Watch {
    Rig* g = nullptr;
    int64_t last[Tracer::STAT_N] = {};
    bool ok = true;
};
void watch(void* ctx, int64_t n) {
    Watch& w = *static_cast<Watch*>(ctx);
    for (int k = 0; k < Tracer::STAT_N; k++) {
        const int64_t v = wd(*w.g, Tracer::W_STAT + k, n + 1), d = v - w.last[k];
        if (d < 0 || (k <= Tracer::ST_STALL_OUTPUT && d > 1)) w.ok = false;
        w.last[k] = v;
    }
    // each stall cycle in one stall counter at most (17.9.8)
}

void counters() {
    suite::group("the statistics counters, words 44-51 (17.9.8)");
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
    w.g = &g;
    g.bench.on_cycle = watch;
    g.bench.on_cycle_ctx = &w;
    const int rows = 2 + 5 * 5 + 1 + 6;
    WindowOut o;
    const bool ok = g.window(uint32_t(rows), 100000, o);
    g.bench.on_cycle = nullptr;
    suite::check(ok && !g.tr.stopped(), "the window drained: %s", error_log(g.tr).c_str());
    suite::check(w.ok, "read every cycle, each stall and cycle counter moves by at most one a cycle, never back (counted in its own cycle)");
    // busy from the cycle after START's store through done's cycle
    suite::eq(int32_t(g.tr.drained_at - g.tr.run_start_at), stat(g, Tracer::ST_CYCLES),
              "STAT_CYCLES: every cycle STATUS.busy was 1, the drain included");
    suite::eq(g.last.done_load - 1 - g.last.start_store, stat(g, Tracer::ST_CYCLES),
              "the same by the controller: done is seen by the load of the cycle after it is posted");
    suite::eq(LOOP_FETCH, stat(g, Tracer::ST_STALL_FETCH), "STAT_STALL_FETCH: the run's first instruction, its fill, five redirects and the end's");
    suite::eq(LOOP_DATA, stat(g, Tracer::ST_STALL_DATA), "STAT_STALL_DATA: the data line's fill");
    suite::eq(LOOP_MAP, stat(g, Tracer::ST_STALL_MAP), "STAT_STALL_MAP: the probe of one line");
    suite::eq(1, stat(g, Tracer::ST_MISS_I), "STAT_MISS_I: one instruction line");
    suite::eq(1, stat(g, Tracer::ST_MISS_D), "STAT_MISS_D: one data line");
    suite::eq(1, stat(g, Tracer::ST_PROBES), "STAT_PROBES: one jump-LRU miss that starts a probe; three hits");
    suite::eq(0, stat(g, Tracer::ST_STALL_OUTPUT), "STAT_STALL_OUTPUT: no scan waited");
    for (int k = 0; k < Tracer::STAT_N; k++)
        suite::eq(int32_t(g.last.stat[k]), stat(g, k), "word %d as the controller loaded it after done", 44 + k);
    const int64_t cyc = stat(g, Tracer::ST_CYCLES), fetch = stat(g, Tracer::ST_STALL_FETCH);
    const int64_t t1 = g.t;
    WindowOut o2;
    const Before before(g);
    const bool ok2 = g.window(4, 10000, o2);  // padding rows: nothing fetched
    suite::check(ok2, "a second run drains");
    suite::eq(cyc + int32_t(g.tr.drained_at - g.tr.run_start_at), stat(g, Tracer::ST_CYCLES),
              "START doesn't clear them: they count across a segment's runs");
    suite::eq(1, stat(g, Tracer::ST_MISS_I), "padding rows fetch nothing: no look-up");
    const int64_t first = stat(g, Tracer::ST_STALL_FETCH) - fetch;
    suite::eq(5, first, "the run's first row, a padding row: 5 cycles in STAT_STALL_FETCH, a fetched instruction's 6 less the fetch");
    cycles_add_up(g, before, "the padding run");
    suite::check(g.tr.run_start_at >= t1, "(the second run started after the first)");
    g.load_record(CODE, int32_t(base[0]), 0, r.data(), {});
    int64_t sum = 0;
    for (int k = 0; k < Tracer::STAT_N; k++) sum |= stat(g, k);
    suite::eq(0, sum, "the next word-0 write clears all eight");

    suite::group("the statistics counters: STAT_STALL_OUTPUT, either scan stalled on a full queue");
    const int orows = 1 + 600 * 5 + 1 + 10;
    WindowOut oh;
    bool okh = false;
    RigP h = streamy(oh, okh, [](Rig& x) { x.bench.hold = 500; set_order(x, 1, 2, 3); }, uint32_t(orows));
    const int64_t out = stat(*h, Tracer::ST_STALL_OUTPUT);
    suite::check(okh && out > 0, "a slow controller: the scans stalled for %lld cycles", (long long)out);
    // one a cycle, whether one scan waits or both: no more than the packers' blocked cycles, nor than the drain's
    suite::check(out <= h->tr.scan_stalls && out <= h->tr.drained_at - h->tr.rows_end_at,
                 "counted once a cycle, in the drain (%lld of %lld blocked packer cycles)", (long long)out,
                 (long long)h->tr.scan_stalls);
    WindowOut of;
    bool okf = false;
    RigP f = streamy(of, okf, nullptr, uint32_t(orows));
    suite::eq(0, stat(*f, Tracer::ST_STALL_OUTPUT), "an ideal controller: no output stall");
    // the stall counters of the rows are the controller's business in neither: the same rows, the same counts
    suite::eq(stat(*f, Tracer::ST_STALL_FETCH), stat(*h, Tracer::ST_STALL_FETCH), "the rows' stalls don't depend on the drain's controller");
    // 600 iterations: the first instruction 6 + its fill, 600 branches a redirect each and the end's; the data: the
    // 600 doublewords are 38 lines (16 a line); the probe once
    suite::eq(6 + (L + 3) + 600 * 5 + 4, stat(*f, Tracer::ST_STALL_FETCH), "STAT_STALL_FETCH: 600 redirects of 5, the end's 4");
    suite::eq(38 * (L + 5), stat(*f, Tracer::ST_STALL_DATA), "STAT_STALL_DATA: 38 data lines' fills");
    suite::eq((L + 4) + 2, stat(*f, Tracer::ST_STALL_MAP), "STAT_STALL_MAP: one probe");
}

}  // namespace

int main(int argc, char**) {
    if (argc != 1) FATAL("usage: suite_tracer");
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
    return suite::done("TracerTest");
}
