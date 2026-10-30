// check_div: the Tracer's division-family templates against the jolt-cpp tracer's own code (WORKER7).
//
// Compiled against the reference tracer (jolt-asic-planning tracer-standalone 4f36b3d; see build.sh). For each
// source instruction (DIV, DIVU, REM, REMU and the W forms), each operand pair and each register assignment:
//
//   Jolt side    expand_virt_seq() expands the instruction and execute_instruction<true>() runs every row, so
//                the advice comes from jolt-cpp's VirtualAdvice* cases (trace_owner.cpp:545-630), compiled.
//   Tracer side  the generated 128-bit words (out/div_templates.inc) are decoded and run: untraced rows on the
//                Tracer's row ALU model (RV64 M division written from the RISC-V spec below; every other
//                opcode by jolt-cpp's execute_instruction on a scratch state), traced rows by jolt-cpp's
//                execute_instruction on the bank-0 state, the advice row taking bank-1 register advice_reg.
//
// Checks, per case:
//   C1  the advice the Tracer logs equals jolt-cpp's advice value;
//   C2  the traced words, resolved, are exactly jolt-cpp's rows (opcode, slots, kinds, immediates,
//       virt_seq_remaining, is_first), the advice row as VirtualAdvice (spec 4.19.7);
//   C3  every traced row's record values (rd_prev, rs1, rs2, rd after) match jolt-cpp's, no assertion fails on
//       either side, and the final bank-0 register file is identical;
//   C4  the final rd equals the RV64 M result (a cross-check of the RV64 M model against Jolt's expansion).
// It also prints, for the edge cases, Jolt's advice against the plain RV64 M operation (REPORT step 2).
#include "tracer/isa/virtual.cpp"
#include "tracer/trace_owner.cpp"

#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace tracer;
using namespace tracer::isa;
using namespace tracer::isa::virt;

#include "../out/div_templates.inc"

// ---------------------------------------------------------------- RV64 M, from the RISC-V spec (vol. I, ch. 13)
static u64 sx32(u64 x) { return (u64)(i64)(i32)(u32)x; }
static u64 zx32(u64 x) { return (u64)(u32)x; }
static u64 rv_div(u64 a, u64 b) {
  if (b == 0) return ~0ull;
  if (a == 0x8000000000000000ull && b == ~0ull) return a;
  return (u64)((i64)a / (i64)b);
}
static u64 rv_divu(u64 a, u64 b) { return b == 0 ? ~0ull : a / b; }
static u64 rv_rem(u64 a, u64 b) {
  if (b == 0) return a;
  if (a == 0x8000000000000000ull && b == ~0ull) return 0;
  return (u64)((i64)a % (i64)b);
}
static u64 rv_remu(u64 a, u64 b) { return b == 0 ? a : a % b; }
static u64 rv_divw(u64 a, u64 b) {
  i32 x = (i32)a, y = (i32)b;
  if (y == 0) return ~0ull;
  if (x == INT32_MIN && y == -1) return sx32((u32)INT32_MIN);
  return sx32((u32)(x / y));
}
static u64 rv_divuw(u64 a, u64 b) {
  u32 x = (u32)a, y = (u32)b;
  return y == 0 ? ~0ull : sx32(x / y);
}
static u64 rv_remw(u64 a, u64 b) {
  i32 x = (i32)a, y = (i32)b;
  if (y == 0) return sx32(a);
  if (x == INT32_MIN && y == -1) return 0;
  return sx32((u32)(x % y));
}
static u64 rv_remuw(u64 a, u64 b) {
  u32 x = (u32)a, y = (u32)b;
  return y == 0 ? sx32(a) : sx32(x % y);
}
static bool rv_native(OpCode op, u64 a, u64 b, u64& out) {
  switch (op) {
    case DIV: out = rv_div(a, b); return true;
    case DIVU: out = rv_divu(a, b); return true;
    case REM: out = rv_rem(a, b); return true;
    case REMU: out = rv_remu(a, b); return true;
    case DIVW: out = rv_divw(a, b); return true;
    case DIVUW: out = rv_divuw(a, b); return true;
    case REMW: out = rv_remw(a, b); return true;
    case REMUW: out = rv_remuw(a, b); return true;
    default: return false;
  }
}

