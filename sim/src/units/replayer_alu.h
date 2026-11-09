// replayer_alu.h: what a replayer row computes (WORKER15; owner W15, W14 reads it).
//
// sim/ref/meshsim/src/ReplayerAlu.java (spec 4.19.7; spec/replayer-opcodes.md 1-5, jolt-cpp 821e1bd): the opcode table
// (88 row opcodes and their classes), and the value for rd and the new pc of one row. Pure functions, no state, no
// allocation. Java's semantics are reproduced exactly: 64-bit wrap-around arithmetic, a shift count taken mod 64 (Java's
// `a >>> 64` is `a`), Long.rotateRight's count mod 64.
//
// Slots: a is the rs1 slot's value, b the rs2 slot's, c the rd slot's (the immediate when rd is an immediate slot, else
// rd's value before the row).
#pragma once

#include <cstdint>

#include "base/fatal.h"

namespace units::alu {

enum : unsigned {
    NOP = 0, ADD = 1, ADDI = 2, SUB = 3, XOR = 4, XORI = 5, OR = 6, ORI = 7, AND = 8, ANDI = 9,
    SLT = 16, SLTI = 17, SLTU = 18, SLTIU = 19, BEQ = 28, BNE = 29, BLT = 30, BGE = 31, BLTU = 32, BGEU = 33,
    JAL = 34, JALR = 35, AUIPC = 36, LUI = 37, MUL = 43, MULHU = 45, FENCE = 52, SD = 53, LD = 54,
    SUBW = 62, ADDW = 63, ADDIW = 64, MULW = 65,
    REV8W = 107, ASSERT_EQ = 108, HOSTIO = 109, LOAD_B = 110, LOAD_H = 111, LOAD_W = 112, STORE_B = 113,
    STORE_H = 114, STORE_W = 115, SEXT_W = 116, ZEXT_W = 117, MULI = 118, MULIW = 119, POW2 = 120, SRBM = 121,
    SRL_V = 122, SRLI_V = 123, SRAI_V = 124, POW2W = 125, ADVICE = 126, ASSERT_LTE = 127, ASSERT_DIV0 = 128,
    ASSERT_UREM = 129, NEGATE_IF = 131, MOVSIGN = 132, SRA_V = 133, ASSERT_MULU = 134, ROTRI = 135,
    ROTRIW = 136, XORROT63 = 137, XORROT32 = 138, XORROT24 = 139, XORROT16 = 140, XORROTW16 = 141,
    XORROTW12 = 142, XORROTW8 = 143, XORROTW7 = 144, WMASK_W = 145, PEXT_S = 146, XORROTW22 = 147,
    XORROTW19 = 148, XORROTW6 = 149, SRBM_W = 150, SRLW_V = 151, SRLIW_V = 152, SRAW_V = 153, SRAIW_V = 154,
    PEXT = 155, WMASK_B = 156, WMASK_H = 157, SHDATA_B = 158, SHDATA_H = 159, SHDATA_W = 160, XORROTL1 = 161,
    ANDN = 162,
};

namespace detail {
struct Entry {
    unsigned code;
    const char* name;
    char cls;   // 'S', 'M', 'L'; 0: named but not executed by the replayer
};

// The 88 row opcodes a template can emit (ReplayerAlu.TABLE), then the nine that occur only in the tracer's rows
// (VirtualMove, expanded away; the DIV/REM advice opcodes, written as VirtualAdvice): named for messages only.
inline constexpr Entry kTable[] = {
    {0, "NOP", 'S'}, {1, "ADD", 'S'}, {2, "ADDI", 'S'}, {3, "SUB", 'S'}, {4, "XOR", 'S'}, {5, "XORI", 'S'},
    {6, "OR", 'S'}, {7, "ORI", 'S'}, {8, "AND", 'S'}, {9, "ANDI", 'S'}, {16, "SLT", 'S'}, {17, "SLTI", 'S'},
    {18, "SLTU", 'S'}, {19, "SLTIU", 'S'}, {28, "BEQ", 'S'}, {29, "BNE", 'S'}, {30, "BLT", 'S'}, {31, "BGE", 'S'},
    {32, "BLTU", 'S'}, {33, "BGEU", 'S'}, {34, "JAL", 'S'}, {35, "JALR", 'S'}, {36, "AUIPC", 'S'}, {37, "LUI", 'S'},
    {43, "MUL", 'M'}, {45, "MULHU", 'M'}, {52, "FENCE", 'S'}, {53, "SD", 'L'}, {54, "LD", 'L'}, {62, "SUBW", 'S'},
    {63, "ADDW", 'S'}, {64, "ADDIW", 'S'}, {65, "MULW", 'M'},
    {107, "VirtualRev8W", 'S'}, {108, "VirtualAssertEQ", 'S'}, {109, "VirtualHostIO", 'S'}, {110, "VirtualLoadB", 'L'},
    {111, "VirtualLoadH", 'L'}, {112, "VirtualLoadW", 'L'}, {113, "VirtualStoreB", 'L'}, {114, "VirtualStoreH", 'L'},
    {115, "VirtualStoreW", 'L'}, {116, "VirtualSignExtendWord", 'S'}, {117, "VirtualZeroExtendWord", 'S'},
    {118, "VirtualMULI", 'M'}, {119, "VirtualMULIW", 'M'}, {120, "VirtualPow2", 'S'},
    {121, "VirtualShiftRightBitmask", 'S'}, {122, "VirtualSRL", 'S'}, {123, "VirtualSRLI", 'S'}, {124, "VirtualSRAI", 'S'},
    {125, "VirtualPow2W", 'S'}, {126, "VirtualAdvice", 'L'}, {127, "VirtualAssertLTE", 'S'},
    {128, "VirtualAssertValidDiv0", 'S'}, {129, "VirtualAssertValidUnsignedRemainder", 'S'}, {131, "VirtualNegateIf", 'S'},
    {132, "VirtualMovsign", 'S'}, {133, "VirtualSRA", 'S'}, {134, "VirtualAssertMulUNoOverflow", 'S'},
    {135, "VirtualROTRI", 'S'}, {136, "VirtualROTRIW", 'S'}, {137, "VirtualXORROT63", 'S'}, {138, "VirtualXORROT32", 'S'},
    {139, "VirtualXORROT24", 'S'}, {140, "VirtualXORROT16", 'S'}, {141, "VirtualXORROTW16", 'S'},
    {142, "VirtualXORROTW12", 'S'}, {143, "VirtualXORROTW8", 'S'}, {144, "VirtualXORROTW7", 'S'},
    {145, "VirtualWindowMaskW", 'S'}, {146, "VirtualPextSigned", 'S'}, {147, "VirtualXORROTW22", 'S'},
    {148, "VirtualXORROTW19", 'S'}, {149, "VirtualXORROTW6", 'S'}, {150, "VirtualShiftRightBitmaskW", 'S'},
    {151, "VirtualSRLW", 'S'}, {152, "VirtualSRLIW", 'S'}, {153, "VirtualSRAW", 'S'}, {154, "VirtualSRAIW", 'S'},
    {155, "VirtualPext", 'S'}, {156, "VirtualWindowMaskB", 'S'}, {157, "VirtualWindowMaskH", 'S'},
    {158, "VirtualShiftDataB", 'S'}, {159, "VirtualShiftDataH", 'S'}, {160, "VirtualShiftDataW", 'S'},
    {161, "VirtualXORROTL1", 'S'}, {162, "ANDN", 'S'},
    {130, "VirtualMove", 0}, {163, "VirtualAdviceDivU", 0}, {164, "VirtualAdviceRemU", 0}, {165, "VirtualAdviceDiv", 0},
    {166, "VirtualAdviceRem", 0}, {167, "VirtualAdviceDivUW", 0}, {168, "VirtualAdviceRemUW", 0},
    {169, "VirtualAdviceDivW", 0}, {170, "VirtualAdviceRemW", 0},
};

struct Tables {
    const char* name[256] = {};
    char cls[256] = {};
    constexpr Tables() {
        for (const Entry& e : kTable) { name[e.code] = e.name; cls[e.code] = e.cls; }
    }
};
inline constexpr Tables kT{};
}  // namespace detail

// The opcode's name (ReplayerAlu.NAME: the 88 row opcodes and the nine not executed), or nullptr.
inline const char* name_or_null(unsigned op) { return op < 256 ? detail::kT.name[op] : nullptr; }
// Its row class (4.19.7): 'S' simple, 'M' multiply, 'L' log; 0 for an opcode the replayer doesn't execute.
inline char cls(unsigned op) { return op < 256 ? detail::kT.cls[op] : 0; }
// The opcode named `name`, or -1 (configuration and tests only).
inline int code(const char* name) {
    for (unsigned i = 0; i < 256; i++)
        if (detail::kT.name[i] && __builtin_strcmp(detail::kT.name[i], name) == 0) return int(i);
    return -1;
}

inline bool executes(unsigned op) { return op < 256 && cls(op) != 0; }
inline unsigned cycles(unsigned op) { return cls(op) == 'M' ? 5 : 3; }

// One row's results. `undefined`: non-null when the result is undefined for these inputs (invariant 16); meshsim flags it.
struct Out {
    uint64_t rd = 0;
    uint64_t new_pc = 0;
    const char* undefined = nullptr;
};

inline uint64_t sx32(uint64_t x) { return uint64_t(int64_t(int32_t(uint32_t(x)))); }
inline uint64_t zx32(uint64_t x) { return x & 0xFFFFFFFFull; }
inline unsigned tz(uint64_t x) { return x == 0 ? 64 : unsigned(__builtin_ctzll(x)); }
inline uint64_t mulhu(uint64_t x, uint64_t y) { return uint64_t((unsigned __int128)x * y >> 64); }
inline uint64_t rotr64(uint64_t x, unsigned k) { k &= 63; return k == 0 ? x : x >> k | x << (64 - k); }
inline uint32_t rotr32(uint32_t x, unsigned k) { k &= 31; return k == 0 ? x : x >> k | x << (32 - k); }
// The general bit extract (any mask): a's bits under the mask, packed from bit 0.
inline uint64_t pext(uint64_t a, uint64_t m) {
    uint64_t v = 0;
    unsigned k = 0;
    for (uint64_t r = m; r != 0; r &= r - 1, k++)
        if (a & (r & (0 - r))) v |= uint64_t(1) << k;
    return v;
}
inline bool is_load(unsigned op) { return op == LD || op == LOAD_B || op == LOAD_H || op == LOAD_W; }
inline bool is_store(unsigned op) { return op == SD || op == STORE_B || op == STORE_H || op == STORE_W; }
inline bool is_shift_data(unsigned op) { return op == SHDATA_B || op == SHDATA_H || op == SHDATA_W; }
// The narrow memory rows' floor (jolt-cpp floor_offset_mask): the address bits a B/H/W access drops.
inline uint64_t floor_mask(unsigned op) {
    switch (op) {
        case LOAD_B: case STORE_B: return 7;
        case LOAD_H: case STORE_H: return 6;
        case LOAD_W: case STORE_W: return 4;
        default: return 0;
    }
}
// A memory row's address (4.19.12 mem_addr): rs1 + imm, rounded down to its doubleword for the narrow rows.
inline uint64_t mem_addr(unsigned op, uint64_t rs1, uint64_t imm) { uint64_t a = rs1 + imm; return a - (a & floor_mask(op)); }

// Execute one row. op must be one the replayer executes (FATAL otherwise, as ref throws). pc is the source
// instruction's pc, pc_inc pc_increment (0, 2 or 4), imm0pc0 the word's flag, log the row's log word (the loads,
// VirtualAdvice), so the row's shift_offset (ShiftData only).
inline void exec(unsigned op, uint64_t a, uint64_t b, uint64_t c, uint64_t pc, unsigned pc_inc, bool imm0pc0, uint64_t log,
                 uint64_t so, Out& o) {
    const int64_t sa = int64_t(a), sb = int64_t(b);
    o.new_pc = pc; o.undefined = nullptr; o.rd = 0;
    switch (op) {
        // A. integer ALU
        case NOP: case FENCE: break;
        case ADD: case ADDI: o.rd = a + b; break;
        case SUB: o.rd = a - b; break;
        case XOR: case XORI: o.rd = a ^ b; break;
        case OR: case ORI: o.rd = a | b; break;
        case AND: case ANDI: o.rd = a & b; break;
        case ANDN: o.rd = a & ~b; break;
        case SLT: case SLTI: o.rd = sa < sb ? 1 : 0; break;
        case SLTU: case SLTIU: o.rd = a < b ? 1 : 0; break;
        // B. word forms, extension and sign helpers
        case ADDW: case ADDIW: o.rd = sx32(a + b); break;
        case SUBW: o.rd = sx32(a - b); break;
        case SEXT_W: o.rd = sx32(a); break;
        case ZEXT_W: o.rd = zx32(a); break;
        case NEGATE_IF: o.rd = sa < 0 ? 0 - b : b; break;
        case MOVSIGN: o.rd = sa < 0 ? ~uint64_t(0) : 0; break;
        case REV8W: o.rd = uint64_t(__builtin_bswap32(uint32_t(a))) | uint64_t(__builtin_bswap32(uint32_t(a >> 32))) << 32; break;
        // C. multiply
        case MUL: case MULI: o.rd = a * b; break;
        case MULW: case MULIW: o.rd = sx32(a * b); break;
        case MULHU: o.rd = mulhu(a, b); break;
        // D. shifts, bitmasks and rotates
        case POW2: o.rd = uint64_t(1) << (a & 63); break;
        case POW2W: o.rd = zx32(uint64_t(1) << (a & 31)); break;
        case SRBM: o.rd = 0 - (uint64_t(1) << (a & 63)); break;
        case SRBM_W: o.rd = (uint64_t(1) << 32) - (uint64_t(1) << (a & 31)); break;
        case SRL_V: case SRLI_V: if (b == 0) o.undefined = "zero shift mask"; o.rd = a >> (tz(b) & 63); break;
        case SRA_V: case SRAI_V: if (b == 0) o.undefined = "zero shift mask"; o.rd = uint64_t(sa >> (tz(b) & 63)); break;
        case SRLW_V: case SRLIW_V: o.rd = sx32(zx32(a) >> (tz(b) & 31)); break;
        case SRAW_V: case SRAIW_V: o.rd = sx32(uint64_t(int64_t(int32_t(uint32_t(a)) >> (tz(b) & 31)))); break;
        case ROTRI: o.rd = rotr64(a, tz(b)); break;                       // by 64 (b = 0) is the identity
        case ROTRIW: { unsigned k = tz(b) < 32 ? tz(b) : 32; o.rd = zx32(k == 32 ? uint32_t(a) : rotr32(uint32_t(a), k)); break; }
        case XORROTL1: o.rd = a ^ (b << 1 | b >> 63); break;
        case XORROT63: o.rd = rotr64(a ^ b, 63); break;
        case XORROT32: o.rd = rotr64(a ^ b, 32); break;
        case XORROT24: o.rd = rotr64(a ^ b, 24); break;
        case XORROT16: o.rd = rotr64(a ^ b, 16); break;
        case XORROTW16: o.rd = rotr32(uint32_t(a ^ b), 16); break;
        case XORROTW12: o.rd = rotr32(uint32_t(a ^ b), 12); break;
        case XORROTW8: o.rd = rotr32(uint32_t(a ^ b), 8); break;
        case XORROTW7: o.rd = rotr32(uint32_t(a ^ b), 7); break;
        case XORROTW22: o.rd = rotr32(uint32_t(a ^ b), 22); break;
        case XORROTW19: o.rd = rotr32(uint32_t(a ^ b), 19); break;
        case XORROTW6: o.rd = rotr32(uint32_t(a ^ b), 6); break;
        // E. sub-word memory helpers
        case WMASK_B: o.rd = uint64_t(0xFF) << (8 * ((a + b) & 7)); break;
        case WMASK_H: o.rd = uint64_t(0xFFFF) << (8 * ((a + b) & 6)); break;
        case WMASK_W: o.rd = uint64_t(0xFFFFFFFF) << (32 * (((a + b) >> 2) & 1)); break;
        // ShiftData (821e1bd): rs2 is the base register; the lane comes from base + shift_offset
        case SHDATA_B: o.rd = (a & 0xFF) << (8 * ((b + so) & 7)); break;
        case SHDATA_H: o.rd = (a & 0xFFFF) << (8 * ((b + so) & 6)); break;
        case SHDATA_W: o.rd = (a & 0xFFFFFFFFull) << (8 * ((b + so) & 4)); break;
        case PEXT: case PEXT_S: {
            // any mask; the signed form extends from the last bit placed; mask 0 gives 0
            unsigned n = unsigned(__builtin_popcountll(b));
            uint64_t v = pext(a, b);
            if (op == PEXT_S && n > 0 && n < 64 && ((a >> (63 - __builtin_clzll(b))) & 1) != 0) v |= ~uint64_t(0) << n;
            o.rd = v;
            break;
        }
        // F. control flow and pc-relative
        case BEQ: if (a == b) o.new_pc = pc + c; break;
        case BNE: if (a != b) o.new_pc = pc + c; break;
        case BLT: if (sa < sb) o.new_pc = pc + c; break;
        case BGE: if (sa >= sb) o.new_pc = pc + c; break;
        case BLTU: if (a < b) o.new_pc = pc + c; break;
        case BGEU: if (a >= b) o.new_pc = pc + c; break;
        case JAL: o.rd = pc + pc_inc; o.new_pc = (a == 0 && imm0pc0) ? 0 : pc + a; break;
        case JALR: o.rd = pc + pc_inc; o.new_pc = (a + b) & ~uint64_t(1); break;
        case AUIPC: o.rd = pc + a; break;
        case LUI: o.rd = a; break;
        // G. memory, the log and advice
        case LD: case LOAD_B: case LOAD_H: case LOAD_W: case ADVICE: o.rd = log; break;   // narrow loads: the whole doubleword
        case SD: case STORE_B: case STORE_H: case STORE_W: break;
        // H. assertions: never evaluated (4.19.7)
        case ASSERT_EQ: case ASSERT_LTE: case ASSERT_DIV0: case ASSERT_UREM: case ASSERT_MULU: break;
        // I. miscellaneous
        case HOSTIO: o.rd = c; break;     // 821e1bd: operand-free (every slot an immediate), so nothing is written
        default: FATAL("alu::exec: opcode %u is not executed by the replayer", op);
    }
}

}  // namespace units::alu
