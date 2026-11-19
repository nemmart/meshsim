// test_lpddr_model: the LPDDR model (src/units/lpddr_model.h; ../v1.4_wip/planning/AXI-INTERFACE.md section 5) driven
// straight on its port by test masters with IDs (tests/axi_test_client.h): its timing to the cycle, its memory the file
// store's, the order within an ID and the round robin between IDs, the limits, the faults and DECERR past the end, the
// jitter mode keeping the rules, and the rules it stops on.
#include <cstring>
#include <vector>

#include "axi_test_client.h"
#include "harness.h"
#include "units/file_store.h"
#include "units/lpddr_model.h"

using units::LpddrModel;
using units::tracer::FileStore;
using axitest::Client;
namespace axi = units::axi;

namespace {

// Several masters on one port, each with its own ID: their outputs merged (one at a time per channel: the tests give
// each channel to one master at a time, or use one master with several IDs).
int64_t run(LpddrModel& m, Client& c, int64_t limit = 100000) {
    int64_t t = 0;
    for (; t < limit && !(c.finished() && m.idle()); t++) {
        const axi::MasterOut mo = c.out;
        const axi::SlaveOut so = m.out();
        m.edge(t, mo);
        c.edge(t, so);
    }
    return t;
}

void fill_page(FileStore& fs, int32_t unit, uint64_t tag) {
    uint64_t pg[512];
    for (int i = 0; i < 512; i++) pg[i] = uint64_t(unit) << 32 ^ uint64_t(i) ^ tag;
    fs.write_page(unit, pg);
}

}  // namespace

TEST(lpddr_line_read_timing_and_data) {
    FileStore fs;
    fill_page(fs, 64, 0);                            // the page at byte 64 * 128 = 8 KB
    LpddrModel m(&fs);
    Client c;
    c.reads.push_back(axitest::read_op(8192 + 256, 8));
    run(m, c);
    const auto& o = c.reads[0];
    CHECK(o.done && o.rlast_on_last);
    CHECK_EQ(o.rlasts, 1u);
    CHECK_EQ(o.t_first - o.t_taken, 70);             // read_latency
    CHECK_EQ(o.t_done - o.t_first, 7);               // a beat a cycle
    for (unsigned k = 0; k < 16; k++) CHECK_EQ(o.got[k], (uint64_t(64) << 32 ^ (32 + k)));
    CHECK_EQ(m.unwritten_reads, 0u);
}

TEST(lpddr_write_lands_and_reads_back) {
    FileStore fs;
    LpddrModel m(&fs);
    Client c;
    c.writes.push_back(axitest::write_op(4096 * 5 + 128, 8, 0xAB));
    run(m, c);
    const auto& w = c.writes[0];
    CHECK(w.done);
    CHECK_EQ(w.resp[0], axi::OKAY);
    CHECK(w.t_done - w.t_taken >= 70 + 7);           // after the later of AW and the last beat, plus write_latency
    uint64_t pg[512];
    CHECK(fs.read_page(5 * 32, pg));
    for (unsigned k = 0; k < 16; k++) CHECK_EQ(pg[16 + k], w.data[k]);
    for (unsigned k = 0; k < 16; k++) CHECK_EQ(pg[k], 0u);   // the rest of the page untouched (zeros)
    Client r;
    r.reads.push_back(axitest::read_op(4096 * 5 + 128, 8));
    run(m, r);
    CHECK(r.reads[0].got == w.data);
}

TEST(lpddr_unwritten_read_counted) {
    FileStore fs;
    LpddrModel m(&fs);
    Client c;
    c.reads.push_back(axitest::read_op(1 << 20, 4));
    run(m, c);
    CHECK(c.reads[0].done);
    CHECK_EQ(m.unwritten_reads, 4u);
    for (uint64_t w : c.reads[0].got) CHECK_EQ(w, 0u);
}

