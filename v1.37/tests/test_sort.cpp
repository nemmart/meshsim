// test_sort: the sort on v1.36's chip, end to end, under meshsim's firmware: the RTL's chip test of the sort (Snarky
// One's rtl/chip/test/src/chip_test.rs, with its bench.rs and sort.rs) on the simulator, its result checks on v1.35's
// firmware upgraded to v1.36 (fw/README.md).
//
// The chip is tests/mach.h's (MachConfig::chip(): units::build_chip's 64 compute tiles, master, memory and tracer tiles,
// request tree and nets). A run: every tile's input placed in its scratchpad; C&C 1's program (fw/cc/cc1.c,
// fw/images/cc/cc1.elf) placed in the master's IMEM and DMEM and the memory tile's (fw/cc/memory.c) in its own before
// the first cycle (their boot straps, machine-spec 6.5); in the master's scratchpad the sort's image
// (fw/images/sort/tile/sort.elf: its IMEM rows, and its DMEM rows behind its arguments' row, the records a tile and the
// format) and a row for the load's COUNT, and in cc1.c's boot_desc where they are, as v1.35's harness placed them; and
// in boot_desc.script C&C 1's commands, which v1.35's harness sent one at a time: CC_UART 1 (a fast dump), CC_PHASE
// with a release at each barrier of the image's schedule, CC_WAIT for the 64 tiles' results, CC_FAULT. So C&C 1 answers
// every LOAD with the sort's image (through the boot ROM), releases each barrier, answers each tile's result request,
// and then sends every compute tile and the memory tile a bad packet; the first error line takes it into its error path
// (cc1_toast), which drains every tile's reports into its REPORTS, records toast_info, dumps over the UART and parks.
// The test polls toast_info's last word every 512 cycles. Nothing here stands in for hardware: the tracer tile waits in
// its boot ROM for a load the master never answers, the memory tile's RW engine is idle, and has no LPDDR controller on
// its lpddr port (a job would stop the simulator: src/units/rw_engine.h; tests/test_memory.cpp is the memory run).
//
// THE CHECKS, chip_test.rs's, from the specification's values written here:
//   - C&C 1's mailbox: its three commands before CC_FAULT completed, the boot ended, 65 LOADs at the boot and one after
//     each tile's result, 64 results answered, no other request;
//   - every tile's final records, and with 128-bit values their values, READ FROM ITS SCRATCHPAD, against the oracle:
//     the global order by (key, tile, position), owner q holding ranks floor(q N / 64) to floor((q + 1) N / 64) - 1.
//     The oracle and the inputs are computed here from the key file, a port of sort.rs written apart from the design;
//     tests/sort_input.h's (meshsim's own SortTest port) is compared with it as a cross-check;
//   - the error path: C&C 1's NMI, every compute tile's and the memory tile's error line, their reports 24 and 25 (6.5),
//     each set whole and once, report 24's ERRORS bad_packet alone (so no tile erred in the sort) and its mcause the
//     NMI, and the toast pin (6.2, 7.4, 17.6, 18).
//
// THE CYCLES are printed: the run's, from the reset to the dump's end, and C&C 1's mcycle at L2's arrivals and release
// (its mailbox). They are the simulator's own figures for meshsim's firmware, compared with nothing here. (Chip-level
// cycle equality with the RTL, under the RTL's own firmware, was established at meshsim's commit 5a87d658 and is no
// longer checked: README.md, Status.)
//
// THE RUNS: sort_16_kv32 and sort_16_k128, 16 records a tile, always; sort_64k_kv32 and sort_64k_k128, 65,536 a tile,
// when SORT_64K_KEYS names the key file (16 MiB, not committed: `fwimage keys 65536 FILE` builds it, fw/README.md):
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
#include "units/ingress_egress.h"
#include "rtl_images.h"
#include "sort_input.h"
#include "units/big_sram.h"
#include "units/regmap.h"
#include "units/reqbus.h"

