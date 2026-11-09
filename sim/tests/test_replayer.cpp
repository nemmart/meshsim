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

using Rows = std::vector<const uint64_t*>;
Rows rows_of(const ReplayDriver& d) {
    Rows r;
    for (uint64_t k = 0; k < d.rows(); k++) r.push_back(d.row(k));
    return r;
}
void check_rows(const std::string& what, const Rows& rows, const std::vector<std::vector<uint64_t>>& exp, unsigned padding) {
    uint64_t n = rows.size();
    T::eq(int64_t(exp.size() + padding), int64_t(n), what + ": rows");
    for (size_t k = 0; k < std::min<size_t>(n, exp.size()); k++) {
        const uint64_t* g = rows[k];
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
        for (unsigned i = 0; i < Replayer::REC_LONGS; i++) zero &= rows[k][i] == 0;
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
    check_rows("raw", rows_of(*d), expected(true), PADDING);
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
        check_rows(n, rows_of(*d), exp, PADDING);
        T::check(d->r->clean(), n + ": clean " + error_report(*d->r));
        if (c[2]) T::check(d->r->wlog.hits >= 1, n + ": the SD's pre-value came from the last-load entry");
    }
}

void store_log_off() {
    T::group("includes store log = 0");
    auto w = program_window(false, false, false, false);
    T::eq(0, int64_t(w->wlog.size()), "the write log is empty");
    auto d = run_window(*w, 25);
    check_rows("no store log", rows_of(*d), expected(false), PADDING);
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

// ================================================================ mid-sequence starts (4.19.2, 4.19.4)
void mid_start() {
    T::group("mid-sequence start");
    ReplayWindow w;
    templates(w);
    uint32_t lbx0 = prog()[2], jal = prog()[12];
    w.decode_rom(lbx0, T_LB); w.decode_rom(jal, T_JAL);
    w.pc = 0x1008; w.instr_idx = 8; w.next_offset = 1;
    w.regs[A0] = 0x100; w.regs[42] = 0x77; w.regs[41] = 0x99; w.regs[40] = 0x5;
    w.set_code({lbx0, jal, 0, 0}, false);
    w.set_logs({{true, 0, false, false, 0}}, false, false);
    auto d = run_window(w, 5);
    T::eq(5, int64_t(d->rows()), "rows");
    const uint64_t *r0 = d->row(0), *r1 = d->row(1);
    T::eq(alu::WMASK_B, int64_t((r0[0] >> 32) & 0xFF), "the first row is the template's row 1 (WindowMaskB)");
    T::eq(42, int64_t((r0[0] >> 40) & 0xFF), "temporary 1 resolves to 42: the offset is 1 (remap, from ROM[start])");
    T::eq(0, int64_t((r0[1] >> 18) & 1), "not is_first: the expansion's first row ran on the previous tile");
    T::eq(1, int64_t((r0[1] >> 17) & 1), "is_virtual");
    T::eq(8, int64_t(r0[0] & 0xFFFFFFFF), "instr_idx from the checkpoint");
    T::eq(alu::PEXT_S, int64_t((r1[0] >> 32) & 0xFF), "then PextSigned");
    T::eq(40, int64_t((r1[0] >> 40) & 0xFF), "into the remapped rd, v40");
    T::eq(0x99, int64_t(r1[4]), "reading v41, the image's value (its VirtualLoadB ran on the previous tile)");
    T::eq(0, int64_t(d->r->cond_events(0) + d->r->cond_events(1) + d->r->cond_events(2)), "no condition");
}

// ================================================================ LD to x0 (LR.D with rd = x0)
void ld_to_x0() {
    T::group("LD to x0");
    uint32_t lrd = 0x02u << 27 | 0u << 20 | uint32_t(A0) << 15 | 3u << 12 | 0u << 7 | AMO;
    uint32_t sd = s_(0, A5, A0, 3, STORE);
    ReplayWindow w;
    templates(w);
    w.rom(30, {W::op(alu::LD).rd(129).rs1(130).rs2(128).cnst(0).fmt(0).from_log().report_c()});
    w.decode_rom(lrd, 30); w.decode_rom(sd, T_SD);
    w.pc = 0x3000; w.instr_idx = 5; w.regs[A0] = 0x200;
    w.set_code({lrd, sd}, false);
    w.set_logs({{true, 0xABCDEF0123ull, true, false, 0}, {false, 0, false, true, 0xABCDEF0123ull}}, false, true);
    auto d = run_window(w, 2);
    const uint64_t* g = d->row(0);
    T::eq(0, int64_t((g[0] >> 40) & 0xFF), "rd_reg 0");
    T::eq(0, int64_t(g[7]), "rd 0: the write is dropped");
    T::eq(0x200, int64_t(g[8]), "mem_addr rs1 + imm");
    T::eq(0, int64_t(g[9]), "mem_value 0: formed from the value written to rd, not the log word (F4)");
    T::eq(0xABCDEF0123ll, int64_t(d->row(1)[9]), "the SD's pre-value: the log word coded as index 15 decodes to the popped LD value");
    T::eq(1, int64_t(d->r->wlog.hits), "via the last-load entry (E[15] takes the popped value even for an LD to x0: REPLAYER-NOTES N5)");
}

// ================================================================ controller-fed expansions (4.19.8, 4.19.9)
void controller_fed() {
    T::group("controller-fed expansion");
    uint32_t inl = r_(0, 2, 1, 0, 3, CUSTOM0);
    ReplayWindow w;
    w.fixed_rom();
    w.decode_class(inl, 1);
    const int n = 300;
    std::vector<W> words(n);
    for (int k = 0; k < n; k++) words[size_t(k)] = W::op(alu::ADDI).rd(unsigned(137 + (k % 8))).rs1(0).rs2(128).cnst(uint64_t(k + 1)).fmt(0);
    words[n - 1].report_c();
    w.template_(inl, words);
    w.pc = 0x2000; w.instr_idx = 100;
    w.set_code({inl, inl}, false);
    w.set_logs({}, false, false);
    auto d = run_window(w, 2 * n);
    T::eq(2 * n, int64_t(d->rows()), "two 300-row inline expansions");
    bool ok = true;
    for (int k = 0; k < std::min<int>(2 * n, int(d->rows())); k++) {
        const uint64_t* g = d->row(uint64_t(k));
        int kk = k % n;
        ok &= g[7] == uint64_t(kk + 1) && ((g[0] >> 40) & 0xFF) == uint64_t(40 + kk % 8) && (g[0] & 0xFFFFFFFF) == uint64_t(100 + k)
              && (g[1] & 0xFFFF) == uint64_t(n - 1 - kk) && ((g[1] >> 18) & 1) == (kk == 0 ? 1u : 0u) && g[2] == (k < n ? 0x2000u : 0x2004u);
    }
    T::check(ok, "every row: its word's constant, its temporary, its instr_idx, seq_remaining, is_first, pc");
    T::eq(2, int64_t(d->r->new_expansions), "two new-expansion events");
    T::eq(0, int64_t(d->missing_templates), "every presented instruction had a template");
    T::check(d->r->stalls[Replayer::ST_NEWEXP] > 0, "the first word of each expansion was waited for");
    T::check(d->r->violations() == 0 && d->r->invalids() == 0 && d->r->overruns() == 0, "clean: " + error_report(*d->r));

    T::group("controller-fed, mid-sequence start");
    w.next_offset = 250;
    w.set_code({inl, inl}, false);
    auto d2 = run_window(w, 50 + n);
    T::eq(50 + n, int64_t(d2->rows()), "rows");
    T::eq(251, int64_t(d2->row(0)[7]), "the first row is word 250: the controller started at PERIPH_STATUS_0's next offset");
    T::eq(49, int64_t(d2->row(0)[1] & 0xFFFF), "seq_remaining 49");
    T::eq(0, int64_t((d2->row(0)[1] >> 18) & 1), "not is_first");
    T::eq(1, int64_t((d2->row(50)[1] >> 18) & 1), "the next expansion's first row is is_first");
}

// ================================================================ N3: the rd = x0 policy on controller-fed words
void controller_fed_rd0() {
    T::group("controller-fed, rd = x0 remap (N3)");
    ReplayWindow w;
    templates(w);
    uint32_t lbx0 = prog()[2], lba5 = prog()[1], jal = prog()[12];
    w.decode_class(lbx0, 1); w.decode_class(lba5, 1); w.decode_rom(jal, T_JAL);
    std::vector<W> lb(3);
    for (unsigned k = 0; k < 3; k++) { lb[k].lo = w.rom_lo[T_LB + k]; lb[k].hi = w.rom_hi[T_LB + k]; }
    w.template_(lbx0, lb); w.template_(lba5, lb);
    w.pc = 0x1004; w.instr_idx = 2; w.regs[A0] = 0x100; w.regs[40] = 0x4040; w.regs[A5] = 0x55;
    w.set_code({lba5, lbx0, jal, 0}, false);
    w.set_logs({{true, V1, true, false, 0}, {true, V2, true, false, 0}, {true, 0, false, false, 0}}, false, false);
    auto d = run_window(w, 8);
    auto all = expected(true);
    std::vector<std::vector<uint64_t>> exp(all.begin() + 1, all.begin() + 7);
    Rows got = rows_of(*d);
    got.resize(std::min<size_t>(6, got.size()));
    check_rows("controller-fed LB and LB x0", got, exp, 0);
    T::check(d->r->violations() == 0 && d->r->invalids() == 0, "clean: " + error_report(*d->r));

    for (unsigned pol : {1u, 3u}) {
        T::group("controller-fed, rd = x0 NOP policy " + std::to_string(pol) + " (N3)");
        uint32_t divx0 = r_(1, A1, A0, 4, 0, OP), div = r_(1, A1, A0, 4, A2, OP), addi = prog()[0];
        ReplayWindow v;
        templates(v);
        v.decode_class(divx0, 1); v.decode_class(div, 1); v.decode_rom(addi, T_ADDI);
        std::vector<W> dw(12);
        for (unsigned k = 0; k < 12; k++) dw[k] = W::op(alu::ADDI).rd(137 + k % 6).rs1(0).rs2(128).cnst(k).fmt(0);
        dw[0].rd0(pol); dw[11].rd(129).report_c();
        v.template_(divx0, dw); v.template_(div, dw);
        v.pc = 0x2000; v.instr_idx = 1;
        v.set_code({divx0, addi, div, divx0, addi}, false);
        v.set_logs({}, false, false);
        auto e = run_window(v, 1 + 1 + 12 + 1 + 1);
        T::eq(16, int64_t(e->rows()), "rows: NOP, ADDI, 12 DIV rows, NOP, ADDI");
        const uint64_t* nop = e->row(0);
        T::eq(2, int64_t((nop[0] >> 32) & 0xFF), "the NOP row is ADDI");
        T::eq(pol == 1 ? 1 : 0, int64_t((nop[1] >> 17) & 1), "is_virtual per rd0 " + std::to_string(pol));
        T::eq(pol == 1 ? 1 : 0, int64_t((nop[1] >> 18) & 1), "is_first per rd0 " + std::to_string(pol));
        T::eq(0, int64_t(nop[7]), "rd 0");
        T::eq(0x100, int64_t(e->row(1)[7]), "the next instruction runs normally");
        T::eq(11, int64_t(e->row(13)[3]), "the full DIV expansion (rd != x0) still runs: its last word's constant");
        T::eq(A2, int64_t((e->row(13)[0] >> 40) & 0xFF), "into rd");
        T::eq(2, int64_t((e->row(14)[0] >> 32) & 0xFF), "the second div x0 is a NOP row too");
        T::check(e->r->exp.occ <= 11, "only the last dropped expansion's words may remain at done: " + std::to_string(e->r->exp.occ));
        T::check(e->r->violations() == 0 && e->r->invalids() == 0 && e->r->overruns() == 0, "clean: " + error_report(*e->r));
    }
}

// ================================================================ an all-padding window (4.19.2)
void padding() {
    T::group("all-padding window");
    ReplayWindow w;
    w.fixed_rom();
    w.pc = 0; w.instr_idx = 0;
    w.set_code(std::vector<uint32_t>(65536, 0), false);
    w.set_logs({}, false, false);
    auto d = std::make_unique<ReplayDriver>(w);
    d->load(); d->pass(400000);
    T::eq(65536, int64_t(d->rows()), "65,536 rows");
    bool zero = true;
    for (uint64_t k = 0; k < d->rows(); k++) for (unsigned i = 0; i < Replayer::REC_LONGS; i++) zero &= d->row(k)[i] == 0;
    T::check(zero, "every record is all zeros");
    T::check(d->done && d->r->violations() == 0 && d->r->invalids() == 0, "done, clean");
    int64_t cyc = d->r->pass_end_at - d->r->pass_start_at;
    T::check(cyc >= 3 * 65536 && cyc < 3 * 65536 + 200, "about 3 cycles per row (v1.26 item 12: no image copy): " + std::to_string(cyc));

    T::group("all-padding window, compressed code");
    w.set_code(std::vector<uint32_t>(65536, 0), true);
    T::check(w.code.size() / 2 < 2600, "65,536 padding words compress to " + std::to_string(w.code.size() / 2) + " entries");
    auto d2 = std::make_unique<ReplayDriver>(w);
    d2->load(); d2->pass(400000);
    bool zero2 = d2->rows() == 65536;
    for (uint64_t k = 0; k < d2->rows(); k++) for (unsigned i = 0; i < Replayer::REC_LONGS; i++) zero2 &= d2->row(k)[i] == 0;
    T::check(zero2 && d2->r->violations() == 0, "65,536 zero records");
}

// ================================================================ the pass length as a parameter (debug windows)
void debug_length() {
    T::group("debug window length");
    ReplayWindow w;
    w.fixed_rom();
    w.rows = 256;
    w.set_code(std::vector<uint32_t>(256, 0), false);
    w.set_logs({}, false, false);
    auto d = std::make_unique<ReplayDriver>(w);
    d->load(); d->pass(10000);
    T::eq(256, int64_t(d->rows()), "a 256-row pass");
    T::check(d->done && !d->r->is_running(), "done after 256 rows");
    T::eq(256, d->r->status(d->t, 0) & 0xFFFF, "PERIPH_STATUS_0 counts the rows run");
    uint32_t rr = 0;
    d->r->word(d->t, Replayer::W_ROWS_RUN, rr);
    T::eq(256, rr, "ROWS_RUN (word 33, v1.26.1) reads the row count after the pass");
    T::eq(0, fresh()->run_config_now(), "RUN_CONFIG is 0 after the reset (v1.26.1)");
    T::dies([] { run_config(1u << 24, false, false); }, "a row count past 24 bits is refused", {"0 to 16,777,215"});
}

// ================================================================ code 145, immediate 0 (26q, 4.19.3)
void immediate_zero() {
    T::group("operand code 145, immediate 0");
    uint32_t inl = r_(0, 2, 1, 0, 3, CUSTOM0);
    for (unsigned zcode : {145u, 132u}) {
        ReplayWindow w;
        w.fixed_rom();
        w.decode_class(inl, 1);
        w.template_(inl, {W::op(alu::LUI).rd(137).rs1(128).rs2(zcode).cnst(0x1000003d1ull).fmt(0).report_c()});
        w.pc = 0x3000; w.instr_idx = 7;
        w.set_code({inl}, false);
        w.set_logs({}, false, false);
        auto d = run_window(w, 1);
        const uint64_t* g = d->row(0);
        std::string c = "code " + std::to_string(zcode);
        T::eq(0x1000003d1ll, int64_t(g[7]), c + ": LUI writes the constant");
        T::eq(0x1000003d1ll, int64_t(g[3]), c + ": imm = the sum of the immediate slots, the zero adding nothing");
        T::eq(3, int64_t((g[1] >> 20) & 3), c + ": rs1 and rs2 both immediate slots");
        T::eq(0, int64_t((g[0] >> 48) & 0xFFFF), c + ": rs1_reg and rs2_reg 0");
        T::check(d->r->invalids() == 0, c + ": not an invalid expansion");
    }
}

// ================================================================ invalid expansions (4.19.11, condition 13)
void invalid() {
    const char* cases[] = {"class 3", "reserved operand code", "format 6", "opcode not executed", "past ROM word 511"};
    for (std::string c : cases) {
        T::group("invalid expansion: " + c);
        ReplayWindow w;
        w.fixed_rom();
        uint32_t ins = i_(5, 0, 0, A0, OP_IMM);
        W word = W::op(alu::ADDI).rd(129).rs1(130).rs2(132).fmt(1).report_c();
        unsigned start = 20;
        if (c == "class 3") w.decode_class(ins, 3);
        else if (c == "reserved operand code") word.rs2(146);
        else if (c == "format 6") word.fmt(6);
        else if (c == "opcode not executed") word.lo = (word.lo & ~0xFFull) | 163;
        if (c == "past ROM word 511") { start = 511; word.seq(1); w.rom_lo[511] = word.lo; w.rom_hi[511] = word.hi; }
        else w.rom(start, {word});
        if (c != "class 3") w.decode_rom(ins, start);
        w.set_logs({}, false, false);
        w.set_code({ins, ins, ins}, false);
        auto d = run_window(w, 10);
        int64_t expect_rows = c == "past ROM word 511" ? 1 : 0;
        T::eq(1, int64_t(d->r->cond_events(Replayer::ERR_INVALID)), "condition 13 once");
        T::check(d->done, "done posted: the pass stops as if it had ended");
        T::eq(expect_rows, int64_t(d->rows()), "rows before it");
        T::check(!d->r->is_running(), "periph_running fell");
    }
    for (unsigned bit : {55u, 56u, 61u}) for (bool fed : {false, true}) {
        T::group("reserved bit " + std::to_string(bit) + (fed ? ", controller-fed" : ", ROM"));
        ReplayWindow w;
        w.fixed_rom();
        W word = W::op(alu::ADDI).rd(129).rs1(130).rs2(132).fmt(1).report_c().reserved(bit);
        uint32_t ins = fed ? r_(0, 2, 1, 0, 3, CUSTOM0) : i_(5, 0, 0, A0, OP_IMM);
        if (fed) { w.decode_class(ins, 1); w.template_(ins, {word}); }
        else { w.rom(20, {word}); w.decode_rom(ins, 20); }
        w.set_logs({}, false, false);
        w.set_code({ins, ins}, false);
        T::dies([&] { run_window(w, 10); }, "a word with bit " + std::to_string(bit) + " set", {"replayer", "reserved bits [61:55]", fed ? "controller-fed" : "ROM"});
    }
}

// ================================================================ the write port (4.19.10)
void write_port() {
    T::group("write port");
    auto r = fresh();
    int64_t t = 0;
    r->accept(t, 3, 0x0, 2, 0x1234, 0); r->step(t++);
    for (int k = 0; k < 4; k++) r->step(t++);
    T::eq(0x1234, int64_t(r->region0[0]), "the state word landed");
    struct Bad { uint32_t at; unsigned size; const char* what; const char* name; };
    const Bad badw[] = {{0x0, 1, "a 64-bit packet", "size 64"}, {0x100000, 0, "a queue takes 128 bits only", "size 32"},
                        {0x800, 2, "unmapped in part 0", "unmapped"}, {0x500000, 2, "part 5 (v1.25 CL1: 1-4 the queues)", "unmapped"},
                        {0x700000, 2, "part 7", "unmapped"}, {0x8, 2, "part 0 off a 128-bit boundary", "item 47"}};
    for (const Bad& bw : badw) {
        auto rb = fresh();
        rb->accept(0, 3, bw.at, bw.size, 1, 1);
        T::dies([&] { for (int64_t u = 0; u < 8; u++) rb->step(u); }, std::string("a bad write: ") + bw.what, {"replayer", bw.name});
    }
    T::eq(0, r->errors(t), "ERRORS [0] is reserved: never set");
    T::eq(1, r->landed(2), "landed(t): a packet accepted at t - 2");
    T::eq(0, r->landed(101), "no packet");
    r->region0[0] = 0;
    r->accept(t, 3, 0x0, 2, 0x99, 0); r->step(t++);
    r->step(t++);
    T::eq(0, int64_t(r->region0[0]), "not landed at t + 1");
    r->step(t++); r->step(t++);
    T::eq(0x99, int64_t(r->region0[0]), "landed by t + 3 (at the end of t + 2)");
    for (uint32_t at : {0x0u, 0x1000u}) {
        auto w = program_window(false, false, false, true);
        auto d = std::make_unique<ReplayDriver>(*w);
        d->w.rows = 25; d->load();
        d->write_config(); d->r->run(d->t); d->step(); d->step();
        d->r->accept(d->t, 3, 0x2000 + 16 * 500, 2, 7, 7); d->step();
        for (int k = 0; k < 4; k++) d->step();
        T::eq(7, int64_t(d->r->rom_lo[500]), "running: the ROM takes writes (its second port)");
        d->r->accept(d->t, 3, at, 2, 5, 5);
        T::dies([&] { for (int k = 0; k < 6; k++) d->step(); },
                std::string("running: ") + (at == 0 ? "the state word" : "the decode table") + " written during a pass", {"replayer", "while a pass runs"});
    }
    {
        auto w = program_window(false, false, false, true);
        auto d = std::make_unique<ReplayDriver>(*w);
        d->w.rows = 25; d->load();
        d->write_config(); d->r->run(d->t); d->step(); d->step();
        T::check(d->r->running(d->t), "a pass runs");
        d->r->run(d->t);
        T::dies([&] { for (int k = 0; k < 3; k++) d->step(); }, "PULSE_RUN while a pass runs", {"replayer", "PULSE_RUN while a pass runs"});
    }
    auto r3 = fresh();
    int64_t t3 = 0;
    const uint32_t ats[] = {0x100000, 0x1FFFF0, 0x200000, 0x280000, 0x2FFFF0, 0x300000, 0x3ABCD0, 0x400000, 0x4FFFF0, 0x5FFFF0, 0x700000};
    for (uint32_t a : ats) if (a < 0x500000) { r3->accept(t3, 3, a, 2, a, 0); r3->step(t3++); }
    for (int k = 0; k < 4; k++) r3->step(t3++);
    T::eq(2, r3->code.occ, "part 1 (0x100000-0x1FFFFF): the code queue");
    T::eq(3, r3->rlog.occ, "part 2 (0x200000-0x2FFFFF): the read-log queue, 0x280000 included");
    T::eq(2, r3->wlog.occ, "part 3 (0x300000-0x3FFFFF): the write-log queue");
    T::eq(2, r3->exp.occ, "part 4 (0x400000-0x4FFFFF): the expansion queue");
    T::eq(0x280000, int64_t(r3->rlog.lo[(r3->rlog.head + 1) % r3->rlog.cap]), "0x280000 went to the read log, in order");
    T::eq(0x400000, int64_t(r3->exp.lo[r3->exp.head]), "0x400000 went to the expansion queue");
    auto r2 = fresh();
    int64_t t2 = 0;
    for (int k = 0; k < 257; k++) { r2->accept(t2, 3, 0x100000, 2, uint64_t(k), 0); r2->step(t2++); }
    for (int k = 0; k < 4; k++) r2->step(t2++);
    T::eq(1, int64_t(r2->cond_events(Replayer::ERR_OVERRUN)), "queue overrun on the 257th push");
    T::eq(1 << Replayer::ERR_OVERRUN, r2->errors(t2), "ERRORS [1], queue overrun");
    T::eq(256, r2->code.occ, "256 entries held");
    T::eq(1 << Replayer::ERR_OVERRUN, r2->errors(t2), "a push while stopped is accepted: only the overrun is set");
}

// ================================================================ events and credits (4.19.8, 4.19.9, 4.17.3)
void lines() {
    T::group("events and credits");
    const uint32_t code = 1u << Replayer::EV_CODE, rlog = 1u << Replayer::EV_RLOG, wlog = 1u << Replayer::EV_WLOG, exp = 1u << Replayer::EV_EXP,
                   done = 1u << Replayer::EV_RUN_DONE;
    auto w = program_window(false, false, false, true);
    auto rp = fresh();
    Replayer& r = *rp;
    w->rows = 25;
    auto d = std::make_unique<ReplayDriver>(*w, 1u << 17, rp.get());
    d->load();
    d->write_config();
    int64_t t = d->t;
    T::check(!r.running(t), "idle: STATUS [0] = 0");
    T::eq(code | rlog | wlog | exp, r.events().value(t), "idle, after the load: the state word's write set every queue's credits to 2");
    r.run(t); d->step(); r.sync(d->t);
    T::check(r.running(t + 1), "running from the end of the run's cycle");
    T::eq(code | rlog | wlog | exp, r.events().value(d->t), "PULSE_RUN leaves them (v1.26)");
    for (unsigned b = Replayer::EV_CODE; b <= Replayer::EV_EXP; b++) T::eq(2, r.events().count_of(b), "credit bit " + std::to_string(b) + ": 2");
    r.consume_events(code); d->step(); r.sync(d->t);
    T::eq(code | rlog | wlog | exp, r.events().value(d->t), "one consuming load: code still has a credit");
    r.consume_events(code); d->step(); r.sync(d->t);
    T::eq(rlog | wlog | exp, r.events().value(d->t), "two: code's bit falls (two refills may be outstanding)");
    T::eq(4, r.params(), "PARAMS: type 4 (4.17.1, D46), version 0");
    T::eq(0, r.status(d->t, 1), "EXPANSION_OPCODE: nothing presented yet");
    uint32_t v35 = 0, v34 = 0, v32 = 0, v36 = 0;
    r.word(d->t, 35, v35); r.word(d->t, 34, v34); r.word(d->t, 32, v32);
    T::eq(v35, r.status(d->t, 1), "word 35 is EXPANSION_OPCODE (v1.26.1; was 34)");
    T::eq(r.status(d->t, 0) >> 16, v34, "word 34 is EXPANSION_OFFSET (was 33)");
    T::eq(25 | Replayer::RC_STORE_LOG | Replayer::RC_RD_PREV, v32, "word 32 reads RUN_CONFIG back");
    T::check(!r.word(d->t, 36, v36), "word 36 undefined");
    T::eq(0, r.write_word(d->t, 33, 1), "a store to ROWS_RUN (33) is a bus error");
    T::eq(0, r.write_word(d->t, 35, 1), "and to 35");
    auto w3 = program_window(false, false, false, true);
    auto d3 = run_window(*w3, 25);
    T::check(d3->done, "the driver saw run done");
    d3->r->sync(d3->t);
    T::eq(0, d3->r->events().value(d3->t) & done, "and its consuming load took it");
    T::check(d3->rows() == 25, "the pass ran its 25 rows on returned credits");
}

// ================================================================ timing (4.19.7, 4.19.11)
void timing() {
    T::group("timing");
    auto w = program_window(false, false, false, true);
    auto d = run_window(*w, 25);
    int64_t cyc = d->r->pass_end_at - d->r->pass_start_at;
    int64_t rows_cycles = 23 * 3 + 2 * 5;
    int64_t stalls = 0;
    std::string st;
    for (unsigned k = 0; k < Replayer::N_STALL; k++) { stalls += int64_t(d->r->stalls[k]); st += (k ? ", " : "") + std::to_string(d->r->stalls[k]); }
    std::printf("  program pass: %lld cycles from the run to the last row: rows %lld, stalls [%s] [IMAGE, CODE, NEWEXP, EXP, RLOG, WLOG, OUTQ]\n",
                (long long)cyc, (long long)rows_cycles, st.c_str());
    T::eq(rows_cycles + stalls, cyc, "cycles = row schedules (3, or 5 for class M) + stall cycles");
    T::eq(0, int64_t(d->r->stalls[Replayer::ST_IMAGE]), "v1.26 (item 12): no image copy, the first row starts at once");
    T::eq(2, int64_t(d->r->rows_m), "two class-M rows");
    T::eq(25, d->r->status(d->t, 0) & 0xFFFF, "PERIPH_STATUS_0 [15:0]: rows run");
}

bool same_rows(const ReplayDriver& a, const ReplayDriver& b) {
    if (a.rows() != b.rows()) return false;
    for (uint64_t k = 0; k < a.rows(); k++)
        for (unsigned i = 0; i < Replayer::REC_LONGS; i++) if (a.row(k)[i] != b.row(k)[i]) return false;
    return true;
}
uint32_t word_of(Replayer& r, int64_t t, unsigned w) { uint32_t v = 0; r.word(t, w, v); return v; }

// ================================================================ v1.26.1 (DECISIONS-v1.26 items 13-16)
void v1261_a() {
    T::group("v1.26.1: RUN_CONFIG's row count");
    auto w1 = program_window(false, false, false, true);
    auto ok = run_window(*w1, 25);
    T::check(ok->rows() == 25 && ok->r->clean(), "a 25-row pass of a 25-row record: clean, " + std::to_string(ok->rows()) + " rows");
    auto w2 = program_window(false, false, false, true);
    auto sh = run_window(*w2, 10);
    T::eq(10, int64_t(sh->rows()), "a 10-row pass of the 25-row record runs exactly 10 rows");
    T::check(sh->done && !sh->r->is_running(), "and raises done");
    T::eq(1 << Replayer::ERR_LEFTOVER, sh->r->errors(sh->t), "with input left over, ERRORS [6] only");
    T::eq(10, word_of(*sh->r, sh->t, Replayer::W_ROWS_RUN), "ROWS_RUN reads 10");

    T::group("v1.26.1: a row count of 0");
    for (bool empty : {true, false}) {
        std::unique_ptr<ReplayWindow> w = empty ? std::make_unique<ReplayWindow>() : program_window(false, false, false, true);
        if (empty) { w->fixed_rom(); w->set_code({}, false); w->set_logs({}, false, false); }
        w->rows = 0;
        auto d = std::make_unique<ReplayDriver>(*w);
        d->load(); d->write_config();
        int64_t t0 = d->t;
        d->r->run(t0); d->step();
        T::check(d->r->running(t0 + 1), std::string(empty ? "empty" : "full") + " record: STATUS [0] rises at the end of the run store's cycle");
        d->step();
        T::check(!d->r->running(t0 + 2), "and falls the next");
        d->r->sync(d->t);
        T::check((d->r->events().value(d->t) & 1u << Replayer::EV_RUN_DONE) != 0, "run done is posted");
        T::eq(0, d->r->rows_done(), "no row");
        T::eq(empty ? 0 : 1 << Replayer::ERR_LEFTOVER, d->r->errors(d->t), empty ? "nothing to consume: no error" : "streams not consumed: [6], as at any done");
    }

    T::group("v1.26.1: RUN_CONFIG, sticky and checked");
    {
        auto w = program_window(false, false, false, true);
        auto d = std::make_unique<ReplayDriver>(*w);
        d->w.rows = 25; d->load();
        d->pass(200000); d->pass(200000);
        T::eq(1, int64_t(d->config_stores), "two passes, one RUN_CONFIG store: it is sticky");
        T::check(d->r->clean() && d->rows() == 25, "the second pass ran its 25 rows on the first write: " + error_report(*d->r));
        T::eq(d->w.run_config_word(), word_of(*d->r, d->t, Replayer::W_RUN_CONFIG), "reads back");
        d->r->reset(d->t); d->step();
        T::eq(0, word_of(*d->r, d->t, Replayer::W_RUN_CONFIG), "0 after the reset");
    }
    {
        auto w = program_window(false, false, false, true);
        auto d = std::make_unique<ReplayDriver>(*w);
        d->w.rows = 25; d->load();
        d->write_config(); d->r->run(d->t); d->step(); d->step();
        d->r->write_word(d->t, Replayer::W_RUN_CONFIG, 7);
        T::dies([&] { for (int k = 0; k < 3; k++) d->step(); }, "a RUN_CONFIG write while a pass runs", {"replayer", "RUN_CONFIG write while a pass runs"});
    }

    T::group("v1.26.1: RUN_CONFIG [28:26], the Tracer's compression enables, ignored");
    {
        auto wb = program_window(true, true, true, true);
        auto base = run_window(*wb, 25);
        for (uint32_t comp = 1; comp < 8; comp++) {
            auto w = program_window(true, true, true, true);
            auto d = std::make_unique<ReplayDriver>(*w);
            w->rows = 25;
            d->run_config = w->run_config_word() | comp << 26 | (comp == 7 ? 7u << 29 : 0);
            d->load(); d->pass(200000);
            T::check(same_rows(*d, *base) && d->r->clean() && d->r->errors(d->t) == 0 && d->r->pass_end_at - d->r->pass_start_at == base->r->pass_end_at - base->r->pass_start_at,
                     "[28:26] = " + std::to_string(comp) + ": the same rows and cycles, no error, nothing flagged " + error_report(*d->r));
            T::eq(w->run_config_word() | comp << 26, word_of(*d->r, d->t, Replayer::W_RUN_CONFIG), "  reads back with [28:26]");
        }
        auto raw = program_window(false, false, false, true);
        auto dr = std::make_unique<ReplayDriver>(*raw);
        raw->rows = 25; dr->run_config = raw->run_config_word() | Replayer::RC_TRACER_COMP; dr->load(); dr->pass(200000);
        T::check(dr->r->clean() && dr->rows() == 25, "raw streams with every enable set: still decoded raw (the stream word decides) " + error_report(*dr->r));
    }

    T::group("v1.26.1: the flags from RUN_CONFIG, not the record");
    {
        auto w = program_window(false, false, false, true);
        w->store_log = false; w->rd_prev = false;
        const uint64_t bm[2] = {0, 0};
        std::vector<uint64_t> rec = w->record(bm);
        rec[1] |= 3ull << 51;
        w->region0 = rec;
        auto d = run_window(*w, 25);
        auto e = expected(false);
        for (auto& row : e) row[11] = 0;
        check_rows("record [115], [116] = 1, RUN_CONFIG [24], [25] = 0", rows_of(*d), e, PADDING);
        T::check(d->r->clean(), "clean, the write log's sizes ignored (no [5], no [6]): " + error_report(*d->r));
        T::check(!d->r->store_log_latched(), "the pass used no store log");
        auto w2 = program_window(false, false, false, true);
        auto on = run_window(*w2, 25);
        check_rows("record [116:115] = 0, RUN_CONFIG [24], [25] = 1", rows_of(*on), expected(true), PADDING);
    }

    T::group("v1.26.1: the advice field is ignored");
    {
        auto w0 = program_window(true, true, true, true), w1 = program_window(true, true, true, true);
        w1->advice_values = 1u << 22;
        auto a = run_window(*w0, 25);
        auto b = run_window(*w1, 25);
        T::check(same_rows(*a, *b), "advice 0 and 2^22: the same rows");
        T::check(b->r->clean(), "clean with advice 2^22: " + error_report(*b->r));
        T::eq(int64_t(1) << 22, int64_t((b->r->region0[3] >> 38) & 0x7FFFFF), "the field stays in part 0 as written");
    }
}

// ConvertDelivery's two groups (ReplayerUnitTests lines 809-865) test ref's Java converter, not the replayer: not
// ported (Stage A converts with ref's ConvertDelivery; its own tests run there).
void rows_run_past_2_16() {
    T::group("v1.26.1: ROWS_RUN past 2^16");
    const uint32_t n = 70000;
    ReplayWindow w;
    w.fixed_rom();
    w.set_code(std::vector<uint32_t>(n, 0), false); w.set_logs({}, false, false); w.rows = n;
    auto d = std::make_unique<ReplayDriver>(w);
    d->load(); d->pass(600000);
    T::eq(n, int64_t(d->rows()), "a 70,000-row pass: RUN_CONFIG past a window");
    T::eq(n, word_of(*d->r, d->t, Replayer::W_ROWS_RUN), "ROWS_RUN after it: 70,000 (24 bits; v1.26's modulo 2^16 would read 4,464)");
    T::check(d->r->clean(), "a 70,000-row pass of 70,000 padding words: clean " + error_report(*d->r));
}

void working_state() {
    T::group("v1.26: part 0 is the working state");
    auto w = program_window(false, false, false, true);
    auto rp = fresh();
    Replayer& r = *rp;
    w->rows = 25;
    auto d = std::make_unique<ReplayDriver>(*w, 1u << 17, rp.get());
    d->load();
    T::eq(0x1000, int64_t(r.region0[0]), "before the pass: the state word's pc");
    d->pass(200000);
    std::vector<uint64_t> first(d->row(0), d->row(0) + d->rows() * Replayer::REC_LONGS);
    T::eq(int64_t(r.pc()), int64_t(r.region0[0]), "after: the state word holds the end pc");
    T::eq(r.instr_idx(), int64_t(r.region0[1] & 0xFFFFFFFF), "and the end instr_idx");
    T::eq(0x101, int64_t(r.region0[4 + A0]), "the image holds the registers as the pass left them (a0 = 0x101)");
    T::eq(0x1036, int64_t(r.region0[4 + 40]), "v40 too");
    T::eq(0, int64_t(r.region0[1] >> 48), "the state word's [127:112] stay 0 (v1.26.1: no flags there)");
    T::eq(w->run_config_word(), r.run_config_now(), "RUN_CONFIG is untouched by the pass (sticky)");
    d->pass(200000);
    std::vector<uint64_t> again(d->row(0), d->row(0) + d->rows() * Replayer::REC_LONGS);
    T::check(again == first, "after reloading words 0-65, the same pass again");
    T::check(r.clean(), "clean: " + error_report(r));
}

void stream_word_layout() {
    namespace sw = units::streamword;
    T::group("v1.26: the stream word");
    JRandom g(26);
    bool ok = true;
    auto same = [](const sw::Size* a, const sw::Size* b) {
        for (int k = 0; k < 3; k++) if (a[k].words != b[k].words || a[k].values != b[k].values || a[k].compressed != b[k].compressed) return false;
        return true;
    };
    for (int k = 0; k < 2000; k++) {
        sw::Size a[3];
        for (int q = 0; q < 3; q++) { int wd = g.next_int(0x10000), vv = g.next_int(0x20000); bool c = g.next_boolean(); a[q] = sw::make(wd, vv, c); }
        uint64_t w[2], wa[2];
        stream_word(a, 0, w);
        sw::Size b[3];
        sw::parse(w[0], w[1], b);
        uint32_t adv = uint32_t(g.next_int((1 << 22) + 1));
        stream_word(a, adv, wa);
        sw::Size c[3];
        sw::parse(wa[0], wa[1], c);
        ok &= same(a, b) && (w[1] >> 38) == 0 && same(a, c) && sw::advice(wa[0], wa[1]) == adv && (wa[1] >> 61) == 0;
    }
    T::check(ok, "2,000 random sizes and advice counts round-trip; the advice field doesn't touch the sizes; [127:125] spare");
    sw::Size one_s[3] = {sw::make(0x1111, 0x10001, true), sw::make(0x2222, 0x10002, false), sw::make(0x3333, 0x10003, true)};
    uint64_t one[2];
    stream_word(one_s, 0, one);
    T::eq(int64_t(0x0001333322221111ull), int64_t(one[0]), "v1.26.1's layout: words [15:0], [31:16], [47:32]; code's values out from [48]");
    T::eq(int64_t(0x1ull | 0x10002ull << 1 | 0x10003ull << 18 | 1ull << 35 | 1ull << 37), int64_t(one[1]), "values out [64:48], [81:65], [98:82]; flags [99], [100], [101]");
    sw::Size z[3] = {sw::make(0, 0, false), sw::make(0, 0, false), sw::make(0, 0, false)};
    uint64_t adv[2];
    stream_word(z, 1u << 22, adv);
    T::eq(0, int64_t(adv[0]), "advice alone: the low half empty");
    T::eq(int64_t(uint64_t(1 << 22) << 38), int64_t(adv[1]), "advice values [124:102], 2^22 inclusive");
    T::dies([&] { uint64_t x[2]; stream_word(z, (1u << 22) + 1, x); }, "advice past 2^22 is refused", {"advice values"});
    sw::Size mxs[3] = {sw::make(33800, 65536, true), sw::make(0xFFFF, 0x1FFFF, true), sw::make(0, 0, false)};
    uint64_t mx[2];
    stream_word(mxs, 0, mx);
    sw::Size m[3];
    sw::parse(mx[0], mx[1], m);
    T::check(m[0].words == 33800 && m[0].values == 65536 && m[0].compressed && m[1].words == 0xFFFF && m[1].values == 0x1FFFF && !m[2].compressed,
             "the largest sizes fit (a stream at most about 33,800 words and 65,536 values)");
    T::eq(0, int64_t(state_hi(0xFFFFFFFF, 0xFFFF) >> 48), "the state word: [127:112] 0, no flags (v1.26.1)");
    T::eq(Replayer::RC_STORE_LOG | Replayer::RC_RD_PREV | 65536, run_config(65536, true, true), "RUN_CONFIG: rows [23:0], use the store log [24], output rd_prev [25]");
    T::dies([] { sw::make(0x10000, 0, false); }, "a size past its field is refused", {"stream size"});
}

// Items 6, 7: a pop past a stream's values, or a decode past its words, is an underflow at once: [3], [4], [5].
void underflows() {
    const char* cases[] = {"code past its values", "code past its words", "read log past its values", "read log past its words",
                           "write log past its values", "write log past its words", "a transfer past the read log", "write log off: no [5]"};
    auto starts = [](const std::string& s, const char* p) { return s.rfind(p, 0) == 0; };
    auto ends = [](const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; };
    for (std::string c : cases) {
        T::group("v1.26 underflow: " + c);
        ReplayWindow w;
        w.fixed_rom();
        uint32_t ins = i_(5, 0, 0, A0, OP_IMM);
        int n_ins = 3;
        uint32_t rows = 3;
        int expect_bit = -1;
        int64_t expect_rows = 0;
        if (starts(c, "code")) {
            w.rom(20, {W::op(alu::ADDI).rd(129).rs1(130).rs2(132).fmt(1).report_c()});
            w.set_logs({}, false, false);
            if (ends(c, "values")) { expect_rows = 3; rows = 4; }
            else { n_ins = 5; rows = 5; w.words_adj[0] = -1; expect_rows = 3; }
            expect_bit = Replayer::ERR_CODE_UNDER;
        } else if (starts(c, "read log") || starts(c, "a transfer")) {
            if (starts(c, "a transfer")) {
                ins = j_(8, 0, JAL);
                w.rom(20, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
                w.set_logs({{true, 2, false, false, 0}}, false, false);
                expect_rows = 1;
            } else {
                w.rom(20, {W::op(alu::ADVICE).rd(129).rs1(128).rs2(128).cnst(0).from_log().report_c()});
                std::vector<LogEvent> ev;
                for (int k = 0; k < (ends(c, "values") ? 1 : 3); k++) ev.push_back({true, uint64_t(9 + k), false, false, 0});
                w.set_logs(ev, false, false);
                if (ends(c, "words")) { w.words_adj[1] = -1; expect_rows = 2; } else expect_rows = 1;
            }
            expect_bit = Replayer::ERR_RLOG_UNDER;
        } else {
            ins = s_(8, A1, A0, 3, STORE);
            w.rom(20, {W::op(alu::SD).rd(132).rs1(130).rs2(131).fmt(2).from_log().report_c().sign()});
            std::vector<LogEvent> ev;
            int n = ends(c, "values") ? 1 : ends(c, "words") ? 3 : 0;
            for (int k = 0; k < n; k++) ev.push_back({false, 0, false, true, uint64_t(7 + k)});
            w.set_logs(ev, false, false);
            if (ends(c, "words")) { w.words_adj[2] = -1; expect_rows = 2; } else expect_rows = ends(c, "values") ? 1 : 3;
            w.store_log = !starts(c, "write log off");
            if (w.store_log) expect_bit = Replayer::ERR_WLOG_UNDER;
            w.regs[A0] = 0x2000;
        }
        w.decode_rom(ins, 20);
        w.pc = 0x1000; w.instr_idx = 1;
        w.set_code(std::vector<uint32_t>(size_t(n_ins), ins), false);
        auto d = run_window(w, rows);
        if (expect_bit < 0) {
            T::eq(0, d->r->errors(d->t), "no error: the write log's sizes are ignored without the store log");
            T::eq(3, int64_t(d->rows()), "all three rows");
            T::check(d->r->clean(), "clean: " + error_report(*d->r));
            continue;
        }
        T::eq(1 << expect_bit, d->r->errors(d->t), "ERRORS [" + std::to_string(expect_bit) + "] alone");
        T::eq(1, int64_t(d->r->cond_events(unsigned(expect_bit))), "once");
        T::check(d->done && !d->r->is_running(), "the pass stopped, done posted");
        T::eq(expect_rows, int64_t(d->rows()), "rows before it");
        T::check(d->r->underflows() == 1 && d->r->invalids() == 0 && d->r->leftovers() == 0, "named as an underflow: " + error_report(*d->r));
    }
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
TEST(unit_mid_start) { CHECK(group_ok(mid_start)); }
TEST(unit_ld_to_x0) { CHECK(group_ok(ld_to_x0)); }
TEST(unit_controller_fed) { CHECK(group_ok(controller_fed)); }
TEST(unit_controller_fed_rd0) { CHECK(group_ok(controller_fed_rd0)); }
TEST(unit_padding) { CHECK(group_ok(padding)); }
TEST(unit_debug_length) { CHECK(group_ok(debug_length)); }
TEST(unit_immediate_zero) { CHECK(group_ok(immediate_zero)); }
TEST(unit_invalid) { CHECK(group_ok(invalid)); }
TEST(unit_write_port) { CHECK(group_ok(write_port)); }
TEST(unit_lines) { CHECK(group_ok(lines)); }
TEST(unit_timing) { CHECK(group_ok(timing)); }
TEST(unit_v1261_run_config) { CHECK(group_ok(v1261_a)); }
TEST(unit_rows_run_past_2_16) { CHECK(group_ok(rows_run_past_2_16)); }
TEST(unit_working_state) { CHECK(group_ok(working_state)); }
TEST(unit_stream_word_layout) { CHECK(group_ok(stream_word_layout)); }
TEST(unit_underflows) { CHECK(group_ok(underflows)); }
