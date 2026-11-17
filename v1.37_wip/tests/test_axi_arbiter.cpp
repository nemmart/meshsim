// test_axi_arbiter: the AXI arbiter (src/units/axi_arbiter.h; ../v1.4_wip/planning/AXI-INTERFACE.md section 3) between
// test masters on the clients' ports (tests/axi_test_client.h) and the LPDDR model (src/units/lpddr_model.h): a line's
// latency through it; a long burst cut into pieces and seen whole; the IDs; round robin and its fairness; the cap that
// lets a line pass a page; the limit of bursts outstanding; errors in one piece; random traffic with jitter; and the RW
// engine on it through its adapter.
#include <cstring>
#include <vector>

#include "axi_test_client.h"
#include "harness.h"
#include "units/axi_arbiter.h"
#include "units/file_store.h"
#include "units/lpddr_model.h"

using units::AxiArbiter;
using units::LpddrModel;
using units::tracer::FileStore;
using axitest::Client;
namespace axi = units::axi;

namespace {

struct Bench {
    FileStore fs;
    LpddrModel lpddr;
    AxiArbiter arb;
    Client cl[axi::CLIENTS];
    std::vector<std::pair<int64_t, unsigned>> ar_ids, aw_ids;   // the controller's AR and AW handshakes: cycle, ID
    std::vector<unsigned> ar_lens;
    int64_t t = 0;
    explicit Bench(const axi::LpddrTiming& tm = {}, const axi::ArbiterConfig& ac = {}) : lpddr(&fs, tm), arb(ac) {}
    void cycle() {
        axi::MasterOut ci[axi::CLIENTS];
        axi::SlaveOut co[axi::CLIENTS];
        for (unsigned c = 0; c < axi::CLIENTS; c++) { ci[c] = cl[c].out; co[c] = arb.client_out(c); }
        const axi::MasterOut m = arb.controller_out();
        const axi::SlaveOut s = lpddr.out();
        if (m.arvalid && s.arready) { ar_ids.push_back({t, m.arid}); ar_lens.push_back(m.arlen + 1u); }
        if (m.awvalid && s.awready) aw_ids.push_back({t, m.awid});
        lpddr.edge(t, m);
        arb.edge(t, ci, s);
        for (unsigned c = 0; c < axi::CLIENTS; c++) cl[c].edge(t, co[c]);
        t++;
    }
    bool done() const {
        for (const Client& c : cl) if (!c.finished()) return false;
        return arb.idle() && lpddr.idle();
    }
    void run(int64_t limit = 1000000) { while (!done() && t < limit) cycle(); CHECK(done()); }
    void fill_page(int32_t unit, uint64_t tag) {
        uint64_t pg[512];
        for (int i = 0; i < 512; i++) pg[i] = uint64_t(unit) << 32 ^ uint64_t(i) ^ tag;
        fs.write_page(unit, pg);
    }
    uint64_t word(uint64_t byte) const {   // a filled page's word at a byte address, tag 0
        return uint64_t((byte >> 12) * 32) << 32 ^ ((byte & 4095) / 8);
    }
};

}  // namespace

// A line through the arbiter: one cycle each way, so its first beat 72 cycles after its handshake and its last 79:
// the 80 cycles of AXI section 5.
TEST(arbiter_a_line_takes_80) {
    Bench b;
    b.fill_page(32, 0);
    b.cl[0].reads.push_back(axitest::read_op(4096 + 384, 8));
    b.run();
    const auto& o = b.cl[0].reads[0];
    CHECK(o.done && o.rlast_on_last && o.rlasts == 1);
    CHECK_EQ(o.t_first - o.t_taken, 72);
    CHECK_EQ(o.t_done - o.t_taken, 79);
    for (unsigned k = 0; k < 16; k++) CHECK_EQ(o.got[k], b.word(4096 + 384 + 8 * k));
    CHECK_EQ(b.ar_ids.size(), size_t(1));
    CHECK_EQ(b.ar_ids[0].second, 0u);
}

