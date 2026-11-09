// test_replayer: ref's test/ReplayerUnitTests.java ported group by group (WORKER15), on units::Replayer with
// tests/replay_window.h (ReplayWindow) and tests/replay_driver.h (ReplayDriver). Each group is one TEST; its checks
// and expected values are ref's, in ref's order, and its seeded inputs come from java.util.Random as ref's do.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "harness.h"
#include "replay_driver.h"
#include "replay_t.h"
#include "replay_window.h"

using namespace rtest;
using units::Replayer;
using units::ReplayerStream;
namespace alu = units::alu;

namespace {

// ---- RISC-V encoders (independent of the decoder) ----
uint32_t r_(int f7, int rs2, int rs1, int f3, int rd, int op) { return uint32_t(f7) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | uint32_t(op); }
uint32_t i_(int imm, int rs1, int f3, int rd, int op) { return uint32_t(imm & 0xFFF) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | uint32_t(op); }
uint32_t s_(int imm, int rs2, int rs1, int f3, int op) { return uint32_t((imm >> 5) & 0x7F) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(imm & 31) << 7 | uint32_t(op); }
uint32_t b_(int imm, int rs2, int rs1, int f3, int op) {
    return uint32_t((imm >> 12) & 1) << 31 | uint32_t((imm >> 5) & 63) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t((imm >> 1) & 15) << 8 | uint32_t((imm >> 11) & 1) << 7 | uint32_t(op);
}
uint32_t u_(int imm, int rd, int op) { return (uint32_t(imm) & 0xFFFFF000u) | uint32_t(rd) << 7 | uint32_t(op); }
uint32_t j_(int imm, int rd, int op) {
    return uint32_t((imm >> 20) & 1) << 31 | uint32_t((imm >> 1) & 0x3FF) << 21 | uint32_t((imm >> 11) & 1) << 20 | uint32_t((imm >> 12) & 0xFF) << 12 | uint32_t(rd) << 7 | uint32_t(op);
}
constexpr int OP_IMM = 0x13, OP = 0x33, LOAD = 0x03, STORE = 0x23, BRANCH = 0x63, JAL = 0x6F, LUI = 0x37, AMO = 0x2F, CUSTOM0 = 0x0B, SYSTEM = 0x73;

int64_t sext(uint64_t v) { return int64_t(v); }
std::unique_ptr<Replayer> fresh() { return std::make_unique<Replayer>(0); }

// ================================================================ the decode index (4.19.5)
void decode_index() {
    T::group("decode index");
    using units::rowcore::decode_index;
    uint32_t add = r_(0, 11, 10, 0, 10, OP), mul = r_(1, 11, 10, 0, 10, OP), sub = r_(0x20, 11, 10, 0, 10, OP);
    T::eq((OP >> 2) << 5, decode_index(add), "ADD: {w[6:2], funct3 0, w30 0, w25 0}");
    T::eq((OP >> 2) << 5 | 1, decode_index(mul), "MUL: w25 = 1");
    T::eq((OP >> 2) << 5 | 2, decode_index(sub), "SUB: w30 = 1");
    T::eq(decode_index(add), decode_index(add & ~1u), "bit 0 (the compressed flag) plays no part");
    std::set<unsigned> seen;
    const int f5[] = {0x02, 0x03, 0x01, 0x00, 0x04, 0x0C, 0x08, 0x10, 0x14, 0x18, 0x1C};
    for (int wd : {2, 3}) for (int f : f5) {
        uint32_t w = uint32_t(f) << 27 | 3u << 25 | 11u << 20 | 10u << 15 | uint32_t(wd) << 12 | 5u << 7 | AMO;
        unsigned x = decode_index(w);
        T::eq(1 << 10 | (wd & 1) << 5 | f, x, "AMO funct5 " + std::to_string(f) + " width " + std::to_string(wd));
        T::check(seen.insert(x).second, "AMO index unique");
    }
    for (int f7 = 0; f7 <= 5; f7++) for (int f3 = 0; f3 < 8; f3++) {
        unsigned x = decode_index(r_(f7, 1, 2, f3, 3, CUSTOM0));
        T::eq(1 << 10 | 1 << 8 | f7 << 3 | f3, x, "inline f7 " + std::to_string(f7) + " f3 " + std::to_string(f3));
    }
    T::eq(1 << 10 | 2 << 8 | 8, decode_index(0x00000073), "ECALL");
    T::eq(1 << 10 | 2 << 8 | 9, decode_index(0x00100073), "EBREAK");
    T::eq(1 << 10 | 2 << 8 | 10, decode_index(0x30200073), "MRET");
    T::eq(1 << 10 | 2 << 8 | 11, decode_index(0x10500073), "WFI: another funct3-0 SYSTEM word");
    T::eq(1 << 10 | 2 << 8 | 1, decode_index(i_(0x305, 10, 1, 0, SYSTEM)), "CSRRW: sel = funct3");
    T::eq(1 << 10 | 2 << 8 | 2, decode_index(i_(0x341, 0, 2, 10, SYSTEM)), "CSRRS");
}

// ================================================================ immediates (4.19.3, code 132)
void immediates() {
    T::group("immediates");
    using units::rowcore::source_imm;
    JRandom rnd(1);
    bool ok = true;
    for (int k = 0; k < 2000; k++) {
        int i12 = rnd.next_int(4096) - 2048, b13 = (rnd.next_int(4096) - 2048) * 2, j21 = (rnd.next_int(1 << 20) - (1 << 19)) * 2;
        int u20 = int(uint32_t(rnd.next_int()) & 0xFFFFF000u);
        ok &= sext(source_imm(1, i_(i12, 5, 0, 6, OP_IMM))) == i12;
        ok &= sext(source_imm(2, s_(i12, 5, 6, 3, STORE))) == i12;
        ok &= sext(source_imm(3, b_(b13, 5, 6, 0, BRANCH))) == b13;
        ok &= sext(source_imm(4, u_(u20, 5, LUI))) == int64_t(u20);
        ok &= sext(source_imm(5, j_(j21, 5, JAL))) == j21;
        ok &= sext(source_imm(5, j_(j21, 5, JAL) & ~1u)) == j21;
    }
    T::check(ok, "I, S, B, U, J: 2,000 random immediates each round-trip, sign-extended to 64 bits");
    T::eq(0, sext(source_imm(0, r_(0x7F, 31, 31, 7, 31, OP))), "R: 0");
}

// ================================================================ the decompressors (4.19.15)
void decompressors() {
    T::group("decompressors");
    JRandom rnd(7);
    for (int trial = 0; trial < 40; trial++) {
        std::vector<LogEvent> ev;
        uint64_t pool[24];
        for (uint64_t& p : pool) p = rnd.next_boolean() ? uint64_t(uint32_t(rnd.next_int())) : uint64_t(rnd.next_long());
        uint64_t last_ld = 0;
        for (int k = 0; k < 3000; k++) {
            int kind = rnd.next_int(10);
            if (kind < 5) {
                uint64_t v = pool[rnd.next_int(24)];
                if (rnd.next_int(20) == 0) v = uint64_t(rnd.next_long());
                ev.push_back({true, v, true, false, 0});
                last_ld = v;
            } else if (kind < 6) ev.push_back({true, uint64_t(int64_t(rnd.next_int(5000))), false, false, 0});
            else {
                uint64_t v = rnd.next_int(3) == 0 ? last_ld : pool[rnd.next_int(24)];
                ev.push_back({false, 0, false, true, v});
            }
        }
        bool rc = trial % 2 == 0, wc = trial % 3 != 0;
        ReplayWindow w;
        w.set_logs(ev, rc, wc);
        auto rp = fresh();
        rp->rlog.reset(rc, unsigned(w.rlog.size() / 2), w.rlog_vals);
        rp->wlog.reset(wc, unsigned(w.wlog.size() / 2), w.wlog_vals);
        size_t rn = 0, wn = 0;
        int64_t t = 0;
        bool ok = true;
        int hits15 = 0;
        for (const LogEvent& e : ev) {
            if (e.has_read) {
                while (rp->rlog.peek() == ReplayerStream::WAIT) { rp->rlog.push(t++, w.rlog[2 * rn], w.rlog[2 * rn + 1]); rn++; }
                ok &= rp->rlog.peek() == ReplayerStream::AVAIL && rp->rlog.peek_value == e.read;
                rp->rlog.commit(); rp->rlog.release_consumed(rp->ev);
                if (e.load) rp->wlog.last_load(e.read);
            }
            if (e.has_write) {
                while (rp->wlog.peek() == ReplayerStream::WAIT) { rp->wlog.push(t++, w.wlog[2 * wn], w.wlog[2 * wn + 1]); wn++; }
                ok &= rp->wlog.peek() == ReplayerStream::AVAIL && rp->wlog.peek_value == e.write;
                if (wc && rp->wlog.peek_hit == 15) hits15++;
                rp->wlog.commit(); rp->wlog.release_consumed(rp->ev);
            }
        }
        std::string n = "trial " + std::to_string(trial) + " (read " + (rc ? "LRU" : "raw") + ", write " + (wc ? "LRU+E15" : "raw") + ")";
        T::check(ok, n + ": every value decodes to what was encoded");
        T::check(rp->rlog.peek() == ReplayerStream::END && rp->wlog.peek() == ReplayerStream::END, n + ": both logs end exactly at their lengths");
        T::eq(0, rp->rlog.occ + rp->wlog.occ, n + ": every entry left its queue, including the one each stream ends inside");
        if (wc) T::check(hits15 > 100, n + ": the last-load entry is used (" + std::to_string(hits15) + " index-15 hits)");
    }
    // move to front, by hand
    Bits bits;
    put_value(bits, -1, 1); put_value(bits, -1, 2); put_value(bits, -1, 3);
    put_value(bits, 2, 0); put_value(bits, 1, 0); put_value(bits, -1, 0x100000000ull);
    std::vector<uint64_t> ent = bits.entries();
    auto rp = fresh();
    rp->rlog.reset(true, unsigned(ent.size() / 2), 6);
    for (size_t k = 0; k < ent.size() / 2; k++) rp->rlog.push(int64_t(k), ent[2 * k], ent[2 * k + 1]);
    const uint64_t expect[] = {1, 2, 3, 1, 3, 0x100000000ull};
    for (uint64_t e : expect) {
        T::check(rp->rlog.peek() == ReplayerStream::AVAIL, "MTF: available");
        T::eq(int64_t(e), int64_t(rp->rlog.peek_value), "MTF value");
        rp->rlog.commit();
    }
    T::check(rp->rlog.E[0] == 0x100000000ull && rp->rlog.E[1] == 3 && rp->rlog.E[2] == 1 && rp->rlog.E[3] == 2 && rp->rlog.E[4] == 0, "MTF table order");
    // the write log: hit 15 reorders nothing; a miss leaves E[15] alone and drops E[14]
    auto rw = fresh();
    Bits wb;
    for (int k = 1; k <= 15; k++) put_value(wb, -1, uint64_t(k));
    put_value(wb, 15, 0); put_value(wb, -1, 99);
    std::vector<uint64_t> we = wb.entries();
    rw->wlog.reset(true, unsigned(we.size() / 2), 17);
    for (size_t k = 0; k < we.size() / 2; k++) rw->wlog.push(int64_t(k), we[2 * k], we[2 * k + 1]);
    for (int k = 0; k < 15; k++) { rw->wlog.peek(); rw->wlog.commit(); }
    rw->wlog.last_load(0xBEEF);
    rw->wlog.peek();
    T::eq(0xBEEF, int64_t(rw->wlog.peek_value), "write log hit 15: the last load");
    rw->wlog.commit();
    T::eq(15, int64_t(rw->wlog.E[0]), "hit 15 reorders nothing");
    rw->wlog.peek(); rw->wlog.commit();
    T::eq(99, int64_t(rw->wlog.E[0]), "a miss goes to the front");
    T::eq(2, int64_t(rw->wlog.E[14]), "E[14] was 1, dropped; now 2");
    T::eq(0xBEEF, int64_t(rw->wlog.E[15]), "E[15] untouched by a miss");
    // a 32-bit miss is zero-extended; a value straddling two entries waits for the second
    Bits b2;
    b2.put(0, 100); put_value(b2, -1, 0xFFFFFFFFull);
    std::vector<uint64_t> e2 = b2.entries();
    auto r2 = fresh();
    r2->rlog.reset(true, unsigned(e2.size() / 2), 2);
    r2->rlog.push(0, e2[0], e2[1]);
    r2->rlog.consumed = 100;
    T::eq(ReplayerStream::WAIT, r2->rlog.peek(), "a value straddling entries waits for the second entry");
    r2->rlog.push(1, e2[2], e2[3]);
    T::eq(ReplayerStream::AVAIL, r2->rlog.peek(), "and decodes once it lands");
    T::eq(0xFFFFFFFFll, int64_t(r2->rlog.peek_value), "a 32-bit miss is zero-extended");
}

// Run a group; the TEST fails if any of its checks did.
bool group_ok(void (*fn)()) {
    int f0 = T::failed;
    fn();
    return T::failed == f0;
}

}  // namespace

TEST(unit_decode_index) { CHECK(group_ok(decode_index)); }
TEST(unit_immediates) { CHECK(group_ok(immediates)); }
TEST(unit_decompressors) { CHECK(group_ok(decompressors)); }