namespace {

constexpr unsigned T = units::TILE_COUNT;

const std::string kDir = rtl_images::fw_images();         // the images, built by fw/build.sh
const std::string kImages = kDir + "sort/";              // the tile image and the keys
// ---- C&C 1's program (fw/cc/cc1.c): its commands, boot_desc's words, its mailbox and toast_info ----
constexpr uint32_t CC_PHASE = 0x50, CC_UART = 0x55, CC_WAIT = 0x52, CC_FAULT = 0x46;
constexpr uint32_t command(uint32_t type, uint32_t arg) { return type << 24 | arg; }
enum : unsigned {
    BOOT_MAGIC = 0, BOOT_UNIT = 1, IMAGE_WORDS = 6,              // unit u: pmem, pmem_packets, dmem, dmem_packets, cmd, entry
    BOOT_GO_LATENCY = 19, BOOT_UART = 20, BOOT_SCRIPT = 21,      // then 32 commands of 4 words
};
constexpr uint32_t BOOT_DESC_MAGIC = 0xB007DE5C;
enum : unsigned {
    MBOX_COMMANDS = 0, MBOX_ARRIVED = 1, MBOX_RELEASED = 2, MBOX_BOOT = 3, MBOX_LOADS = 4, MBOX_RESULTS = 6, MBOX_OTHERS = 7,
    TOAST_CAUSE = 0, TOAST_LINES = 2, TOAST_REPORTS = 5, TOAST_BEGUN = 6, TOAST_DONE = 7, TOAST_FLAGS = 8, TOAST_END = 10,
};
constexpr uint32_t REPORTS_BASE = 0x1000;                  // cc1.c's REPORTS: four words a report
// Where the test puts the image in the master's scratchpad (word addresses, rows of 4 words).
constexpr uint32_t PMEM_CHUNK = 0x0000, DMEM_CHUNK = 0x4000, COUNT_CHUNK = 0x8000;

// ---- the specification's values ----
constexpr uint32_t CAUSE_NMI = 0x80000000;                 // mcause of an NMI (6.2)
constexpr uint32_t ERROR_BAD_PACKET = 1u << 1;             // ERRORS.bad_packet (7.4)
constexpr unsigned MEMORY_LINE_WORD = (T + 1) / 32;        // the memory tile's bit of ERROR_LINES: tile 65's (17.6)
constexpr uint32_t MEMORY_LINE_BIT = 1u << ((T + 1) % 32);
constexpr uint32_t REPORT_ERRORS = 24, REPORT_MTVAL = 25;  // the boot ROM's reports (6.5)
constexpr uint32_t OP_REPORT = 1;                          // 11.5
constexpr unsigned LAYERS = 5, SPLIT_BARRIERS = 66;        // the tile image's schedule (sort/program/sw/main.c)

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
    const rtl_images::Section s = rtl_images::section(kImages + "tile/sort.elf", ".sort_layout");
    if (!s.found || s.data.size() != 20) FATAL("%stile/sort.elf: no .sort_layout of 5 words", kImages.c_str());
    const std::vector<uint32_t> w = rtl_images::words(s.data);
    if (w[0] != 0x4C41594F) FATAL("%stile/sort.elf: .sort_layout's magic 0x%x", kImages.c_str(), w[0]);
    return {w[1], w[2], w[3], w[4]};
}

void place_scratchpad(mach::Mach& m, unsigned tile, const Region& r) {
    units::BigSram& s = m.c.unit<units::BigSram>(units::tile_unit(tile, "sram"));
    if (r.address % 16) FATAL("a region starts a row");
    for (size_t i = 0; i < r.words.size(); i++) s.poke(r.address / 4 + uint32_t(i), r.words[i]);
    m.c.touched(units::tile_unit(tile, "sram"));
}

// The image's rows for the master's scratchpad: its IMEM rows from IMEM's base, and its DMEM rows from DMEM_FIRST, the
// arguments' row (the records a tile, the format) first, as the boot ROM's load lands them (6.5).
struct ImageRows { std::vector<uint32_t> pmem, dmem; };
ImageRows image_rows(const std::string& elf, uint32_t records, uint32_t format) {
    ImageRows r;
    r.dmem = {records, format, 0, 0};
    for (const rtl_images::Row& row : rtl_images::program_rows(elf)) {
        std::vector<uint32_t>& to = row.address >= units::IMEM_BASE ? r.pmem : r.dmem;
        const uint32_t base = row.address >= units::IMEM_BASE ? uint32_t(units::IMEM_BASE) : 0x10u;
        if (row.address - base != 4 * to.size()) FATAL("%s: its rows are not contiguous from the image's base", elf.c_str());
        for (uint64_t half : {row.lo, row.hi}) { to.push_back(uint32_t(half)); to.push_back(uint32_t(half >> 32)); }
    }
    return r;
}

