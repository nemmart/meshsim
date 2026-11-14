// test_rtl_sort: ACCEPTANCE B. The RTL's own sort images on v1.36's chip, unmodified, end to end: the RTL's chip test
// (Snarky One's rtl/chip/test/src/chip_test.rs, with its bench.rs and sort.rs) on the simulator.
//
// The chip is tests/mach.h's (MachConfig::chip(): units::build_chip's 64 compute tiles, master, memory and tracer tiles,
// request tree and nets). A run, as chip_test.rs's: every tile's input placed in its scratchpad, the master's firmware
// (fw/rtl/sort/<run>/master.elf: the run and the tile image linked in) and the memory tile's (fw/rtl/sort/memory-tile/
// memory.elf) placed in their IMEM and DMEM before the first cycle (their boot straps, machine-spec 6.5); then the chip
// runs. The master answers every compute tile's LOAD with the sort's image (fw/rtl/sort/tile/sort.elf, through the boot
// ROM), releases each barrier of the image's schedule, collects the exit requests, measures the links, provokes and
// checks every tile's error, and writes a summary into its DMEM, whose done word the test polls every 512 cycles.
// Nothing here stands in for hardware: the tracer tile waits in its boot ROM for a load the master never answers, the
// memory tile's RW engine is idle, and has no LPDDR controller on its lpddr port (a job would stop the simulator:
// src/units/rw_engine.h; tests/test_rtl_memory.cpp is the memory run).
//
// THE CHECKS, chip_test.rs's, from the specification's values written here:
//   - the summary: no failure, the run's kind, a release for the load and one at each barrier of the schedule, numbered
//     in order; an exit request from every compute tile, its own (11.3), no stray; each tile's exit words;
//   - every tile's final records, and with 128-bit values their values, READ FROM ITS SCRATCHPAD, against the oracle:
//     the global order by (key, tile, position), owner q holding ranks floor(q N / 64) to floor((q + 1) N / 64) - 1.
//     The oracle and the inputs are computed here from the key file, a port of sort.rs written apart from the design;
//     tests/sort_input.h's (meshsim's own SortTest port) is compared with it as a cross-check;
//   - the error lines, the master's NMI, the tiles' reports 24 and 25 and the toast pin (6.2, 6.5, 7.4, 17.6, 18);
//   - the links' latency: one hop is four cycles more (16.1), and at tile 0 it is the RTL chip's 36 cycles;
//   - THE CYCLES. It prints, as chip_test.rs does, each release's cycles, each step's (a step from its release to the
//     next step's, L2 to its all arrived) and the total from L1's release to L2's all arrived, and checks them equal to
//     the RTL chip's, as Snarky One's rtl/chip/README.md (Results) gives them for these four runs: every step, the
//     empty phases together, the total, the load from the reset to its release, and the image's start-up: 16,033
//     cycles from the load's release to every tile's arrival at the start-up's barrier, as the master records them
//     (the README's "the image's start-up 16,033", chip_test.rs's line for release 1), and 16,036 from the load's
//     release to L1's, which the README does not give: the RTL chip's summary in all four runs (phase 0 arrived at
//     20,780 and released at 20,784, phase 1 at 36,817 and 36,820), read from the master's DMEM on Snarky One's main at
//     076e647 (PENDING-v1.36.md, section 6, says how). The figures are the RTL's, written here; the simulator is not
//     tuned to them: its units are the specification's.
//
// THE RUNS: sort_16_kv32 and sort_16_k128, 16 records a tile, always; sort_64k_kv32 and sort_64k_k128, 65,536 a tile,
// when RTL_SORT_64K_KEYS names the key file (fw/rtl/README.md: 16 MiB, not committed; its sha256 beside the images):
// without it they print SKIPPED and pass.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "harness.h"
#include "mach.h"
#include "rtl_images.h"
#include "sort_input.h"
#include "units/big_sram.h"
#include "units/regmap.h"
#include "units/reqbus.h"

