// test_rtl_memory: the memory tile in the chip. The RTL's own images on v1.36's chip, unmodified: the RTL's chip test of
// the memory tile (Snarky One's rtl/chip/test/src/chip_memory_test.rs, with its bench.rs) on the simulator.
//
// The chip is tests/mach.h's (MachConfig::chip()). The master tile's firmware for the memory run
// (fw/rtl/sort/memory/master.elf) and the memory tile's (fw/rtl/sort/memory-tile/memory.elf) are placed in their IMEM
// and DMEM before the first cycle (their boot straps, machine-spec 6.5), and the test plays the LPDDR controller on the
// RW engine's lpddr port, the chip's boundary (src/units/rw_engine.h): tests/axi4_slave.h's ChipLpddr, the RTL test's
// AXI4 slave, one ID, answering in order, over a memory whose beats are a function of their index until written. The
// master (its src/bin/master.rs):
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
// THE CYCLES, against the RTL chip's. The RTL records no figure for this run (its slave's timing is random), so its
// reference is the RTL's test itself, run in a Snarky One scratch worktree with every handshake on the lpddr port and
// the cycle of the master's done word printed (tests/rwengine/README.md: the patch and the commands), three ways: the
// test as it is (its slave's readies and latencies at random, from its seed), and its slave made deterministic, every
// ready 1 and each latency 0, or 3. tests/rwengine/chip-memory-*.ev hold what the RTL's chip did. The same slave is on
// the simulator's port here, the random one drawing the RTL test's sequence (ChipLpddr draws for every cycle, called
// or not), and every handshake's cycle, channel and values and the done word's cycle must equal the RTL chip's: the
// requests through the tree, the memory tile's firmware, the RW engine, its DMA, the packets back to the master and
// the master's firmware between them, 35,645 to 35,665 cycles of the two tiles, cycle for cycle.
//   - The cycles' names: a handshake the RTL's test sees in its cycle `now` (0 its first cycle out of the reset) is in
//     the simulator's cycle now + 1 (CYCLE_OFFSET), every one of the 91 in each run; the slave's time, which its random
//     sequence is counted from, is set by that.
//   - The done word is read in place in the master's DMEM after every cycle, here and in the instrumented RTL test, and
//     is there after the same number of cycles on both sides. With the names a cycle apart, the simulator's back door
//     shows the master's store a cycle before the RTL's SRAM rows do; not traced further (no unit of the chip reads
//     DMEM that way).
//
// From rtl/tile/memory/test/src/memory_tile_test.rs (the memory tile alone at the chip's boundary: a program written in
// the test in place of the firmware, the root and the attach tile's router played, an AXI4 slave; its parts A to D).
// What it checks of the tile's composition is checked here in the chip, by the RTL's firmware: the straps (the boot
// from IMEM, the receive queue, the peripheral with its DMA), a request's path to the core and its credit back (the 300
// requests), the engine's registers through the register bus (the firmware's stores and loads), the events to the
// tile's EVENTS.peripheral_1 (the firmware sleeps on them), the write port from the tile's receive unit into the write
// buffer, counted on the memory tile's counter (the WRITE's beats), and the DMA's input to the router with the tile's
// id as src (the answers reach the master by their dst and are counted there). NOT TAKEN, each for want of a one-tile
// bench for a special tile, which the simulator has not (PENDING-v1.36.md, section 1's `special` row, and section 5):
// its part B's read under a stalled attach output (the DMA waiting for the router's ready, queue 0 holding only what
// it has room for: test_rw_engine has it at the slot's port, with the router input played; with the simulator's router
// behind it the cycles would differ, PENDING section 5); part C's 64-bit and 32-bit region WRITEs through the tile
// (test_rw_engine's write port and slot tests have the three sizes; the chip's firmware sends 128-bit beats); part D's
// LPDDR error to ERRORS.peripheral_1, the NMI, the reports 24 and 25 and the error line (test_rw_engine has the
// engine's bit and the slot's live error bit; test_rtl_sort the NMI, the reports and the lines for the tile's other
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

const std::string kDir = "fw/rtl/sort/";
const std::string kRtl = "tests/rwengine/";

// ---- the run (the master's src/bin/master.rs) ----
constexpr unsigned READ_BEATS = 20, WRITE_BEATS = 24;
constexpr uint64_t READ_LPDDR = 0x0FF8, WRITE_LPDDR = 0x20F4;
constexpr unsigned CREDIT_REQUESTS = 300;
constexpr unsigned MEMORY_QUEUE_DEPTH = 256;            // the memory target's receive queue, its credits, at 8 x 8 (11.3)
static_assert(CREDIT_REQUESTS > MEMORY_QUEUE_DEPTH);
uint32_t written_word(uint32_t beat, uint32_t w) { return 0x57000000 | beat << 8 | w; }   // word w of written beat `beat`

// ---- the summary (the master's MemorySummary): its words, by index ----
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