// ---------------------------------------------------------------- assertion capture
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_armed = 0;
static void on_abort(int) {
  if (g_armed) siglongjmp(g_jmp, 1);
  _exit(99);
}

// ---------------------------------------------------------------- shared setup
static VirtualBytecode* CODE;
static const u64 BASE = 0x80000000ull, PC = BASE + 0x100;

static InterpreterState fresh_state() {
  InterpreterState st{};
  st.code = CODE;
  st.risc_pc = PC;
  st.instr_idx = CODE->get_instr_idx(PC);
  st.ram = nullptr;
  return st;
}

struct RowRec { OpCode op; u64 rd_reg, rs1_reg, rs2_reg; bool rd_i, rs1_i, rs2_i; u64 rem; bool first;
                u64 rd_prev, rs1_v, rs2_v, rd_after; };

// Runs one row on `st` the way the tracer does, and records it. Returns false on an assertion failure.
static bool run_row(InterpreterState& st, VirtualInstruction in, RowRec& rec, bool last) {
  in.risc_pc = PC;
  in.pc_increment = last ? 4 : 0;
  auto [rdv, r1, r2] = st.read_reg_vals(in);
  rec.rd_prev = in.rd_is_imm ? 0 : rdv;
  rec.rs1_v = in.rs1_is_imm ? 0 : r1;
  rec.rs2_v = in.rs2_is_imm ? 0 : r2;
  g_armed = 1;
  if (sigsetjmp(g_jmp, 1)) { g_armed = 0; return false; }
  execute_instruction<true>(in, st);
  g_armed = 0;
  rec.rd_after = in.rd_is_imm ? 0 : st.regs[in.rd];
  return true;
}

// ---------------------------------------------------------------- the Jolt side
struct JoltRun { std::vector<RowRec> rows; u64 advice = 0; bool aborted = false; std::array<u64, 128> regs{}; };

static JoltRun run_jolt(OpCode src, u32 rd, u32 rs1, u32 rs2, u64 a, u64 b) {
  JoltRun r;
  static VirtualInstruction buf[64];
  u64 n = expand_virt_seq(std::span<VirtualInstruction>(buf, 64), Instruction(src, rd, rs1, rs2, false, false, false));
  InterpreterState st = fresh_state();
  st.regs[rs1] = a; st.regs[rs2] = b; st.regs[0] = 0;
  for (u64 i = 0; i < n; i++) {
    const VirtualInstruction& in = buf[i];
    RowRec rec{in.opcode, in.rd, in.rs1, in.rs2, in.rd_is_imm, in.rs1_is_imm, in.rs2_is_imm,
               in.virt_seq_remaining, in.is_first_in_sequence};
    if (!run_row(st, in, rec, i + 1 == n)) { r.aborted = true; return r; }
    if (is_virtual_advice(in.opcode)) r.advice = rec.rd_after;
    r.rows.push_back(rec);
  }
  for (int i = 0; i < 128; i++) r.regs[i] = st.regs[i];
  return r;
}

// ---------------------------------------------------------------- the Tracer side
struct Word { u8 op, rd, rs1, rs2; u16 seq; bool nfv, hold; u8 rd0, fmt; bool virt, imm0, rc, from_log, untraced; u8 advice_reg; u64 k; };
static Word decode(Word128 w) {
  Word d;
  d.op = w.lo & 0xff; d.rd = (w.lo >> 8) & 0xff; d.rs1 = (w.lo >> 16) & 0xff; d.rs2 = (w.lo >> 24) & 0xff;
  d.seq = (w.lo >> 32) & 0xfff; d.nfv = (w.lo >> 44) & 1; d.hold = (w.lo >> 45) & 1; d.rd0 = (w.lo >> 46) & 3;
  d.fmt = (w.lo >> 48) & 7; d.virt = (w.lo >> 51) & 1; d.imm0 = (w.lo >> 52) & 1; d.rc = (w.lo >> 53) & 1;
  d.from_log = (w.lo >> 54) & 1; d.untraced = (w.lo >> 55) & 1; d.advice_reg = (w.lo >> 56) & 0x7f; d.k = w.hi;
  return d;
}

