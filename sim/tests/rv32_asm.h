// rv32_asm.h: a tiny RV32 assembler for the tests and the benchmark (instruction words from fields).
#pragma once

#include <cstdint>

namespace rv32_asm {

inline uint32_t R(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op = 0x33) {
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
inline uint32_t I(int32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op = 0x13) {
    return uint32_t(imm & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
inline uint32_t S(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    uint32_t u = uint32_t(imm);
    return (u >> 5 & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (u & 31) << 7 | 0x23;
}
inline uint32_t B(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    uint32_t u = uint32_t(imm);
    return (u >> 12 & 1) << 31 | (u >> 5 & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (u >> 1 & 0xF) << 8 | (u >> 11 & 1) << 7 | 0x63;
}
inline uint32_t U(uint32_t imm, uint32_t rd, uint32_t op) { return (imm & 0xFFFFF000u) | rd << 7 | op; }
inline uint32_t J(int32_t imm, uint32_t rd) {
    uint32_t u = uint32_t(imm);
    return (u >> 20 & 1) << 31 | (u >> 1 & 0x3FF) << 21 | (u >> 11 & 1) << 20 | (u >> 12 & 0xFF) << 12 | rd << 7 | 0x6F;
}
inline uint32_t LW(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 2, rd, 0x03); }
inline uint32_t SW(uint32_t rs2, uint32_t rs1, int32_t imm) { return S(imm, rs2, rs1, 2); }
inline uint32_t ADDI(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 0, rd); }
inline uint32_t LUI(uint32_t rd, uint32_t imm) { return U(imm, rd, 0x37); }
inline uint32_t CSR(uint32_t f3, uint32_t csr, uint32_t rs1, uint32_t rd) { return csr << 20 | rs1 << 15 | f3 << 12 | rd << 7 | 0x73; }
inline constexpr uint32_t ECALL = 0x00000073, EBREAK = 0x00100073, WFI = 0x10500073, NOP = 0x00000013;


}  // namespace rv32_asm
