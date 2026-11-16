// test_memory: the memory tile in the chip, under meshsim's firmware: the RTL's chip test of the memory tile (Snarky
// One's rtl/chip/test/src/chip_memory_test.rs, with its bench.rs) on the simulator.
//
// The chip is tests/mach.h's (MachConfig::chip()). The master tile's firmware for the memory run
// (fw/cc/cc_mem.c, fw/images/cc/cc_mem.elf) and the memory tile's (fw/cc/memory.c, fw/images/cc/memory.elf) are placed in their IMEM
// and DMEM before the first cycle (their boot straps, machine-spec 6.5), and the test plays the LPDDR controller on the
// RW engine's lpddr port, the chip's boundary (src/units/rw_engine.h): tests/axi4_slave.h's ChipLpddr, the RTL test's
// AXI4 slave, one ID, answering in order, over a memory whose beats are a function of their index until written. The
// master (cc_mem.c):
//   - sends the memory tile a READ of READ_BEATS beats of LPDDR from beat address READ_LPDDR into its summary, with a
//     reply;
//   - a WRITE of WRITE_BEATS beats to beat address WRITE_LPDDR, whose reply names a part of the write buffer and the
//     memory tile's counter, then those beats, 128-bit WRITEs into the part on that counter, each word a function of
//     its beat and its place (written_word);
//   - a READ of those beats back, with a reply;
//   - CREDIT_REQUESTS WRITEs of 0 beats, more than the memory target's receive queue holds, three outstanding at a
//     time, each with a reply into a slot of its own: the request tree's root has a credit for each only if the memory
//     tile returns one as it pops each request (11.3).
// Each READ and WRITE crosses a 4 KiB boundary of LPDDR, which no burst may (3, 17.7).
//
// THE CHECKS, chip_memory_test.rs's, from the specification's values written here: no failure; the WRITE's reply, one
// 128-bit reply (11.4, 11.5): the part's address in the memory tile's region 0x04 in [31:0], a counter of 1-7 in
// [34:32], and the bits above 0; each READ's reply, a 128-bit 0; the first READ's beats the model's words at their
// addresses, and the second's the written words; every credits' request's reply, as the WRITE's; on the lpddr port,
// every read burst and every write burst INCR of 16-byte beats, none crossing a 4 KiB boundary, their beats in order
// covering the READs' and the WRITE's addresses exactly, the reads before and after the write as the requests were
// served, and nothing for the WRITEs of 0 beats, each written beat's data its words with every byte enabled, WLAST on
// each burst's last beat; handshake monitors on AR, AW and W (R and B need none: RREADY and BREADY are 1 by the
// model's contract, so the slave's beats and responses are taken in the cycle it gives them); the toast pin low, so no
// tile's error line rose (17.6); all in fewer than CYCLE_LIMIT cycles.
//
// THE CYCLES are printed: the first burst's, the last handshake's and the done word's. They are the simulator's own
// figures for meshsim's firmware (README.md, Status), compared with nothing here. (Under the RTL's own images the
// simulator gave the RTL chip's handshakes on this port cycle for cycle; the RW engine alone is still compared with
// the RTL's, tests/test_rw_engine.cpp.) The slave's time starts three cycles before the simulator's cycle 2, as the
// RTL test's does (its reset): its random sequence is the RTL test's.
//
// From rtl/tile/memory/test/src/memory_tile_test.rs (the memory tile alone at the chip's boundary: a program written in
// the test in place of the firmware, the root and the attach tile's router played, an AXI4 slave; its parts A to D).
// What it checks of the tile's composition is checked here in the chip, by the RTL's firmware: the straps (the boot
// from IMEM, the receive queue, the peripheral with its DMA), a request's path to the core and its credit back (the 300
// requests), the engine's registers through the register bus (the firmware's stores and loads), the events to the
// tile's EVENTS.peripheral_1 (the firmware sleeps on them), the write port from the tile's receive unit into the write
// buffer, counted on the memory tile's counter (the WRITE's beats), and the DMA's input to the router with the tile's
// id as src (the answers reach the master by their dst and are counted there). NOT TAKEN, each for want of a one-tile
// bench for a special tile, which the simulator has not (OPEN-v1.36.md, (b)):
// its part B's read under a stalled attach output (the DMA waiting for the router's ready, queue 0 holding only what
// it has room for: test_rw_engine has it at the slot's port, with the router input played; with the simulator's router
// behind it the cycles would differ, PENDING section 5); part C's 64-bit and 32-bit region WRITEs through the tile
// (test_rw_engine's write port and slot tests have the three sizes; the chip's firmware sends 128-bit beats); part D's
// LPDDR error to ERRORS.peripheral_1, the NMI, the reports 24 and 25 and the error line (test_rw_engine has the
// engine's bit and the slot's live error bit; test_sort the NMI, the reports and the lines for the tile's other
// errors); and its registers read back at their widths through the tile (test_rw_engine, at three configurations).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "axi4_slave.h"
#include "harness.h"
#include "mach.h"
#include "rtl_images.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/rw_engine.h"