// An operand code resolved: a register (bank 0 or bank 1, 0-255) or an immediate value.
struct Slot { bool imm; u32 reg; u64 val; };
static Slot resolve(u8 c, u32 rd, u32 rs1, u32 rs2, u64 k, std::string& err) {
  if (c < 128) return {false, c, 0};
  if (c == 128) return {true, 0, k};
  if (c == 129) return {false, rd, 0};
  if (c == 130) return {false, rs1, 0};
  if (c == 131) return {false, rs2, 0};
  if (c >= 137 && c <= 144) return {false, 40u + (c - 137), 0};  // offset 0: rd != x0 (rd = x0 is the NOP row)
  if (c == 145) return {true, 0, 0};
  if (c >= 146) return {false, c, 0};
  err = "operand code " + std::to_string(c) + " is not valid";
  return {true, 0, 0};
}

struct TracerRun { std::vector<RowRec> rows; std::vector<u64> log; bool aborted = false; std::string err;
                   std::array<u64, 128> regs{}; };

static TracerRun run_tracer(const TracerTemplate& t, u32 rd, u32 rs1, u32 rs2, u64 a, u64 b, std::mt19937_64& rng) {
  TracerRun r;
  InterpreterState st = fresh_state();
  st.regs[rs1] = a; st.regs[rs2] = b; st.regs[0] = 0;
  u64 bank1[256];
  for (auto& v : bank1) v = rng();  // stale bank-1 contents: the templates must not depend on them
  auto get = [&](const Slot& s) -> u64 { return s.imm ? s.val : (s.reg >= 128 ? bank1[s.reg] : st.regs[s.reg]); };
  int traced_total = 0;
  for (int i = 0; i < t.n; i++) traced_total += !decode(t.w[i]).untraced;
  int traced_seen = 0;
  for (int i = 0; i < t.n; i++) {
    Word w = decode(t.w[i]);
    if (w.seq != t.n - 1 - i) { r.err = "seq_remaining"; return r; }
    Slot d = resolve(w.rd, rd, rs1, rs2, w.k, r.err), s1 = resolve(w.rs1, rd, rs1, rs2, w.k, r.err),
         s2 = resolve(w.rs2, rd, rs1, rs2, w.k, r.err);
    if (!r.err.empty()) return r;
    if (w.untraced) {
      // Tracer row ALU: bank-1 rd; sources may be either bank.
      if (d.imm || d.reg < 146) { r.err = "untraced rd outside bank 1"; return r; }
      u64 x = get(s1), y = get(s2), out;
      if (!rv_native((OpCode)w.op, x, y, out)) {
        // Any other opcode: jolt-cpp's own semantics, on a scratch state.
        InterpreterState sc = fresh_state();
        VirtualInstruction in{};
        in.opcode = (OpCode)w.op; in.rd = 3; in.rd_is_imm = false;
        in.rs1 = s1.imm ? x : 1; in.rs1_is_imm = s1.imm; in.rs2 = s2.imm ? y : 2; in.rs2_is_imm = s2.imm;
        sc.regs[1] = x; sc.regs[2] = y; sc.regs[3] = bank1[d.reg];
        RowRec dummy{};
        if (!run_row(sc, in, dummy, false)) { r.aborted = true; return r; }
        out = sc.regs[3];
      }
      bank1[d.reg] = out;
      continue;
    }
    // A traced row: bank 0 only.
    auto bank0 = [](const Slot& s) { return s.imm || s.reg < 128; };
    if (!bank0(d) || !bank0(s1) || !bank0(s2)) { r.err = "traced row names bank 1"; return r; }
    VirtualInstruction in{};
    in.opcode = (OpCode)w.op;
    in.rd = d.imm ? d.val : d.reg; in.rd_is_imm = d.imm;
    in.rs1 = s1.imm ? s1.val : s1.reg; in.rs1_is_imm = s1.imm;
    in.rs2 = s2.imm ? s2.val : s2.reg; in.rs2_is_imm = s2.imm;
    in.virt_seq_remaining = traced_total - 1 - traced_seen;
    in.is_first_in_sequence = traced_seen == 0;
    RowRec rec{in.opcode, in.rd, in.rs1, in.rs2, in.rd_is_imm, in.rs1_is_imm, in.rs2_is_imm,
               in.virt_seq_remaining, in.is_first_in_sequence};
    bool last = traced_seen + 1 == traced_total;
    if (w.from_log) {
      // The advice row: rd takes bank-1 register advice_reg, and that value is the row's log word.
      if (w.op != VirtualAdvice || d.imm) { r.err = "from_log row is not VirtualAdvice rd"; return r; }
      u64 v = bank1[128 + w.advice_reg];
      rec.rd_prev = st.regs[d.reg]; rec.rs1_v = 0; rec.rs2_v = 0;
      if (d.reg != 0) st.regs[d.reg] = v;
      rec.rd_after = st.regs[d.reg];
      st.cycle += 1; st.risc_pc += last ? 4 : 0; st.instr_idx += 1;
      r.log.push_back(v);
    } else if (!run_row(st, in, rec, last)) {
      r.aborted = true; return r;
    }
    r.rows.push_back(rec);
    traced_seen++;
  }
  for (int i = 0; i < 128; i++) r.regs[i] = st.regs[i];
  return r;
}

