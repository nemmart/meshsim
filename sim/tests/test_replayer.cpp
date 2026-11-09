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

// ================================================================ a hand-checked program
constexpr int A0 = 10, A1 = 11, A2 = 12, A5 = 15;
constexpr unsigned T_ADD = 2, T_ADDI = 3, T_LB = 4, T_SD = 8, T_BEQ = 9, T_JAL = 10, T_MUL = 11, T_SLLI = 12;
constexpr uint64_t V1 = 0x112233F555667788ull, V2 = 0x42;
constexpr unsigned PADDING = 8;

void templates(ReplayWindow& w) {
    w.fixed_rom();
    w.rom(T_ADD, {W::op(alu::ADD).rd(129).rs1(130).rs2(131).fmt(0).rd0(1).report_c()});
    w.rom(T_ADDI, {W::op(alu::ADDI).rd(129).rs1(130).rs2(132).fmt(1).rd0(3).report_c()});
    w.rom(T_LB, {W::op(alu::LOAD_B).rd(137).rs1(130).rs2(132).fmt(1).rd0(2).virt().from_log().sign(),
                 W::op(alu::WMASK_B).rd(138).rs1(130).rs2(132).fmt(1).rd0(2).virt(),
                 W::op(alu::PEXT_S).rd(129).rs1(137).rs2(138).fmt(1).rd0(2).virt().report_c()});
    w.rom(T_SD, {W::op(alu::SD).rd(132).rs1(130).rs2(131).fmt(2).from_log().report_c().sign()});
    w.rom(T_BEQ, {W::op(alu::BEQ).rd(132).rs1(130).rs2(131).fmt(3).nfv().report_c().sign()});
    w.rom(T_JAL, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
    w.rom(T_MUL, {W::op(alu::MUL).rd(129).rs1(130).rs2(131).fmt(0).rd0(1).report_c()});
    w.rom(T_SLLI, {W::op(alu::MULI).rd(129).rs1(130).rs2(134).fmt(1).rd0(1).virt().report_c()});
}

const std::vector<uint32_t>& prog() {
    static const std::vector<uint32_t> p = {
        i_(0x100, 0, 0, A0, OP_IMM),   // 0x1000 addi a0, x0, 0x100
        i_(4, A0, 0, A5, LOAD),        // 0x1004 lb a5, 4(a0)
        i_(4, A0, 0, 0, LOAD),         // 0x1008 lb x0, 4(a0): remapped, temporaries + 1
        r_(0, A0, A0, 0, 0, OP),       // 0x100c add x0, a0, a0: the shared NOP row, virtual
        i_(0, 0, 0, 0, OP_IMM),        // 0x1010 addi x0, x0, 0: the shared NOP row, not virtual
        s_(8, A5, A0, 3, STORE),       // 0x1014 sd a5, 8(a0)
        i_(3, A0, 1, A1, OP_IMM),      // 0x1018 slli a1, a0, 3: VirtualMULI
        r_(1, A0, A1, 0, A2, OP),      // 0x101c mul a2, a1, a0: 5 cycles
        b_(8, A0, A0, 0, BRANCH),      // 0x1020 beq a0, a0, +8: taken
        b_(8, A1, A0, 0, BRANCH),      // 0x1028 beq a0, a1, +8: not taken
        b_(0, A0, A0, 0, BRANCH),      // 0x102c beq a0, a0, 0: a self-jump, sequential
        i_(1, A0, 0, A0, OP_IMM) & ~1u,  // 0x1030 c.addi a0, 1 (stored decompressed, bit 0 clear)
        j_(0, 0, JAL),                 // 0x1032 jal x0, 0: the program ends; link into v40
    };
    return p;
}

std::unique_ptr<ReplayWindow> program_window(bool cc, bool rc, bool wc, bool store_log) {
    auto w = std::make_unique<ReplayWindow>();
    templates(*w);
    for (uint32_t ins : prog()) {
        uint32_t op = (ins | 1) & 0x7F, f3 = (ins >> 12) & 7;
        unsigned t = op == OP ? ((ins >> 25) == 1 ? T_MUL : T_ADD) : op == OP_IMM ? (f3 == 1 ? T_SLLI : T_ADDI) : op == LOAD ? T_LB
                     : op == STORE ? T_SD : op == BRANCH ? T_BEQ : T_JAL;
        w->decode_rom(ins, t);
    }
    w->pc = 0x1000; w->instr_idx = 1; w->store_log = store_log;
    w->regs[A5] = 0x55; w->regs[40] = 0x4040;
    std::vector<uint32_t> code = prog();
    code.resize(code.size() + PADDING, 0);
    w->set_code(code, cc);
    std::vector<LogEvent> ev = {{true, V1, true, false, 0}, {true, V2, true, false, 0}, {false, 0, false, store_log, V2},
                                {true, 17, false, false, 0}, {true, 0, false, false, 0}};
    w->set_logs(ev, rc, wc);
    return w;
}

// Expected rows: {instr_idx, opcode, rd_reg, rs1_reg, rs2_reg, seq, flags, pc, imm, rs1, rs2, rd_prev, rd, mem_addr,
// mem_value, sign_imm}; flags written comp virt first rdI rs1I rs2I, left to right.
std::vector<std::vector<uint64_t>> expected(bool store_log) {
    const uint64_t M32 = 0xFFull << 32, F5 = 0xFFFFFFFFFFFFFFF5ull;
    return {
        {1, 2, A0, 0, 0, 0, 0b000001, 0x1000, 0x100, 0, 0, 0, 0x100, 0, 0, 0},
        {2, 110, 40, A0, 0, 2, 0b011001, 0x1004, 4, 0x100, 0, 0x4040, V1, 0x100, V1, 1},
        {3, 156, 41, A0, 0, 1, 0b010001, 0x1004, 4, 0x100, 0, 0, M32, 0, 0, 0},
        {4, 146, A5, 40, 41, 0, 0b010000, 0x1004, 0, V1, M32, 0x55, F5, 0, 0, 0},
        {5, 110, 41, A0, 0, 2, 0b011001, 0x1008, 4, 0x100, 0, M32, V2, 0x100, V2, 1},
        {6, 156, 42, A0, 0, 1, 0b010001, 0x1008, 4, 0x100, 0, 0, M32, 0, 0, 0},
        {7, 146, 40, 41, 42, 0, 0b010000, 0x1008, 0, V2, M32, V1, 0, 0, 0, 0},
        {8, 2, 0, 0, 0, 0, 0b011001, 0x100c, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 2, 0, 0, 0, 0, 0b000001, 0x1010, 0, 0, 0, 0, 0, 0, 0, 0},
        {10, 53, 0, A0, A5, 0, 0b000100, 0x1014, 8, 0x100, F5, 0, 0, 0x108, store_log ? V2 : 0, 1},
        {11, 118, A1, A0, 0, 0, 0b011001, 0x1018, 8, 0x100, 0, 0, 0x800, 0, 0, 0},
        {12, 43, A2, A1, A0, 0, 0b000000, 0x101c, 0, 0x800, 0x100, 0, 0x80000, 0, 0, 0},
        {13, 28, 0, A0, A0, 0, 0b000100, 0x1020, 8, 0x100, 0x100, 0, 0, 0, 0, 1},
        {17, 28, 0, A0, A1, 0, 0b000100, 0x1028, 8, 0x100, 0x800, 0, 0, 0, 0, 1},
        {18, 28, 0, A0, A0, 0, 0b000100, 0x102c, 0, 0x100, 0x100, 0, 0, 0, 0, 1},
        {19, 2, A0, A0, 0, 0, 0b100001, 0x1030, 1, 0x100, 0, 0x100, 0x101, 0, 0, 0},
        {20, 34, 40, 0, 0, 0, 0b000011, 0x1032, 0, 0, 0, 0, 0x1036, 0, 0, 0},
    };
}

std::string fmt(const std::vector<uint64_t>& v) {
    std::string s;
    char b[24];
    for (uint64_t x : v) { std::snprintf(b, sizeof b, "%llx ", (unsigned long long)x); s += b; }
    return s;
}

void check_rows(const std::string& what, const ReplayDriver& d, const std::vector<std::vector<uint64_t>>& exp, unsigned padding) {
    uint64_t n = d.rows();
    T::eq(int64_t(exp.size() + padding), int64_t(n), what + ": rows");
    for (size_t k = 0; k < std::min<size_t>(n, exp.size()); k++) {
        const uint64_t* g = d.row(k);
        const std::vector<uint64_t>& e = exp[k];
        uint64_t flags = ((g[1] >> 16) & 1) | ((g[1] >> 17) & 1) << 1 | ((g[1] >> 18) & 1) << 2 | ((g[1] >> 19) & 1) << 3 | ((g[1] >> 20) & 1) << 4 | ((g[1] >> 21) & 1) << 5;
        uint64_t ef = 0;
        for (int bit = 0; bit < 6; bit++) ef |= ((e[6] >> bit) & 1) << (5 - bit);   // Long.reverse(e[6]) >>> 58
        std::vector<uint64_t> got = {g[0] & 0xFFFFFFFF, (g[0] >> 32) & 0xFF, (g[0] >> 40) & 0xFF, (g[0] >> 48) & 0xFF, g[0] >> 56, g[1] & 0xFFFF, flags,
                                     g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], (g[1] >> Replayer::REC_SIGN_IMM) & 1};
        std::vector<uint64_t> want = e;
        want[6] = ef;
        T::check(got == want, what + ": row " + std::to_string(k + 1) + "\n   got  " + fmt(got) + "\n   want " + fmt(want));
        T::eq(0, int64_t(g[1] >> 23), what + ": row " + std::to_string(k + 1) + " spare bits");
    }
    for (size_t k = exp.size(); k < n; k++) {
        bool zero = true;
        for (unsigned i = 0; i < Replayer::REC_LONGS; i++) zero &= d.row(k)[i] == 0;
        T::check(zero, what + ": padding row " + std::to_string(k + 1) + " is all zeros");
    }
}