// A page read is cut into 8 pieces of 32 beats with the client's ID; the client sees one burst, one RLAST.
TEST(arbiter_a_page_read_cut_and_seen_whole) {
    Bench b;
    b.fill_page(32 * 7, 0);
    b.cl[1].reads.push_back(axitest::read_op(7 * 4096, 256));
    b.run();
    const auto& o = b.cl[1].reads[0];
    CHECK(o.done && o.rlast_on_last);
    CHECK_EQ(o.rlasts, 1u);
    for (unsigned k = 0; k < 512; k++) CHECK_EQ(o.got[k], b.word(7 * 4096 + 8 * k));
    CHECK_EQ(b.ar_ids.size(), size_t(8));
    for (auto& [cy, id] : b.ar_ids) CHECK_EQ(id, 1u);
    for (unsigned l : b.ar_lens) CHECK_EQ(l, 32u);
    CHECK_EQ(b.arb.read_pieces[1], 8u);
}

// A page and a half page written, one response each; the data in LPDDR; the pieces' order.
TEST(arbiter_page_writes_one_response) {
    Bench b;
    b.cl[2].writes.push_back(axitest::write_op(9 * 4096, 256, 0x11));
    b.cl[2].writes.push_back(axitest::write_op(10 * 4096 + 2048, 128, 0x22));
    b.run();
    for (auto& w : b.cl[2].writes) { CHECK(w.done); CHECK_EQ(w.resp[0], axi::OKAY); }
    uint64_t pg[512];
    b.fs.read_page(9 * 32, pg);
    CHECK(std::memcmp(pg, b.cl[2].writes[0].data.data(), 4096) == 0);
    b.fs.read_page(10 * 32, pg);
    CHECK(std::memcmp(pg + 256, b.cl[2].writes[1].data.data(), 2048) == 0);
    CHECK_EQ(b.aw_ids.size(), size_t(12));
    CHECK_EQ(b.arb.write_pieces[2], 12u);
}

// Three clients reading pages at once: the controller sees their pieces round robin, one each in turn.
TEST(arbiter_round_robin_is_fair) {
    Bench b;
    for (unsigned c = 1; c <= 3; c++) {
        b.fill_page(int32_t(32 * c), 0);
        for (int k = 0; k < 2; k++) b.cl[c].reads.push_back(axitest::read_op(c * 4096, 256));
        b.cl[c].max_reads = 2;
    }
    b.run();
    CHECK_EQ(b.ar_ids.size(), size_t(48));
    // once all three are waiting, the IDs go 1, 2, 3, 1, 2, 3, ...
    unsigned seen_bad = 0;
    for (size_t i = 3; i < 45; i++) if (b.ar_ids[i].second == b.ar_ids[i - 1].second) seen_bad++;
    CHECK_EQ(seen_bad, 0u);
    for (unsigned c = 1; c <= 3; c++) CHECK_EQ(b.arb.read_pieces[c], 16u);
    for (unsigned c = 1; c <= 3; c++) for (auto& o : b.cl[c].reads) CHECK(o.done && o.rlasts == 1);
}

// The cap: a line asked for while another client streams pages goes after at most one piece.
TEST(arbiter_a_line_passes_a_page) {
    Bench b;
    for (int p = 0; p < 4; p++) b.fill_page(32 * (20 + p), 0);
    for (int p = 0; p < 4; p++) b.cl[1].reads.push_back(axitest::read_op(uint64_t(20 + p) * 4096, 256));
    b.cl[1].max_reads = 1;                            // a page at a time, as the Updater: 8 pieces at the controller
    for (int k = 0; k < 400; k++) b.cycle();          // the pages under way
    b.cl[0].reads.push_back(axitest::read_op(20 * 4096, 8));
    b.run();
    const auto& line = b.cl[0].reads[0];
    CHECK(line.done);
    CHECK(line.t_done - line.t_taken <= 80 + 32);
    // with no cap, a page-long piece would hold it back by up to 256 beats: the same run with turn_beats 256
    axi::ArbiterConfig wide;
    wide.turn_beats = 256;
    Bench w(axi::LpddrTiming{}, wide);
    for (int p = 0; p < 4; p++) w.fill_page(32 * (20 + p), 0);
    for (int p = 0; p < 4; p++) w.cl[1].reads.push_back(axitest::read_op(uint64_t(20 + p) * 4096, 256));
    w.cl[1].max_reads = 1;
    for (int k = 0; k < 400; k++) w.cycle();
    w.cl[0].reads.push_back(axitest::read_op(20 * 4096, 8));
    w.run();
    CHECK(w.cl[0].reads[0].t_done - w.cl[0].reads[0].t_taken > line.t_done - line.t_taken);
}