namespace {

using axi4::ChipLpddr;
using axi4::u128;

const std::string kDir = rtl_images::fw_images();

// ---- the run (the master's program, fw/cc/cc_mem.c) ----
constexpr unsigned READ_BEATS = 20, WRITE_BEATS = 24;
constexpr uint64_t READ_LPDDR = 0x0FF8, WRITE_LPDDR = 0x20F4;
constexpr unsigned CREDIT_REQUESTS = 300;
constexpr unsigned MEMORY_QUEUE_DEPTH = 256;            // the memory target's receive queue, its credits, at 8 x 8 (11.3)
static_assert(CREDIT_REQUESTS > MEMORY_QUEUE_DEPTH);
uint32_t written_word(uint32_t beat, uint32_t w) { return 0x57000000 | beat << 8 | w; }   // word w of written beat `beat`

// ---- the summary (cc_mem.c's mem_summary): its words, by index ----
enum : unsigned {
    SUMMARY_DONE = 0, SUMMARY_FAILURE = 1, SUMMARY_KIND = 3,
    SUMMARY_PART = 4, SUMMARY_REPLIES = 8, SUMMARY_READ = 16,
    SUMMARY_READ_BACK = SUMMARY_READ + 4 * READ_BEATS,
    SUMMARY_ANSWERS = SUMMARY_READ_BACK + 4 * WRITE_BEATS,
    SUMMARY_WORDS = SUMMARY_ANSWERS + 4 * CREDIT_REQUESTS,
};
constexpr uint32_t DONE = 0x444F4E45, KIND_MEMORY = 1;
constexpr uint32_t WRITE_BUFFER = 0x04000000;           // the memory tile's region 0x04, its write buffer (17.7)
constexpr unsigned LPDDR_ADDRESS_BITS = 32;             // 19
constexpr uint64_t CYCLE_LIMIT = 80000;
constexpr uint32_t SLAVE_SEED = 0x24681357;             // the RTL test's
constexpr int64_t RTL_RESET_CYCLES = 3;                 // bench.rs: the RTL's slave draws in them too
constexpr int64_t CYCLE_OFFSET = 1;                     // the simulator's cycle t is the RTL test's cycle `now` = t - 1 (header)

// A 16-byte beat's four 32-bit words, word 0 first.
std::vector<uint32_t> beat_words(u128 beat) { return {uint32_t(beat), uint32_t(beat >> 32), uint32_t(beat >> 64), uint32_t(beat >> 96)}; }

// The bursts' beats, in order, each burst checked: INCR, 16-byte beats, no 4 KiB boundary crossed.
bool burst_beats(const std::vector<ChipLpddr::Burst>& bursts, std::vector<uint64_t>& beats) {
    bool ok = true;
    for (const ChipLpddr::Burst& b : bursts) {
        if (b.size != axi4::SIZE_16_BYTES || b.kind != axi4::BURST_INCR) ok = false;
        const uint64_t first = b.beat, last = b.beat + b.beats - 1;
        if (first >> 8 != last >> 8) ok = false;                // a 4 KiB boundary is every 256 beats
        for (uint64_t x = first; x <= last; x++) beats.push_back(x);
    }
    return ok;
}
// A WRITE's reply, its four words (11.5): the part's address in the memory tile's region 0x04 in [31:0], a counter of
// 1-7 in [34:32], and the bits above 0 (11.4).
bool write_reply_ok(const uint32_t* w) { return (w[0] & 0xFF000000) == WRITE_BUFFER && w[1] >= 1 && w[1] <= 7 && w[2] == 0 && w[3] == 0; }

// One run: `fixed` < 0 the RTL test's random slave, else every ready 1 and each latency `fixed`.
void run_memory(const char* tag, int fixed) {
    const std::string master_elf = kDir + "cc/cc_mem.elf", memory_elf = kDir + "cc/memory.elf";
    CHECK_EQ(rtl_images::entry(master_elf), uint32_t(units::IMEM_BASE));
    CHECK_EQ(rtl_images::entry(memory_elf), uint32_t(units::IMEM_BASE));
    const uint32_t summary_address = rtl_images::elf_symbol(master_elf, "mem_summary");
    mach::Mach m(mach::Mach::chip_cfg(-1));
    m.place_elf(units::ID_MASTER, master_elf);
    m.place_elf(units::ID_MEMORY, memory_elf);
    // the LPDDR controller on the chip's lpddr port; its time starts with the RTL test's reset, three cycles before the
    // RTL test's cycle 0
    ChipLpddr lpddr(SLAVE_SEED, LPDDR_ADDRESS_BITS, CYCLE_OFFSET - RTL_RESET_CYCLES);
    if (fixed >= 0) { lpddr.fixed = true; lpddr.fixed_latency = uint32_t(fixed); }
    units::RwEngine& engine = m.c.unit<units::PeriphSlot>("cc2.slot").s.rw_engine(1);
    engine.attach(&lpddr);

    // ---- the run, until the master's done word: read after every cycle, for its cycle ----
    const uint32_t done_address = summary_address + 4 * SUMMARY_DONE;
    uint64_t now = 0;
    bool done = false;
    while (!done && now < CYCLE_LIMIT) {
        m.c.run(1);
        now++;
        done = m.word(units::ID_MASTER, done_address) == DONE;
    }
    std::printf("test_memory: %s: %s after %llu cycles; the engine's jobs %llu, bursts %llu, beats read %llu, written %llu, replies %llu, "
                "port writes %llu; the slave was asked in %llu cycles\n", tag, done ? "the done word" : "NO done word", (unsigned long long)now,
                (unsigned long long)engine.jobs, (unsigned long long)engine.bursts, (unsigned long long)engine.beats_read,
                (unsigned long long)engine.beats_written, (unsigned long long)engine.replies, (unsigned long long)engine.port_writes,
                (unsigned long long)lpddr.calls);
    CHECK(done);

    // ---- the summary ----
    std::vector<uint32_t> summary(SUMMARY_WORDS);
    for (unsigned i = 0; i < SUMMARY_WORDS; i++) summary[i] = m.word(units::ID_MASTER, summary_address + 4 * i);
    CHECK_EQ(summary[SUMMARY_KIND], KIND_MEMORY);                       // the run's kind
    CHECK_EQ(summary[SUMMARY_FAILURE], 0u);                             // the master's failure
    CHECK(write_reply_ok(&summary[SUMMARY_PART]));                      // the WRITE's reply
    for (unsigned i = 0; i < 8; i++) CHECK_EQ(summary[SUMMARY_REPLIES + i], 0u);   // the READs' replies, each a 128-bit 0
    for (unsigned beat = 0; beat < READ_BEATS; beat++) {                // the first READ's beats
        const std::vector<uint32_t> expected = beat_words(axi4::chip_lpddr_word(READ_LPDDR + beat));
        for (unsigned w = 0; w < 4; w++) CHECK_EQ(summary[SUMMARY_READ + 4 * beat + w], expected[w]);
    }
    for (unsigned beat = 0; beat < WRITE_BEATS; beat++)                 // the READ back's beats
        for (unsigned w = 0; w < 4; w++) CHECK_EQ(summary[SUMMARY_READ_BACK + 4 * beat + w], written_word(beat, w));
    for (unsigned request = 0; request < CREDIT_REQUESTS; request++) CHECK(write_reply_ok(&summary[SUMMARY_ANSWERS + 4 * request]));

    // ---- the lpddr port: the reads, the write, the reads after it ----
    std::vector<ChipLpddr::Burst> reads, writes;
    for (const auto& b : lpddr.bursts) (b.first ? writes : reads).push_back(b.second);
    std::vector<uint64_t> read_beats, write_beats, expected, written;
    CHECK(burst_beats(reads, read_beats));
    for (uint64_t x = READ_LPDDR; x < READ_LPDDR + READ_BEATS; x++) expected.push_back(x);
    for (uint64_t x = WRITE_LPDDR; x < WRITE_LPDDR + WRITE_BEATS; x++) { expected.push_back(x); written.push_back(x); }
    CHECK(read_beats == expected);                                      // the read bursts' beats
    CHECK(burst_beats(writes, write_beats));
    CHECK(write_beats == written);                                      // the write bursts' beats
    // The second READ's first burst after the WRITE's last, as the requests were served.
    size_t first_read_back = lpddr.bursts.size(), last_write = 0;
    for (size_t i = 0; i < lpddr.bursts.size(); i++) {
        if (!lpddr.bursts[i].first && lpddr.bursts[i].second.beat >= WRITE_LPDDR && first_read_back == lpddr.bursts.size()) first_read_back = i;
        if (lpddr.bursts[i].first) last_write = i;
    }
    CHECK(first_read_back < lpddr.bursts.size() && first_read_back > last_write);
    std::vector<bool> ends;
    for (const ChipLpddr::Burst& b : writes) for (uint32_t beat = 1; beat <= b.beats; beat++) ends.push_back(beat == b.beats);
    CHECK_EQ(lpddr.beats.size(), size_t(WRITE_BEATS));                  // the written beats
    for (unsigned beat = 0; beat < WRITE_BEATS; beat++) {
        const ChipLpddr::Written& x = lpddr.beats[beat];
        CHECK_EQ(x.index, WRITE_LPDDR + beat);
        const std::vector<uint32_t> words = beat_words(x.data);
        for (unsigned w = 0; w < 4; w++) CHECK_EQ(words[w], written_word(beat, w));
        CHECK_EQ(x.strobes, 0xFFFFu);                                   // every byte enabled
        CHECK_EQ(x.last, bool(ends[beat]));                             // WLAST on each burst's last beat
    }
    CHECK(lpddr.ar_monitor.ok());
    CHECK(lpddr.aw_monitor.ok());
    CHECK(lpddr.w_monitor.ok());
    CHECK(lpddr.idle());
    CHECK(!m.c.toast());                                                // the toast pin: no tile's error line rose
    CHECK_EQ(m.c.unit<units::PeriphSlot>("cc2.slot").s.violation_total(), uint64_t(0));

    // ---- the cycles, printed ----
    std::printf("test_memory: %s: the first burst at cycle %lld, the last handshake at %lld, %zu handshakes; the done word after %llu cycles\n",
                tag, lpddr.events.empty() ? -1LL : (long long)(lpddr.events.front().t - CYCLE_OFFSET),
                lpddr.events.empty() ? -1LL : (long long)(lpddr.events.back().t - CYCLE_OFFSET), lpddr.events.size(), (unsigned long long)now);
}

}  // namespace

// The RTL's test as it is: its slave's readies and latencies at random, from its seed.
TEST(chip_memory_random_slave) { run_memory("the RTL test's slave", -1); }
// Its slave made deterministic: every ready 1, each latency 0 (a burst answered the cycle after its handshake).
TEST(chip_memory_prompt_slave) { run_memory("every ready 1, latency 0", 0); }
// And each latency 3.
TEST(chip_memory_slave_of_latency_3) { run_memory("every ready 1, latency 3", 3); }

// The lpddr port is the chip's boundary: with no controller on it the sort's runs go by (the memory tile's firmware
// starts no job there), and a job stops the simulator, named.
DEATH_TEST(chip_memory_without_a_controller_stops_the_simulator, "a job raises a burst at [0-9]+ with no LPDDR controller on the lpddr port") {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    m.place_elf(units::ID_MASTER, kDir + "cc/cc_mem.elf");
    m.place_elf(units::ID_MEMORY, kDir + "cc/memory.elf");
    m.c.run(20000);
}