std::unique_ptr<ReplayDriver> run_window(ReplayWindow& w, uint32_t rows) {
    auto d = std::make_unique<ReplayDriver>(w);
    w.rows = rows;
    d->load();
    d->pass(200000);
    return d;
}

std::string error_report(const Replayer& r) {
    std::string s;
    char b[200];
    for (unsigned i = 0; i < r.fact_shown(); i++) { Replayer::fact_text(r.fact(i), b, sizeof b); s += b; s += "; "; }
    return s;
}

void program() {
    T::group("program, raw streams");
    auto w = program_window(false, false, false, true);
    auto d = run_window(*w, 25);
    check_rows("raw", *d, expected(true), PADDING);
    T::eq(0, int64_t(d->r->pc()), "pc 0 after the program end");
    T::eq(0, d->r->instr_idx(), "instr_idx 0 after the program end");
    T::eq(0x1036, int64_t(d->r->reg(40)), "the remapped link is written into v40");
    T::eq(0x101, int64_t(d->r->reg(A0)), "a0");
    T::eq(0, d->r->errors(d->t), "no condition (v1.26: ERRORS [0]-[6])");
    T::check(d->r->clean(), "clean: " + error_report(*d->r));
    T::check(d->done, "done was posted");
}

void program_compressed() {
    auto exp = expected(true);
    const bool combos[][3] = {{true, true, true}, {true, false, true}, {false, true, false}, {true, true, false}, {false, false, true}};
    for (const auto& c : combos) {
        std::string n = std::string("code ") + (c[0] ? "LRU" : "raw") + ", read " + (c[1] ? "LRU" : "raw") + ", write " + (c[2] ? "LRU" : "raw");
        T::group("program, " + n);
        auto w = program_window(c[0], c[1], c[2], true);
        auto d = run_window(*w, 25);
        check_rows(n, *d, exp, PADDING);
        T::check(d->r->clean(), n + ": clean " + error_report(*d->r));
        if (c[2]) T::check(d->r->wlog.hits >= 1, n + ": the SD's pre-value came from the last-load entry");
    }
}