namespace {

constexpr unsigned T = units::TILE_COUNT;
const std::string kDir = "fw/rtl/sort/";

// ---- the summary (master.rs's Summary and SortSummary), by word, as chip_test.rs and bench.rs index it ----
enum : unsigned {
    SUMMARY_DONE = 0, SUMMARY_FAILURE = 1, SUMMARY_FAILED_PHASE = 2, SUMMARY_KIND = 3,
    SUMMARY_PHASES = 4, SUMMARY_EXITS = 5, SUMMARY_STRAYS = 6, SUMMARY_TRAP_CAUSE = 7, SUMMARY_TRAP_ERROR_LINES = 9,
    SUMMARY_ERROR_LINES = 12, SUMMARY_ERROR_REPORTS = 15, SUMMARY_EXIT = 16,
    MAX_PHASES = 128,
    SUMMARY_PHASE = SUMMARY_EXIT + 4 * T,
    SUMMARY_LATENCY = SUMMARY_PHASE + 3 * MAX_PHASES,
    SUMMARY_LINE_LATENCY = SUMMARY_LATENCY + T,
    SUMMARY_ERROR_REPORT = SUMMARY_LINE_LATENCY + T,
    SUMMARY_WORDS = (SUMMARY_ERROR_REPORT + 6 * (T + 1) + 3) / 4 * 4,
};
constexpr uint32_t DONE = 0x444F4E45, NONE = 0xFFFFFFFF;

// ---- the specification's values ----
constexpr uint32_t CAUSE_NMI = 0x80000000;                 // mcause of an NMI (6.2)
constexpr uint32_t ERROR_BAD_PACKET = 1u << 1;             // ERRORS.bad_packet (7.4)
constexpr unsigned MEMORY_LINE_WORD = (T + 1) / 32;        // the memory tile's bit of ERROR_LINES: tile 65's (17.6)
constexpr uint32_t MEMORY_LINE_BIT = 1u << ((T + 1) % 32);
constexpr unsigned LAYERS = 5, SPLIT_BARRIERS = 66;        // the tile image's schedule (sort/sw/main.c)
constexpr uint32_t DMEM_BYTES = 32768;
// A tile's exit request's word 0 (port.c; sort.rs's EXIT_WORD_0): op 2 at [17:10], target 0, the reply's counter 3 at
// [20:18] and its address over 16 at [31:21], the master tile's pair of the reply block
constexpr uint32_t EXIT_WORD_0 = 2u << 10 | 3u << 18 | ((DMEM_BYTES - 0x400 + 8 * T) >> 4) << 21;

constexpr uint64_t CYCLE_LIMIT = 4000000, POLL_CYCLES = 512;

enum Format { KV32 = 0, K128 = 1 };

std::vector<const char*> schedule(Format f) {
    std::vector<const char*> s = {"start-up", "L1"};
    for (unsigned i = 0; i < SPLIT_BARRIERS + 1; i++) s.push_back("SPLIT");
    for (const char* x : {"XB", "RED", "XC", "TH", "SWEEP", "DESC", "K"}) s.push_back(x);
    for (unsigned i = 0; i < LAYERS; i++) s.push_back("K1");
    for (unsigned i = 0; i < LAYERS; i++) s.push_back("K2");
    if (f == K128) {
        s.push_back("V");
        for (unsigned i = 0; i < LAYERS; i++) s.push_back("V1");
        for (unsigned i = 0; i < LAYERS; i++) s.push_back("V2");
    }
    s.push_back("L2");
    return s;
}

// ---- the input and the expected result (sort.rs), from the keys alone ----
uint32_t value32(uint32_t tile, uint32_t position) { return tile * 0x9E3779B1u + position * 0x85EBCA77u; }
uint64_t value_half(uint64_t tile, uint64_t position, uint64_t half) {
    uint64_t z = tile << 40 ^ position << 8 ^ half * 0x9E3779B97F4A7C15ull;
    z = (z ^ z >> 31) * 0xBF58476D1CE4E5B9ull;
    return z ^ z >> 29;
}
std::array<uint32_t, 4> value128_words(uint32_t tile, uint32_t position) {
    const uint64_t lo = value_half(tile, position, 0), hi = value_half(tile, position, 1);
    return {uint32_t(lo), uint32_t(lo >> 32), uint32_t(hi), uint32_t(hi >> 32)};
}
struct Region { uint32_t address; std::vector<uint32_t> words; };   // 32-bit words at a scratchpad byte address
struct Expected {
    std::vector<std::vector<std::array<uint32_t, 2>>> records;      // each owner's BUF_Y: key, then value32 or ordinal
    std::vector<std::vector<std::array<uint32_t, 4>>> values;       // each owner's BUF_VS (128-bit values)
};
// SortTest's oracle as sort.rs writes it: the global order by (key unsigned, tile, position); owner q holds ranks R_q to
// R_(q+1) - 1, R_q = floor(q N / tiles). With 128-bit values a record's high word is its receive ordinal, where it
// landed in the owner's BUF_Y before the owner's sort: the records arrive source-major, each source's in its own order.
Expected oracle(const std::vector<std::vector<uint32_t>>& keys, Format f) {
    const size_t tiles = keys.size();
    size_t total = 0;
    for (const auto& k : keys) total += k.size();
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> order;
    order.reserve(total);
    for (size_t t = 0; t < tiles; t++) for (size_t j = 0; j < keys[t].size(); j++) order.emplace_back(keys[t][j], uint32_t(t), uint32_t(j));
    std::sort(order.begin(), order.end());
    auto bound = [&](size_t owner) { return owner * total / tiles; };
    std::vector<uint32_t> owner_of(total);
    for (size_t q = 0; q < tiles; q++) for (size_t r = bound(q); r < bound(q + 1); r++) owner_of[r] = uint32_t(q);
    std::vector<std::vector<size_t>> counts(tiles, std::vector<size_t>(tiles, 0));   // counts[p][q]: tile p's records owned by q
    for (size_t r = 0; r < total; r++) counts[std::get<1>(order[r])][owner_of[r]]++;
    std::vector<std::vector<size_t>> next(tiles, std::vector<size_t>(tiles, 0));     // next[q][p]: the next ordinal of p's at q
    for (size_t q = 0; q < tiles; q++) { size_t start = 0; for (size_t p = 0; p < tiles; p++) { next[q][p] = start; start += counts[p][q]; } }
    Expected e;
    e.records.resize(tiles); e.values.resize(tiles);
    for (size_t r = 0; r < total; r++) {
        const auto [key, tile, position] = order[r];
        const uint32_t q = owner_of[r];
        const uint32_t high = f == KV32 ? value32(tile, position) : uint32_t(next[q][tile]++);
        e.records[q].push_back({key, high});
        if (f == K128) e.values[q].push_back(value128_words(tile, position));
    }
    return e;
}

// ---- one run ----
struct Layout { uint32_t buf_in, buf_vin, buf_y, buf_vs; };   // word addresses (the tile image's .sort_layout: port.c)
Layout read_layout() {
    const rtl_images::Section s = rtl_images::section(kDir + "tile/sort.elf", ".sort_layout");
    if (!s.found || s.data.size() != 20) FATAL("%stile/sort.elf: no .sort_layout of 5 words", kDir.c_str());
    const std::vector<uint32_t> w = rtl_images::words(s.data);
    if (w[0] != 0x4C41594F) FATAL("%stile/sort.elf: .sort_layout's magic 0x%x", kDir.c_str(), w[0]);
    return {w[1], w[2], w[3], w[4]};
}

void place_scratchpad(mach::Mach& m, unsigned tile, const Region& r) {
    units::BigSram& s = m.c.unit<units::BigSram>(units::tile_unit(tile, "sram"));
    if (r.address % 16) FATAL("a region starts a row");
    for (size_t i = 0; i < r.words.size(); i++) s.poke(r.address / 4 + uint32_t(i), r.words[i]);
    m.c.touched(units::tile_unit(tile, "sram"));
}

// `parts` > 1: the chip split by bands of rows over as many threads (mach.h); `digests`: the machine's digest at every
// poll, for the comparison of a split run with one domain's.
void run_sort(Format format, bool full, unsigned parts = 1, std::vector<uint64_t>* digests = nullptr) {
    const std::string name = std::string(format == KV32 ? "kv32" : "k128") + (full ? "-64k" : "") +
                             (parts > 1 ? " on " + std::to_string(parts) + " threads" : "");
    const std::string run_dir = std::string(format == KV32 ? "kv32" : "k128") + (full ? "-64k" : "");
    const char* tag = name.c_str();
    std::string keys_path = kDir + "sort.keys";
    if (full) {
        const char* env = std::getenv("RTL_SORT_64K_KEYS");
        if (!env || !*env) {
            std::printf("test_rtl_sort: %s: SKIPPED: RTL_SORT_64K_KEYS does not name the 64K key file (fw/rtl/README.md; its "
                        "sha256 in fw/rtl/sort/sort-64k.keys.sha256)\n", tag);
            return;
        }
        keys_path = env;
    }
    const auto wall0 = std::chrono::steady_clock::now();
    std::string err;
    const sortin::Keys keys = sortin::read_keys(keys_path, err);
    if (keys.size() != T) { ::test::fail(__FILE__, __LINE__, "%s: the keys: %s", tag, err.c_str()); return; }
    for (const auto& k : keys) CHECK_EQ(k.size(), full ? size_t(65536) : size_t(16));
    const Layout lay = read_layout();
    const Expected expected = oracle(keys, format);
    {   // the cross-check of this port against meshsim's own port of SortTest (tests/sort_input.h): values and oracle
        CHECK_EQ(uint64_t(value32(3, 5)), sortin::value32(3, 5));
        CHECK_EQ(value_half(63, 15, 1), sortin::value_half(63, 15, 1));
        CHECK_EQ(value32(3, 5), 2017550694u);                           // sort.rs's own tests' values (Java's)
        CHECK_EQ(value_half(3, 5, 0), 0xb7877253128ab9b5ull);
    }

    // ---- the chip, its images and its input, before the first cycle ----
    const std::string master_elf = kDir + run_dir + "/master.elf", memory_elf = kDir + "memory-tile/memory.elf";
    CHECK_EQ(rtl_images::entry(master_elf), uint32_t(units::IMEM_BASE));
    CHECK_EQ(rtl_images::entry(memory_elf), uint32_t(units::IMEM_BASE));
    const rtl_images::Section summary_section = rtl_images::section(master_elf, ".summary");
    CHECK(summary_section.found);
    CHECK(summary_section.size / 4 >= SUMMARY_WORDS);                   // the firmware's summary holds the layout written here
    mach::MachConfig mc = mach::Mach::chip_cfg(-1);
    mc.parts = parts; mc.threads = parts;
    mach::Mach m(mc);
    if (parts > 1) std::printf("test_rtl_sort: %s: %s\n", tag, m.c.sim().partition().c_str());
    for (unsigned tile = 0; tile < T; tile++) {
        const std::vector<uint32_t>& k = keys[tile];
        if (format == KV32) {
            Region r{4 * lay.buf_in, {}};
            for (uint32_t j = 0; j < k.size(); j++) { r.words.push_back(k[j]); r.words.push_back(value32(tile, j)); }
            place_scratchpad(m, tile, r);
        } else {
            place_scratchpad(m, tile, Region{4 * lay.buf_in, k});
            Region v{4 * lay.buf_vin, {}};
            for (uint32_t j = 0; j < k.size(); j++) for (uint32_t w : value128_words(tile, j)) v.words.push_back(w);
            place_scratchpad(m, tile, v);
        }
    }
    m.place_elf(units::ID_MASTER, master_elf);
    m.place_elf(units::ID_MEMORY, memory_elf);

    // ---- the run: to the master's done word ----
    const uint32_t done_address = summary_section.address + 4 * SUMMARY_DONE;
    uint64_t cycle = 0;
    bool finished = false;
    while (!finished && cycle < CYCLE_LIMIT) {
        m.c.run(POLL_CYCLES);
        cycle += POLL_CYCLES;
        finished = m.word(units::ID_MASTER, done_address) == DONE;
        if (digests) digests->push_back(m.c.sim().digest());
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    if (finished) std::printf("test_rtl_sort: %s ran in %llu cycles from the reset (%.1f s)\n", tag, (unsigned long long)cycle, wall);
    else std::printf("test_rtl_sort: %s: no summary in %llu cycles; what the master recorded:\n", tag, (unsigned long long)CYCLE_LIMIT);

    // ---- the summary: the releases, then the steps ----
    std::vector<uint32_t> summary(SUMMARY_WORDS);
    for (unsigned i = 0; i < SUMMARY_WORDS; i++) summary[i] = m.word(units::ID_MASTER, summary_section.address + 4 * i);
    const unsigned phases = std::min<unsigned>(summary[SUMMARY_PHASES], MAX_PHASES);
    auto phase = [&](unsigned i, unsigned k) { return summary[SUMMARY_PHASE + 3 * i + k]; };   // number, arrived at, released at
    const std::vector<const char*> sched = schedule(format);
    std::vector<std::pair<std::string, uint32_t>> steps;
    uint32_t total = 0;
    const bool verbose = std::getenv("RTL_SORT_RELEASES") != nullptr;
    for (unsigned i = 0; i < phases; i++) {
        const uint32_t since = i == 0 ? 0 : phase(i - 1, 2);
        const char* step = i < sched.size() ? sched[i] : "?";
        if (verbose)
            std::printf("test_rtl_sort: %s release %u (starting %s): all arrived %u cycles after the release before, released at %u\n",
                        tag, phase(i, 0), step, phase(i, 1) - since, phase(i, 2));
        // a step runs from its release to the next step's, the last, L2, to its all arrived; the start-up is not the sort's
        if (i + 1 < phases && i < sched.size() && std::string(step) != "start-up") {
            const uint32_t end = i + 1 == sched.size() ? phase(i + 1, 1) : phase(i + 1, 2);
            const uint32_t cycles = end - phase(i, 2);
            if (!steps.empty() && steps.back().first == step) steps.back().second += cycles; else steps.emplace_back(step, cycles);
            total += cycles;
        }
    }
    std::string report;
    uint32_t empty = 0;
    for (const auto& s : steps) {
        const bool named = s.first == "L1" || s.first == "SPLIT" || s.first == "XB" || s.first == "RED" || s.first == "K" || s.first == "V" || s.first == "L2";
        if (!named) empty += s.second;
        report += (report.empty() ? "" : ", ") + s.first + " " + std::to_string(s.second);
    }
    if (phases >= 2)
        std::printf("test_rtl_sort: %s the load, from the reset to its release: %u cycles; the image's start-up: all arrived %u "
                    "cycles after the load's release, L1 released %u after it\n",
                    tag, phase(0, 2), phase(1, 1) - phase(0, 2), phase(1, 2) - phase(0, 2));
    std::printf("test_rtl_sort: %s steps (each release to the next step's): %s\n", tag, report.c_str());
    std::printf("test_rtl_sort: %s the empty phases together (XC, TH, SWEEP, DESC, K1, K2%s): %u\n", tag, format == K128 ? ", V1, V2" : "", empty);
    std::printf("test_rtl_sort: %s total, L1's release to L2's all arrived: %u cycles\n", tag, total);
    // ---- the cycles against the RTL chip's (rtl/chip/README.md, Results: its chip_test's figures for the same runs) ----
    if (finished && summary[SUMMARY_FAILURE] == 0) {
        struct Rtl { const char* run; uint32_t l1, split, xb, red, k, v, l2, empty, total; };
        static const Rtl kRtl[] = {
            {"kv32", 7518, 118367, 9856, 6818, 263, 0, 6841, 598, 150261},
            {"k128", 7646, 118367, 10176, 7474, 1047, 267, 6968, 1068, 153013},
            {"kv32-64k", 335118, 259473, 9856, 6664, 71524, 0, 268921, 598, 952154},
            {"k128-64k", 475646, 259473, 10176, 7260, 72308, 71528, 409448, 1068, 1306907},
        };
        const Rtl* r = nullptr;
        for (const Rtl& x : kRtl) if (run_dir == x.run) r = &x;
        CHECK(r != nullptr);
        auto step_cycles = [&](const char* name) { for (const auto& s : steps) if (s.first == name) return s.second; return 0u; };
        CHECK_EQ(step_cycles("L1"), r->l1);
        CHECK_EQ(step_cycles("SPLIT"), r->split);
        CHECK_EQ(step_cycles("XB"), r->xb);
        CHECK_EQ(step_cycles("RED"), r->red);
        CHECK_EQ(step_cycles("K"), r->k);
        CHECK_EQ(step_cycles("V"), r->v);
        CHECK_EQ(step_cycles("L2"), r->l2);
        CHECK_EQ(empty, r->empty);
        CHECK_EQ(total, r->total);
        CHECK_EQ(phase(0, 1), 20780u);                                  // the load: every tile arrived in its boot ROM,
        CHECK_EQ(phase(0, 2), 20784u);                                  //   and its release, from the reset
        CHECK_EQ(phase(1, 1) - phase(0, 2), 16033u);                    // the image's start-up, to every tile's arrival
        CHECK_EQ(phase(1, 2) - phase(0, 2), 16036u);                    //   and to L1's release
    }
    std::printf("test_rtl_sort: %s summary: failure 0x%x in phase %u, %u exits, %u stray, mcause 0x%x, %u error reports\n", tag,
                summary[SUMMARY_FAILURE], summary[SUMMARY_FAILED_PHASE], summary[SUMMARY_EXITS], summary[SUMMARY_STRAYS],
                summary[SUMMARY_TRAP_CAUSE], summary[SUMMARY_ERROR_REPORTS]);
    if (!finished || summary[SUMMARY_FAILURE] != 0) {                   // what the chip shows of a run that failed
        for (unsigned u = 0; u < T + 3; u++) {
            const uint32_t e = m.c.tile(u).errors();
            if (e) std::printf("test_rtl_sort: %s: unit %u: ERRORS 0x%x, pc 0x%08x\n", tag, u, e, m.c.tile(u).pc());
        }
        const units::reqbus::ReqRecv& q = m.c.unit<units::reqbus::ReqRecv>("cc1.reqrecv");
        for (unsigned i = 0; i < q.qn && i < 8; i++) {
            const auto& e = q.q[(q.qh + i) % q.depth];
            std::printf("test_rtl_sort: %s: the master's receive queue [%u]: 0x%08x 0x%08x 0x%08x 0x%08x (requester %u, op %u)\n", tag, i,
                        e.w[0], e.w[1], e.w[2], e.w[3], e.w[0] & 0x7F, e.w[0] >> 10 & 0xFF);
        }
    }
    CHECK(finished);
    CHECK_EQ(summary[SUMMARY_KIND], 0u);                                // the run's kind, a sort
    CHECK_EQ(summary[SUMMARY_FAILURE], 0u);
    CHECK_EQ(phases, unsigned(sched.size() + 1));                       // the load's release and one at each barrier
    for (unsigned i = 0; i < phases; i++) CHECK_EQ(phase(i, 0), i);

    // ---- every tile's exit request ----
    CHECK_EQ(summary[SUMMARY_EXITS], T);
    CHECK_EQ(summary[SUMMARY_STRAYS], 0u);
    unsigned exit_bad = 0;
    for (unsigned tile = 0; tile < T; tile++) {
        const uint32_t want[4] = {EXIT_WORD_0 | tile, 0, uint32_t(expected.records[tile].size()), 0};
        for (unsigned j = 0; j < 4; j++) {
            const uint32_t got = summary[SUMMARY_EXIT + 4 * tile + j];
            if (got != want[j] && exit_bad++ < 16) std::printf("%s: tile %u exit word %u is 0x%08x, expected 0x%08x\n", tag, tile, j, got, want[j]);
        }
    }
    CHECK_EQ(exit_bad, 0u);

    // ---- every tile's final records, read from its scratchpad ----
    uint64_t word_bad = 0;
    for (unsigned tile = 0; tile < T; tile++) {
        const units::BigSram& s = m.c.unit<units::BigSram>(units::tile_unit(tile, "sram"));
        auto compare = [&](uint32_t word, uint32_t want) {
            const uint32_t got = s.peek(word);
            if (got != want && word_bad++ < 16) std::printf("%s: tile %u: scratchpad byte 0x%x is 0x%08x, expected 0x%08x\n", tag, tile, 4 * word, got, want);
        };
        for (size_t i = 0; i < expected.records[tile].size(); i++)
            for (unsigned w = 0; w < 2; w++) compare(lay.buf_y + uint32_t(2 * i + w), expected.records[tile][i][w]);
        for (size_t i = 0; i < expected.values[tile].size(); i++)
            for (unsigned w = 0; w < 4; w++) compare(lay.buf_vs + uint32_t(4 * i + w), expected.values[tile][i][w]);
    }
    CHECK_EQ(word_bad, uint64_t(0));

    // ---- the error lines, the master's NMI, the toast pin and the reports ----
    CHECK_EQ(summary[SUMMARY_TRAP_CAUSE], CAUSE_NMI);                   // the master's trap: the memory tile's error's NMI
    uint32_t memory_alone[3] = {}, every_tile[3] = {};
    memory_alone[MEMORY_LINE_WORD] = MEMORY_LINE_BIT;
    every_tile[MEMORY_LINE_WORD] = MEMORY_LINE_BIT;
    for (unsigned tile = 0; tile < T; tile++) every_tile[tile / 32] |= 1u << (tile % 32);
    for (unsigned w = 0; w < 3; w++) {
        CHECK_EQ(summary[SUMMARY_TRAP_ERROR_LINES + w], memory_alone[w]);   // ERROR_LINES at the NMI
        CHECK_EQ(summary[SUMMARY_ERROR_LINES + w], every_tile[w]);          // and after the line checks
    }
    for (unsigned tile = 0; tile < T; tile++) CHECK(summary[SUMMARY_LINE_LATENCY + tile] != NONE);   // every error line rose
    CHECK(m.c.toast());                                                 // latched by the master's own error line
    CHECK_EQ(summary[SUMMARY_ERROR_REPORTS], 2 * (T + 1));              // the reports 24 and 25 received
    for (unsigned tile = 0; tile <= T; tile++) {
        CHECK_EQ(summary[SUMMARY_ERROR_REPORT + 6 * tile], ERROR_BAD_PACKET);   // report 24: ERRORS, bad_packet alone
        CHECK_EQ(summary[SUMMARY_ERROR_REPORT + 6 * tile + 1], CAUSE_NMI);      // and mcause
    }

    // ---- the links' latency (none, one and two hops each way from tile 0) ----
    for (unsigned tile = 0; tile < T; tile++) CHECK(summary[SUMMARY_LATENCY + tile] != NONE);
    const uint32_t zero_hops = summary[SUMMARY_LATENCY];
    std::printf("test_rtl_sort: %s: the load's COUNT to tile 0 and its arrival back, %u cycles (the RTL's chip: 36)\n", tag, zero_hops);
    CHECK_EQ(zero_hops, 36u);                                           // rtl/chip/README.md: "36 cycles at tile 0, 4 more a hop"
    for (unsigned way = 0; way < 4; way++) {
        auto destination = [&](unsigned hops) { return way == 0 ? hops : way == 1 ? T - hops : way == 2 ? 8 * hops : T - 8 * hops; };
        const unsigned one = destination(1), two = destination(2);
        const uint32_t one_hop = summary[SUMMARY_LATENCY + one], two_hops = summary[SUMMARY_LATENCY + two];
        std::printf("test_rtl_sort: %s: to tile %u (one hop) %u cycles, to tile %u (two) %u\n", tag, one, one_hop, two, two_hops);
        CHECK_EQ(two_hops, one_hop + 4);
        CHECK_EQ(one_hop, zero_hops + 4);
    }
    std::printf("test_rtl_sort: %s passed in %llu cycles (%.1f s)\n", tag, (unsigned long long)cycle,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count());
}

}  // namespace

// The test's own oracle (sort.rs's unit test): two owners, ties across tiles and within one.
TEST(sort_oracle_orders_by_key_tile_and_position) {
    const std::vector<std::vector<uint32_t>> keys = {{5, 1, 5}, {5, 0, 9}};
    const Expected kv = oracle(keys, KV32);
    CHECK_EQ(kv.records[0].size(), size_t(3));
    CHECK_EQ(kv.records[0][0][0], 0u); CHECK_EQ(kv.records[0][1][0], 1u); CHECK_EQ(kv.records[0][2][0], 5u);
    CHECK_EQ(kv.records[1][0][0], 5u); CHECK_EQ(kv.records[1][1][0], 5u); CHECK_EQ(kv.records[1][2][0], 9u);
    // owner 0: (0, tile 1, 1), (1, tile 0, 1), (5, tile 0, 0); owner 1: (5, tile 0, 2), (5, tile 1, 0), (9, tile 1, 2)
    CHECK_EQ(kv.records[0][2][1], value32(0, 0));
    CHECK_EQ(kv.records[1][0][1], value32(0, 2));
    CHECK_EQ(kv.records[1][1][1], value32(1, 0));
    const Expected k = oracle(keys, K128);
    // owner 0 receives tile 0's two records first (ordinals 0, 1), then tile 1's one (2)
    CHECK_EQ(k.records[0][0][1], 2u); CHECK_EQ(k.records[0][1][1], 0u); CHECK_EQ(k.records[0][2][1], 1u);
    CHECK_EQ(k.records[1][0][1], 0u); CHECK_EQ(k.records[1][1][1], 1u); CHECK_EQ(k.records[1][2][1], 2u);
    CHECK(k.values[1][2] == value128_words(1, 2));
}

TEST(sort_16_kv32) { run_sort(KV32, false); }
TEST(sort_16_k128) { run_sort(K128, false); }
// The chip split over threads (src/units/chip.h, THE PARTITION: the nets and the request tree cross domains at latency
// 1, so the barrier interval is 1): the same run, the same checks, and the machine's digest at every poll (512 cycles)
// equal to one domain's.
TEST(sort_16_kv32_split) {
    std::vector<uint64_t> one, split;
    run_sort(KV32, false, 1, &one);
    for (unsigned parts : {2u, 8u}) {
        split.clear();
        run_sort(KV32, false, parts, &split);
        CHECK_EQ(split.size(), one.size());
        size_t differ = 0;
        for (size_t i = 0; i < one.size(); i++) if (split[i] != one[i] && differ++ == 0) std::printf("test_rtl_sort: split %u: the digest differs first at cycle %zu\n", parts, 512 * (i + 1));
        CHECK_EQ(differ, size_t(0));
        std::printf("test_rtl_sort: kv32 on %u threads: %zu digests equal to one domain's\n", parts, one.size());
    }
}
TEST(sort_64k_kv32) { run_sort(KV32, true); }
TEST(sort_64k_k128) { run_sort(K128, true); }