TEST(lpddr_page_burst_and_order_within_an_id) {
    FileStore fs;
    for (int p = 0; p < 4; p++) fill_page(fs, 32 * (10 + p), 7);
    LpddrModel m(&fs);
    Client c;
    c.max_reads = 4;
    for (int p = 0; p < 4; p++) c.reads.push_back(axitest::read_op(uint64_t(10 + p) * 4096, 256, 1));
    run(m, c);
    for (int p = 0; p < 4; p++) {
        CHECK(c.reads[p].done && c.reads[p].rlast_on_last);
        CHECK_EQ(c.reads[p].got.size(), size_t(512));
        CHECK_EQ(c.reads[p].got[100], (uint64_t(32 * (10 + p)) << 32 ^ 100u ^ 7u));
        if (p > 0) CHECK(c.reads[p].t_first > c.reads[p - 1].t_done);   // in the order taken, back to back
    }
    CHECK_EQ(c.reads[1].t_first, c.reads[0].t_done + 1);
}

// Two IDs due at once: R alternates between them burst by burst (round robin), each ID's in its order.
TEST(lpddr_round_robin_between_ids) {
    FileStore fs;
    fill_page(fs, 32 * 3, 0);
    LpddrModel m(&fs);
    Client c;
    c.max_reads = 8;
    for (int k = 0; k < 6; k++) c.reads.push_back(axitest::read_op(3 * 4096 + 512 * uint64_t(k), 32, uint8_t(k % 2 == 0 ? 1 : 0)));
    run(m, c);
    // taken in cycles 1..6, due 71..76; R: the first due (k 0, ID 1) at 71, then ID 0 (k 1), ID 1 (k 2), ...
    for (int k = 0; k < 6; k++) CHECK(c.reads[k].done);
    for (int k = 1; k < 6; k++) CHECK_EQ(c.reads[k].t_first, c.reads[k - 1].t_done + 1);
}

// The line of ID 0 comes after at most the burst on R when it falls due, not after every piece taken before it.
TEST(lpddr_a_line_passes_pieces_of_another_id) {
    FileStore fs;
    for (int p = 0; p < 8; p++) fill_page(fs, 32 * (20 + p), 0);
    LpddrModel m(&fs);
    Client c;
    c.max_reads = 16;
    for (int k = 0; k < 8; k++) c.reads.push_back(axitest::read_op(20 * 4096 + 512 * uint64_t(k), 32, 1));   // a page, as 8 pieces
    c.reads.push_back(axitest::read_op(27 * 4096, 8, 0));                                                    // then a line
    run(m, c);
    CHECK(c.reads[8].done);
    CHECK(c.reads[8].t_first < c.reads[7].t_first);   // ahead of the page's last pieces
    CHECK(c.reads[8].t_first - c.reads[8].t_taken <= 70 + 32);
}

TEST(lpddr_limits_hold_ready) {
    FileStore fs;
    axi::LpddrTiming tm;
    tm.max_reads = 2;
    LpddrModel m(&fs, tm);
    fill_page(fs, 0, 0);
    Client c;
    c.max_reads = 8;
    for (int k = 0; k < 5; k++) c.reads.push_back(axitest::read_op(16 * uint64_t(k), 1));
    run(m, c);
    for (int k = 0; k < 5; k++) CHECK(c.reads[k].done);
    CHECK(c.reads[2].t_taken > c.reads[0].t_done);    // the third waits for the first's answer
}