// ---------------------------------------------------------------- comparison
static const char* JOLT_ADVICE_NAME(OpCode src) {
  switch (src) {
    case DIV: return "VirtualAdviceDiv"; case DIVU: case REMU: return "VirtualAdviceDivU";
    case REM: return "VirtualAdviceRem"; case DIVW: return "VirtualAdviceDivW";
    case DIVUW: case REMUW: return "VirtualAdviceDivUW"; case REMW: return "VirtualAdviceRemW";
    default: return "?";
  }
}

struct Tally { u64 cases = 0, fail[5] = {0, 0, 0, 0, 0}, aborts = 0; std::string first[5]; };

static void check(const TracerTemplate& t, u32 rd, u32 rs1, u32 rs2, u64 a, u64 b, std::mt19937_64& rng, Tally& ty) {
  if (rs1 == 0) a = 0;
  if (rs2 == 0) b = 0;
  if (rs1 == rs2) b = a;
  ty.cases++;
  char ctx[160];
  snprintf(ctx, sizeof ctx, "%s rd=x%u rs1=x%u rs2=x%u a=0x%016llx b=0x%016llx", t.name, rd, rs1, rs2,
           (unsigned long long)a, (unsigned long long)b);
  auto fail = [&](int c, const std::string& why) {
    if (ty.fail[c]++ == 0) ty.first[c] = std::string(ctx) + ": " + why;
  };
  JoltRun J = run_jolt(t.src, rd, rs1, rs2, a, b);
  TracerRun R = run_tracer(t, rd, rs1, rs2, a, b, rng);
  if (!R.err.empty()) { fail(1, "template: " + R.err); return; }
  if (J.aborted) { fail(2, "jolt-cpp's own expansion failed an assertion"); return; }
  // C1, first: the advice is logged before any of the checks that can fail an assertion.
  if (R.log.size() != 1 || R.log[0] != J.advice) {
    char m[96]; snprintf(m, sizeof m, "advice 0x%016llx, jolt-cpp 0x%016llx",
                         (unsigned long long)(R.log.empty() ? 0 : R.log[0]), (unsigned long long)J.advice);
    fail(0, m);
  }
  if (R.aborted) { ty.aborts++; fail(2, "an assertion failed on the Tracer side"); return; }
  // C2, C3
  if (R.rows.size() != J.rows.size()) { fail(1, "traced row count differs"); return; }
  for (size_t i = 0; i < R.rows.size(); i++) {
    const RowRec &x = R.rows[i], &y = J.rows[i];
    bool adv = is_virtual_advice(y.op);
    bool same_op = adv ? (x.op == VirtualAdvice && std::string(opcode_to_string(y.op)) == JOLT_ADVICE_NAME(t.src))
                       : x.op == y.op;
    bool same_slots = x.rd_reg == y.rd_reg && x.rd_i == y.rd_i &&
                      (adv || (x.rs1_reg == y.rs1_reg && x.rs1_i == y.rs1_i && x.rs2_reg == y.rs2_reg && x.rs2_i == y.rs2_i));
    if (!same_op || !same_slots || x.rem != y.rem || x.first != y.first) {
      fail(1, "row " + std::to_string(i) + " differs from jolt-cpp's: " + opcode_to_string(x.op) + " vs " + opcode_to_string(y.op));
      return;
    }
    // The record reports 0 for an advice row's rs1 and rs2 (trace_owner.hpp:173-177), so compare those as 0.
    u64 yr1 = adv ? 0 : y.rs1_v, yr2 = adv ? 0 : y.rs2_v;
    if (x.rd_prev != y.rd_prev || x.rs1_v != yr1 || x.rs2_v != yr2 || x.rd_after != y.rd_after) {
      fail(2, "row " + std::to_string(i) + " (" + opcode_to_string(x.op) + ") record values differ");
      return;
    }
  }
  if (R.regs != J.regs) { fail(2, "final register file differs"); return; }
  // C4
  u64 want;
  rv_native(t.src, a, b, want);
  if (rd != 0 && R.regs[rd] != want) {
    char m[96]; snprintf(m, sizeof m, "rd 0x%016llx, RV64 M 0x%016llx", (unsigned long long)R.regs[rd], (unsigned long long)want);
    fail(3, m);
  }
}

