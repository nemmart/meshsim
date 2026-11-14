// guest.cpp: the synthetic guest, its interpreter and its checks (guest.h).
#include "guest.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../tracer_block.h"
#include "base/fatal.h"
#include "gold.h"
#include "report.h"
#include "units/file_store.h"
#include "units/mem_system.h"
#include "units/replay_tables.h"
#include "units/tracer.h"

namespace tracer_block {

namespace {

using units::tracer::FileStore;
using units::tracer::MemSystem;
using units::tracer::ReplayTables;
using units::tracer::Tracer;

// ================================================================ the assembler (RV64IMAC encodings)
constexpr uint32_t enc_r(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t enc_i(int32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) {
    return (uint32_t(imm) & 0xFFF) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
constexpr uint32_t enc_s(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    return ((uint32_t(imm) >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (uint32_t(imm) & 31) << 7 | 0x23;
}
constexpr uint32_t enc_b(int32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) {
    const uint32_t m = uint32_t(imm);
    return ((m >> 12) & 1) << 31 | ((m >> 5) & 63) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | ((m >> 1) & 15) << 8 | ((m >> 11) & 1) << 7 | 0x63;
}
constexpr uint32_t enc_j(int32_t imm, uint32_t rd) {
    const uint32_t m = uint32_t(imm);
    return ((m >> 20) & 1) << 31 | ((m >> 1) & 0x3FF) << 21 | ((m >> 11) & 1) << 20 | ((m >> 12) & 0xFF) << 12 | rd << 7 | 0x6F;
}
constexpr uint32_t addi(uint32_t rd, uint32_t rs1, int32_t imm) { return enc_i(imm, rs1, 0, rd, 0x13); }
constexpr uint32_t add(uint32_t rd, uint32_t rs1, uint32_t rs2) { return enc_r(0, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t xor_(uint32_t rd, uint32_t rs1, uint32_t rs2) { return enc_r(0, rs2, rs1, 4, rd, 0x33); }
constexpr uint32_t mul(uint32_t rd, uint32_t rs1, uint32_t rs2) { return enc_r(1, rs2, rs1, 0, rd, 0x33); }
constexpr uint32_t div(uint32_t rd, uint32_t rs1, uint32_t rs2) { return enc_r(1, rs2, rs1, 4, rd, 0x33); }
constexpr uint32_t remu(uint32_t rd, uint32_t rs1, uint32_t rs2) { return enc_r(1, rs2, rs1, 7, rd, 0x33); }
constexpr uint32_t lui(uint32_t rd, uint32_t imm20) { return imm20 << 12 | rd << 7 | 0x37; }
constexpr uint32_t auipc(uint32_t rd, uint32_t imm20) { return imm20 << 12 | rd << 7 | 0x17; }
constexpr uint32_t load(uint32_t f3, uint32_t rd, uint32_t rs1, int32_t imm) { return enc_i(imm, rs1, f3, rd, 0x03); }
constexpr uint32_t store(uint32_t f3, uint32_t rs2, uint32_t rs1, int32_t imm) { return enc_s(imm, rs2, rs1, f3); }
constexpr uint32_t F_B = 0, F_H = 1, F_W = 2, F_D = 3, F_BU = 4, F_HU = 5, F_WU = 6;
constexpr uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t imm) { return enc_b(imm, rs2, rs1, 0); }
constexpr uint32_t bne(uint32_t rs1, uint32_t rs2, int32_t imm) { return enc_b(imm, rs2, rs1, 1); }
constexpr uint32_t jal(uint32_t rd, int32_t imm) { return enc_j(imm, rd); }
constexpr uint32_t jalr(uint32_t rd, uint32_t rs1, int32_t imm) { return enc_i(imm, rs1, 0, rd, 0x67); }
constexpr uint32_t csrrw(uint32_t rd, uint32_t csr, uint32_t rs1) { return csr << 20 | rs1 << 15 | 1u << 12 | rd << 7 | 0x73; }
constexpr uint32_t csrrs(uint32_t rd, uint32_t csr, uint32_t rs1) { return csr << 20 | rs1 << 15 | 2u << 12 | rd << 7 | 0x73; }
constexpr uint32_t END = 0x0000006F;   // JAL x0, 0: the program's end
// 16-bit: c.addi rd, imm; c.li rd, imm; c.mv rd, rs2; c.add rd, rs2
constexpr uint32_t c_imm(uint32_t f3, uint32_t rd, int32_t imm) {
    return f3 << 13 | ((uint32_t(imm) >> 5) & 1) << 12 | rd << 7 | (uint32_t(imm) & 31) << 2 | 1;
}
constexpr uint32_t c_addi(uint32_t rd, int32_t imm) { return c_imm(0, rd, imm); }
constexpr uint32_t c_li(uint32_t rd, int32_t imm) { return c_imm(2, rd, imm); }
constexpr uint32_t c_mv(uint32_t rd, uint32_t rs2) { return 0x8000 | rd << 7 | rs2 << 2 | 2; }
constexpr uint32_t c_add(uint32_t rd, uint32_t rs2) { return 0x9000 | rd << 7 | rs2 << 2 | 2; }

struct Asm {
    uint64_t base;
    std::vector<uint8_t> bytes;
    std::map<std::string, uint64_t> labels;   // kept from the first pass to the second
    uint64_t pc() const { return base + bytes.size(); }
    void w(uint32_t x) { for (int k = 0; k < 4; k++) bytes.push_back(uint8_t(x >> (8 * k))); }
    void c(uint32_t h) { bytes.push_back(uint8_t(h)); bytes.push_back(uint8_t(h >> 8)); }
    void label(const char* name) { labels[name] = pc(); }
    int32_t to(const char* name) const {
        const auto it = labels.find(name);
        return it == labels.end() ? 0 : int32_t(int64_t(it->second) - int64_t(pc()));
    }
    // instructions that change nothing the program reads (x9 = 0), up to the next address at `at` in its line
    void fill_to(unsigned at) {
        while ((pc() & 127) != at) {
            if (((at - unsigned(pc() & 127)) & 127) >= 4) w(addi(9, 0, 0));
            else c(c_li(9, 0));
        }
    }
};

// A small generator (splitmix64), for the seed's values.
struct Random {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

// ================================================================ the guest
constexpr uint64_t LOWEST = 0x7FF00000ull, CODE = 0x80000000ull, DATA = 0x80010000ull, DATA_PAGES = 16, MEMORY_END = DATA + 4096 * DATA_PAGES;
constexpr int LOG_K = 18;                                    // K = 2^18 cells: the first 2 MiB of the file
constexpr uint32_t MAP_K = 5, MAP_MUL = 0x9E3779B1u, MAP_STEP = 11;
constexpr uint32_t CSR_MTVEC = 0x305;

struct Params { int loop, lines, value; };

void emit(Asm& a, const Params& p) {
    a.w(auipc(10, 0x10));                                    // x10 = the data's address
    a.w(addi(5, 0, p.loop));
    a.c(c_li(6, 3));
    a.w(addi(7, 0, p.value));
    a.w(csrrw(0, CSR_MTVEC, 7));                             // core-fed
    a.w(csrrs(8, CSR_MTVEC, 0));                             // core-fed: x8 = x7
    a.w(store(F_D, 7, 10, 0)); a.w(store(F_W, 6, 10, 8)); a.w(store(F_H, 7, 10, 12)); a.w(store(F_B, 6, 10, 14));
    a.w(load(F_D, 11, 10, 0)); a.w(load(F_W, 12, 10, 8)); a.w(load(F_WU, 13, 10, 8)); a.w(load(F_H, 14, 10, 12));
    a.w(load(F_HU, 15, 10, 12)); a.w(load(F_B, 16, 10, 14)); a.w(load(F_BU, 17, 10, 14));
    a.w(load(F_D, 30, 10, 16)); a.w(load(F_B, 29, 10, 23)); a.w(load(F_W, 28, 10, 20)); a.w(load(F_H, 27, 10, 18));   // the seed's data, its signs
    a.w(mul(18, 7, 6));
    a.w(div(19, 18, 6));                                     // x19 = x7, with its advice
    a.w(remu(20, 30, 7));
    a.w(bne(19, 7, a.to("end")));                            // not taken
    a.w(beq(0, 0, 8));                                       // taken, over the next instruction
    a.w(addi(21, 0, 99));
    a.w(jal(1, a.to("function")));
    a.w(auipc(22, 0));
    a.w(jalr(1, 22, a.to("far") + 4));                       // the AUIPC-JALR pair
    a.fill_to(126); a.c(c_li(9, 1));                         // a 16-bit instruction in its line's last two bytes
    a.fill_to(126); a.w(addi(9, 9, 1));                      // a 32-bit instruction straddling two lines
    a.label("loop");
    a.w(load(F_D, 23, 10, 32)); a.w(add(23, 23, 5)); a.w(store(F_D, 23, 10, 32)); a.w(xor_(24, 24, 23)); a.w(mul(25, 23, 6));
    a.w(div(26, 25, 7)); a.c(c_addi(5, -1)); a.w(bne(5, 0, a.to("loop")));
    a.w(addi(5, 0, p.lines)); a.w(lui(31, 1)); a.c(c_add(31, 10));   // x31 = the data's second page
    a.label("lines");
    a.w(store(F_D, 5, 31, 0)); a.w(addi(31, 31, 128)); a.c(c_addi(5, -1)); a.w(bne(5, 0, a.to("lines")));
    a.c(c_mv(4, 24));
    a.label("end");
    a.w(END);
    a.label("function");
    a.w(add(3, 6, 7)); a.w(jalr(0, 1, 0));
    a.fill_to(64);
    a.label("far");
    a.c(c_mv(4, 3)); a.w(jalr(0, 1, 0));
}

// ================================================================ the interpreter
struct Step {
    uint64_t pc = 0;
    uint32_t code = 0;                 // the code value (17.9.4): a 16-bit instruction's 32-bit form, bit 0 cleared
    bool wide = false, csr = false, transfer = false, end = false;
    uint64_t new_pc = 0;
    std::vector<std::pair<uint64_t, uint64_t>> loads, stores;   // (the doubleword's address, the doubleword read)
    std::vector<uint64_t> advice;
};

struct Machine {
    uint64_t x[32] = {};
    uint64_t pc = 0;
    std::map<uint32_t, uint64_t> csr;
    std::vector<uint64_t> mem;         // the doublewords from LOWEST
    Machine() : mem(size_t((MEMORY_END - LOWEST) / 8), 0) {}
    uint64_t& cell(uint64_t a) {
        if (a < LOWEST || a >= MEMORY_END) FATAL("the synthetic guest: an access at 0x%llx, outside its memory", (unsigned long long)a);
        return mem[size_t((a - LOWEST) / 8)];
    }
    uint32_t parcel(uint64_t a) { return uint32_t(cell(a) >> (8 * (a & 7))) & 0xFFFF; }
    void set(uint32_t rd, uint64_t v) { if (rd != 0) x[rd] = v; }

    // One instruction.
    Step step() {
        Step s;
        s.pc = pc;
        uint32_t w = parcel(pc);
        s.wide = (w & 3) == 3;
        if (s.wide) w |= parcel(pc + 2) << 16;
        else {   // its 32-bit form
            const uint32_t rd = (w >> 7) & 31, rs2 = (w >> 2) & 31;
            const int32_t imm = int32_t(((w >> 2) & 31) | ((w >> 12) & 1) << 5) << 26 >> 26;
            if ((w & 0xE003) == 0x0001) w = addi(rd, rd, imm);
            else if ((w & 0xE003) == 0x4001) w = addi(rd, 0, imm);
            else if ((w & 0xF003) == 0x8002 && rs2 != 0) w = add(rd, 0, rs2);
            else if ((w & 0xF003) == 0x9002 && rs2 != 0) w = add(rd, rd, rs2);
            else FATAL("the synthetic guest: a 16-bit instruction 0x%04x its interpreter has not", w);
        }
        s.code = s.wide ? w : w & ~1u;
        const uint32_t op = w & 0x7F, rd = (w >> 7) & 31, f3 = (w >> 12) & 7, rs1 = (w >> 15) & 31, rs2 = (w >> 20) & 31, f7 = w >> 25;
        const uint64_t a = x[rs1], b = x[rs2], size = s.wide ? 4 : 2;
        const uint64_t imm_i = uint64_t(int64_t(int32_t(w) >> 20));
        uint64_t next = pc + size;
        bool jump = false;                 // a taken branch, a JAL or a JALR: its new pc is `next`
        switch (op) {
            case 0x37: set(rd, uint64_t(int64_t(int32_t(w & 0xFFFFF000u)))); break;
            case 0x17: set(rd, pc + uint64_t(int64_t(int32_t(w & 0xFFFFF000u)))); break;
            case 0x6F: {
                const uint64_t imm = uint64_t(int64_t(int32_t(((w >> 31) & 1) << 20 | ((w >> 12) & 0xFF) << 12 | ((w >> 20) & 1) << 11 | ((w >> 21) & 0x3FF) << 1) << 11 >> 11));
                set(rd, pc + size);
                if (imm == 0) { s.end = true; next = 0; }   // the program's end (17.9.4)
                else { next = pc + imm; jump = true; }
                break;
            }
            case 0x67: next = (a + imm_i) & ~uint64_t(1); jump = true; set(rd, pc + size); break;
            case 0x63: {
                const uint64_t imm = uint64_t(int64_t(int32_t(((w >> 31) & 1) << 12 | ((w >> 7) & 1) << 11 | ((w >> 25) & 63) << 5 | ((w >> 8) & 15) << 1) << 19 >> 19));
                const bool taken = f3 == 0 ? a == b : f3 == 1 ? a != b : f3 == 4 ? int64_t(a) < int64_t(b) : f3 == 5 ? int64_t(a) >= int64_t(b)
                                 : f3 == 6 ? a < b : a >= b;
                if (f3 == 2 || f3 == 3) FATAL("the synthetic guest: branch funct3 %u", f3);
                if (taken) { next = pc + imm; jump = true; }
                break;
            }
            case 0x03: {
                const uint64_t address = a + imm_i, dw = cell(address & ~uint64_t(7)), v = dw >> (8 * (address & 7));
                s.loads.push_back({address & ~uint64_t(7), dw});
                switch (f3) {
                    case F_B: set(rd, uint64_t(int64_t(int8_t(v)))); break;
                    case F_H: set(rd, uint64_t(int64_t(int16_t(v)))); break;
                    case F_W: set(rd, uint64_t(int64_t(int32_t(v)))); break;
                    case F_D: set(rd, v); break;
                    case F_BU: set(rd, v & 0xFF); break;
                    case F_HU: set(rd, v & 0xFFFF); break;
                    case F_WU: set(rd, v & 0xFFFFFFFFull); break;
                    default: FATAL("the synthetic guest: load funct3 %u", f3);
                }
                break;
            }
            case 0x23: {
                const uint64_t address = a + uint64_t(int64_t(int32_t(((w >> 25) << 5 | ((w >> 7) & 31)) << 20) >> 20));
                uint64_t& dw = cell(address & ~uint64_t(7));
                const unsigned shift = unsigned(8 * (address & 7));
                if (f3 > F_D) FATAL("the synthetic guest: store funct3 %u", f3);
                // a narrow store reads its doubleword on a load row, then writes it whole on its store row (17.9.4)
                if (f3 != F_D) s.loads.push_back({address & ~uint64_t(7), dw});
                s.stores.push_back({address & ~uint64_t(7), dw});
                const uint64_t mask = f3 == F_D ? ~uint64_t(0) : ((uint64_t(1) << (8u << f3)) - 1) << shift;
                dw = (dw & ~mask) | ((b << shift) & mask);
                break;
            }
            case 0x13:
                if (f3 == 0) set(rd, a + imm_i);
                else FATAL("the synthetic guest: op-imm funct3 %u", f3);
                break;
            case 0x33:
                if (f7 == 0 && f3 == 0) set(rd, a + b);
                else if (f7 == 0 && f3 == 4) set(rd, a ^ b);
                else if (f7 == 1 && f3 == 0) set(rd, a * b);
                else if (f7 == 1 && f3 == 4) {   // DIV: its advice the quotient (appendix G, VirtualAdviceDiv)
                    const uint64_t q = b == 0 ? ~uint64_t(0) : int64_t(a) == INT64_MIN && int64_t(b) == -1 ? a : uint64_t(int64_t(a) / int64_t(b));
                    s.advice.push_back(q);
                    set(rd, q);
                } else if (f7 == 1 && f3 == 7) { // REMU: its advice the unsigned quotient (VirtualAdviceDivU)
                    s.advice.push_back(b == 0 ? ~uint64_t(0) : a / b);
                    set(rd, b == 0 ? a : a % b);
                } else FATAL("the synthetic guest: op funct7 %u funct3 %u", f7, f3);
                break;
            case 0x73: {
                const uint64_t old = csr[w >> 20];
                s.csr = true;
                if (f3 == 1) csr[w >> 20] = a;
                else if (f3 == 2) { if (rs1 != 0) csr[w >> 20] = old | a; }
                else FATAL("the synthetic guest: system funct3 %u", f3);
                set(rd, old);
                break;
            }
            default: FATAL("the synthetic guest: opcode 0x%02x at 0x%llx", op, (unsigned long long)pc);
        }
        s.new_pc = next;
        s.transfer = jump && next != pc;   // a new pc equal to pc is sequential (17.8.7)
        pc = s.end ? 0 : jump && next == pc ? pc + size : next;
        return s;
    }
};

// ================================================================ the rows, from the tables
// A row's opcode's kind (17.8.7's classes; data/tables/opcode_table.txt).
bool op_load(unsigned op) { return op == 54 || (op >= 110 && op <= 112); }
bool op_store(unsigned op) { return op == 53 || (op >= 113 && op <= 115); }
bool op_advice(unsigned op) { return op == 126; }
bool op_m(unsigned op) { return op == 43 || op == 45 || op == 65 || op == 118 || op == 119; }
bool op_sets_pc(unsigned op) { return op >= 28 && op <= 35; }

// The row opcodes of code value w's expansion (17.8.5, 17.8.6); nop: the shared NOP row, under the rd = x0 policy.
std::vector<unsigned> expansion(const ReplayTables& tb, uint32_t w, bool& nop) {
    std::vector<unsigned> ops;
    const unsigned entry = tb.table[decode_index(w)], cls = entry & 3, start = (entry >> 2) & 511;
    const bool rd_zero = ((w >> 7) & 31) == 0;
    nop = false;
    if (cls == 0) {
        const unsigned rd0 = unsigned(tb.rom_lo[start] >> 46) & 3;
        if (rd_zero && (rd0 == 1 || rd0 == 3)) { nop = true; return {unsigned(tb.rom_lo[1] & 0xFF)}; }
        for (unsigned j = start; j < 512; j++) {
            ops.push_back(unsigned(tb.rom_lo[j] & 0xFF));
            if (((tb.rom_lo[j] >> 32) & 0xFFF) == 0) return ops;
        }
        FATAL("the synthetic guest: 0x%08x's template runs past the ROM", w);
    }
    if (cls == 3) FATAL("the synthetic guest: 0x%08x has no template", w);
    const int m = tb.lookup(w);
    if (m < 0) FATAL("the synthetic guest: no core-fed template for 0x%08x", w);
    if (rd_zero && (tb.rd0[size_t(m)] == 1 || tb.rd0[size_t(m)] == 3)) { nop = true; return {unsigned(tb.rom_lo[1] & 0xFF)}; }
    for (size_t k = 0; 2 * k < tb.words[size_t(m)].size(); k++) ops.push_back(unsigned(tb.words[size_t(m)][2 * k] & 0xFF));
    return ops;
}

// ================================================================ what a run must give
constexpr uint8_t TAG_LOAD = 0, TAG_OTHER = 1, TAG_WRITE = 2;
struct RunPlan {
    uint32_t rows = 0, start_offset = 0;
    std::vector<uint32_t> code;                              // the code stream's values
    std::vector<std::pair<uint8_t, uint64_t>> log;           // the log words in row order, each with its tag
    std::vector<std::pair<uint32_t, uint32_t>> posts;
    uint64_t end_pc = 0, regs[32] = {};
    uint32_t end_index = 0, end_offset = 0, advice_left = 0;
    uint64_t instructions = 0, padding = 0, m_rows = 0, sets_pc = 0, sets_pc_before_last = 0, loads = 0, stores = 0;
    uint64_t miss_i = 0, miss_d = 0, probes = 0, map_lines = 0;   // so far in the segment
    bool quiet = true;
};

struct Guest {
    Params params;
    std::vector<uint8_t> code;                               // the program, from CODE
    std::vector<uint64_t> data;                              // the data's first doublewords, from DATA
    uint64_t regs[32] = {};
    std::map<uint64_t, uint32_t> base;                       // each instruction's base instr_idx, by its pc
    std::vector<uint64_t> map;                               // the jump-target map's lines, 16 entries each
    std::map<uint32_t, int> probe_lines;                     // the lines a probe of each key reads
    std::vector<uint64_t> advice;
    std::vector<RunPlan> runs;
    Machine end;                                             // the machine at the program's end
    Lru icache{128}, dcache{128}, jump{64};
    uint64_t total_instructions = 0;

    Machine fresh() const {
        Machine m;
        for (size_t k = 0; k < code.size(); k++) m.cell(CODE + k) |= uint64_t(code[k]) << (8 * (k & 7));
        for (size_t k = 0; k < data.size(); k++) m.cell(DATA + 8 * k) = data[k];
        std::memcpy(m.x, regs, sizeof regs);
        m.pc = CODE;
        return m;
    }

    void build(uint64_t seed, const ReplayTables& tb) {
        Random rnd{seed};
        params.loop = 280 + int(rnd.next() % 16);
        params.lines = 200 + int(rnd.next() % 8);
        params.value = 100 + int(rnd.next() % 900);
        for (int r = 1; r < 32; r++) regs[r] = rnd.next();
        data.assign(8, 0);
        data[2] = rnd.next() | uint64_t(1) << 63 | 0x80008000ull;   // the narrow loads' signs set
        data[4] = rnd.next();
        // the program, in two passes: the labels' addresses, then the code
        Asm first{CODE, {}, {}};
        emit(first, params);
        Asm a{CODE, {}, first.labels};
        emit(a, params);
        code = a.bytes;
        while (code.size() % 8 != 0) code.push_back(0);
        // every instruction's base: 1 + the expansions' rows before it (the host's bytecode index)
        std::vector<std::pair<uint32_t, uint32_t>> keys;
        uint32_t index = 1;
        Machine m = fresh();
        for (uint64_t pc = CODE; pc < CODE + a.bytes.size();) {
            m.pc = pc;
            Machine probe = m;                               // its code value, by the interpreter's expansion of it
            const Step s = static_step(probe);
            base[pc] = index;
            keys.push_back({uint32_t(pc - LOWEST), index});
            bool nop;
            index += uint32_t(expansion(tb, s.code, nop).size());
            pc += s.wide ? 4 : 2;
        }
        // the map, as the host builds it (17.9.4): keys ascending, each in the first of its 3 lines with a free entry
        map.assign(size_t(16) << MAP_K, ~uint64_t(0));
        std::vector<int> fill(size_t(1) << MAP_K, 0);
        for (const auto& e : keys) {
            uint32_t line = uint32_t(e.first * MAP_MUL) >> (32 - MAP_K);
            int n = 1;
            for (; n <= 3 && fill[line] == 16; n++) line = (line + MAP_STEP) & ((1u << MAP_K) - 1);
            if (n > 3) FATAL("the synthetic guest: the map is too full");
            map[size_t(line) * 16 + size_t(fill[line]++)] = uint64_t(e.first) | uint64_t(e.second) << 32;
            probe_lines[e.first] = n;
        }
        // the advice, the host's: the interpreter's, to the program's end
        end = fresh();
        for (;;) {
            const Step s = end.step();
            total_instructions++;
            advice.insert(advice.end(), s.advice.begin(), s.advice.end());
            if (s.end) break;
            if (total_instructions > 1000000) FATAL("the synthetic guest does not end");
        }
        plan(tb);
    }

    // An instruction's code value and size without running it: the interpreter's decode on a copy, its memory
    // accesses made harmless (x0-relative addresses are outside the memory, so the registers point into the data).
    static Step static_step(Machine& m) {
        for (int r = 1; r < 32; r++) m.x[r] = DATA + 64;
        m.x[7] = 1;   // a divisor
        return m.step();
    }

    // The runs: their sizes, and what each must give.
    void plan(const ReplayTables& tb) {
        static const uint32_t kSizes[] = {48, 8, 512, 3, 512, 500};
        Machine m = fresh();
        Step cur;
        struct Row { unsigned op; bool last; };
        std::vector<Row> rows;
        size_t offset = 0, next_load = 0, next_store = 0, next_advice = 0;
        uint32_t advice_taken = 0;
        bool ended = false, cur_nop = false;
        for (size_t k = 0;; k++) {
            const bool only_padding = ended;
            RunPlan r;
            r.rows = only_padding ? 64 : k < sizeof kSizes / sizeof kSizes[0] ? kSizes[k] : 512;
            r.start_offset = uint32_t(offset < rows.size() ? offset : 0);
            if (r.start_offset != 0) r.code.push_back(cur.code);   // inside an expansion: its code value again
            bool last_sets_pc = false;
            for (uint32_t n = 0; n < r.rows; n++) {
                last_sets_pc = false;
                if (ended) { r.code.push_back(0); r.padding++; continue; }
                if (offset == rows.size()) {   // the next instruction
                    cur = m.step();
                    rows.clear();
                    for (unsigned op : expansion(tb, cur.code, cur_nop)) rows.push_back({op, false});
                    rows.back().last = true;
                    offset = next_load = next_store = next_advice = 0;
                    r.code.push_back(cur.code);
                    r.instructions++;
                    const uint64_t o = cur.pc - LOWEST;
                    const bool straddles = cur.wide && o % 128 == 126;
                    if (!icache.touch(o >> 7)) r.quiet = false;
                    if (straddles) { icache.touch((o >> 7) + 1); r.quiet = false; }
                    if (cur.csr) { r.posts.push_back({cur.code, 0}); r.quiet = false; }
                    if (cur_nop) r.quiet = false;
                }
                const Row row = rows[offset++];
                r.m_rows += op_m(row.op) ? 1 : 0;
                if (!cur_nop && op_load(row.op)) {
                    if (next_load == cur.loads.size()) FATAL("the synthetic guest: 0x%08x's rows load more than the interpreter", cur.code);
                    const auto& l = cur.loads[next_load++];
                    r.log.push_back({TAG_LOAD, l.second});
                    r.loads++;
                    if (!dcache.touch((l.first - LOWEST) >> 7)) r.quiet = false;
                } else if (!cur_nop && op_store(row.op)) {
                    if (next_store == cur.stores.size()) FATAL("the synthetic guest: 0x%08x's rows store more than the interpreter", cur.code);
                    const auto& s = cur.stores[next_store++];
                    r.log.push_back({TAG_WRITE, s.second});
                    r.stores++;
                    if (!dcache.touch((s.first - LOWEST) >> 7)) r.quiet = false;
                } else if (!cur_nop && op_advice(row.op)) {
                    if (next_advice == cur.advice.size()) FATAL("the synthetic guest: 0x%08x's rows take more advice than the interpreter", cur.code);
                    r.log.push_back({TAG_OTHER, cur.advice[next_advice++]});
                    // the read queue's credit returns once 128 entries, 256 values, are popped: the next value waits
                    // for the core
                    if (advice_taken != 0 && advice_taken % 256 == 0) r.quiet = false;
                    advice_taken++;
                }
                if (row.last) {
                    if (!cur_nop && (next_load != cur.loads.size() || next_store != cur.stores.size() || next_advice != cur.advice.size()))
                        FATAL("the synthetic guest: 0x%08x's rows take fewer words than the interpreter", cur.code);
                    if (op_sets_pc(row.op)) { r.sets_pc++; last_sets_pc = true; }
                    if (cur.end) {
                        r.log.push_back({TAG_OTHER, 0});
                        ended = true;
                    } else if (cur.transfer) {
                        const uint32_t key = uint32_t(cur.new_pc - LOWEST);
                        if (!base.count(cur.new_pc)) FATAL("the synthetic guest: a transfer to 0x%llx, no instruction", (unsigned long long)cur.new_pc);
                        r.log.push_back({TAG_OTHER, base[cur.new_pc]});
                        if (!jump.touch(key)) { r.map_lines += uint64_t(probe_lines[key]); r.quiet = false; }
                    }
                }
            }
            r.sets_pc_before_last = r.sets_pc - (last_sets_pc ? 1 : 0);
            if (ended) { r.end_pc = 0; r.end_index = 0; r.end_offset = 0; }
            else if (offset == rows.size()) { r.end_pc = m.pc; r.end_index = base[m.pc]; r.end_offset = 0; }
            else { r.end_pc = cur.pc; r.end_index = base[cur.pc] + uint32_t(offset); r.end_offset = uint32_t(offset); }
            std::memcpy(r.regs, m.x, sizeof r.regs);
            r.advice_left = uint32_t(advice.size()) - advice_taken;
            r.miss_i = icache.misses; r.miss_d = dcache.misses; r.probes = jump.misses;
            r.map_lines += runs.empty() ? 0 : runs.back().map_lines;
            runs.push_back(r);
            if (only_padding) break;
        }
        if (std::memcmp(m.x, end.x, sizeof m.x) != 0 || m.mem != end.mem) FATAL("the synthetic guest: the interpreter's two passes differ");
    }
};

// ================================================================ the streams, decoded (17.8.12)
struct BitReader {
    const std::vector<uint64_t>& e;
    uint64_t position = 0;
    bool over = false;
    uint64_t take(unsigned n) {
        uint64_t v = 0;
        for (unsigned k = 0; k < n; k++, position++) {
            if (position / 64 >= e.size()) { over = true; continue; }
            v |= ((e[size_t(position / 64)] >> (position % 64)) & 1) << k;
        }
        return v;
    }
};
// A stream's values: raw, or LRU-coded against E[0]-E[15]; the write log's E[15] the last load, by the run's log
// words in row order (`log`), whose loads it follows.
std::vector<uint64_t> decode(const std::vector<uint64_t>& entries, StreamSize size, int stream,
                             const std::vector<std::pair<uint8_t, uint64_t>>& log, bool& bad) {
    std::vector<uint64_t> values;
    BitReader in{entries};
    if (!size.compressed) {
        for (uint32_t k = 0; k < size.values; k++) values.push_back(in.take(stream == 0 ? 32 : 64));
        bad = in.over;
        return values;
    }
    uint64_t E[16] = {};
    const int lru = stream == 2 ? 15 : 16;
    size_t at = 0;                                           // the write log: the next log word of the run
    for (uint32_t k = 0; k < size.values; k++) {
        if (stream == 2)
            for (; at < log.size() && log[at].first != TAG_WRITE; at++)
                if (log[at].first == TAG_LOAD) E[15] = log[at].second;
        at++;
        uint64_t v;
        if (in.take(1) == 1) {
            const unsigned i = unsigned(in.take(4));
            v = E[i];
            if (int(i) < lru) { for (unsigned j = i; j > 0; j--) E[j] = E[j - 1]; E[0] = v; }
        } else {
            v = in.take(in.take(1) == 1 ? 64 : 32);
            for (int j = lru - 1; j > 0; j--) E[j] = E[j - 1];
            E[0] = v;
        }
        values.push_back(v);
    }
    bad = in.over;
    return values;
}

uint64_t stored_cell(const FileStore& fs, FileStore::File f, uint64_t cell) {
    FileStore::Walk w;
    fs.walk(f.root, f.depth, int64_t(cell / 16), w);
    if (w.data_line < 0) return 0;
    uint64_t line[16];
    fs.read_line(w.data_line, line);
    return line[cell % 16];
}

// ================================================================ one setting
bool run_setting(const Guest& g, const ReplayTables& tb, const std::string& fill, uint32_t compression, bool quiet) {
    const auto began = std::chrono::steady_clock::now();
    // ---- the files: the guest's memory (the code's pages another's, the data's its own) and the map
    FileStore store;
    const Machine start = g.fresh();
    const FileStore::File image = store.create(2);
    std::vector<int64_t> owned;
    for (uint64_t page = CODE; page < CODE + g.code.size(); page += 4096) store.put_page(image, int64_t((page - LOWEST) / 4096), &start.mem[size_t((page - LOWEST) / 8)]);
    for (uint64_t page = DATA; page < MEMORY_END; page += 4096) {
        store.put_page(image, int64_t((page - LOWEST) / 4096), &start.mem[size_t((page - LOWEST) / 8)]);
        owned.push_back(int64_t((page - LOWEST) / 4096));
    }
    store.set_ownership(image, owned);
    const FileStore::File map = store.create(FileStore::depth_for(int64_t(128) << MAP_K));
    std::vector<uint64_t> page(512, 0);
    std::memcpy(page.data(), g.map.data(), 8 * g.map.size());
    store.put_page(map, 0, page.data());

    MemSystem ms(&store);
    ms.map_root_for_stats = map.root;
    Tracer tr(66);
    tr.mem = &ms;
    tr.cold_mem_on_state_write = true;
    if (fill != "model") tr.fill_latency[0] = tr.fill_latency[1] = std::atoi(fill.c_str());

    // ---- the record (17.9.2)
    uint64_t rec[2 * Tracer::REC_WORDS] = {};
    rec[0] = CODE;
    rec[1] = g.base.at(CODE);
    rec[3] = uint64_t(g.advice.size()) << 38;
    for (int r = 1; r < 32; r++) rec[4 + r] = g.regs[r];
    uint64_t files[2];
    Tracer::files_word(image.root, image.depth, map.root, map.depth, LOG_K, int64_t(LOWEST), files);
    rec[2 * Tracer::REC_FILES] = files[0]; rec[2 * Tracer::REC_FILES + 1] = files[1];

    Bench bench(tr, tb);
    bench.setup(MAP_MUL, MAP_STEP, MAP_K);
    bench.segment(rec, g.advice, g.runs[0].rows | compression << 26);

    Differences d;
    Figures fig;
    uint32_t last[Tracer::STAT_N] = {};
    int64_t last_waits = 0, quiet_runs = 0;
    size_t log_at = 0;
    for (size_t k = 0; k < g.runs.size(); k++) {
        const RunPlan& p = g.runs[k];
        char context[64];
        std::snprintf(context, sizeof context, "run %zu", k);
        if (k != 0) bench.store(Bench::B_TWO, Tracer::W_RUN_CONFIG - 32, p.rows | compression << 26);
        RunOut o;
        bench.run(o);
        if (!o.done) {
            std::printf("FAIL %s: not done: ERRORS 0x%x after %u rows (cycle %lld)\n", context, o.errors, o.rows_run, (long long)bench.cycle);
            for (int64_t e = 0; e < tr.n_errors_logged && e < Tracer::FACTS; e++)
                std::printf("  %s at %lld (0x%llx, 0x%llx)\n", Tracer::error_name(tr.error_log[e].code), (long long)tr.error_log[e].t,
                            (unsigned long long)tr.error_log[e].a, (unsigned long long)tr.error_log[e].b);
            return false;
        }
        // ---- the words
        uint32_t state_length = 0;
        tr.word(bench.cycle, Tracer::W_STATE_LENGTH, state_length);
        if (state_length != uint32_t(Tracer::REC_WORDS)) d.add("%s: STATE_LENGTH %u", context, state_length);
        if (o.status != 0) d.add("%s: STATUS 0x%x after done", context, o.status);
        if (o.errors != 0) d.add("%s: ERRORS 0x%x after done", context, o.errors);
        if ((o.events_peek & (0xFu << 5)) != 0) d.add("%s: a live event after done: EVENTS_PEEK 0x%x", context, o.events_peek);
        if (o.rows_run != p.rows) d.add("%s: ROWS_RUN %u of %u", context, o.rows_run, p.rows);
        // ---- output queue 0
        const std::vector<uint64_t>& q0 = o.queue[0];
        if (q0.size() != 2 * size_t(Tracer::REC_WORDS)) { d.add("%s: output queue 0's packets: %zu", context, q0.size() / 2); continue; }
        if (q0[0] != p.end_pc || q0[1] != (uint64_t(p.end_index) | uint64_t(p.end_offset) << 32))
            d.add("%s: the state word: pc 0x%llx instr_idx %u row offset %u, the interpreter's 0x%llx %u %u", context, (unsigned long long)q0[0],
                  uint32_t(q0[1]), uint32_t(q0[1] >> 32), (unsigned long long)p.end_pc, p.end_index, p.end_offset);
        if (uint32_t(q0[3] >> 38) != p.advice_left) d.add("%s: the advice field %u, the interpreter's %u", context, uint32_t(q0[3] >> 38), p.advice_left);
        if (p.end_offset == 0)
            for (int r = 0; r < 32; r++)
                if (q0[size_t(4 + r)] != p.regs[r])
                    d.add("%s: x%d is 0x%llx, the interpreter's 0x%llx", context, r, (unsigned long long)q0[size_t(4 + r)], (unsigned long long)p.regs[r]);
        if (q0[2 * 66] != files[0] || q0[2 * 66 + 1] != files[1]) d.add("%s: record word 66 is not as written", context);
        if (q0[2 * 67] != 0 || q0[2 * 67 + 1] != 0) d.add("%s: the template bitmap is not 0", context);
        // ---- output queues 1-3: the stream word's sizes, the entries, the values
        uint64_t expected_values[3] = {p.code.size(), 0, 0};
        for (const auto& l : p.log) expected_values[l.first == TAG_WRITE ? 2 : 1]++;
        static const char* const kName[3] = {"code", "the read log", "the write log"};
        for (int s = 0; s < 3; s++) {
            const StreamSize size = stream_size(q0[2], q0[3], s);
            const uint32_t raw_words = uint32_t((expected_values[s] + (s == 0 ? 3 : 1)) / (s == 0 ? 4 : 2));
            if (size.values != expected_values[s]) d.add("%s: %s's values %u, the interpreter's %llu", context, kName[s], size.values, (unsigned long long)expected_values[s]);
            if (o.lengths[s] != size.words || o.queue[s + 1].size() != 2 * size_t(size.words))
                d.add("%s: %s's words %u, its length word %u, its entries %zu", context, kName[s], size.words, o.lengths[s], o.queue[s + 1].size() / 2);
            if (size.compressed ? ((compression >> s) & 1) == 0 || size.words >= raw_words : size.words != raw_words)
                d.add("%s: %s is %s in %u words, raw %u, tracer_compression %u", context, kName[s], size.compressed ? "LRU-coded" : "raw", size.words,
                      raw_words, compression);
            bool bad = false;
            const std::vector<uint64_t> values = decode(o.queue[s + 1], size, s, p.log, bad);
            if (bad) d.add("%s: %s's values run past its entries", context, kName[s]);
            size_t at = 0;
            for (size_t v = 0; v < values.size() && v < expected_values[s]; v++) {
                uint64_t want;
                if (s == 0) want = p.code[v];
                else {
                    while (at < p.log.size() && (p.log[at].first == TAG_WRITE) != (s == 2)) at++;
                    want = p.log[at++].second;
                }
                if (values[v] != want) {
                    d.add("%s: %s's value %zu is 0x%llx, the interpreter's 0x%llx", context, kName[s], v, (unsigned long long)values[v], (unsigned long long)want);
                    break;
                }
            }
        }
        log_at += p.log.size();
        // ---- the posts
        if (o.post_opcode.size() != p.posts.size()) d.add("%s: %zu posts, the interpreter's %zu", context, o.post_opcode.size(), p.posts.size());
        for (size_t n = 0; n < o.post_opcode.size() && n < p.posts.size(); n++)
            if (o.post_opcode[n] != p.posts[n].first || o.post_offset[n] != p.posts[n].second)
                d.add("%s: post %zu: 0x%08x at row offset %u, the interpreter's 0x%08x at %u", context, n, o.post_opcode[n], o.post_offset[n],
                      p.posts[n].first, p.posts[n].second);
        // ---- the exact counters and the fill port's reads
        if (o.stat[Tracer::ST_MISS_I] != p.miss_i) d.add("%s: STAT_MISS_I %u, the model's %llu", context, o.stat[Tracer::ST_MISS_I], (unsigned long long)p.miss_i);
        if (o.stat[Tracer::ST_MISS_D] != p.miss_d) d.add("%s: STAT_MISS_D %u, the model's %llu", context, o.stat[Tracer::ST_MISS_D], (unsigned long long)p.miss_d);
        if (o.stat[Tracer::ST_PROBES] != p.probes) d.add("%s: STAT_PROBES %u, the model's %llu", context, o.stat[Tracer::ST_PROBES], (unsigned long long)p.probes);
        const uint64_t reads = p.map_lines + p.miss_i + p.miss_d - uint64_t(tr.take_backs);
        if (uint64_t(ms.reads) != reads) d.add("%s: the fill port's reads %lld, the models' %llu", context, (long long)ms.reads, (unsigned long long)reads);
        // ---- the cycles add up (17.9.8), and a quiet run's closed form
        const int64_t schedule = 4 * int64_t(p.rows) + 2 * int64_t(p.m_rows);
        const int64_t fetch = int64_t(o.stat[1] - last[1]), data = int64_t(o.stat[2] - last[2]), mapc = int64_t(o.stat[3] - last[3]);
        const int64_t waits_now = tr.stalls[Tracer::S_ADVICE] + tr.stalls[Tracer::S_EXP], waits = waits_now - last_waits;
        last_waits = waits_now;
        if (o.rows_cycles != schedule + fetch + data + mapc + waits + 3)
            d.add("%s: the cycles do not add up: %lld from START to the rows' end, the schedules %lld, the stall counters %lld %lld %lld, the waits on the "
                  "core %lld", context, (long long)o.rows_cycles, (long long)schedule, (long long)fetch, (long long)data, (long long)mapc, (long long)waits);
        const int64_t first = p.start_offset != 0 ? 3 : p.padding == p.rows ? 5 : 6;
        const int64_t closed = schedule + 5 * int64_t(p.sets_pc_before_last) + first + 3;
        if (p.quiet) {
            quiet_runs++;
            if (o.rows_cycles != closed || fetch != 5 * int64_t(p.sets_pc_before_last) + first || data != 0 || mapc != 0 || waits != 0)
                d.add("%s, a quiet run: %lld cycles from START to the rows' end, by hand %lld (4 x %u rows + 2 x %llu M rows + 5 x %llu rows that set "
                      "their successor's pc + %lld to the first instruction + 3); STAT_STALL_FETCH +%lld, STAT_STALL_DATA +%lld, STAT_STALL_MAP +%lld, "
                      "waits on the core %lld", context, (long long)o.rows_cycles, (long long)closed, p.rows, (unsigned long long)p.m_rows,
                      (unsigned long long)p.sets_pc_before_last, (long long)first, (long long)fetch, (long long)data, (long long)mapc, (long long)waits);
        }
        if (!quiet) {
            std::printf("run %2zu: %3u rows, %3llu instructions, %2llu padding rows, %2llu M rows, %2llu rows that set their successor's pc", k, p.rows,
                        (unsigned long long)p.instructions, (unsigned long long)p.padding, (unsigned long long)p.m_rows, (unsigned long long)p.sets_pc);
            if (p.start_offset != 0) std::printf(", from row offset %u of an expansion", p.start_offset);
            if (p.end_offset != 0) std::printf(", to row offset %u of one", p.end_offset);
            std::printf(": %lld cycles to the rows' end = schedules %lld + fetch %lld + data %lld + map %lld + the core %lld + 3", (long long)o.rows_cycles,
                        (long long)schedule, (long long)fetch, (long long)data, (long long)mapc, (long long)waits);
            if (p.quiet) std::printf(" (quiet: by hand %lld)", (long long)closed);
            std::printf("; drain %lld; misses i %u d %u, probes %u; posts %zu; code %u %s, read log %u %s, write log %u %s\n", (long long)o.drain_cycles,
                        o.stat[5] - last[5], o.stat[6] - last[6], o.stat[7] - last[7], o.post_opcode.size(),
                        o.lengths[0], stream_size(q0[2], q0[3], 0).compressed ? "coded" : "raw", o.lengths[1],
                        stream_size(q0[2], q0[3], 1).compressed ? "coded" : "raw", o.lengths[2], stream_size(q0[2], q0[3], 2).compressed ? "coded" : "raw");
        }
        for (int n = 0; n < Tracer::STAT_N; n++) last[n] = o.stat[n];
        fig.windows++; fig.rows += p.rows; fig.padding_rows += p.padding; fig.instructions += p.instructions; fig.rows_m += p.m_rows;
        fig.loads += p.loads; fig.stores += p.stores; fig.posts += p.posts.size(); fig.sets_pc += p.sets_pc;
        fig.rows_cycles += uint64_t(o.rows_cycles); fig.drain_cycles += uint64_t(o.drain_cycles); fig.waits_on_core += uint64_t(waits);
        for (int q = 0; q < 4; q++) fig.packets += o.queue[q].size() / 2;
        if (p.padding == p.rows) { fig.padding_window_rows += p.rows; fig.padding_window_cycles += uint64_t(o.rows_cycles); }
    }
    // ---- the end: the write-back buffer's line and the data cache's dirty lines written, the guest's memory against
    // the interpreter's
    for (int k = 0; k < 16; k++) bench.idle_cycle();
    if (tr.errors(bench.cycle) != 0) d.add("ERRORS 0x%x after the last write-back", unsigned(tr.errors(bench.cycle)));
    const uint64_t write_backs = uint64_t(ms.writes);
    tr.write_back_dirty_untimed();
    int64_t cells = 0;
    for (uint64_t a = CODE; a < MEMORY_END; a += 8) {
        if (a >= CODE + g.code.size() && a < DATA) continue;
        const uint64_t got = stored_cell(store, image, (a - LOWEST) / 8), want = g.end.mem[size_t((a - LOWEST) / 8)];
        cells++;
        if (got != want) d.add("the guest's memory at 0x%llx is 0x%llx, the interpreter's 0x%llx", (unsigned long long)a, (unsigned long long)got, (unsigned long long)want);
    }
    for (int n = 0; n < Tracer::STAT_N; n++) fig.counters[n] = last[n];
    fig.fetch_lookups = g.icache.lookups; fig.data_lookups = g.dcache.lookups; fig.jump_lookups = g.jump.lookups;
    fig.map_lines = g.runs.back().map_lines;
    fig.write_backs = write_backs; fig.take_backs = uint64_t(tr.take_backs);
    fig.model_cycles = bench.cycle;
    fig.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    report("the synthetic guest", compression, fill, fig);
    std::printf("against the interpreter: %zu runs, %lld of them quiet with their cycles by hand; %lld log words; %lld cells of memory at the end: "
                "%lld differences\n", g.runs.size(), (long long)quiet_runs, (long long)log_at, (long long)cells, (long long)d.count);
    std::printf("%s the synthetic guest, compression %u, fill %s\n", d.count == 0 ? "PASS" : "FAIL", compression, fill.c_str());
    return d.count == 0;
}

}  // namespace

int synthetic(uint64_t seed, bool quiet) {
    const ReplayTables tb = ReplayTables::load(ReplayTables::default_dir());
    Guest g;
    g.build(seed, tb);
    uint64_t rows = 0;
    for (const RunPlan& r : g.runs) rows += r.rows;
    std::printf("the synthetic guest, seed %llu: %zu bytes of code, %zu instructions in the image, %llu run; its loop %d times, %d lines stored; "
                "%zu advice values; %zu runs, %llu rows\n", (unsigned long long)seed, g.code.size(), g.base.size(),
                (unsigned long long)g.total_instructions, g.params.loop, g.params.lines, g.advice.size(), g.runs.size(), (unsigned long long)rows);
    bool ok = run_setting(g, tb, "1", 0, quiet);
    ok = run_setting(g, tb, "1", 7, quiet) && ok;
    ok = run_setting(g, tb, "model", 0, quiet) && ok;
    return ok ? 0 : 1;
}

}  // namespace tracer_block
