// row_core.h: what the replayer (4.19) and the Tracer (4.21) share as rules.
//
// sim/ref/meshsim/src/RowCore.java, function for function: the decode table's index (4.19.5), the source instruction's
// immediate, the expansion word's fields and checks (4.19.3, 4.19.11), the operand codes (4.19.3, 4.19.4) and the decode
// (K_PAD/ROM/NOP/CTRL/BAD). Pure functions, no state; Java's int arithmetic is reproduced with explicit casts (an
// arithmetic shift of a 32-bit value is `int32_t >> n`, a logical one `uint32_t >> n`).
#pragma once

#include <cstdint>

#include "units/replayer_alu.h"

namespace units::rowcore {

// The decode table's 11-bit index (4.19.5); bit 0 of w (the compressed flag) plays no part.
inline unsigned decode_index(uint32_t w) {
    w |= 1;
    uint32_t op = w & 0x7F, f3 = (w >> 12) & 7;
    if (op == 0x2F) return 1u << 10 | ((w >> 12) & 1) << 5 | ((w >> 27) & 31);                       // AMO
    if (op == 0x0B) return 1u << 10 | 1u << 8 | ((w >> 25) & 31) << 3 | f3;                          // custom-0
    if (op == 0x73) {                                                                                 // SYSTEM
        uint32_t sel = f3 != 0 ? f3 : w == 0x00000073u ? 8 : w == 0x00100073u ? 9 : w == 0x30200073u ? 10 : 11;
        return 1u << 10 | 2u << 8 | sel;
    }
    return ((w >> 2) & 31) << 5 | f3 << 2 | ((w >> 30) & 1) << 1 | ((w >> 25) & 1);
}

// The source instruction's immediate by format (4.19.3, code 132): 0 R, 1 I, 2 S, 3 B, 4 U, 5 J; sign-extended to 64.
inline uint64_t source_imm(unsigned format, uint32_t wu) {
    wu |= 1;
    const int32_t w = int32_t(wu);
    int32_t v;
    switch (format) {
        case 0: v = 0; break;
        case 1: v = w >> 20; break;
        case 2: v = int32_t(uint32_t(w >> 25) << 5 | ((wu >> 7) & 31)); break;
        case 3: v = int32_t(uint32_t(w >> 31) << 12 | ((wu >> 7) & 1) << 11 | ((wu >> 25) & 63) << 5 | ((wu >> 8) & 15) << 1); break;
        case 4: v = int32_t(wu & 0xFFFFF000u); break;
        case 5: v = int32_t(uint32_t(w >> 31) << 20 | ((wu >> 12) & 0xFF) << 12 | ((wu >> 20) & 1) << 11 | ((wu >> 21) & 0x3FF) << 1); break;
        default: FATAL("rowcore::source_imm: format %u", format);
    }
    return uint64_t(int64_t(v));
}

// ---- the expansion word (4.19.3), low half ------------------------------------------------------------------------------
inline unsigned op(uint64_t lo) { return unsigned(lo & 0xFF); }
inline unsigned c_rd(uint64_t lo) { return unsigned(lo >> 8) & 0xFF; }
inline unsigned c_rs1(uint64_t lo) { return unsigned(lo >> 16) & 0xFF; }
inline unsigned c_rs2(uint64_t lo) { return unsigned(lo >> 24) & 0xFF; }
inline unsigned seq_remaining(uint64_t lo) { return unsigned(lo >> 32) & 0xFFF; }
inline bool next_from_value(uint64_t lo) { return (lo >> 44) & 1; }
inline bool hold(uint64_t lo) { return (lo >> 45) & 1; }
inline unsigned rd0(uint64_t lo) { return unsigned(lo >> 46) & 3; }
inline unsigned format(uint64_t lo) { return unsigned(lo >> 48) & 7; }
inline bool virt(uint64_t lo) { return (lo >> 51) & 1; }
inline bool imm0pc0(uint64_t lo) { return (lo >> 52) & 1; }
inline bool report_c(uint64_t lo) { return (lo >> 53) & 1; }
inline bool from_log(uint64_t lo) { return (lo >> 54) & 1; }
inline unsigned reserved(uint64_t lo) { return unsigned(lo >> 55) & 0x7F; }   // [61:55], reserved since v1.25 s9
inline bool sign_imm(uint64_t lo) { return (lo >> 62) & 1; }
inline bool offset_imm(uint64_t lo) { return (lo >> 63) & 1; }

// 4.19.11's invalid-expansion checks on a word (reserved bits apart, which are UB). Returns 0 if the word is good, else
// the cause: 1 an opcode the replayer doesn't execute, 2 a reserved operand code (146-255), 3 format 6 or 7.
enum : unsigned { INV_OK = 0, INV_OPCODE = 1, INV_OPERAND = 2, INV_FORMAT = 3 };
inline unsigned invalid_word(uint64_t lo) {
    if (!alu::executes(op(lo))) return INV_OPCODE;
    if (c_rd(lo) >= 146 || c_rs1(lo) >= 146 || c_rs2(lo) >= 146) return INV_OPERAND;
    if (format(lo) >= 6) return INV_FORMAT;
    return INV_OK;
}

// Whether any slot or the shift offset reads the source immediate (codes 132-136, offset_imm).
inline bool needs_source_imm(uint64_t lo) {
    unsigned d = c_rd(lo), s1 = c_rs1(lo), s2 = c_rs2(lo);
    return (s1 >= 132 && s1 <= 136) || (s2 >= 132 && s2 <= 136) || (d >= 132 && d <= 136) || offset_imm(lo);
}

// A register operand code's register, or -1 for an immediate code (4.19.3, 4.19.4); w the code-stream word, offset the
// temporary offset bit.
inline int reg(unsigned c, uint32_t w, unsigned offset) {
    if (c < 128) return int(c);
    if (c == 129) return offset == 1 ? 40 : int((w >> 7) & 31);
    if (c == 130) return int((w >> 15) & 31);
    if (c == 131) return int((w >> 20) & 31);
    if (c >= 137 && c <= 144) return int(40 + (c - 137) + offset);
    return -1;
}

// An immediate operand code's value (4.19.3).
inline uint64_t imm_val(unsigned c, uint64_t cnst, uint64_t imm) {
    switch (c) {
        case 128: return cnst;
        case 145: return 0;                                   // immediate 0 (26q): a zero slot beside a non-zero constant
        case 132: return imm;
        case 133: return 0 - (uint64_t(1) << (imm & 63));
        case 134: return uint64_t(1) << (imm & 63);
        case 135: return uint64_t(1) << (imm & 31);
        case 136: return (uint64_t(1) << 32) - (uint64_t(1) << (imm & 31));
        default: FATAL("rowcore::imm_val: operand code %u", c);
    }
}

// ---- the decode (4.19.4, 4.19.5) ----------------------------------------------------------------------------------------
// The template an instruction takes: K_ROM (start, offset), K_NOP (the shared NOP row, rd0 1 or 3), K_CTRL
// (controller-fed, class 1 or 2) or K_BAD (class 3). The padding word (bits [1:0] = 00) is the caller's.
enum : unsigned { K_PAD = 0, K_ROM = 1, K_NOP = 2, K_CTRL = 3, K_BAD = 4 };
struct Decoded {
    unsigned kind = 0, start = 0, rd0 = 0, offset = 0, cls = 0;
};
// decode_table: 2048 entries; rom_lo: the ROM's 512 low halves.
inline void decode(uint32_t w, const uint16_t* decode_table, const uint64_t* rom_lo, Decoded& d) {
    unsigned e = decode_table[decode_index(w)], cls = e & 3, rd_field = (w >> 7) & 31;
    d.cls = cls; d.start = 0; d.rd0 = 0; d.offset = 0;
    if (cls == 3) d.kind = K_BAD;
    else if (cls == 0) {
        d.start = (e >> 2) & 0x1FF;
        d.rd0 = unsigned(rom_lo[d.start] >> 46) & 3;
        if (rd_field == 0 && (d.rd0 == 1 || d.rd0 == 3)) d.kind = K_NOP;
        else { d.kind = K_ROM; d.offset = rd_field == 0 && d.rd0 == 2 ? 1 : 0; }
    } else d.kind = K_CTRL;
}

}  // namespace units::rowcore
