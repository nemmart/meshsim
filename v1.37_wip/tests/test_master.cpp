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
//   kin-big     keccak256 with 24 KB of untrusted advice: a data image of 1,584 words, three times Ingress's queue of
//               512 (WORKER4's fixture)
// The forward count (WORKER7): the frames' tails (the fixtures hold frames of the header word alone, frames shorter
// than a packet, frames that are no multiple of 32), a wake a frame for the bulk data, the orders the firmware relies on
// with the timing varied (the link from 4 cycles a byte to 8 bytes a cycle and as fast as taken, the Updater's memory
// side fast and slow, so that a frame's flag comes before or long after the last run's done), and frames that end
// inside a packet.
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

segment::Run fixture(const char* name, unsigned s, const Change& change = nullptr, uint32_t lpddr_seed = 0) {
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
    o.lpddr_seed = lpddr_seed;
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
// The same guests with the LPDDR model's timing varied (its jitter mode, MachConfig::lpddr_seed): the memory system's
// every unit under the Updater and the Tracer, every check as above (v1.37 stage 1: the cache and the Updater's
// controller are the machine; WORKER5's runs on the cache's controller were these with the timing fixed)
TEST(master_lpddr_jitter_kin_raw_first) { CHECK_EQ(fixture("kin-raw", 0, nullptr, 1).failures, 0); }
TEST(master_lpddr_jitter_kin_raw_writes) { CHECK_EQ(fixture("kin-raw", 60, nullptr, 2).failures, 0); }
TEST(master_lpddr_jitter_kin_lru_last) { CHECK_EQ(fixture("kin-lru", 63, nullptr, 3).failures, 0); }
TEST(master_lpddr_jitter_jumps_base_first) { CHECK_EQ(fixture("jumps-base", 0, nullptr, 4).failures, 0); }
TEST(master_lpddr_jitter_divs_middle) { CHECK_EQ(fixture("divs", 20, nullptr, 5).failures, 0); }
TEST(master_lpddr_jitter_kin_big) { CHECK_EQ(fixture("kin-big", 20, nullptr, 6).failures, 0); }
// the link as fast as the master takes it: the frames back to back, the Updater and the pump never waiting on the link
TEST(master_kin_lru_fast_link) {
    CHECK_EQ(fixture("kin-lru", 60, [](proof::Case& c, segment::Segment&) { c.link_rate = 8; }).failures, 0);
}
// a fast link (8 bytes a cycle) and a slow memory side: the Updater waits on its first page of each run while the frame arrives
TEST(master_kin_raw_slow_memory) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment&) { c.link_rate = 8; c.updater_lookup = 3000; }).failures, 0);
}

