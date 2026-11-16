// rv32_decode.h: the core's instruction decode, the RTL's `decompress.veryl` and `decode.veryl` as plain functions
// (spec/core.md §1: RV32I + C + Zmmul + Zicsr + Zicond; custom-0 and custom-1 are the coprocessor's, illegal where the
// tile has none; FENCE.I, DIV/REM and every other encoding are illegal).
//
//   expand(h)   a 16-bit instruction's 32-bit form, 0 (an illegal word) for a reserved or unimplemented encoding
//   decode(w)   a 32-bit instruction's class, registers, immediate and operation
#pragma once

#include <cstdint>

namespace units::rv32d {

enum Class : uint8_t {
    ALU, BRANCH, JAL, JALR, LOAD, STORE, MUL, CSR, ECALL, EBREAK, MRET, WFI, NOP, COPROCESSOR, ILLEGAL
};
enum Alu : uint8_t { ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, CZERO_EQZ, CZERO_NEZ };
enum OperandA : uint8_t { A_RS1, A_PC, A_ZERO };

struct Decoded {
    Class cls = ILLEGAL;
    uint8_t rs1 = 0, rs2 = 0, rd = 0;
    bool uses1 = false, uses2 = false, writes = false;   // never true for x0 (decode.veryl: hazard checks trust them)
    uint32_t imm = 0;
    Alu alu = ADD;
    OperandA a = A_RS1;
    bool b_imm = false;                                  // operand b: the immediate, else rs2
    uint8_t f3 = 0;                                      // a branch's condition, a load's or store's size and sign,
                                                         // a multiply's and a CSR instruction's operation
    bool csr_writes = false;
};

inline uint32_t sext(uint32_t v, unsigned bits) { return uint32_t(int32_t(v << (32 - bits)) >> (32 - bits)); }

// ------------------------------------------------------------------------------------------------- decompress.veryl
inline uint32_t r_type(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
inline uint32_t i_type(uint32_t imm12, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return (imm12 & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
inline uint32_t s_type(uint32_t imm12, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    return (imm12 >> 5 & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm12 & 0x1F) << 7 | 0x23;
}
inline uint32_t b_type(uint32_t off, uint32_t rs2, uint32_t rs1, uint32_t f3) {   // off: the byte offset
    return (off >> 12 & 1) << 31 | (off >> 5 & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (off >> 1 & 0xF) << 8 |
           (off >> 11 & 1) << 7 | 0x63;
}
inline uint32_t j_type(uint32_t off, uint32_t rd) {                               // off: the byte offset
    return (off >> 20 & 1) << 31 | (off >> 1 & 0x3FF) << 21 | (off >> 11 & 1) << 20 | (off >> 12 & 0xFF) << 12 | rd << 7 | 0x6F;
}

inline uint32_t expand(uint32_t c) {
    const auto bit = [c](unsigned i) { return c >> i & 1; };
    const auto bits = [c](unsigned hi, unsigned lo) { return c >> lo & ((1u << (hi - lo + 1)) - 1); };
    const uint32_t r7 = bits(11, 7), r2 = bits(6, 2), s7 = 8 | bits(9, 7), s2 = 8 | bits(4, 2);
    const uint32_t small = sext(bit(12) << 5 | bits(6, 2), 6);
    const uint32_t shamt = bits(6, 2);
    const uint32_t word_off = bit(5) << 6 | bits(12, 10) << 3 | bit(6) << 2;
    const uint32_t jump = sext(bit(12) << 11 | bit(8) << 10 | bits(10, 9) << 8 | bit(6) << 7 | bit(7) << 6 | bit(2) << 5 |
                               bit(11) << 4 | bits(5, 3) << 1, 12);
    const uint32_t branch = sext(bit(12) << 8 | bits(6, 5) << 6 | bit(2) << 5 | bits(11, 10) << 3 | bits(4, 3) << 1, 9);
    switch (bits(15, 13) << 2 | bits(1, 0)) {
    // ---- quadrant 0
    case 0b000'00: {                                                              // C.ADDI4SPN (0 reserved)
        const uint32_t imm = bits(10, 7) << 6 | bits(12, 11) << 4 | bit(5) << 3 | bit(6) << 2;
        return imm == 0 ? 0 : i_type(imm, 2, 0, s2, 0x13);
    }
    case 0b010'00: return i_type(word_off, s7, 2, s2, 0x03);                      // C.LW
    case 0b110'00: return s_type(word_off, s2, s7, 2);                            // C.SW
    // ---- quadrant 1
    case 0b000'01: return i_type(small, r7, 0, r7, 0x13);                         // C.ADDI
    case 0b001'01: return j_type(jump, 1);                                        // C.JAL
    case 0b010'01: return i_type(small, 0, 0, r7, 0x13);                          // C.LI
    case 0b011'01:
        if (r7 == 2) {                                                            // C.ADDI16SP (0 reserved)
            const uint32_t imm = sext(bit(12) << 9 | bits(4, 3) << 7 | bit(5) << 6 | bit(2) << 5 | bit(6) << 4, 10);
            return imm == 0 ? 0 : i_type(imm, 2, 0, 2, 0x13);
        } else {                                                                  // C.LUI (0 reserved)
            const uint32_t up = sext(bit(12) << 5 | bits(6, 2), 6) & 0xFFFFF;
            return up == 0 ? 0 : up << 12 | r7 << 7 | 0x37;
        }
    case 0b100'01:
        switch (bits(11, 10)) {
        case 0: return bit(12) ? 0 : i_type(shamt, s7, 5, s7, 0x13);              // C.SRLI
        case 1: return bit(12) ? 0 : i_type(0x400 | shamt, s7, 5, s7, 0x13);      // C.SRAI
        case 2: return i_type(small, s7, 7, s7, 0x13);                            // C.ANDI
        default:
            if (bit(12)) return 0;                                                // RV64's, or reserved
            switch (bits(6, 5)) {
            case 0: return r_type(0x20, s2, s7, 0, s7, 0x33);                     // C.SUB
            case 1: return r_type(0, s2, s7, 4, s7, 0x33);                        // C.XOR
            case 2: return r_type(0, s2, s7, 6, s7, 0x33);                        // C.OR
            default: return r_type(0, s2, s7, 7, s7, 0x33);                       // C.AND
            }
        }
    case 0b101'01: return j_type(jump, 0);                                        // C.J
    case 0b110'01: return b_type(branch, 0, s7, 0);                               // C.BEQZ
    case 0b111'01: return b_type(branch, 0, s7, 1);                               // C.BNEZ
    // ---- quadrant 2
    case 0b000'10: return bit(12) ? 0 : i_type(shamt, r7, 1, r7, 0x13);           // C.SLLI
    case 0b010'10:                                                                // C.LWSP (rd = x0 reserved)
        return r7 == 0 ? 0 : i_type(bits(3, 2) << 6 | bit(12) << 5 | bits(6, 4) << 2, 2, 2, r7, 0x03);
    case 0b100'10:
        if (!bit(12)) {
            if (r2 == 0) return r7 == 0 ? 0 : i_type(0, r7, 0, 0, 0x67);          // C.JR (rs1 = x0 reserved)
            return r_type(0, r2, 0, 0, r7, 0x33);                                 // C.MV
        }
        if (r2 == 0) return r7 == 0 ? i_type(1, 0, 0, 0, 0x73) : i_type(0, r7, 0, 1, 0x67);   // C.EBREAK, C.JALR
        return r_type(0, r2, r7, 0, r7, 0x33);                                    // C.ADD
    case 0b110'10: return s_type(bits(8, 7) << 6 | bits(12, 9) << 2, r2, 2, 2);   // C.SWSP
    default: return 0;                                                            // F, D, Zcb, quadrant 3
    }
}

// ----------------------------------------------------------------------------------------------------- decode.veryl
inline Decoded decode(uint32_t ins, bool coprocessor_present = false) {
    Decoded d;
    const uint32_t op = ins & 0x7F, f3 = ins >> 12 & 7, f7 = ins >> 25;
    const int32_t s = int32_t(ins);
    d.rs1 = ins >> 15 & 31; d.rs2 = ins >> 20 & 31; d.rd = ins >> 7 & 31; d.f3 = uint8_t(f3);
    bool r1 = false, r2 = false, wr = false;
    const uint32_t i_imm = uint32_t(s >> 20), u_imm = ins & 0xFFFFF000u;
    switch (op) {
    case 0x37: d.cls = ALU; d.a = A_ZERO; d.b_imm = true; d.imm = u_imm; wr = true; break;            // LUI
    case 0x17: d.cls = ALU; d.a = A_PC; d.b_imm = true; d.imm = u_imm; wr = true; break;              // AUIPC
    case 0x6F:                                                                                        // JAL
        d.cls = JAL; wr = true;
        d.imm = uint32_t(s >> 31) << 20 | (ins & 0xFF000) | (ins >> 20 & 1) << 11 | (ins >> 21 & 0x3FF) << 1;
        break;
    case 0x67: if (f3 == 0) { d.cls = JALR; d.imm = i_imm; r1 = true; wr = true; } break;             // JALR
    case 0x63:                                                                                        // branches
        if (f3 == 2 || f3 == 3) break;
        d.cls = BRANCH; r1 = r2 = true;
        d.imm = uint32_t(s >> 31) << 12 | (ins >> 7 & 1) << 11 | (ins >> 25 & 0x3F) << 5 | (ins >> 8 & 0xF) << 1;
        break;
    case 0x03:                                                                                        // loads
        if (f3 == 3 || f3 > 5) break;
        d.cls = LOAD; d.imm = i_imm; r1 = true; wr = true;
        break;
    case 0x23:                                                                                        // stores
        if (f3 > 2) break;
        d.cls = STORE; d.imm = uint32_t(s >> 25) << 5 | d.rd; r1 = r2 = true;
        break;
    case 0x13: {                                                                                      // op-imm
        static constexpr Alu k[8] = {ADD, SLL, SLT, SLTU, XOR, SRL, OR, AND};
        d.alu = k[f3];
        if (f3 == 1 && f7 != 0) break;
        if (f3 == 5) { if (f7 == 0x20) d.alu = SRA; else if (f7 != 0) break; }
        d.cls = ALU; d.b_imm = true; d.imm = i_imm; r1 = true; wr = true;
        break;
    }
    case 0x33: {                                                                                      // op
        static constexpr Alu k[8] = {ADD, SLL, SLT, SLTU, XOR, SRL, OR, AND};
        if (f7 == 0) d.alu = k[f3];
        else if (f7 == 0x20) { if (f3 == 0) d.alu = SUB; else if (f3 == 5) d.alu = SRA; else break; }
        else if (f7 == 1) { if (f3 > 3) break; }                                                      // Zmmul; no DIV/REM
        else if (f7 == 7) { if (f3 == 5) d.alu = CZERO_EQZ; else if (f3 == 7) d.alu = CZERO_NEZ; else break; }   // Zicond
        else break;
        d.cls = f7 == 1 ? MUL : ALU; r1 = r2 = true; wr = true;
        break;
    }
    case 0x0F: if (f3 == 0) d.cls = NOP; break;                                                       // FENCE; no FENCE.I
    case 0x73:
        if (f3 == 0) {
            d.cls = ins == 0x00000073 ? ECALL : ins == 0x00100073 ? EBREAK : ins == 0x30200073 ? MRET
                  : ins == 0x10500073 ? WFI : ILLEGAL;
        } else if (f3 != 4) {                                                                         // Zicsr
            d.cls = CSR; d.b_imm = true; wr = true;
            d.csr_writes = (f3 & 3) == 1 || d.rs1 != 0;
            if (f3 & 4) { d.a = A_ZERO; d.imm = d.rs1; } else r1 = true;                              // operand: rs1 + 0, or 0 + uimm
        }
        break;
    case 0x0B: case 0x2B: if (coprocessor_present) { d.cls = COPROCESSOR; r1 = r2 = true; wr = true; } break;
    default: break;
    }
    d.uses1 = r1 && d.rs1 != 0;
    d.uses2 = r2 && d.rs2 != 0;
    d.writes = wr && d.rd != 0;
    return d;
}

}  // namespace units::rv32d
