// test_master: the master tile's program for v1.37 (fw/cc/master.c) on the whole chip, from a packet file
// (tests/proof_bench.h): gold's frame files of the repository's guests (tests/updater/, its make_fixtures.sh: N 64, each
// guest cut into segments of 50 rows), wrapped in the link's packets, with N and s. The master reads the frames out of
// Ingress, drives the Updater, and hands the tracer tile segment s; the tracer tile traces its 50 rows as one window on
// the images the Updater built, tile 0 replays it. The checks are proof_bench.h's, with gold's reference for each s
// (tests/updater/NAME-sS.images.txt): every image as its run ended, PAGE_COUNT 0 at every done, no error bit, the page
// map, the hand-over, and the Tracer's last record against record s + 1.
//
//   kin-raw     keccak256 with input and both advices, a full new table, no compression. Its segments from 1 on start
//               in the middle of the inline's sequence (the tracer tile feeds the template from an offset)
//   kin-lru     the same, on a static base table, every update-word frame LRU-coded
//   jumps-base  jumpmap's jumps guest, on a static base table, each frame in its smaller form: the Tracer's jump map
//   divs        a loop of divisions and stores: every segment has advice values (frame 8's log s, handed over) and
//               writes a page, which the Tracer writes in its copy, in place
// Then the error path (a packet damaged, a packet left out, a page count too small: the master's NMI, its results, its
// exit), a file that ends inside a packet, and the checks' teeth: runs that must fail.
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <string>

#include "harness.h"
#include "proof_bench.h"
#include "units/ingress_egress.h"
#include "units/updater.h"

namespace {

using Change = std::function<void(proof::Case&, segment::Segment&)>;

segment::Run fixture(const char* name, unsigned s, const Change& change = nullptr, bool cache = false) {
    proof::Case c;
    std::string err;
    const std::string base = std::string("tests/updater/") + name;
    if (!c.file.read(base + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return segment::Run(); }
    c.packets = proof::packets_of(c.file);
    c.asics = 64; c.segment = s;
    c.gold = proof::gold_digests(base + "-s" + std::to_string(s) + ".images.txt");
    segment::Segment seg;
    seg.name = std::string(name) + ", s " + std::to_string(s);
    seg.windows = 1; seg.rows = 50;                 // gold's --segment-rows=50: one window
    seg.cycle_limit = 4000000;
    if (change) change(c, seg);
    segment::Options o;
    o.quiet = true;
    o.cache_ctl = cache;
    return proof::run(c, seg, o);
}

// A run that must fail: the checks that caught it, counted. Its lines go to the log with FAILED reworded, since
// run_tests.sh takes any FAIL line in a log for a failure of the step.
int caught(const char* name, unsigned s, const Change& change) {
    std::fflush(stdout);
    const int out = dup(1);
    FILE* kept = std::tmpfile();
    dup2(fileno(kept), 1);
    const int failures = fixture(name, s, change).failures;
    std::fflush(stdout);
    dup2(out, 1);
    close(out);
    std::rewind(kept);
    char line[1024];
    while (std::fgets(line, sizeof line, kept)) {
        std::string text = line;
        for (size_t at; (at = text.find("FAILED")) != std::string::npos;) text.replace(at, 6, "caught");
        std::fputs(text.c_str(), stdout);
    }
    std::fclose(kept);
    return failures;
}

// Where each packet of a packet file starts: a header word, 16 bytes, its second byte the data's words.
std::vector<size_t> packet_starts(const std::vector<uint8_t>& bytes) {
    std::vector<size_t> starts;
    for (size_t at = 0; at + 16 <= bytes.size(); at += 16 + 16 * size_t(bytes[at + 1])) starts.push_back(at);
    return starts;
}
// Frame 1's page count (HOST-FRAMES.md: the stem, the file length, the 64-bit words, the 128-bit words, the pages)
// changed by `by`, and the file packetized again.
void program_pages(proof::Case& c, int by) {
    size_t at = c.file.frame[0].data();
    at += 4 + ((size_t(c.file.u32(at)) + 3) & ~size_t(3));
    c.file.bytes[at + 12] = uint8_t(c.file.bytes[at + 12] + by);
    c.packets = proof::packets_of(c.file);
}

}  // namespace

TEST(master_kin_raw_first) { CHECK_EQ(fixture("kin-raw", 0).failures, 0); }
TEST(master_kin_raw_middle) { CHECK_EQ(fixture("kin-raw", 20).failures, 0); }
TEST(master_kin_lru_middle) { CHECK_EQ(fixture("kin-lru", 20).failures, 0); }
TEST(master_jumps_base_first) { CHECK_EQ(fixture("jumps-base", 0).failures, 0); }
// segment 60 writes memory: the Tracer's copy has a page of its own, which the Tracer writes in place
TEST(master_kin_raw_writes) { CHECK_EQ(fixture("kin-raw", 60).failures, 0); }
TEST(master_kin_lru_writes) { CHECK_EQ(fixture("kin-lru", 60).failures, 0); }
// the last segment: no record s + 1, so no end state for the tracer tile to check
TEST(master_kin_lru_last) { CHECK_EQ(fixture("kin-lru", 63).failures, 0); }
// the segment as two windows of 25 rows, on tiles 0 and 1
TEST(master_kin_raw_two_windows) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case&, segment::Segment& seg) { seg.windows = 2; seg.rows = 25; }).failures, 0);
}
// advice and memory writes in every segment: the first, one in the middle, and one as two windows
TEST(master_divs_first) { CHECK_EQ(fixture("divs", 0).failures, 0); }
TEST(master_divs_middle) { CHECK_EQ(fixture("divs", 20).failures, 0); }
TEST(master_divs_two_windows) {
    CHECK_EQ(fixture("divs", 20, [](proof::Case&, segment::Segment& seg) { seg.windows = 2; seg.rows = 25; }).failures, 0);
}
// updater_wip, WORKER5: the same guests with the Tracer on the cache's controller, the AXI arbiter and the LPDDR model
// (MachConfig::cache_ctl): every check as above, and the cache's error flag clear and no read of unwritten LPDDR
TEST(master_cache_kin_raw_first) { CHECK_EQ(fixture("kin-raw", 0, nullptr, true).failures, 0); }
TEST(master_cache_kin_raw_writes) { CHECK_EQ(fixture("kin-raw", 60, nullptr, true).failures, 0); }
TEST(master_cache_kin_lru_last) { CHECK_EQ(fixture("kin-lru", 63, nullptr, true).failures, 0); }
TEST(master_cache_jumps_base_first) { CHECK_EQ(fixture("jumps-base", 0, nullptr, true).failures, 0); }
TEST(master_cache_divs_middle) { CHECK_EQ(fixture("divs", 20, nullptr, true).failures, 0); }
TEST(master_cache_divs_two_windows) {
    CHECK_EQ(fixture("divs", 20, [](proof::Case&, segment::Segment& seg) { seg.windows = 2; seg.rows = 25; }, true).failures, 0);
}
// the link as fast as the master takes it: the frames back to back, the Updater and the pump never waiting on the link
TEST(master_kin_lru_fast_link) {
    CHECK_EQ(fixture("kin-lru", 60, [](proof::Case& c, segment::Segment&) { c.cycles_per_byte = 0; }).failures, 0);
}
// a fast link and a slow memory side: the Updater waits on its first page of each run while the frame arrives, and
// only the allowance keeps the frame's words from overrunning the segment queue (the data image is 334 words, the
// queue 256)
TEST(master_kin_raw_slow_memory) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment&) { c.cycles_per_byte = 0; c.updater_lookup = 3000; }).failures, 0);
}