// A line's write waits behind at most one piece of another client's page.
TEST(arbiter_a_line_write_passes_a_page_write) {
    Bench b;
    for (int p = 0; p < 3; p++) b.cl[1].writes.push_back(axitest::write_op(uint64_t(30 + p) * 4096, 256, uint64_t(p)));
    for (int k = 0; k < 40; k++) b.cycle();
    b.cl[0].writes.push_back(axitest::write_op(40 * 4096, 8, 9));
    b.run();
    const auto& line = b.cl[0].writes[0];
    CHECK(line.done);
    CHECK(line.t_done < b.cl[1].writes[0].t_done);    // its response before the first page's
    CHECK(line.t_done - line.t_raised <= 2 + 32 + 8 + 70 + 4);
}

// The limit: a client's fifth read waits for its first's answer.
TEST(arbiter_client_bursts_limit) {
    Bench b;
    b.fill_page(0, 0);
    b.cl[3].max_reads = 8;
    for (int k = 0; k < 6; k++) b.cl[3].reads.push_back(axitest::read_op(64 * uint64_t(k), 1));
    b.run();
    CHECK(b.cl[3].reads[4].t_taken >= b.cl[3].reads[0].t_done);   // taken in the cycle the first's last beat reaches the client
    CHECK(b.cl[3].reads[3].t_taken < b.cl[3].reads[0].t_done);
}

// An error in one piece: the read's beats of that piece carry it, the others OKAY; the write's one response carries it.
TEST(arbiter_error_in_one_piece) {
    Bench b;
    b.fill_page(32 * 5, 0);
    axi::LpddrFault f;
    f.first = 5 * 4096 + 1024; f.last = 5 * 4096 + 1024; f.resp = axi::SLVERR;
    b.lpddr.add_fault(f);
    b.cl[1].reads.push_back(axitest::read_op(5 * 4096, 256));
    b.cl[2].writes.push_back(axitest::write_op(5 * 4096, 256, 3));
    b.run();
    const auto& r = b.cl[1].reads[0];
    CHECK(r.done && r.rlasts == 1);
    for (unsigned k = 0; k < 256; k++) CHECK_EQ(r.resp[k], k / 32 == 2 ? axi::SLVERR : axi::OKAY);
    CHECK_EQ(b.cl[2].writes[0].resp[0], axi::SLVERR);
}