void store_log_off() {
    T::group("includes store log = 0");
    auto w = program_window(false, false, false, false);
    T::eq(0, int64_t(w->wlog.size()), "the write log is empty");
    auto d = run_window(*w, 25);
    check_rows("no store log", *d, expected(false), PADDING);
    T::eq(0, d->wlog_next, "the write-log queue was never fed (its line stayed quiet)");
    T::check(d->r->clean(), "clean: " + error_report(*d->r));
}

void pext_any_mask() {
    T::group("PEXT, a non-contiguous mask");
    ReplayWindow w;
    w.fixed_rom();
    uint32_t ins = r_(0, A1, A0, 0, A2, OP);
    w.rom(20, {W::op(alu::PEXT_S).rd(129).rs1(130).rs2(131).fmt(0).report_c()});
    w.decode_rom(ins, 20);
    w.pc = 0x1000; w.instr_idx = 1; w.regs[A0] = 0x8000000000000001ull; w.regs[A1] = 0x8000000000000001ull;
    w.set_code({ins, ins}, false);
    w.set_logs({}, false, false);
    auto d = run_window(w, 2);
    T::eq(-1, int64_t(d->row(0)[7]), "two single-bit runs, top bit set: sign-extended from the last bit placed");
    T::eq(2, int64_t(d->r->pext_non_contiguous), "counted as a statistic");
    T::check(d->r->violations() == 0 && d->r->invalids() == 0, "not flagged: " + error_report(*d->r));
    int64_t cyc = d->r->pass_end_at - d->r->pass_start_at;
    T::check(cyc < 64 + 2 * 3 + 20, "timed like any S row (3 cycles): pass " + std::to_string(cyc));
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
TEST(unit_program) { CHECK(group_ok(program)); }
TEST(unit_program_compressed) { CHECK(group_ok(program_compressed)); }
TEST(unit_store_log_off) { CHECK(group_ok(store_log_off)); }
TEST(unit_pext_any_mask) { CHECK(group_ok(pext_any_mask)); }