// ---------------------------------------------------------------- the edge-case table (REPORT step 2)
static u64 jolt_advice_value(OpCode adv, u64 a, u64 b) {
  InterpreterState st = fresh_state();
  st.regs[6] = a; st.regs[7] = b;
  VirtualInstruction in{};
  in.opcode = adv; in.rd = 5; in.rs1 = 6; in.rs2 = 7;
  RowRec rec{};
  run_row(st, in, rec, true);
  return st.regs[5];
}

int main(int argc, char** argv) {
  signal(SIGABRT, on_abort);
  utils::log::set_level("off");
  std::vector<u8> code(0x400, 0);
  for (size_t i = 0; i < code.size(); i += 4) { u32 w = 0x00000013; memcpy(&code[i], &w, 4); }
  static VirtualBytecode bc = VirtualBytecode::from_riscv_code(BASE, std::span<const u8>(code.data(), code.size()));
  CODE = &bc;
  u64 nrand = argc > 1 ? strtoull(argv[1], nullptr, 0) : 1000000;
  // Optional: load the Tracer's words from a .hex file (gen.py's format) instead of the compiled table, so
  // mutants (harness/mutants.py) need no rebuild.
  std::vector<TracerTemplate> loaded;
  if (argc > 2) {
    FILE* f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 2; }
    char name[32], hex[40]; int idx;
    while (fscanf(f, "%31s %d %39s", name, &idx, hex) == 3) {
      const TracerTemplate* base = nullptr;
      for (const TracerTemplate& t : kTemplates) if (!strcmp(t.name, name)) base = &t;
      if (!base) { fprintf(stderr, "unknown template %s\n", name); return 2; }
      if (idx == 0) { loaded.push_back(*base); loaded.back().n = 0; }
      if (idx >= kMaxWords) { fprintf(stderr, "template too long\n"); return 2; }
      std::string h(hex);
      loaded.back().w[idx] = {strtoull(h.substr(16).c_str(), nullptr, 16), strtoull(h.substr(0, 16).c_str(), nullptr, 16)};
      loaded.back().n = idx + 1;
    }
    fclose(f);
  }

  const u64 MIN = 0x8000000000000000ull, MAX = 0x7fffffffffffffffull, M1 = ~0ull;
  const std::vector<u64> edge = {
      0, 1, 2, 3, 7, M1, M1 - 1, MIN, MIN + 1, MAX,
      0x000000007fffffffull, 0x0000000080000000ull, 0x00000000ffffffffull, 0xffffffff80000000ull,
      0xffffffff7fffffffull, 0x0000000100000000ull, 0xdeadbeef00000000ull, 0x1234567880000000ull,
      0xabcdef01ffffffffull, 0x00000001ffffffffull, 0x0000000000000005ull, 0xfffffffffffffff9ull,
      0x123456789abcdef0ull, 0xfedcba9876543210ull, 0x0000000080000001ull};

  // ---- table: Jolt's advice against the plain RV64 M result, edge cases only
  printf("# Edge cases: Jolt's advice (jolt-cpp, compiled) against plain RV64 M\n");
  printf("# advice\tplain RV64 M counterpart\tcases\tdiffer\tclasses that differ (count)\tfirst difference\n");
  struct Pair { OpCode adv; const char* cname; u64 (*plain)(u64, u64); };
  auto negabs = [](u64 v) { return (i64)v < 0 ? 0 - v : v; };
  static auto abs_div = [](u64 a, u64 b) { u64 q = rv_div(a, b); return (i64)q < 0 ? 0 - q : q; };
  static auto abs_divw = [](u64 a, u64 b) { u64 q = rv_divw(a, b); return (i64)q < 0 ? 0 - q : q; };
  (void)negabs;
  const Pair pairs[] = {
      {VirtualAdviceDivU, "DIVU", rv_divu}, {VirtualAdviceDiv, "DIV", rv_div},
      {VirtualAdviceRem, "|DIV| (NegateIf)", +[](u64 a, u64 b) { return abs_div(a, b); }},
      {VirtualAdviceDivUW, "DIVUW", rv_divuw}, {VirtualAdviceDivW, "DIVW", rv_divw},
      {VirtualAdviceRemW, "|DIVW| (NegateIf)", +[](u64 a, u64 b) { return abs_divw(a, b); }}};
  for (const Pair& p : pairs) {
    bool word = p.adv == VirtualAdviceDivUW || p.adv == VirtualAdviceDivW || p.adv == VirtualAdviceRemW;
    u64 n = 0, nd = 0, by_class[4] = {0, 0, 0, 0};
    std::string first;
    for (u64 a : edge) for (u64 b : edge) {
      n++;
      u64 j = jolt_advice_value(p.adv, a, b), q = p.plain(a, b);
      if (j == q) continue;
      nd++;
      bool z = word ? (u32)b == 0 : b == 0;
      bool ov = word ? ((i32)a == INT32_MIN && (i32)b == -1) : (a == MIN && b == M1);
      int cls = z ? 0 : ov ? 1 : (word && ((u32)(j >> 32) != (u32)(q >> 32)) ? 2 : 3);
      by_class[cls]++;
      if (first.empty()) {
        char m[128]; snprintf(m, sizeof m, "a=0x%016llx b=0x%016llx: Jolt 0x%016llx, RV64 M 0x%016llx",
                              (unsigned long long)a, (unsigned long long)b, (unsigned long long)j, (unsigned long long)q);
        first = m;
      }
    }
    std::string cls;
    const char* cn[4] = {"divide by zero", "overflow", "upper half (extension)", "other"};
    for (int c = 0; c < 4; c++) if (by_class[c]) cls += std::string(cls.empty() ? "" : ", ") + cn[c] + " (" + std::to_string(by_class[c]) + ")";
    printf("%s\t%s\t%llu\t%llu\t%s\t%s\n", opcode_to_string(p.adv), p.cname, (unsigned long long)n,
           (unsigned long long)nd, cls.empty() ? "none" : cls.c_str(), first.empty() ? "-" : first.c_str());
  }

  // ---- the templates
  struct Regs { u32 rd, rs1, rs2; const char* what; };
  const Regs regsets[] = {{5, 6, 7, "distinct"}, {6, 6, 7, "rd = rs1"}, {7, 6, 7, "rd = rs2"}, {5, 6, 6, "rs1 = rs2"},
                          {6, 6, 6, "rd = rs1 = rs2"}, {5, 0, 7, "rs1 = x0"}, {5, 6, 0, "rs2 = x0"},
                          {31, 30, 29, "high registers"}};
  std::mt19937_64 rng(0x5eed7);
  auto rnd = [&]() -> u64 {
    switch (rng() % 8) {
      case 0: return rng();                                   // any 64-bit value
      case 1: return rng() & 0xff;                            // small
      case 2: return (u64)-(i64)(rng() & 0xff);               // small negative
      case 3: return rng() & 0xffffffffull;                   // a 32-bit value, upper half 0
      case 4: return sx32(rng());                             // a sign-extended 32-bit value
      case 5: return (rng() << 32) | (rng() & 0xff);          // garbage upper half, small low word
      case 6: return edge[rng() % edge.size()];               // an edge value
      default: return rng() >> (rng() % 64);                  // a random width
    }
  };
  printf("\n# Templates: Tracer words (out/div_templates.inc) against jolt-cpp\n");
  printf("# template\tcases\tC1 advice\tC2 rows\tC3 values+asserts\tC4 RV64 M\t(of C3: assertion failures)\n");
  u64 total = 0, bad = 0;
  std::vector<std::string> firsts;
  std::vector<TracerTemplate> todo(std::begin(kTemplates), std::end(kTemplates));
  if (!loaded.empty()) todo = loaded;
  for (const TracerTemplate& t : todo) {
    Tally ty;
    for (const Regs& g : regsets)
      for (u64 a : edge) for (u64 b : edge) check(t, g.rd, g.rs1, g.rs2, a, b, rng, ty);
    for (u64 i = 0; i < nrand; i++) {
      const Regs& g = regsets[i % 8 == 0 ? (rng() % 8) : 0];
      check(t, g.rd, g.rs1, g.rs2, rnd(), rnd(), rng, ty);
    }
    // rd = x0: Jolt emits the single NOP row, so the template isn't used (rd0 = 1, spec 4.19.4).
    {
      static VirtualInstruction buf[64];
      u64 n = expand_virt_seq(std::span<VirtualInstruction>(buf, 64), Instruction(t.src, 0, 6, 7, false, false, false));
      Word w0 = decode(t.w[0]);
      if (!(n == 1 && buf[0].opcode == ADDI && buf[0].rd == 0 && buf[0].is_virtual) || w0.rd0 != 1)
        { if (ty.fail[1]++ == 0) ty.first[1] = std::string(t.name) + " rd=x0: not the virtual NOP row / rd0 != 1"; }
    }
    printf("%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n", t.name, (unsigned long long)ty.cases, (unsigned long long)ty.fail[0],
           (unsigned long long)ty.fail[1], (unsigned long long)ty.fail[2], (unsigned long long)ty.fail[3],
           (unsigned long long)ty.aborts);
    total += ty.cases;
    for (int c = 0; c < 4; c++) if (ty.fail[c]) { bad += ty.fail[c]; firsts.push_back(ty.first[c]); }
  }
  printf("\n# %llu cases, %llu failures\n", (unsigned long long)total, (unsigned long long)bad);
  for (auto& f : firsts) printf("# first failure: %s\n", f.c_str());
  return bad ? 1 : 0;
}