// Random traffic from four clients, each in a region of its own, reads and writes, the LPDDR model jittering: every
// read sees what the client last wrote there (it reads only after the write's response) or the page as filled.
TEST(arbiter_random_with_jitter) {
    for (uint32_t seed = 1; seed <= 12; seed++) {
        axi::LpddrTiming tm;
        tm.jitter = true; tm.seed = seed;
        Bench b(tm);
        test::Rng rng(seed);
        for (unsigned c = 0; c < axi::CLIENTS; c++) {
            b.fill_page(int32_t(32 * (100 + c)), 0);
            b.cl[c].seed = seed * 10 + c; b.cl[c].gap_one_in = 4; b.cl[c].max_reads = 1 + unsigned(rng.below(4));
            for (int k = 0; k < 20; k++) {
                const unsigned sizes[4] = {8, 32, 128, 256};
                const unsigned beats = sizes[rng.below(4)];
                const uint64_t off = 16 * beats * rng.below(256 / beats);
                b.cl[c].reads.push_back(axitest::read_op((100 + c) * 4096 + off, beats));
            }
        }
        b.run();
        for (unsigned c = 0; c < axi::CLIENTS; c++)
            for (auto& o : b.cl[c].reads) {
                CHECK(o.done && o.rlasts == 1 && o.rlast_on_last);
                for (unsigned k = 0; k < 2 * o.beats; k++) CHECK_EQ(o.got[k], b.word(o.addr + 8 * k));
            }
        // then writes, each read back after its response
        Bench w(tm);
        for (unsigned c = 0; c < axi::CLIENTS; c++) {
            w.cl[c].seed = seed + c; w.cl[c].gap_one_in = 3;
            for (int k = 0; k < 6; k++) w.cl[c].writes.push_back(axitest::write_op((200 + 4 * c + rng.below(4)) * 4096, 256, rng.next()));
        }
        w.run();
        for (unsigned c = 0; c < axi::CLIENTS; c++) for (auto& o : w.cl[c].writes) CHECK(o.done && o.resp[0] == axi::OKAY);
        // the last write to each page is what LPDDR holds
        for (unsigned c = 0; c < axi::CLIENTS; c++)
            for (unsigned p = 0; p < 4; p++) {
                const uint64_t a = (200 + 4 * c + p) * 4096;
                const axitest::Op* last = nullptr;
                for (auto& o : w.cl[c].writes) if (o.addr == a) last = &o;
                if (!last) continue;
                uint64_t pg[512];
                w.fs.read_page(int32_t(a >> 7), pg);
                CHECK(std::memcmp(pg, last->data.data(), 4096) == 0);
            }
    }
}

// The RW engine (v1.36's, unchanged) on the arbiter through its adapter: a write job, then a read job of what it wrote.
TEST(arbiter_rw_engine_through_its_adapter) {
    FileStore fs;
    LpddrModel lpddr(&fs);
    AxiArbiter arb;
    units::RwEngineOnArbiter adapter;
    units::RwEngine e(65, units::RwEngineConfig{});
    e.attach(&adapter);
    const unsigned beats = 40;
    for (unsigned k = 0; k < beats; k++) e.accept(0, 1, 16 * k, 2 /* 128 bits */, 0x1000 + k, 0x2000 + k);
    int64_t t = 1;
    auto cycle = [&]() {
        axi::MasterOut ci[axi::CLIENTS];
        const axi::MasterOut m = arb.controller_out();
        const axi::SlaveOut s = lpddr.out();
        adapter.in = arb.client_out(axi::RW_ENGINE);
        e.step(t);
        ci[axi::RW_ENGINE] = adapter.out;
        adapter.clear();
        lpddr.edge(t, m);
        arb.edge(t, ci, s);
        t++;
    };
    e.write_word(t, units::RwEngine::RW_CONFIG, 1u | beats << 1);       // a write of 40 beats
    e.write_word(t, units::RwEngine::RW_LPDDR_ADDR, 0x300);             // from beat 0x300: byte 0x3000
    e.write_word(t, units::RwEngine::RW_BUFFER_ADDR, 0);
    e.run(t);
    cycle();
    while (e.busy && t < 10000) cycle();
    CHECK(!e.busy);
    CHECK(!e.lpddr_error);
    uint64_t pg[512];
    fs.read_page(0x3000 >> 7, pg);
    for (unsigned k = 0; k < beats; k++) { CHECK_EQ(pg[2 * k], 0x1000u + k); CHECK_EQ(pg[2 * k + 1], 0x2000u + k); }
    // a read of 12 of them (the output queue holds 16)
    e.write_word(t, units::RwEngine::RW_CONFIG, 0u | 12u << 1);
    e.write_word(t, units::RwEngine::RW_LPDDR_ADDR, 0x304);
    e.run(t);
    cycle();
    while (e.busy && t < 20000) cycle();
    CHECK(!e.busy);
    for (unsigned k = 0; k < 12; k++) {
        uint64_t w[2];
        CHECK(e.out_ready(t, 0));
        e.out_take(t, 0, w);
        CHECK_EQ(w[0], 0x1000u + 4 + k);
        CHECK_EQ(w[1], 0x2000u + 4 + k);
        t++;
    }
    CHECK_EQ(arb.write_bursts[axi::RW_ENGINE], 10u);   // 40 beats in groups of 4 (17.7)
}