// `parts` > 1: the chip split by bands of rows over as many threads (mach.h); `digests`: the machine's digest at every
// poll, for the comparison of a split run with one domain's.
void run_sort(Format format, bool full, unsigned parts = 1, std::vector<uint64_t>* digests = nullptr) {
    const std::string name = std::string(format == KV32 ? "kv32" : "k128") + (full ? "-64k" : "") +
                             (parts > 1 ? " on " + std::to_string(parts) + " threads" : "");
    const char* tag = name.c_str();
    std::string keys_path = kImages + "sort.keys";
    if (full) {
        const char* env = std::getenv("SORT_64K_KEYS");
        if (!env || !*env) {
            std::printf("test_sort: %s: SKIPPED: SORT_64K_KEYS does not name the 64K key file (16 MiB, built by "
                        "`fwimage keys 65536 FILE`: fw/README.md; run_tests.sh's step sort-64k builds it)\n", tag);
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
    const std::string cc1_elf = kDir + "cc/cc1.elf", memory_elf = kDir + "cc/memory.elf";
    CHECK_EQ(rtl_images::entry(cc1_elf), uint32_t(units::IMEM_BASE));
    CHECK_EQ(rtl_images::entry(memory_elf), uint32_t(units::IMEM_BASE));
    const uint32_t boot_desc = rtl_images::elf_symbol(cc1_elf, "boot_desc"), mbox = rtl_images::elf_symbol(cc1_elf, "mbox");
    const uint32_t toast_info = rtl_images::elf_symbol(cc1_elf, "toast_info");
    mach::MachConfig mc = mach::Mach::chip_cfg(-1);
    mc.parts = parts; mc.threads = parts;
    mach::Mach m(mc);
    if (parts > 1) std::printf("test_sort: %s: %s\n", tag, m.c.sim().partition().c_str());
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
    m.place_elf(units::ID_MASTER, cc1_elf);
    m.place_elf(units::ID_MEMORY, memory_elf);
    const ImageRows rows = image_rows(kImages + "tile/sort.elf", uint32_t(keys[0].size()), format);
    place_scratchpad(m, units::ID_MASTER, Region{4 * PMEM_CHUNK, rows.pmem});
    place_scratchpad(m, units::ID_MASTER, Region{4 * DMEM_CHUNK, rows.dmem});
    place_scratchpad(m, units::ID_MASTER, Region{4 * COUNT_CHUNK, {0, 0, 0, 0}});
    std::vector<uint32_t> desc(BOOT_SCRIPT + 4 * 4, 0);
    desc[BOOT_MAGIC] = BOOT_DESC_MAGIC;
    const uint32_t tiles_image[IMAGE_WORDS] = {PMEM_CHUNK, uint32_t(rows.pmem.size() / 4), DMEM_CHUNK, uint32_t(rows.dmem.size() / 4), COUNT_CHUNK, 0};
    for (unsigned i = 0; i < IMAGE_WORDS; i++) desc[BOOT_UNIT + 2 * IMAGE_WORDS + i] = tiles_image[i];   // unit 2, the tiles
    desc[BOOT_GO_LATENCY] = 1;
    const uint32_t releases = uint32_t(schedule(format).size());        // a release at each barrier of the image's schedule
    const uint32_t script[4] = {command(CC_UART, 1), command(CC_PHASE, releases << 16), command(CC_WAIT, T), command(CC_FAULT, 0)};
    for (unsigned i = 0; i < 4; i++) desc[BOOT_SCRIPT + 4 * i] = script[i];
    m.place(units::ID_MASTER, boot_desc, desc);

    // ---- the run: to the end of C&C 1's dump ----
    uint64_t cycle = 0;
    bool finished = false;
    while (!finished && cycle < CYCLE_LIMIT) {
        m.c.run(POLL_CYCLES);
        cycle += POLL_CYCLES;
        finished = m.word(units::ID_MASTER, toast_info + 4 * TOAST_END) != 0;
        if (digests) digests->push_back(m.c.sim().digest());
    }
    auto mb = [&](unsigned i) { return m.word(units::ID_MASTER, mbox + 4 * i); };
    auto toast = [&](unsigned i) { return m.word(units::ID_MASTER, toast_info + 4 * i); };
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    std::printf("test_sort: %s: %s %llu cycles from the reset (%.1f s); L2's arrivals at mcycle %u, its release at %u\n", tag,
                finished ? "C&C 1's dump ended within" : "no end of C&C 1's dump in", (unsigned long long)cycle, wall, mb(MBOX_ARRIVED), mb(MBOX_RELEASED));
    std::printf("test_sort: %s: mailbox: %u commands, boot %u, %u LOADs, %u results, %u others; toast: mcause 0x%x, %u reports, %u sets "
                "begun, %u done, flags 0x%x\n", tag, mb(MBOX_COMMANDS), mb(MBOX_BOOT), mb(MBOX_LOADS), mb(MBOX_RESULTS), mb(MBOX_OTHERS),
                toast(TOAST_CAUSE), toast(TOAST_REPORTS), toast(TOAST_BEGUN), toast(TOAST_DONE), toast(TOAST_FLAGS));
    if (!finished) {                                                    // what the chip shows of a run that failed
        for (unsigned u = 0; u < T + 3; u++) {
            const uint32_t e = m.c.tile(u).errors();
            if (e) std::printf("test_sort: %s: unit %u: ERRORS 0x%x, pc 0x%08x\n", tag, u, e, m.c.tile(u).pc());
        }
        const units::reqbus::ReqRecv& q = m.c.unit<units::reqbus::ReqRecv>("cc1.reqrecv");
        for (unsigned i = 0; i < q.qn && i < 8; i++) {
            const auto& e = q.q[(q.qh + i) % q.depth];
            std::printf("test_sort: %s: the master's receive queue [%u]: 0x%08x 0x%08x 0x%08x 0x%08x (requester %u, op %u)\n", tag, i,
                        e.w[0], e.w[1], e.w[2], e.w[3], e.w[0] & 0x7F, e.w[0] >> 10 & 0xFF);
        }
    }
    CHECK(finished);

    // ---- C&C 1's mailbox ----
    CHECK_EQ(mb(MBOX_COMMANDS), 3u);                                    // CC_UART, CC_PHASE, CC_WAIT; CC_FAULT never ends
    CHECK_EQ(mb(MBOX_BOOT), 2u);                                        // the boot ran to its release
    CHECK_EQ(mb(MBOX_LOADS), 2 * T + 1);                                // the boot's 64 + the tracer tile's, then one a tile's end
    CHECK_EQ(mb(MBOX_RESULTS), T);                                      // every tile's result request answered
    CHECK_EQ(mb(MBOX_OTHERS), 0u);
    // the dump went out on the UART line (v1.37, 30.2: the UART is off at the reset, and an image that prints turns it on)
    if (m.ingress().serial_n == 0) ::test::fail(__FILE__, __LINE__, "%s: the dump sent no character on the UART line", tag);

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

    // ---- the error path: the NMI, the error lines, the reports, the toast pin ----
    CHECK_EQ(toast(TOAST_CAUSE), CAUSE_NMI);                           // C&C 1's trap: the NMI of its peripheral's error
    uint32_t every_tile[3] = {};
    every_tile[MEMORY_LINE_WORD] = MEMORY_LINE_BIT;
    for (unsigned tile = 0; tile < T; tile++) every_tile[tile / 32] |= 1u << (tile % 32);
    for (unsigned w = 0; w < 3; w++) CHECK_EQ(toast(TOAST_LINES + w), every_tile[w]);   // ERROR_LINES after the drain
    CHECK_EQ(toast(TOAST_REPORTS), 2 * (T + 1));                       // the reports 24 and 25 of 65 units
    CHECK_EQ(toast(TOAST_BEGUN), T + 1);
    CHECK_EQ(toast(TOAST_DONE), T + 1);
    CHECK_EQ(toast(TOAST_FLAGS), 0u);                                   // each set's numbers in it, once; nothing else
    CHECK(m.c.toast());                                                 // latched by the error lines (17.6, 18)
    std::vector<unsigned> seen(T + 2, 0);
    for (unsigned r = 0; r < 2 * (T + 1); r++) {
        uint32_t w[4];
        for (unsigned i = 0; i < 4; i++) w[i] = m.word(units::ID_MASTER, REPORTS_BASE + 16 * r + 4 * i);
        const unsigned who = w[0] & 0x7F, number = w[0] >> 18 & 0xFF;
        CHECK_EQ(w[0] >> 10 & 0xFF, OP_REPORT);
        CHECK(who < T || who == T + 1);
        if (number == REPORT_ERRORS) {
            CHECK_EQ(w[1], ERROR_BAD_PACKET);                           // report 24: ERRORS, bad_packet alone
            CHECK_EQ(w[2], CAUSE_NMI);                                  //   and mcause
        } else CHECK_EQ(number, REPORT_MTVAL);
        if (who < T + 2) seen[who]++;
    }
    for (unsigned u = 0; u < T + 2; u++) CHECK_EQ(seen[u], u == T ? 0u : 2u);   // two from each, none from the master
    std::printf("test_sort: %s passed (%.1f s)\n", tag, std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count());
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
        for (size_t i = 0; i < one.size(); i++) if (split[i] != one[i] && differ++ == 0) std::printf("test_sort: split %u: the digest differs first at cycle %zu\n", parts, 512 * (i + 1));
        CHECK_EQ(differ, size_t(0));
        std::printf("test_sort: kv32 on %u threads: %zu digests equal to one domain's\n", parts, one.size());
    }
}
TEST(sort_64k_kv32) { run_sort(KV32, true); }
TEST(sort_64k_k128) { run_sort(K128, true); }
