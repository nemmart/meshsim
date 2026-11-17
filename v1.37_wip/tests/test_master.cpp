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
//   kin-big     keccak256 with 24 KB of untrusted advice: a data image of 1,584 words, more than Ingress's buffer of
//               1,024 and three credits of the segment queue (WORKER4)
// The threshold (WORKER4): the frames' tails (the fixtures hold frames of the header word alone, frames shorter than a
// packet, frames that are no multiple of 32 or 256), the threshold at 1, 32, 64, 128 and 1,024 with the same images,
// frames that end inside a packet, and the link at 2, 4 and 8 bytes a cycle.
// Then the error path (a packet damaged, a packet left out, a page count too small: the master's NMI, its results, its
// exit), a file that ends inside a packet, and the checks' teeth: runs that must fail.
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <string>

#include "harness.h"
#include "packet_writer.h"
#include "proof_bench.h"
#include "units/ingress_egress.h"
#include "units/updater.h"

namespace {

using Change = std::function<void(proof::Case&, segment::Segment&)>;

segment::Run fixture(const char* name, unsigned s, const Change& change = nullptr) {
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
// the link as fast as the master takes it: the frames back to back, the Updater and the pump never waiting on the link
TEST(master_kin_lru_fast_link) {
    CHECK_EQ(fixture("kin-lru", 60, [](proof::Case& c, segment::Segment&) { c.cycles_per_byte = 0; }).failures, 0);
}
// a fast link and a slow memory side: the Updater waits on its first page of each run while the frame arrives
TEST(master_kin_raw_slow_memory) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment&) { c.cycles_per_byte = 0; c.updater_lookup = 3000; }).failures, 0);
}

// ---- the threshold (INGRESS-INTERFACE.md, "Waking for more than a packet"; WORKER4)
namespace {
const char* const kFixtures[] = {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"};
const unsigned kSegment[] = {20, 60, 0, 20, 20};
}  // namespace
// a frame of 1,584 words for the Updater: Ingress's buffer fills and the link waits; a wake, a credit and a transfer
// go together
TEST(master_kin_big) {
    const segment::Run r = fixture("kin-big", 20);
    CHECK_EQ(r.failures, 0);
    CHECK(r.summary.size() > 122);
    if (r.summary.size() > 122) CHECK(r.summary[120] > 0);  // MR_THRESHOLD_STORES: the threshold was set
}
TEST(master_kin_big_last) { CHECK_EQ(fixture("kin-big", 63).failures, 0); }
// a fast link and a slow memory side: only the allowance keeps the data image's 1,584 words from overrunning the
// segment queue of 512
TEST(master_kin_big_slow_memory) {
    CHECK_EQ(fixture("kin-big", 20, [](proof::Case& c, segment::Segment&) { c.cycles_per_byte = 0; c.updater_lookup = 3000; }).failures, 0);
}
// the tails: the fixtures hold frames of the header word alone, frames shorter than a packet, and frames whose length
// is a multiple of neither 32 nor 256, and longer than 256; each fixture passes above and at every threshold below
TEST(master_tail_frames) {
    unsigned header_alone = 0, short_of_a_packet = 0, odd_short = 0, odd_long = 0;
    for (const char* name : kFixtures) {
        frames::File f;
        std::string err;
        CHECK(f.read(std::string("tests/updater/") + name + ".frames", err));
        for (const frames::Frame& fr : f.frame) {
            if (fr.length == 0) header_alone++;
            else if (fr.length < 32) short_of_a_packet++;
            else if (fr.length % 32 && fr.length < 256) odd_short++;
            else if (fr.length % 32 && fr.length > 256) odd_long++;
        }
    }
    CHECK(header_alone >= 4);                               // kin-lru's frame 5, jumps-base's 6 and 9, divs's 6
    CHECK(short_of_a_packet >= 10);
    CHECK(odd_short >= 3);
    CHECK(odd_long >= 4);                                   // 257, 334, 1,584
}
// the threshold after a header word at 1, 32, 64, 128 and 1,024 in place of 256: the same images, every check
TEST(master_thresholds) {
    for (size_t k = 0; k < sizeof kSegment / sizeof kSegment[0]; k++)
        for (uint32_t threshold : {1u, 32u, 64u, 128u, 1024u}) {
            const segment::Run r = fixture(kFixtures[k], kSegment[k], [threshold](proof::Case& c, segment::Segment& seg) {
                c.threshold = threshold; seg.name += ", threshold " + std::to_string(threshold);
            });
            CHECK_EQ(r.failures, 0);
        }
}
// a frame that ends inside a packet (the frame-ends-at-a-packet check is dropped, II's dagger): the whole file cut into
// packets regardless of frames, of 32 words and of 13: the next frame's header word is the next word, and the proof is
// the same
TEST(master_frames_across_packets) {
    for (size_t k = 0; k < sizeof kSegment / sizeof kSegment[0]; k++)
        for (unsigned packed : {32u, 13u}) {
            const segment::Run r = fixture(kFixtures[k], kSegment[k], [packed](proof::Case& c, segment::Segment& seg) {
                std::string err;
                packet_writer::Options o;
                o.packed = packed;
                if (!packet_writer::packetize(c.file.bytes, c.packets, err, o)) ::test::fail(__FILE__, __LINE__, "%s", err.c_str());
                seg.name += ", packets of " + std::to_string(packed) + " across frames";
            });
            CHECK_EQ(r.failures, 0);
        }
}
// a link slower than the master (4 cycles a byte): the master is ahead of the link at every frame's start, so each
// header word waits for its flag at the threshold of 1
TEST(master_slow_link) {
    for (size_t k = 0; k < sizeof kSegment / sizeof kSegment[0]; k++)
        CHECK_EQ(fixture(kFixtures[k], kSegment[k], [](proof::Case& c, segment::Segment& seg) { c.cycles_per_byte = 4; seg.name += ", 4 cycles a byte"; }).failures, 0);
}
// the link as fast as the master takes it, at thresholds of 1 and 32: flags posted while the master's transfers run
TEST(master_fast_link_small_thresholds) {
    for (size_t k = 0; k < sizeof kSegment / sizeof kSegment[0]; k++)
        for (uint32_t threshold : {1u, 32u}) {
            const segment::Run r = fixture(kFixtures[k], kSegment[k], [threshold](proof::Case& c, segment::Segment& seg) {
                c.cycles_per_byte = 0; c.threshold = threshold; seg.name += ", fast link, threshold " + std::to_string(threshold);
            });
            CHECK_EQ(r.failures, 0);
        }
}
// the link at 2, 4 and 8 bytes a cycle
TEST(master_faster_links) {
    for (int rate : {2, 4, 8}) {
        CHECK_EQ(fixture("kin-big", 20, [rate](proof::Case& c, segment::Segment& seg) { c.link_rate = rate; seg.name += ", " + std::to_string(rate) + " bytes a cycle"; }).failures, 0);
        CHECK_EQ(fixture("divs", 20, [rate](proof::Case& c, segment::Segment& seg) { c.link_rate = rate; seg.name += ", " + std::to_string(rate) + " bytes a cycle"; }).failures, 0);
    }
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
