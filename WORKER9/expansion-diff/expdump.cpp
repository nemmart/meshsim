// Dump every non-virtual opcode's expansion (expand_virt_seq) for a grid of operands.
// Built once against each tracer tree; outputs are diffed by opcode name.
#include "tracer/isa/virtual.cpp"
#include <cstdio>
#include <vector>

int main() {
  const u32 rds[] = {0, 5};
  const u32 rs1s[] = {0, 6};
  const u32 cs[] = {7, 0xFFFFFFF4u, 0x800, 0};
  std::vector<VirtualInstruction> buf(1 << 16);
  for (int op = 0; op < (int)isa::NUM_OPCODES; ++op) {
    auto code = (isa::OpCode)op;
    if (isa::is_virtual(code)) continue;
    std::printf("OP %s num_cycles=%llu\n", isa::opcode_to_string(code),
                (unsigned long long)virt::num_cycles(code));
    for (u32 rd : rds) for (u32 rs1 : rs1s) for (u32 c : cs) for (int imm = 0; imm < 2; ++imm) {
      isa::Instruction src(code, rd, rs1, c, false, false, imm);
      for (auto &v : buf) v = VirtualInstruction{};
      u64 n = expand_virt_seq(std::span<VirtualInstruction>(buf.data(), buf.size()), src);
      std::printf("V %s rd=%u rs1=%u c=%#x imm_c=%d rows=%llu ncyc=%llu\n", isa::opcode_to_string(code),
                  rd, rs1, c, imm, (unsigned long long)n, (unsigned long long)virt::num_cycles(src));
      for (u64 i = 0; i < n; ++i) {
        const auto &r = buf[i];
#ifdef HAS_SHIFT_OFFSET
        long so = r.shift_offset;
#else
        long so = 0;
#endif
        std::printf("  %llu %s rd=%#llx%s rs1=%#llx%s rs2=%#llx%s so=%ld first=%d rem=%u virt=%d adv=%u,%u,%u\n",
                    (unsigned long long)i, isa::opcode_to_string(r.opcode),
                    (unsigned long long)r.rd, r.rd_is_imm ? "i" : "",
                    (unsigned long long)r.rs1, r.rs1_is_imm ? "i" : "",
                    (unsigned long long)r.rs2, r.rs2_is_imm ? "i" : "",
                    so, r.is_first_in_sequence, r.virt_seq_remaining, r.is_virtual,
                    (unsigned)r.advice_source.operation, r.advice_source.input1_reg, r.advice_source.input2_reg);
      }
    }
  }
}