// The RTL chip's handshakes and its done word's cycle (tests/rwengine/chip-memory-<run>.ev).
struct RtlRun {
    std::vector<ChipLpddr::Event> events;
    uint64_t done = 0;
    explicit RtlRun(const std::string& run) {
        const std::string b = rtl_images::read_file(kRtl + "chip-memory-" + run + ".ev");
        for (size_t at = 0; at < b.size();) {
            size_t e = b.find('\n', at);
            if (e == std::string::npos) e = b.size();
            const std::string line = b.substr(at, e - at);
            at = e + 1;
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("done ", 0) == 0) { done = std::stoull(line.substr(5)); continue; }
            char channel = 0;
            unsigned long long t = 0, a = 0, x = 0;
            if (std::sscanf(line.c_str(), "%llu %c %llx %llu", &t, &channel, &a, &x) != 4) FATAL("%schip-memory-%s.ev: a bad line: %s", kRtl.c_str(), run.c_str(), line.c_str());
            events.push_back({int64_t(t), channel, a, x});
        }
    }
};

// One run: `fixed` < 0 the RTL test's random slave, else every ready 1 and each latency `fixed`.
void run_memory(const char* tag, int fixed, const char* rtl_run) {
    const std::string master_elf = kDir + "memory/master.elf", memory_elf = kDir + "memory-tile/memory.elf";
    CHECK_EQ(rtl_images::entry(master_elf), uint32_t(units::IMEM_BASE));
    CHECK_EQ(rtl_images::entry(memory_elf), uint32_t(units::IMEM_BASE));
    const rtl_images::Section summary_section = rtl_images::section(master_elf, ".summary");
    CHECK(summary_section.found);
    CHECK(summary_section.size / 4 >= SUMMARY_WORDS);
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
    const uint32_t done_address = summary_section.address + 4 * SUMMARY_DONE;
    uint64_t now = 0;
    bool done = false;
    while (!done && now < CYCLE_LIMIT) {
        m.c.run(1);
        now++;
        done = m.word(units::ID_MASTER, done_address) == DONE;
    }
    std::printf("test_rtl_memory: %s: %s after %llu cycles; the engine's jobs %llu, bursts %llu, beats read %llu, written %llu, replies %llu, "
                "port writes %llu; the slave was asked in %llu cycles\n", tag, done ? "the done word" : "NO done word", (unsigned long long)now,
                (unsigned long long)engine.jobs, (unsigned long long)engine.bursts, (unsigned long long)engine.beats_read,
                (unsigned long long)engine.beats_written, (unsigned long long)engine.replies, (unsigned long long)engine.port_writes,
                (unsigned long long)lpddr.calls);
    CHECK(done);

    // ---- the summary ----
    std::vector<uint32_t> summary(SUMMARY_WORDS);
    for (unsigned i = 0; i < SUMMARY_WORDS; i++) summary[i] = m.word(units::ID_MASTER, summary_section.address + 4 * i);
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

    // ---- the cycles: every handshake on the port and the done word, against the RTL chip's ----
    const RtlRun rtl(rtl_run);
    std::printf("test_rtl_memory: %s: the first burst at cycle %lld (the RTL's chip: %lld), the last handshake at %lld (%lld), the done word "
                "after %llu cycles (%llu)\n", tag, lpddr.events.empty() ? -1LL : (long long)(lpddr.events.front().t - CYCLE_OFFSET),
                rtl.events.empty() ? -1LL : (long long)rtl.events.front().t, lpddr.events.empty() ? -1LL : (long long)(lpddr.events.back().t - CYCLE_OFFSET),
                rtl.events.empty() ? -1LL : (long long)rtl.events.back().t, (unsigned long long)now, (unsigned long long)rtl.done);
    CHECK_EQ(lpddr.events.size(), rtl.events.size());
    unsigned differ = 0;
    for (size_t i = 0; i < rtl.events.size(); i++) {
        const ChipLpddr::Event& a = lpddr.events[i];
        const ChipLpddr::Event& b = rtl.events[i];
        if (a.t == b.t + CYCLE_OFFSET && a.channel == b.channel && a.a == b.a && a.b == b.b) continue;
        if (differ++ < 8)
            std::printf("test_rtl_memory: %s: handshake %zu is %c %llx %llu at cycle %lld; the RTL's chip: %c %llx %llu at %lld\n", tag, i, a.channel,
                        (unsigned long long)a.a, (unsigned long long)a.b, (long long)(a.t - CYCLE_OFFSET), b.channel, (unsigned long long)b.a, (unsigned long long)b.b,
                        (long long)b.t);
    }
    CHECK_EQ(differ, 0u);
    CHECK_EQ(now, rtl.done);
}

}  // namespace

// The RTL's test as it is: its slave's readies and latencies at random, from its seed.
TEST(chip_memory_random_slave) { run_memory("the RTL test's slave", -1, "random"); }
// Its slave made deterministic: every ready 1, each latency 0 (a burst answered the cycle after its handshake).
TEST(chip_memory_prompt_slave) { run_memory("every ready 1, latency 0", 0, "fixed0"); }
// And each latency 3.
TEST(chip_memory_slave_of_latency_3) { run_memory("every ready 1, latency 3", 3, "fixed3"); }

// The lpddr port is the chip's boundary: with no controller on it the sort's runs go by (the memory tile's firmware
// starts no job there), and a job stops the simulator, named.
DEATH_TEST(chip_memory_without_a_controller_stops_the_simulator, "a job raises a burst at [0-9]+ with no LPDDR controller on the lpddr port") {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    m.place_elf(units::ID_MASTER, kDir + "memory/master.elf");
    m.place_elf(units::ID_MEMORY, kDir + "memory-tile/memory.elf");
    m.c.run(20000);
}