// ---- the forward count (INGRESS-INTERFACE.md, "The forward count"; WORKER7)
namespace {
const char* const kFixtures[] = {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"};
const unsigned kSegment[] = {20, 60, 0, 20, 20};
// The master's wakes from frame k's header word to the next frame's (MR_FRAME_WAKES, 88 on; MR_FRAMES, 2).
unsigned frame_wakes(const segment::Run& r, unsigned k) { return r.summary[88 + k + 1] - r.summary[88 + k]; }
}  // namespace
// a frame of 1,584 words for the Updater, three times Ingress's queue: the link is held by the port's flow control, and
// the master wakes a few times for the frame, not for its words
TEST(master_kin_big) {
    const segment::Run r = fixture("kin-big", 20);
    CHECK_EQ(r.failures, 0);
    CHECK(r.summary.size() > 122);
    if (r.summary.size() <= 122) return;
    CHECK(r.summary[120] >= 6);                             // MR_COUNT_STORES: the update frames' and frames 7 and 8's
    unsigned data_image = 99;
    for (unsigned k = 0; k + 1 < r.summary[2]; k++) if (r.summary[56 + k] == 6) data_image = k;
    CHECK(data_image != 99);
    if (data_image != 99) CHECK(frame_wakes(r, data_image) <= 6);   // the flag, the header's landing, the last run's done, a credit
}
TEST(master_kin_big_last) { CHECK_EQ(fixture("kin-big", 63).failures, 0); }
// a fast link (8 bytes a cycle) and a slow memory side: only the port's flow control keeps the data image's 1,584 words from overrunning
// the Updater's 4; Ingress's queue fills and the link waits
TEST(master_kin_big_slow_memory) {
    CHECK_EQ(fixture("kin-big", 20, [](proof::Case& c, segment::Segment&) { c.link_rate = 8; c.updater_lookup = 3000; }).failures, 0);
}
// the tails: the fixtures hold frames of the header word alone, frames shorter than a packet, and frames whose length
// is no multiple of 32, short and long; each fixture passes at every timing below
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
// The orders the firmware relies on (WORKER7's NOTES, O1 to O6), with the timing varied, on every fixture: the link as
// fast as taken with the Updater's memory side slow (a frame's flag long before the last run's done: O2; the count
// written while the run's root is made: O1), and fast (the Updater waiting on the link); the link at 8 bytes a cycle
// with a middling memory side; the link slow (4 cycles a byte: the master ahead of the link at every frame, waiting
// with the count at 0 and the queue empty).
TEST(master_orders_with_the_timing_varied) {
    struct Timing { int link_every, link_rate, lookup; const char* name; };
    const Timing timings[] = {{1, 8, 3000, "8 bytes a cycle, slow memory"}, {1, 8, 0, "8 bytes a cycle"}, {1, 8, 300, "8 bytes a cycle, middling memory"},
                              {4, 1, 0, "4 cycles a byte"}, {4, 1, 3000, "4 cycles a byte, slow memory"}};
    for (size_t k = 0; k < sizeof kSegment / sizeof kSegment[0]; k++)
        for (const Timing& tm : timings) {
            const segment::Run r = fixture(kFixtures[k], kSegment[k], [tm](proof::Case& c, segment::Segment& seg) {
                c.link_every = tm.link_every; c.link_rate = tm.link_rate; c.updater_lookup = tm.lookup; seg.name += std::string(", ") + tm.name;
                seg.cycle_limit = 20000000;
            });
            CHECK_EQ(r.failures, 0);
        }
}
// divs at s 60: log s is empty, with logs before it and (made here) after it, so frame 8's two drops come one after the
// other, written as one count (a second store would replace the first while it runs: WORKER7's O3, the teeth's "drops
// in a row"); at a byte a cycle and with the frame all there at once. divs's own logs from 51 on are empty; the test
// gives segments 61 to 63 a row of two values each, which no ASIC running s 60 uses: the images are the same.
namespace {
void logs_after(proof::Case& c, unsigned from) {
    for (const frames::Frame& f : c.file.frame) {
        if (f.type != 8) continue;
        std::vector<uint8_t>& b = c.file.bytes;
        const size_t counts = f.data(), end = f.data() + 16 * size_t(f.length);
        const unsigned added = c.asics - from;
        for (unsigned k = from; k < c.asics; k++) b[counts + 4 * k] = 2;   // two values: one row
        std::vector<uint8_t> rows(16 * added);
        for (size_t i = 0; i < rows.size(); i++) rows[i] = uint8_t(0xA5 ^ i);
        b.insert(b.begin() + long(end), rows.begin(), rows.end());
        const uint32_t length = f.length + added;
        for (unsigned i = 0; i < 4; i++) b[f.at + 12 + i] = uint8_t(length >> (8 * i));
        std::string err;
        c.file.frame.clear();
        if (!c.file.parse(err)) ::test::fail(__FILE__, __LINE__, "%s", err.c_str());
        c.packets = proof::packets_of(c.file);
        return;
    }
    ::test::fail(__FILE__, __LINE__, "no frame 8");
}
}  // namespace
TEST(master_divs_empty_log) {
    CHECK_EQ(fixture("divs", 60).failures, 0);
    for (int fast : {0, 1})
        CHECK_EQ(fixture("divs", 60, [fast](proof::Case& c, segment::Segment& seg) {
            logs_after(c, 61);
            if (fast) c.link_rate = 8;
            seg.name += fast ? ", logs after s, 8 bytes a cycle" : ", logs after s";
        }).failures, 0);
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
// the link at 2, 4 and 8 bytes a cycle
TEST(master_faster_links) {
    for (int rate : {2, 4, 8}) {
        CHECK_EQ(fixture("kin-big", 20, [rate](proof::Case& c, segment::Segment& seg) { c.link_rate = rate; seg.name += ", " + std::to_string(rate) + " bytes a cycle"; }).failures, 0);
        CHECK_EQ(fixture("divs", 20, [rate](proof::Case& c, segment::Segment& seg) { c.link_rate = rate; seg.name += ", " + std::to_string(rate) + " bytes a cycle"; }).failures, 0);
    }
}

// ---- the error path: an error bit is the master's NMI; it writes what there is into its results and exits
// v1.37 stage 4 (28.8, 28.14): noise on the line is no error. A byte the chip receives changed, now and then, in the
// link's first 200,000 cycles (a bit of one byte in 5,000, so headers, data and the zeros between): the link ends
// recover, every link packet is sent again until it is taken, and the proof is the same, with no error bit.
TEST(master_noise_on_the_line_is_recovered) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        c.noise = [](int64_t t, unsigned, uint8_t b) {
            const uint64_t h = uint64_t(t) * 0x9E3779B97F4A7C15ull;
            return t < 200000 && (h >> 40) % 5000 == 0 ? uint8_t(b ^ (1u << (h >> 61))) : b;
        };
        seg.name += ", noise on the line";
    }).failures, 0);
}
// v1.37 stage 4 (28.9, 17.6.12): the host's end starts its link again in the middle of the frames (its power_on, in
// cycle 60,000): the chip's link end hears "I have started again", its link starts again with link packets it had taken
// lost to nothing but the host's, and ERRORS.link_error is the master's NMI: the error path.
TEST(master_host_link_started_again) {
    CHECK_EQ(fixture("kin-raw", 20, [](proof::Case& c, segment::Segment& seg) {
        c.host_hook = [](::hostlink::End& end, int64_t t) { if (t == 60000) end.power_on(); };
        c.expect.trap = true; c.expect.ingress_errors = units::IngressEgress::ERR_LINK;
        seg.name += ", the host's link started again";
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