TEST(lpddr_faults_and_the_end) {
    FileStore fs;
    fill_page(fs, 0, 0);
    fill_page(fs, 32, 0);
    LpddrModel m(&fs, axi::LpddrTiming{}, 24);         // 16 MB of LPDDR
    axi::LpddrFault f;
    f.first = 4096 + 256; f.last = 4096 + 300; f.resp = axi::SLVERR; f.bursts = 2;
    m.add_fault(f);
    Client c;
    c.reads.push_back(axitest::read_op(4096 + 256, 8));     // faulty
    c.reads.push_back(axitest::read_op(4096, 8));           // clear of it
    c.writes.push_back(axitest::write_op(4096 + 288, 2, 5));   // faulty: writes nothing
    c.writes.push_back(axitest::write_op(4096 + 256, 8, 6));   // the count used up: written
    c.reads.push_back(axitest::read_op((1u << 24) - 64, 4));  // the last bytes: fine
    c.reads.push_back(axitest::read_op(1u << 24, 4));         // past the end: DECERR
    c.max_reads = 1;
    run(m, c);
    for (auto& o : c.reads) CHECK(o.done && o.rlast_on_last);
    for (uint8_t r : c.reads[0].resp) CHECK_EQ(r, axi::SLVERR);
    for (uint64_t w : c.reads[0].got) CHECK_EQ(w, axi::FAULT_DATA);
    for (uint8_t r : c.reads[1].resp) CHECK_EQ(r, axi::OKAY);
    CHECK_EQ(c.writes[0].resp[0], axi::SLVERR);
    CHECK_EQ(c.writes[1].resp[0], axi::OKAY);
    for (uint8_t r : c.reads[2].resp) CHECK_EQ(r, axi::OKAY);
    for (uint8_t r : c.reads[3].resp) CHECK_EQ(r, axi::DECERR);
    uint64_t pg[512];
    fs.read_page(32, pg);
    for (unsigned k = 0; k < 16; k++) CHECK_EQ(pg[32 + k], c.writes[1].data[k]);
    CHECK_EQ(m.error_bursts, 3u);
}

// Jitter: random latencies and readies; every read sees what the store held, each ID's in its order.
TEST(lpddr_jitter_keeps_the_rules) {
    for (uint32_t seed = 1; seed <= 20; seed++) {
        FileStore fs;
        for (int p = 0; p < 4; p++) fill_page(fs, 32 * p, seed);
        axi::LpddrTiming tm;
        tm.jitter = true; tm.seed = seed;
        LpddrModel m(&fs, tm);
        Client c;
        c.max_reads = 8; c.seed = seed; c.gap_one_in = 3;
        test::Rng rng(seed);
        for (int k = 0; k < 40; k++) {
            const unsigned beats = 1 + unsigned(rng.below(32));
            const uint64_t off = 16 * rng.below(256 - beats);
            c.reads.push_back(axitest::read_op(4096 * rng.below(4) + off, beats, uint8_t(rng.below(4))));
        }
        run(m, c, 1000000);
        for (auto& o : c.reads) {
            CHECK(o.done && o.rlast_on_last && o.rlasts == 1);
            for (unsigned b = 0; b < o.beats; b++) {
                const uint64_t a = o.addr + 16 * b;
                const uint64_t page = a >> 12, word = (a & 4095) / 8;
                CHECK_EQ(o.got[2 * b], (uint64_t(page * 32) << 32 ^ word ^ seed));
            }
        }
        for (size_t i = 0; i < c.reads.size(); i++)
            for (size_t j = i + 1; j < c.reads.size(); j++)
                if (c.reads[i].id == c.reads[j].id) CHECK(c.reads[j].t_first > c.reads[i].t_done);
    }
}

DEATH_TEST(lpddr_stops_on_a_burst_across_4k, "across a 4 KB") {
    FileStore fs;
    LpddrModel m(&fs);
    Client c;
    c.reads.push_back(axitest::read_op(4096 - 64, 8));
    run(m, c);
}

DEATH_TEST(lpddr_stops_on_a_misplaced_wlast, "wlast set on beat 1 of 2") {
    FileStore fs;
    LpddrModel m(&fs);
    axi::MasterOut aw;
    aw.awvalid = true; aw.awaddr = 0; aw.awlen = 1;   // a write of two beats
    m.edge(0, aw);
    axi::MasterOut w;
    w.wvalid = true; w.wlast = true;                  // its first beat, with wlast
    m.edge(1, w);
}