// ---- the error path: an error bit is the master's NMI; it writes what there is into its results and exits
// a bit of a packet's data changed, in frame 2 (the program image going to the Updater): ERRORS.crc_error
TEST(master_bad_packet_crc) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        c.packets[packet_starts(c.packets)[5] + 16 + 37] ^= 0x10;
        c.expect.trap = true; c.expect.ingress_errors = units::IngressEgress::ERR_CRC;
        seg.name += ", a data bit changed";
    }).failures, 0);
}
// a packet of frame 7 left out: ERRORS.sequence_error
TEST(master_bad_packet_sequence) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        const std::vector<size_t> at = packet_starts(c.packets);
        c.packets.erase(c.packets.begin() + long(at[60]), c.packets.begin() + long(at[61]));
        c.expect.trap = true; c.expect.ingress_errors = units::IngressEgress::ERR_SEQUENCE;
        seg.name += ", a packet left out";
    }).failures, 0);
}
// frame 1 counts one page too few for the program image: the Updater's ERRORS.out_of_pages
TEST(master_out_of_pages) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        program_pages(c, -1);
        c.expect.trap = true; c.expect.updater_errors = units::Updater::ERR_OUT_OF_PAGES;
        seg.name += ", a page too few";
    }).failures, 0);
}
// the file ends inside a packet: no error, the master waits (INGRESS-INTERFACE.md: at the end of the file no more
// packets come, which is not an error)
TEST(master_link_cut) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        c.packets.resize(packet_starts(c.packets)[60] + 100);
        c.expect.cut = true;
        seg.cycle_limit = 400000;
        seg.name += ", cut inside a packet";
    }).failures, 0);
}

// ---- the checks' teeth: each of these runs must fail, and its lines say which checks caught it
// frame 1 counts one page too many: the run ends with PAGE_COUNT 1, and the page stays in the free page queue
TEST(master_teeth_page_count) {
    CHECK(caught("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) { program_pages(c, +1); seg.name = "teeth (a page too many)"; }) > 0);
}
// gold's reference for another image
TEST(master_teeth_reference) {
    CHECK(caught("kin-raw", 60, [](proof::Case& c, segment::Segment& seg) { c.gold["final"] = c.gold["cycle0"]; seg.name = "teeth (another image's digest)"; }) > 0);
}
// the system told another s than the reference's: the start image is segment 61's, the record handed over too
TEST(master_teeth_segment) {
    CHECK(caught("kin-raw", 60, [](proof::Case& c, segment::Segment& seg) {
        c.segment = 61;
        c.gold["start-61"] = c.gold["start-60"];
        seg.name = "teeth (s is 61, the reference is 60's)";
    }) > 0);
}
