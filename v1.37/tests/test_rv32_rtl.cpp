// test_rv32_rtl.cpp: the core (src/units/rv32.h: the RTL's `rv32_core`, cycle for cycle) against its contract, with no
// framework.
//
// Group A, the core on a small bus (rv32.h's header, "A bus"): every expectation is taken from the core specification's
// §1 (spec/core.md, Snarky One's rtl/blocks/core/spec.md; "spec" below, quoted by the name of its row or section), and
// where that is silent from the RTL's source (rtl/blocks/core/src, cited by file and line). Never from the model's own
// output.
// Group B, the core on the tile's environment (rv32.h's header, "The tile": bus_kind, bus_load, bus_store, meip, nmi,
// core_status), with region 0xFF decoded by src/units/register_bus.h: machine-spec 5 (the map) and 6.4 ("6.4" below),
// and the register bus's timing (Snarky One's rtl/blocks/register_bus/src/register_bus.veryl, its header's "Timing":
// "register_bus" below). tests/test_core_tile.cpp checks the same join on a scripted environment, where the test says
// what each address is; here the map is the simulator's decode, and the words are the specification's.
//
// Cycles are rv32.h's: step(env, 1) is the first cycle after reset. What the cycle numbers rest on:
//   (R0) With memory that answers the next cycle the first fetch is raised in cycle 1 and answered in 2, and the first
//        instruction is in decode in 3, in execute in 4 and in writeback, where it retires, in 5 (spec, Pipeline: "Four
//        logical stages: fetch, decode, execute, writeback; single issue, in-order retirement"). Instruction k
//        (0-based) of straight-line code that nothing stalls retires in 5 + k.
//   (R1) A redirect in cycle J puts the target's first instruction in writeback in J + 4 (spec, Control transfers: "the
//        next instruction retires ... 4 cycles after a taken branch or any other JALR, and 4 cycles after a JAL or
//        ready return enters execute"); J is "the cycle in which the redirecting instruction leaves writeback (retiring,
//        or trapping with an exception), the cycle in which an interrupt is taken" (spec, Memory and reset interface
//        details).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core_programs.h"
#include "harness.h"
#include "rv32_asm.h"
#include "units/register_bus.h"
#include "units/rv32.h"

using namespace units;

#define CHECK_STR(a, b)                                                                                     \
    do {                                                                                                    \
        const std::string check_a_ = (a), check_b_ = (b);                                                   \
        if (check_a_ != check_b_) {                                                                         \
            ::test::fail(__FILE__, __LINE__, "CHECK_STR(%s):\n    got  '%s'\n    want '%s'", #a, check_a_.c_str(), \
                         check_b_.c_str());                                                                 \
            return;                                                                                         \
        }                                                                                                   \
    } while (0)

namespace {

using namespace rv32_asm;

// ---- a few more instructions (rv32_asm.h has the formats)
constexpr uint32_t MRET = 0x30200073;
constexpr uint32_t FENCE = 0x0FF0000F;                       // fence iorw, iorw
constexpr uint32_t FENCE_I = 0x0000100F;                     // fence.i
uint32_t BEQ(uint32_t rs1, uint32_t rs2, int32_t off) { return B(off, rs2, rs1, 0); }
uint32_t BNE(uint32_t rs1, uint32_t rs2, int32_t off) { return B(off, rs2, rs1, 1); }
uint32_t JAL(uint32_t rd, int32_t off) { return J(off, rd); }
uint32_t JALR(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 0, rd, 0x67); }
uint32_t MULx(uint32_t f3, uint32_t rd, uint32_t rs1, uint32_t rs2) { return R(1, rs2, rs1, f3, rd); }   // mul, mulh, mulhsu, mulhu
uint32_t CSRRS(uint32_t rd, uint32_t csr, uint32_t rs1) { return CSR(2, csr, rs1, rd); }
uint32_t CSRR(uint32_t rd, uint32_t csr) { return CSR(2, csr, 0, rd); }      // csrr rd, csr: reads, never writes
uint32_t CSRW(uint32_t csr, uint32_t rs1) { return CSR(1, csr, rs1, 0); }    // csrw csr, rs1
// 16-bit instructions, as the GNU assembler encodes them.
constexpr uint16_t C_LI_X5_3 = 0x428D;                       // c.li x5, 3
constexpr uint16_t C_ADDI_X5_1 = 0x0285;                     // c.addi x5, 1
constexpr uint16_t C_MV_X6_X5 = 0x8316;                      // c.mv x6, x5
constexpr uint16_t C_ADD_X6_X5 = 0x9316;                     // c.add x6, x5
constexpr uint16_t C_EBREAK = 0x9002;                        // c.ebreak
constexpr uint16_t C_LWSP_X0 = 0x407E;                       // .2byte 0x407e: c.lwsp with rd = x0, a reserved encoding

// ---- the trace: the cycle an instruction retires in, and the traps
uint64_t retired_in(const TraceLog& log, uint32_t pc, unsigned nth = 0) {   // 0: it did not retire
    for (uint32_t i = 0; i < log.n; i++)
        if (log.rec[i].kind == T_RETIRE && log.rec[i].w[0] == pc && nth-- == 0) return log.rec[i].cycle;
    return 0;
}
uint32_t retired_value(const TraceLog& log, uint32_t pc, unsigned nth = 0) {   // what it wrote to its rd
    for (uint32_t i = 0; i < log.n; i++)
        if (log.rec[i].kind == T_RETIRE && log.rec[i].w[0] == pc && nth-- == 0) return log.rec[i].w[4];
    return 0xDEADDEAD;
}
struct Trap { uint64_t cycle; uint32_t cause, epc, tval, vector; };
std::vector<Trap> traps_of(const TraceLog& log) {
    std::vector<Trap> v;
    for (uint32_t i = 0; i < log.n; i++)
        if (log.rec[i].kind == T_TRAP) v.push_back({log.rec[i].cycle, log.rec[i].w[0], log.rec[i].w[1], log.rec[i].w[2], log.rec[i].w[3]});
    return v;
}

// ---- group A's stand: the RTL's test memory map (core_programs.h: IMEM at 0, fetches only; DMEM at 0x1000_0000), every
// port ready in every cycle, IMEM and DMEM answering the next; a device that answers every access with an error two
// cycles after its request; the two interrupt inputs. It records what reaches the ports.
constexpr uint32_t IMEM = core_programs::IMEM_BASE, DMEM = core_programs::DMEM_BASE, BYTES = core_programs::BYTES;
constexpr uint32_t ERROR_DEVICE = 0x20000000;
struct Bus {
    std::vector<uint32_t> imem = std::vector<uint32_t>(BYTES / 4, 0), dmem = std::vector<uint32_t>(BYTES / 4, 0);
    uint64_t now = 0;                                        // the cycle being stepped
    bool external_interrupt = false, non_maskable = false;
    struct Access { uint64_t t; uint32_t a; bool store; };
    std::vector<Access> accesses;                            // every data access that reached a target
    std::vector<std::pair<uint64_t, uint32_t>> fetches;      // every word fetched: the cycle of its request, its address
    uint32_t bus_fetch(uint32_t a, bool& err) {
        fetches.push_back({now, a});
        err = a - IMEM >= BYTES;
        return err ? 0 : imem[(a - IMEM) >> 2];
    }
    uint32_t bus_route(uint32_t a) const { return a - DMEM < BYTES ? 1 : (a & ~3u) == ERROR_DEVICE ? 2 : 0; }
    bool bus_access(uint64_t t, uint32_t a, bool store, uint32_t mask, uint32_t wdata, uint32_t& rdata) {
        accesses.push_back({t, a, store});
        rdata = 0;
        if (a - DMEM >= BYTES) return false;
        uint32_t& w = dmem[(a - DMEM) >> 2];
        if (store) w = (w & ~mask) | (wdata & mask); else rdata = w;
        return true;
    }
    bool meip() const { return external_interrupt; }
    bool nmi() const { return non_maskable; }
};
struct A {
    Bus bus;
    TraceBuffer tb;
    Rv32 core;
    uint64_t t = 0;
    A() : tb(4096) {
        core.pc = IMEM;
        core.dmem = bus.dmem.data(); core.imem = bus.imem.data();   // (the digest's; the core reads the bus)
        core.log = &tb.log;
    }
    void put16(uint32_t at, uint16_t h) {                    // a halfword at byte address `at`
        uint32_t& w = bus.imem[at >> 2];
        const unsigned sh = (at & 2) * 8;
        w = (w & ~(0xFFFFu << sh)) | uint32_t(h) << sh;
    }
    void put32(uint32_t at, uint32_t ins) { put16(at, uint16_t(ins)); put16(at + 2, uint16_t(ins >> 16)); }
    void prog(std::initializer_list<uint32_t> ins, uint32_t at = 0) { for (uint32_t i : ins) { put32(at, i); at += 4; } }
    uint64_t step() { bus.now = ++t; return core.step(bus, t); }
    void run_to(uint64_t cycle) { while (t < cycle) step(); }   // steps through `cycle`
    uint64_t retired(uint32_t pc, unsigned nth = 0) const { return retired_in(tb.log, pc, nth); }
    std::vector<Trap> traps() const { return traps_of(tb.log); }
};

// ---- group B's stand: the tile's environment (rv32.h's header). What an access to region 0xFF is comes from
// register_bus.h's decode, by the straps; the owners are the stand's: the tile window's words and a present
// peripheral's control block and DMA block as plain words, the tables as plain words. A peripheral here is of a
// reserved type, so its blocks 2-7 are unmapped (register_bus: "Any other type, a reserved one: none, every word of
// blocks 2-7 unmapped"). A load made in cycle t returns the owner's word as the test left it before step t (the
// state at the end of t - 1); a store made in t is in the word from then on (in effect at the end of t). The stores
// that may stall are register_bus's list for the tile window: "WRITE_SEND_32, _64 and _128 ... REQUEST_SEND".
// Everything that reaches the environment is recorded with its cycle.
struct Made { uint64_t t; uint32_t a, v; bool store; };
struct Pulse { uint64_t t; uint32_t bit; };
struct Consumed { uint64_t t; uint32_t events; };
struct Env {
    bus::Straps straps;                                      // no peripheral, unless the test straps one
    uint32_t regs[WINDOW_WORDS] = {};                        // the tile window's words
    uint32_t periph[3][WINDOW_WORDS] = {};                   // peripheral n's window, at [n - 1]
    std::vector<uint32_t> tables = std::vector<uint32_t>((TABLE_END - REGS_BASE) / 4, 0);
    bool meip_level = false, nmi_level = false;
    bool ready = true;                                       // a stalling store's owner takes it in this cycle
    uint64_t now = 0;
    unsigned asked = 0;                                      // bus_kind's calls
    std::vector<Made> made;                                  // the accesses that reached an owner
    std::vector<Consumed> consumed;                          // what each EVENTS load consumed (7.2)
    std::vector<Pulse> pulses;                               // core_status's
    bool meip() const { return meip_level; }
    bool nmi() const { return nmi_level; }
    void core_status(uint32_t bit) { pulses.push_back({now, bit}); }
    BusKind bus_kind(uint32_t a, bool store, bool whole) {
        asked++;
        const bus::Decoded d = bus::decode(straps, a, store, whole);
        switch (d.target) {
        case bus::Target::TABLE: return BusKind::table;
        case bus::Target::ZERO: return BusKind::window;      // "reads 0 after the same 2 cycles"
        case bus::Target::TILE:
            return store && ((d.word >= WRITE_SEND_32 && d.word <= WRITE_SEND_128) || d.word == REQUEST_SEND)
                ? BusKind::window_stalls : BusKind::window;
        case bus::Target::PERIPH_CONTROL: return BusKind::window;
        default: return BusKind::fault;                      // a fault, and a reserved type's own words
        }
    }
    uint32_t bus_load(uint64_t t, uint32_t a) {
        const bus::Decoded d = bus::decode(straps, a, false, true);
        if (d.target == bus::Target::ZERO) return 0;         // it "reaches no owner"
        made.push_back({t, a, 0, false});
        if (d.target == bus::Target::TABLE) return tables[(a - REGS_BASE) / 4];
        if (d.target == bus::Target::PERIPH_CONTROL) return periph[d.n - 1][d.word];
        if (d.word == EVENTS_PEEK) return regs[EVENTS];      // 7.2: "the same value and consumes nothing"
        const uint32_t v = regs[d.word];
        if (d.word == EVENTS) {                              // 7.2: "consumes ... each counted bit that it returns set
            const uint32_t taken = v & regs[EVENTS_MASK] & ~uint32_t(EVENTS_LIVE);   // and that EVENTS_MASK selects"
            regs[EVENTS] &= ~taken;
            consumed.push_back({t, taken});
        }
        return v;
    }
    bool bus_store(uint64_t t, uint32_t a, uint32_t v) {
        const bus::Decoded d = bus::decode(straps, a, true, true);
        if (d.target == bus::Target::TILE && !ready &&
            ((d.word >= WRITE_SEND_32 && d.word <= WRITE_SEND_128) || d.word == REQUEST_SEND)) return false;
        made.push_back({t, a, v, true});
        if (d.target == bus::Target::TABLE) tables[(a - REGS_BASE) / 4] = v;
        else if (d.target == bus::Target::PERIPH_CONTROL) periph[d.n - 1][d.word] = v;
        else regs[d.word] = v;
        return true;
    }
};
constexpr uint32_t PMEM = IMEM_BASE;                         // IMEM (regmap.h), where group B's programs are
constexpr uint32_t ACCESS_FAULT_BIT = 7, ILLEGAL_INSTRUCTION_BIT = 8;   // ERRORS' bits (machine-spec 7.4)
struct M {
    std::vector<uint32_t> dmem, imem;
    TraceBuffer tb;
    Rv32 core;
    Env env;
    uint64_t t = 0;
    M() : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0), tb(4096) {
        core.dmem = dmem.data(); core.imem = imem.data(); core.log = &tb.log;
        core.pc = PMEM;
        core.mtvec = PMEM + 0x100;
    }
    void prog(std::initializer_list<uint32_t> ins, uint32_t at = 0) { for (uint32_t i : ins) imem[at++] = i; }   // at: a word
    uint64_t step() { env.now = ++t; return core.step(env, t); }
    void run_to(uint64_t cycle) { while (t < cycle) step(); }
    uint64_t retired(uint32_t pc, unsigned nth = 0) const { return retired_in(tb.log, pc, nth); }
    std::vector<Trap> traps() const { return traps_of(tb.log); }
    std::vector<std::string> lines() const { return format_body(tb.log); }
    std::string last() const { auto l = lines(); return l.empty() ? "" : l.back(); }
};

}  // namespace

// ============================================================================================ A: the core on a bus
// ------------------------------------------------------------------------------------------- 1. straight-line code
// (R0): eight ALU instructions retire in cycles 5 .. 12.
TEST(rtl_straight_line_alu_code_retires_one_a_cycle_from_cycle_5) {
    A m;
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6), ADDI(11, 0, 7), ADDI(12, 0, 8)});
    m.run_to(4);
    CHECK_EQ(m.core.instructions, 0u);
    m.run_to(12);
    for (uint32_t k = 0; k < 8; k++) {
        CHECK_EQ(m.retired(4 * k), 5u + k);
        CHECK_EQ(m.core.x[5 + k], k + 1);
    }
    CHECK_EQ(m.core.instructions, 8u);
}

// Spec, Fetch: "16/32-bit instructions, including cross-word instructions": fetch brings a word a cycle, so 16-bit
// instructions and 32-bit ones that span two words also retire one a cycle (R0).
TEST(rtl_compressed_and_cross_word_instructions_retire_one_a_cycle) {
    A m;
    m.put16(0x0, C_LI_X5_3);
    m.put32(0x2, ADDI(7, 5, 10));                            // spans words 0 and 4
    m.put16(0x6, C_ADDI_X5_1);
    m.put16(0x8, C_MV_X6_X5);
    m.put16(0xA, C_ADD_X6_X5);
    m.put32(0xC, ADDI(8, 6, 1));
    m.run_to(10);
    const uint32_t pcs[] = {0x0, 0x2, 0x6, 0x8, 0xA, 0xC};
    for (uint32_t k = 0; k < 6; k++) CHECK_EQ(m.retired(pcs[k]), 5u + k);
    CHECK_EQ(m.core.x[5], 4u);
    CHECK_EQ(m.core.x[7], 13u);
    CHECK_EQ(m.core.x[6], 8u);
    CHECK_EQ(m.core.x[8], 9u);
}

// The ALU's results (RV32I; spec, Conditional zero: "rd = 0 if rs2 = 0, else rs1" and "rd = 0 if rs2 != 0, else rs1").
TEST(rtl_alu_results) {
    A m;
    m.prog({ADDI(1, 0, -5), I(-4, 1, 2, 2), I(-4, 1, 3, 3), I(0x0F0, 1, 4, 4), I(1, 1, 5, 5) | 0x40000000u, I(1, 1, 5, 6),
            I(3, 1, 1, 7), R(0, 1, 1, 0, 8), R(0x20, 7, 1, 0, 9), R(0, 3, 1, 2, 10), R(0, 3, 1, 3, 11), R(0x20, 3, 1, 5, 12),
            R(0, 3, 1, 5, 13), LUI(14, 0xABCDE000), U(0x1000, 15, 0x17),
            R(7, 0, 1, 5, 16), R(7, 3, 1, 5, 17), R(7, 0, 1, 7, 18), R(7, 3, 1, 7, 19)});   // czero.eqz, czero.nez
    m.run_to(5 + 18);
    CHECK_EQ(m.core.x[1], uint32_t(-5));
    CHECK_EQ(m.core.x[2], 1u);                     // slti -5 < -4
    CHECK_EQ(m.core.x[3], 1u);                     // sltiu 0xFFFFFFFB < 0xFFFFFFFC
    CHECK_EQ(m.core.x[4], uint32_t(-5) ^ 0x0F0u);
    CHECK_EQ(m.core.x[5], uint32_t(-3));           // srai -5 >> 1
    CHECK_EQ(m.core.x[6], 0x7FFFFFFDu);            // srli
    CHECK_EQ(m.core.x[7], uint32_t(-40));          // slli 3
    CHECK_EQ(m.core.x[8], uint32_t(-10));
    CHECK_EQ(m.core.x[9], uint32_t(35));           // -5 - -40
    CHECK_EQ(m.core.x[10], 1u);                    // slt -5 < 1 (x3)
    CHECK_EQ(m.core.x[11], 0u);                    // sltu 0xFFFFFFFB < 1: no
    CHECK_EQ(m.core.x[12], uint32_t(-3));          // sra -5 >> 1
    CHECK_EQ(m.core.x[13], 0x7FFFFFFDu);           // srl
    CHECK_EQ(m.core.x[14], 0xABCDE000u);
    CHECK_EQ(m.core.x[15], 0x38u + 0x1000u);       // auipc at the 15th word
    CHECK_EQ(m.core.x[16], 0u);                    // czero.eqz, rs2 = x0
    CHECK_EQ(m.core.x[17], uint32_t(-5));          // czero.eqz, rs2 = 1
    CHECK_EQ(m.core.x[18], uint32_t(-5));          // czero.nez, rs2 = x0
    CHECK_EQ(m.core.x[19], 0u);                    // czero.nez, rs2 = 1
}

// ------------------------------------------------------------------------------------------- 2. control transfers
// Spec, Control transfers: "the next instruction retires 1 cycle after a branch that is not taken".
TEST(rtl_branch_not_taken_costs_nothing) {
    A m;
    m.core.x[5] = 1;
    m.prog({BEQ(5, 0, 12), ADDI(6, 0, 1), ADDI(7, 0, 2)});
    m.run_to(7);
    CHECK_EQ(m.retired(0x0), 5u);                  // (R0)
    CHECK_EQ(m.retired(0x4), 6u);
    CHECK_EQ(m.retired(0x8), 7u);
}

// Spec, Control transfers: "4 cycles after a taken branch"; "the instruction fetched behind it is cancelled".
TEST(rtl_taken_branch_target_retires_4_cycles_after_it) {
    A m;
    m.prog({BEQ(0, 0, 12), ADDI(6, 0, 1), ADDI(7, 0, 2), ADDI(8, 0, 3), ADDI(9, 0, 4)});
    m.run_to(10);
    CHECK_EQ(m.retired(0x0), 5u);
    CHECK_EQ(m.retired(0xC), 9u);
    CHECK_EQ(m.retired(0x10), 10u);
    CHECK_EQ(m.retired(0x4), 0u);                  // behind the branch: cancelled
    CHECK_EQ(m.retired(0x8), 0u);
    CHECK_EQ(m.core.x[6], 0u);
    CHECK_EQ(m.core.x[8], 3u);
}

// Spec, Control transfers: "4 cycles after a taken branch or any other JALR" (one not through x1); JALR clears bit 0
// (spec, Traps: "JALR clears bit 0").
TEST(rtl_jalr_target_retires_4_cycles_after_it) {
    A m;
    m.core.x[5] = 0x21;
    m.prog({JALR(6, 5, 0), ADDI(7, 0, 1)});
    m.prog({ADDI(8, 0, 3)}, 0x20);
    m.run_to(9);
    CHECK_EQ(m.retired(0x0), 5u);
    CHECK_EQ(m.retired(0x20), 9u);
    CHECK_EQ(m.core.x[6], 4u);                     // the link
    CHECK_EQ(m.core.x[7], 0u);
}

// Spec, Control transfers: "4 cycles after a JAL or ready return enters execute: 3 cycles after it retires".
TEST(rtl_jal_target_retires_3_cycles_after_it) {
    A m;
    m.prog({NOP, JAL(6, 0x1C), ADDI(7, 0, 1)});
    m.prog({ADDI(8, 0, 3), ADDI(9, 0, 4)}, 0x20);
    m.run_to(10);
    CHECK_EQ(m.retired(0x4), 6u);
    CHECK_EQ(m.retired(0x20), 9u);
    CHECK_EQ(m.retired(0x24), 10u);
    CHECK_EQ(m.core.x[6], 8u);
    CHECK_EQ(m.core.x[7], 0u);
}

// Spec, Control transfers: "a JALR through x1 (a return) whose x1 is ready (committed: in its last cycle in decode, no
// instruction in execute or writeback writes x1)" is resolved as a JAL: 3 cycles after it retires. In straight-line
// code the instruction just before the return is in execute and the one before that in writeback while the return is
// in decode, so x1 written by either makes it "any other JALR" (4 cycles), and x1 written three before is committed.
TEST(rtl_return_is_a_jal_when_x1_is_ready_and_a_jalr_when_not) {
    {   // x1 from before the program: ready
        A m;
        m.core.x[1] = 0x20;
        m.prog({NOP, NOP, NOP, JALR(0, 1, 0)});
        m.prog({ADDI(8, 0, 3)}, 0x20);
        m.run_to(11);
        CHECK_EQ(m.retired(0xC), 8u);
        CHECK_EQ(m.retired(0x20), 11u);
    }
    {   // x1 written three instructions before the return: committed, ready
        A m;
        m.prog({ADDI(1, 0, 0x20), NOP, NOP, JALR(0, 1, 0)});
        m.prog({ADDI(8, 0, 3)}, 0x20);
        m.run_to(11);
        CHECK_EQ(m.retired(0xC), 8u);
        CHECK_EQ(m.retired(0x20), 11u);
    }
    {   // x1 written by the instruction just before it (in execute): any other JALR
        A m;
        m.prog({NOP, NOP, ADDI(1, 0, 0x20), JALR(0, 1, 0)});
        m.prog({ADDI(8, 0, 3)}, 0x20);
        m.run_to(12);
        CHECK_EQ(m.retired(0xC), 8u);
        CHECK_EQ(m.retired(0x20), 12u);
    }
    {   // x1 written two before it (in writeback): any other JALR
        A m;
        m.prog({NOP, ADDI(1, 0, 0x20), NOP, JALR(0, 1, 0)});
        m.prog({ADDI(8, 0, 3)}, 0x20);
        m.run_to(12);
        CHECK_EQ(m.retired(0xC), 8u);
        CHECK_EQ(m.retired(0x20), 12u);
    }
}

// Spec, Control transfers: "whatever the target's instruction and its alignment: a redirect's first fetch is at the
// target and returns every word the instruction there occupies, so none needs a second response": a 32-bit
// instruction at an odd halfword costs the same, and the code after it runs on one a cycle.
TEST(rtl_targets_at_odd_halfwords_cost_the_same) {
    {   // a taken branch
        A m;
        m.prog({BEQ(0, 0, 0x22)});
        m.put32(0x22, ADDI(8, 0, 3)); m.put32(0x26, ADDI(9, 0, 4));
        m.run_to(10);
        CHECK_EQ(m.retired(0x0), 5u);
        CHECK_EQ(m.retired(0x22), 9u);
        CHECK_EQ(m.retired(0x26), 10u);
        CHECK_EQ(m.core.x[8], 3u);
        CHECK_EQ(m.core.x[9], 4u);
    }
    {   // a JAL
        A m;
        m.prog({NOP, JAL(0, 0x22 - 4)});
        m.put32(0x22, ADDI(8, 0, 3)); m.put32(0x26, ADDI(9, 0, 4));
        m.run_to(10);
        CHECK_EQ(m.retired(0x4), 6u);
        CHECK_EQ(m.retired(0x22), 9u);
        CHECK_EQ(m.retired(0x26), 10u);
    }
    {   // a JALR
        A m;
        m.core.x[5] = 0x22;
        m.prog({JALR(0, 5, 0)});
        m.put32(0x22, ADDI(8, 0, 3)); m.put32(0x26, ADDI(9, 0, 4));
        m.run_to(10);
        CHECK_EQ(m.retired(0x0), 5u);
        CHECK_EQ(m.retired(0x22), 9u);
        CHECK_EQ(m.retired(0x26), 10u);
    }
}

// ------------------------------------------------------------------------------------------------------- 3. data
// Spec, Data: "with memory that answers the next cycle and no other stall, back-to-back loads and stores retire one
// per cycle, and an instruction that uses a load's result right after it waits one cycle". A store's data from the
// ALU instruction before it is forwarded, as any ALU result (spec, Multiply: "other instructions are not delayed").
// The requests: rv32.h's header, "raised in the cycle q its instruction leaves execute", the cycle before it retires.
TEST(rtl_dmem_loads_and_stores_retire_one_a_cycle_and_a_load_use_waits_one) {
    A m;
    m.core.x[5] = DMEM;
    m.bus.dmem[0] = 0x11; m.bus.dmem[1] = 0x22;
    m.prog({LW(6, 5, 0), LW(7, 5, 4), SW(6, 5, 8), SW(7, 5, 12),   // 0x00: retire in 5, 6, 7, 8
            LW(8, 5, 0), ADDI(9, 8, 1),                            // 0x10: 9; its use waits one: 11
            ADDI(10, 0, 7), SW(10, 5, 16), NOP});                  // 0x18: 12; the store of its result: 13; 14
    m.run_to(14);
    const uint64_t want[] = {5, 6, 7, 8, 9, 11, 12, 13, 14};
    for (uint32_t k = 0; k < 9; k++) CHECK_EQ(m.retired(4 * k), want[k]);
    CHECK_EQ(m.bus.dmem[2], 0x11u);
    CHECK_EQ(m.bus.dmem[3], 0x22u);
    CHECK_EQ(m.core.x[9], 0x12u);
    CHECK_EQ(m.bus.dmem[4], 7u);
    CHECK_EQ(m.bus.accesses.size(), 6u);
    const uint64_t at[] = {4, 5, 6, 7, 8, 12};               // each the cycle before its instruction retires
    for (uint32_t k = 0; k < 6; k++) CHECK_EQ(m.bus.accesses[k].t, at[k]);
}

// Loads extend, stores write their lanes (RV32I).
TEST(rtl_loads_and_stores_extend) {
    A m;
    m.core.x[1] = DMEM + 0x100; m.core.x[2] = 0x8081FF80;
    m.prog({SW(2, 1, 0), I(0, 1, 0, 3, 0x03), I(0, 1, 4, 4, 0x03), I(2, 1, 1, 5, 0x03), I(2, 1, 5, 6, 0x03),
            S(5, 2, 1, 0), S(6, 2, 1, 1), LW(7, 1, 4)});
    m.run_to(5 + 7);
    CHECK_EQ(m.core.x[3], 0xFFFFFF80u);            // lb
    CHECK_EQ(m.core.x[4], 0x80u);                  // lbu
    CHECK_EQ(m.core.x[5], 0xFFFF8081u);            // lh
    CHECK_EQ(m.core.x[6], 0x8081u);                // lhu
    CHECK_EQ(m.core.x[7], 0xFF808000u);            // sb 0x80 at byte 5, sh 0xFF80 at bytes 6-7
}

// --------------------------------------------------------------------------------------------------- 4. multiply
// Spec, Multiply: "Operands are not forwarded into it: a multiply whose operand the instruction in writeback is still
// producing waits one cycle, as does an instruction that uses a multiply's result right after it; other instructions
// are not delayed". The operand's producer may be an ALU instruction, whose result any other instruction would take
// forwarded: rv32_pipeline.veryl:341-342, `execute_waits_for_operand = (writeback_produces_source1 ||
// writeback_produces_source2) && (!writeback_result_is_ready || execute_is_multiply)`.
TEST(rtl_multiply_waits_for_an_operand_in_writeback_and_its_user_waits) {
    A m;
    m.prog({ADDI(5, 0, 3),                         // 0x00: 5
            MULx(0, 6, 5, 5),                      // 0x04: its operand is in writeback: waits one: 7
            ADDI(7, 6, 1),                         // 0x08: uses the product right after it: waits one: 9
            ADDI(8, 0, 5), NOP,                    // 0x0C: 10, 11
            MULx(0, 9, 8, 5),                      // 0x14: its operands are committed: 12
            NOP});                                 // 0x18: does not use the product: 13
    m.run_to(13);
    const uint64_t want[] = {5, 7, 9, 10, 11, 12, 13};
    for (uint32_t k = 0; k < 7; k++) CHECK_EQ(m.retired(4 * k), want[k]);
    CHECK_EQ(m.core.x[6], 9u);
    CHECK_EQ(m.core.x[7], 10u);
    CHECK_EQ(m.core.x[9], 15u);
}

// Spec, Multiply: "All four Zmmul operations", against 64-bit arithmetic.
TEST(rtl_multiplies_against_wide_arithmetic) {
    test::Rng rng(7);
    for (int k = 0; k < 500; k++) {
        A m;
        uint32_t a = uint32_t(rng.next()), b = uint32_t(rng.next());
        if (k < 4) { a = k & 1 ? 0x80000000u : 0xFFFFFFFFu; b = k & 2 ? 0x80000000u : 0xFFFFFFFFu; }
        m.core.x[1] = a; m.core.x[2] = b;
        m.prog({MULx(0, 3, 1, 2), MULx(1, 4, 1, 2), MULx(2, 5, 1, 2), MULx(3, 6, 1, 2)});
        m.run_to(8);                                // no multiply uses another's product: 5 .. 8
        const int64_t sa = int32_t(a), sb = int32_t(b);
        CHECK_EQ(m.core.x[3], a * b);
        CHECK_EQ(m.core.x[4], uint32_t(uint64_t(sa * sb) >> 32));
        CHECK_EQ(m.core.x[5], uint32_t(uint64_t((__int128)sa * (__int128)uint64_t(b) >> 32)));
        CHECK_EQ(m.core.x[6], uint32_t(uint64_t(a) * b >> 32));
        CHECK_EQ(m.core.instructions, 4u);
    }
}

// Spec, Excluded: "DIV/REM": an illegal instruction (Traps: mcause 2, mtval the instruction's bits).
TEST(rtl_divide_and_remainder_are_illegal) {
    for (uint32_t f3 = 4; f3 < 8; f3++) {
        A m;
        m.core.mtvec = 0x100;
        const uint32_t ins = R(1, 2, 1, f3, 3);
        m.prog({ins});
        m.run_to(5);
        CHECK_EQ(m.core.traps, 1u);
        CHECK_EQ(m.core.mcause, 2u);
        CHECK_EQ(m.core.mtval, ins);
        CHECK_EQ(m.core.mepc, 0u);
        CHECK_EQ(m.core.instructions, 0u);
    }
}

// ------------------------------------------------------------------------------------------------------- 5. CSRs
// Spec, Pipeline event priority: "The core holds the instruction after any CSR write for one cycle in execute". A CSR
// instruction that does not write (spec, CSRs: "CSRRS/CSRRC with rs1 = x0 and CSRRSI/CSRRCI with a zero immediate do
// not write") holds nothing.
TEST(rtl_instruction_after_a_csr_write_is_held_one_cycle) {
    A m;
    m.core.x[5] = 0x1234;
    m.prog({CSRW(0x340, 5), NOP,                   // 0x00: 5; held one cycle: 7
            CSRR(6, 0x340), NOP,                   // 0x08: 8; not held: 9
            CSR(6, 0x340, 0, 7), NOP,              // 0x10: csrrsi x7, mscratch, 0: 10; not held: 11
            CSR(5, 0x340, 0, 0), NOP});            // 0x18: csrrwi x0, mscratch, 0 writes: 12; held: 14
    m.run_to(14);
    const uint64_t want[] = {5, 7, 8, 9, 10, 11, 12, 14};
    for (uint32_t k = 0; k < 8; k++) CHECK_EQ(m.retired(4 * k), want[k]);
    CHECK_EQ(m.core.x[6], 0x1234u);
    CHECK_EQ(m.core.x[7], 0x1234u);
    CHECK_EQ(m.core.mscratch, 0u);
}

// Spec, the CSR table's Reset column, with `mstatus` "MPP (bits 12:11) reads `11`" and `misa` "0x4000_0104".
TEST(rtl_csr_reset_values) {
    A m;
    m.core.hart_id = 0x2A;
    const uint32_t csrs[] = {0x300, 0x301, 0x304, 0x305, 0x310, 0x320, 0x323, 0x33F, 0x340, 0x341, 0x342, 0x343, 0x344,
                             0xB03, 0xB1F, 0xB83, 0xB9F, 0xF11, 0xF12, 0xF13, 0xF14, 0xF15, 0xB80, 0xB82};
    uint32_t at = 0;
    for (uint32_t k = 0; k < 24; k++, at += 4) m.put32(at, CSRR(5 + k, csrs[k]));
    m.run_to(5 + 23);
    CHECK_EQ(m.core.traps, 0u);
    CHECK_EQ(m.core.instructions, 24u);
    for (uint32_t k = 0; k < 24; k++) {
        const uint32_t want = csrs[k] == 0x300 ? 0x1800u : csrs[k] == 0x301 ? 0x40000104u : csrs[k] == 0xF14 ? 0x2Au : 0u;
        if (m.core.x[5 + k] != want) { test::fail(__FILE__, __LINE__, "CSR 0x%x reads 0x%x after reset, the table's 0x%x", csrs[k], m.core.x[5 + k], want); return; }
    }
}

// Spec, CSRs: `mtvec`'s "reset value is `rv32_core`'s parameter" (rv32.h: reset_trap_vector, what reset() sets).
TEST(rtl_mtvec_resets_to_the_reset_trap_vector) {
    A m;
    m.core.reset_trap_vector = 0x200;
    m.core.mscratch = 5; m.core.mstatus = 0x88; m.core.mie = 0x800;
    m.core.reset(0, IMEM);
    m.prog({CSRR(5, 0x305), CSRR(6, 0x340), CSRR(7, 0x300), CSRR(8, 0x304)});
    m.run_to(8);
    CHECK_EQ(m.core.x[5], 0x200u);
    CHECK_EQ(m.core.x[6], 0u);
    CHECK_EQ(m.core.x[7], 0x1800u);
    CHECK_EQ(m.core.x[8], 0u);
}

// Spec, the CSR table's Behavior column: all ones written to each, then read.
TEST(rtl_csr_writable_bits) {
    struct Row { uint32_t csr, reads; };
    const Row rows[] = {
        {0x300, 0x1888},        // "MIE (bit 3) and MPIE (bit 7) writable. MPP (bits 12:11) reads `11`"
        {0x301, 0x40000104},    // "Writes ignored"
        {0x304, 0x800},         // "MEIE (bit 11) writable; every other bit reads zero"
        {0x305, 0xFFFFFFFC},    // "BASE (bits 31:2) writable; MODE (bits 1:0) reads `00`"
        {0x310, 0},             // "All zero"
        {0x320, 0},             // "Reads zero, ignores writes"
        {0x323, 0},             // "Read zero, writes ignored"
        {0x340, 0xFFFFFFFF},    // "Fully writable"
        {0x341, 0xFFFFFFFE},    // "Bits 31:1 writable; bit 0 reads zero"
        {0x342, 0x8000001F},    // "Interrupt (bit 31) and exception code (bits 4:0) writable"
        {0x343, 0xFFFFFFFF},    // "Fully writable"
        {0x344, 0},             // "writes ignored" (external_interrupt is low)
        {0xB03, 0},             // "Read zero, writes ignored"
        {0xB83, 0},
    };
    A m;
    m.core.x[1] = 0xFFFFFFFF;
    uint32_t at = 0;
    for (uint32_t k = 0; k < 14; k++, at += 8) { m.put32(at, CSRW(rows[k].csr, 1)); m.put32(at + 4, CSRR(5 + k, rows[k].csr)); }
    m.run_to(4 + 14 * 3);                                    // each pair: 3 cycles (the hold after the write)
    CHECK_EQ(m.core.traps, 0u);
    CHECK_EQ(m.core.instructions, 28u);
    for (uint32_t k = 0; k < 14; k++)
        if (m.core.x[5 + k] != rows[k].reads) { test::fail(__FILE__, __LINE__, "CSR 0x%x reads 0x%x after all ones, the table's 0x%x", rows[k].csr, m.core.x[5 + k], rows[k].reads); return; }
}

// Spec, CSRs: `mip` "MEIP (bit 11) shows `external_interrupt`"; `mhartid` "Read-only `hart_id` input".
TEST(rtl_mip_shows_the_external_interrupt_and_mhartid_the_hart_id) {
    A m;
    m.core.hart_id = 63;
    m.bus.external_interrupt = true;               // mstatus.MIE and mie are clear: never taken
    m.prog({CSRR(5, 0x344), CSRR(6, 0xF14)});
    m.run_to(6);
    CHECK_EQ(m.core.x[5], 0x800u);
    CHECK_EQ(m.core.x[6], 63u);
    CHECK_EQ(m.core.traps, 0u);
}

// Spec, CSRs: "Any CSR address not listed raises an illegal-instruction exception. A write to a read-only address
// (bits 11:10 = `11`) raises an illegal-instruction exception. CSRRS/CSRRC with rs1 = x0 and CSRRSI/CSRRCI with a zero
// immediate do not write, so they never fault for that reason"; "Not present (accesses are illegal): `medeleg`,
// `mideleg`, `mcounteren`, ... PMP, debug/trigger CSRs, and the user counters `cycle`, `time`, `instret`".
TEST(rtl_csr_addresses_and_read_only_writes) {
    const uint32_t illegal[] = {
        CSRR(5, 0x302), CSRR(5, 0x303), CSRR(5, 0x306), CSRR(5, 0x30A), CSRR(5, 0x3A0), CSRR(5, 0x7A0),   // not present
        CSRR(5, 0xC00), CSRR(5, 0xC01), CSRR(5, 0xC02), CSRR(5, 0xC81),          // cycle, time (no mtime), instret, timeh
        CSRR(5, 0x322), CSRR(5, 0xB01), CSRR(5, 0xB20), CSRR(5, 0xF10), CSRR(5, 0xF16),   // beside the listed ranges
        CSRW(0xF11, 1), CSRW(0xF14, 0),            // a write to a read-only address (csrrw always writes)
        CSRRS(5, 0xF14, 1),                        // csrrs with rs1 != x0 writes
        CSR(3, 0xF12, 1, 5),                       // csrrc with rs1 != x0
        CSR(5, 0xF15, 0, 5),                       // csrrwi
        CSR(6, 0xF13, 1, 5),                       // csrrsi with a nonzero immediate
    };
    for (uint32_t ins : illegal) {
        A m;
        m.core.mtvec = 0x100;
        m.prog({ins});
        m.run_to(5);
        if (m.core.traps != 1 || m.core.mcause != 2 || m.core.mtval != ins || m.core.mepc != 0) {
            test::fail(__FILE__, __LINE__, "0x%08x: %llu traps, mcause %u, mtval 0x%x: an illegal instruction is expected", ins, (unsigned long long)m.core.traps, m.core.mcause, m.core.mtval);
            return;
        }
    }
    const uint32_t legal[] = {CSRRS(5, 0xF14, 0), CSR(3, 0xF11, 0, 5), CSR(6, 0xF12, 0, 5), CSR(7, 0xF15, 0, 5)};
    for (uint32_t ins : legal) {
        A m;
        m.core.mtvec = 0x100;
        m.prog({ins});
        m.run_to(5);
        if (m.core.traps != 0 || m.core.instructions != 1) { test::fail(__FILE__, __LINE__, "0x%08x does not write and is legal", ins); return; }
    }
}

// Spec, CSRs: `minstret` "counts every retired instruction"; "A CSR write to `mcycle` or `minstret` replaces the value,
// and that instruction's own increment is dropped". A read returns the count before the reading instruction's own.
TEST(rtl_minstret_counts_retired_instructions_and_a_write_replaces_it) {
    A m;
    m.prog({NOP, NOP, CSRR(5, 0xB02),              // two retired before it: 2
            ADDI(6, 0, 100), CSRW(0xB02, 6),       // the count is 100: the write's own increment dropped
            CSRR(7, 0xB02),                        // 100
            NOP, CSRR(8, 0xB02),                   // the read and the NOP since: 102
            CSRW(0xB82, 6), CSRR(9, 0xB82), CSRR(10, 0xB02)});   // minstreth = 100; minstret: 103, and the read of minstreth
    m.run_to(5 + 10 + 2);                          // two holds
    CHECK_EQ(m.core.x[5], 2u);
    CHECK_EQ(m.core.x[7], 100u);
    CHECK_EQ(m.core.x[8], 102u);
    CHECK_EQ(m.core.x[9], 100u);
    CHECK_EQ(m.core.x[10], 104u);
    CHECK_EQ(m.core.instructions, 11u);
}

// Spec, CSRs: `mcycle` "counts every cycle", from 0 at reset: in cycle C it holds C - 1. A write in cycle C replaces
// the count of that cycle, so the value written is what cycle C + 1 reads, and C + 2 reads it + 1.
TEST(rtl_mcycle_counts_cycles_and_a_write_replaces_it) {
    A m;
    m.prog({CSRR(5, 0xB00),                        // retires in 5: reads 4
            ADDI(6, 0, 1000), CSRW(0xB00, 6),      // 6; the write retires in 7: cycle 8 reads 1000
            CSRR(7, 0xB00),                        // held one cycle after the write: retires in 9: 1001
            CSRR(8, 0xB80)});                      // mcycleh: 0
    m.run_to(10);
    CHECK_EQ(m.retired(0x0), 5u);
    CHECK_EQ(m.core.x[5], 4u);
    CHECK_EQ(m.retired(0x8), 7u);
    CHECK_EQ(m.retired(0xC), 9u);
    CHECK_EQ(m.core.x[7], 1001u);
    CHECK_EQ(m.core.x[8], 0u);
}

// -------------------------------------------------------------------------------------------------------- 6. traps
// The Traps table, one row a case, behind two NOPs (minstret 2), with a handler that reads minstret:
//   mcause, mepc and mtval as the table gives them;
//   "Trap entry: ... `mstatus.MPIE` <- `MIE`; `mstatus.MIE` <- 0; the PC becomes `mtvec.BASE << 2`";
//   "The trapping instruction does not retire" and "does not increment `minstret`";
//   the trap in the cycle the instruction is in writeback, 7 by (R0), or the cycle of its access's answer (a load or
//   store leaves execute in 6: answered in 6 + L), and the handler's first instruction 4 cycles later (R1);
//   spec, Status: `access_fault` and `illegal_instruction` "one-cycle pulses from registers, in the cycle after the
//   core takes an access fault (instruction, load or store) or an illegal-instruction exception".
namespace {
struct TrapCase {
    const char* name;
    uint32_t ins;                                  // at 0x8; a 16-bit one in the low half, 0xFFFF above it
    bool compressed;
    uint32_t x5;                                   // the data address's base
    uint32_t cause, tval;
    uint64_t taken;                                // the cycle the trap is taken in
    bool reaches_a_target;                         // the access is made
};
// False, with a failure reported: the case does not behave as the table says.
bool check_trap_case(const TrapCase& c, uint32_t mstatus_before, uint32_t mstatus_after) {
    A m;
    m.core.mtvec = 0x100; m.core.mstatus = mstatus_before;
    m.core.x[5] = c.x5; m.core.x[6] = 0x77;
    m.bus.dmem[0] = 0x5A5A5A5A;
    m.prog({NOP, NOP});
    if (c.compressed) { m.put16(0x8, uint16_t(c.ins)); m.put16(0xA, 0xFFFF); } else m.put32(0x8, c.ins);
    m.prog({CSRR(10, 0xB02), ADDI(11, 0, 1)}, 0x100);
    bool ok = true;
    const auto expect = [&](bool cond, const char* what) {
        if (!cond && ok) { test::fail(__FILE__, __LINE__, "%s (mstatus 0x%x): %s", c.name, mstatus_before, what); ok = false; }
    };
    m.run_to(c.taken - 1);
    expect(m.core.traps == 0, "a trap before its cycle");
    expect(!m.core.core_status_access_fault && !m.core.core_status_illegal_instruction, "a status pulse before the trap");
    m.step();                                      // the trap's cycle
    const std::vector<Trap> traps = m.traps();
    expect(traps.size() == 1 && traps[0].cycle == c.taken, "the trap is not taken in its cycle");
    if (!ok) return false;
    expect(traps[0].cause == c.cause && m.core.mcause == c.cause, "mcause");
    expect(traps[0].epc == 0x8 && m.core.mepc == 0x8, "mepc is not the instruction's address");
    expect(traps[0].tval == c.tval && m.core.mtval == c.tval, "mtval");
    expect(traps[0].vector == 0x100, "the PC is not mtvec's");
    expect(m.core.mstatus == mstatus_after, "mstatus: MPIE <- MIE, MIE <- 0");
    const bool access_fault = c.cause == 1 || c.cause == 5 || c.cause == 7;   // the value of cycle taken + 1
    expect(m.core.core_status_access_fault == access_fault, "core_status_access_fault in the cycle after the trap");
    expect(m.core.core_status_illegal_instruction == (c.cause == 2), "core_status_illegal_instruction in the cycle after the trap");
    m.step();
    expect(!m.core.core_status_access_fault && !m.core.core_status_illegal_instruction, "a status pulse longer than one cycle");
    m.run_to(c.taken + 5);
    expect(m.retired(0x100) == c.taken + 4, "the handler's first instruction does not retire 4 cycles after the trap");
    expect(m.retired(0x104) == c.taken + 5, "the handler's second instruction");
    expect(m.retired(0x8) == 0, "the trapping instruction retired");
    expect(m.core.x[10] == 2, "minstret counts the trapping instruction");
    expect(m.core.instructions == 4, "instructions retired");
    expect(m.bus.accesses.size() == (c.reaches_a_target ? 1u : 0u), "the data accesses that reached a target");
    expect(m.bus.dmem[0] == 0x5A5A5A5A && m.core.x[6] == 0x77, "the trapping instruction had an effect");
    return ok;
}
}  // namespace

TEST(rtl_traps_table) {
    const TrapCase cases[] = {
        // "Illegal instruction | 2 | Instruction address | Original instruction bits: 16-bit encodings zero-extended"
        {"illegal, 32-bit", 0xFFFFFFFF, false, 0, 2, 0xFFFFFFFF, 7, false},
        {"illegal, 16-bit", C_LWSP_X0, true, 0, 2, C_LWSP_X0, 7, false},
        // "Breakpoint (EBREAK, C.EBREAK) | 3 | Instruction address | Instruction address"
        {"ebreak", EBREAK, false, 0, 3, 0x8, 7, false},
        {"c.ebreak", C_EBREAK, true, 0, 3, 0x8, 7, false},
        // "Environment call (ECALL) | 11 | Instruction address | 0"
        {"ecall", ECALL, false, 0, 11, 0, 7, false},
        // "Load address misaligned | 4 | Instruction address | Data address"; spec, Alignment: "trap misaligned
        // halfword/word accesses without issuing a transaction"
        {"lw misaligned", LW(6, 5, 2), false, DMEM, 4, DMEM + 2, 7, false},
        {"lh misaligned", I(1, 5, 1, 6, 0x03), false, DMEM, 4, DMEM + 1, 7, false},
        // "Store address misaligned | 6 | Instruction address | Data address"
        {"sw misaligned", SW(6, 5, 1), false, DMEM, 6, DMEM + 1, 7, false},
        {"sh misaligned", S(3, 6, 5, 1), false, DMEM, 6, DMEM + 3, 7, false},
        // "Load access fault | 5 | Instruction address | Data address"; spec, Memory and reset interface details:
        // "every data access to either [IMEM, the boot ROM] faults"; Data-side address decoder: "An address in no
        // window ... is accepted at once and answered with an error the next cycle without reaching any target"
        {"lw from IMEM", LW(6, 5, 0x40), false, IMEM, 5, IMEM + 0x40, 7, false},
        {"lw unmapped", LW(6, 5, 4), false, 0x30000000, 5, 0x30000004, 7, false},
        {"lw past DMEM's end", LW(6, 5, 0), false, DMEM + BYTES, 5, DMEM + BYTES, 7, false},
        // a target's error response ("data_response_error | Access failed"), two cycles after the request of cycle 6
        {"lw, the target's error", LW(6, 5, 0), false, ERROR_DEVICE, 5, ERROR_DEVICE, 8, true},
        // "Store access fault | 7 | Instruction address | Data address"
        {"sw to IMEM", SW(6, 5, 0x40), false, IMEM, 7, IMEM + 0x40, 7, false},
        {"sw unmapped", SW(6, 5, 0), false, 0x30000000, 7, 0x30000000, 7, false},
        {"sb, the target's error", S(1, 6, 5, 0), false, ERROR_DEVICE, 7, ERROR_DEVICE + 1, 8, true},
        // FENCE.I: spec, Memory and reset interface details: "FENCE.I raises an illegal-instruction exception"
        {"fence.i", FENCE_I, false, 0, 2, FENCE_I, 7, false},
        // custom-0 and custom-1 without a coprocessor: spec, Coprocessor: "when it is 0, those instructions are illegal"
        {"custom-0", R(0, 6, 5, 0, 7, 0x0B), false, 0, 2, R(0, 6, 5, 0, 7, 0x0B), 7, false},
        {"custom-1", R(0, 6, 5, 0, 7, 0x2B), false, 0, 2, R(0, 6, 5, 0, 7, 0x2B), 7, false},
    };
    for (const TrapCase& c : cases) {
        if (!check_trap_case(c, Rv32::MSTATUS_MIE, Rv32::MSTATUS_MPIE)) return;   // MIE 1: MPIE 1, MIE 0
        if (!check_trap_case(c, Rv32::MSTATUS_MPIE, 0)) return;                   // MIE 0: MPIE 0, MIE 0
    }
}

// "Instruction access fault | 1 | Instruction address | 0": a fetch outside IMEM (spec, Instruction-side windows: "the
// core sees an error, and so an instruction access fault, for every fetched word outside both IMEM's window and the
// boot ROM's"). The JAL at 0x8 enters execute in 6, so its target is in writeback, and traps, in 10 (R1).
TEST(rtl_fetch_outside_imem_is_an_instruction_access_fault) {
    A m;
    m.core.mtvec = 0x100; m.core.mstatus = Rv32::MSTATUS_MIE;
    m.prog({NOP, NOP, JAL(0, int32_t(BYTES) - 8)});
    m.prog({CSRR(10, 0xB02)}, 0x100);
    m.run_to(9);
    CHECK_EQ(m.core.traps, 0u);
    m.step();
    const std::vector<Trap> traps = m.traps();
    CHECK_EQ(traps.size(), 1u);
    CHECK_EQ(traps[0].cycle, 10u);
    CHECK_EQ(m.core.mcause, 1u);
    CHECK_EQ(m.core.mepc, BYTES);
    CHECK_EQ(m.core.mtval, 0u);
    CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MPIE));
    CHECK(m.core.core_status_access_fault && !m.core.core_status_illegal_instruction);
    m.step();
    CHECK(!m.core.core_status_access_fault);
    m.run_to(14);
    CHECK_EQ(m.retired(0x100), 14u);
    CHECK_EQ(m.core.x[10], 3u);                    // the NOPs and the JAL
}

// Spec, Traps: "MRET: `mstatus.MIE` <- `MPIE`; `MPIE` <- 1; the PC becomes `mepc`"; MRET is a redirect in the cycle it
// retires (R1): its target retires 4 cycles after it.
TEST(rtl_mret) {
    {   // MPIE 1: MIE 1, MPIE 1
        A m;
        m.core.mepc = 0x40; m.core.mstatus = Rv32::MSTATUS_MPIE;
        m.prog({NOP, NOP, MRET, ADDI(6, 0, 1)});
        m.prog({ADDI(7, 0, 2)}, 0x40);
        m.run_to(11);
        CHECK_EQ(m.retired(0x8), 7u);
        CHECK_EQ(m.retired(0x40), 11u);
        CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MIE | Rv32::MSTATUS_MPIE));
        CHECK_EQ(m.core.x[6], 0u);
        CHECK_EQ(m.core.x[7], 2u);
    }
    {   // MPIE 0: MIE 0, MPIE 1
        A m;
        m.core.mepc = 0x40; m.core.mstatus = Rv32::MSTATUS_MIE;   // (mie is 0: nothing to take)
        m.prog({NOP, NOP, MRET});
        m.prog({ADDI(7, 0, 2)}, 0x40);
        m.run_to(11);
        CHECK_EQ(m.retired(0x40), 11u);
        CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MPIE));
    }
}

// A trap and its return: an ECALL's handler moves mepc past it and returns.
TEST(rtl_ecall_handler_returns_past_it) {
    A m;
    m.core.mtvec = 0x100;
    m.prog({ADDI(5, 0, 1), ECALL, ADDI(6, 0, 2), JAL(0, 0)});
    m.prog({CSRR(10, 0x341), ADDI(10, 10, 4), CSRW(0x341, 10), MRET}, 0x100);
    m.run_to(30);
    CHECK_EQ(m.core.traps, 1u);
    CHECK(m.retired(0x10C) != 0);
    CHECK_EQ(m.retired(0x8), m.retired(0x10C) + 4);
    CHECK_EQ(m.core.x[6], 2u);
    CHECK_EQ(m.core.mepc, 0x8u);
}

// Spec, Memory ordering: "ordinary FENCE drains preceding transactions": nothing else; it retires as a NOP does.
TEST(rtl_fence_is_a_no_op) {
    A m;
    m.prog({ADDI(5, 0, 1), FENCE, ADDI(6, 0, 2)});
    m.run_to(7);
    CHECK_EQ(m.retired(0x4), 6u);
    CHECK_EQ(m.retired(0x8), 7u);
    CHECK_EQ(m.core.traps, 0u);
}

// --------------------------------------------------------------------------------------------------- 7. interrupts
// Spec, Pipeline event priority: "an interrupt is taken when it is pending in `mip`, enabled in `mie`, and
// `mstatus.MIE` = 1 ... at an instruction boundary where every older instruction has fully completed and the next
// instruction has produced no effect"; "`mepc` is that next instruction's address"; Traps: "Machine external interrupt
// | 0x8000_000B | Next unexecuted instruction | 0"; "no interrupt is taken while writeback holds an instruction". The
// cycle: the interrupt holds execute's instruction while writeback's completes, and is taken in the next cycle
// (rv32_pipeline.veryl:673-674, interrupt_holds_execute and `interrupt_taken = interrupt_holds_execute &&
// !writeback_valid`): raised in cycle 8, where 0xC retires and 0x10 is in execute, it is taken in 9.
TEST(rtl_external_interrupt_is_taken_at_an_instruction_boundary) {
    A m;
    m.core.mtvec = 0x100; m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE;
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6)});
    m.prog({ADDI(20, 0, 1)}, 0x100);
    m.run_to(7);
    m.bus.external_interrupt = true;
    m.run_to(13);
    const std::vector<Trap> traps = m.traps();
    CHECK_EQ(traps.size(), 1u);
    CHECK_EQ(traps[0].cycle, 9u);
    CHECK_EQ(m.core.mcause, 0x8000000Bu);
    CHECK_EQ(m.core.mepc, 0x10u);
    CHECK_EQ(m.core.mtval, 0u);
    CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MPIE));
    CHECK_EQ(m.retired(0xC), 8u);                  // every older instruction completed
    CHECK_EQ(m.core.x[8], 4u);
    CHECK_EQ(m.retired(0x10), 0u);                 // the next has no effect
    CHECK_EQ(m.core.x[9], 0u);
    CHECK_EQ(m.retired(0x100), 13u);               // (R1)
    CHECK(!m.core.core_status_access_fault && !m.core.core_status_illegal_instruction);
}

// ... "and `mstatus.MIE` = 1": with it clear, or with MEIE clear, the pending interrupt is never taken.
TEST(rtl_external_interrupt_is_not_taken_when_disabled) {
    for (int k = 0; k < 2; k++) {
        A m;
        m.core.mtvec = 0x100;
        if (k == 0) m.core.mie = Rv32::MEIE; else m.core.mstatus = Rv32::MSTATUS_MIE;
        m.bus.external_interrupt = true;
        m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4)});
        m.run_to(8);
        CHECK_EQ(m.core.traps, 0u);
        for (uint32_t i = 0; i < 4; i++) CHECK_EQ(m.retired(4 * i), 5u + i);
    }
}

// Spec, Pipeline event priority: "a CSR write that enables or disables an interrupt takes effect for the very next
// instruction: an interrupt it enables is taken with `mepc` at that instruction. The core holds the instruction after
// any CSR write for one cycle in execute to do this": the write retires in 6, the interrupt is taken in 7.
TEST(rtl_csr_write_enabling_a_pending_interrupt_takes_it_at_the_next_instruction) {
    A m;
    m.core.mtvec = 0x100; m.core.mie = Rv32::MEIE;
    m.bus.external_interrupt = true;
    m.prog({ADDI(5, 0, 1), CSR(6, 0x300, 8, 0), ADDI(6, 0, 2), ADDI(7, 0, 3)});   // csrrsi x0, mstatus, 8
    m.prog({ADDI(20, 0, 1)}, 0x100);
    m.run_to(11);
    const std::vector<Trap> traps = m.traps();
    CHECK_EQ(traps.size(), 1u);
    CHECK_EQ(m.retired(0x4), 6u);
    CHECK_EQ(traps[0].cycle, 7u);
    CHECK_EQ(m.core.mcause, 0x8000000Bu);
    CHECK_EQ(m.core.mepc, 0x8u);
    CHECK_EQ(m.core.x[6], 0u);
    CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MPIE));
    CHECK_EQ(m.retired(0x100), 11u);
}

// Spec, Traps: "NMI | 0x8000_0000 | Next unexecuted instruction | 0"; "It can be taken while `nmi` is high and the core
// has taken no trap since reset, whatever `mstatus.MIE` and `mie` hold; its own trap entry is one, so it is taken at
// most once". Raised in 8, taken in 9, as the external interrupt above.
TEST(rtl_nmi_is_taken_whatever_mstatus_and_mie_hold_and_once) {
    A m;
    m.core.mtvec = 0x100;                          // mstatus.MIE 0, mie 0
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6)});
    m.prog({ADDI(20, 0, 1), ADDI(21, 0, 2), ADDI(22, 0, 3), JAL(0, 0)}, 0x100);
    m.run_to(7);
    m.bus.non_maskable = true;                     // held until reset
    m.run_to(60);
    const std::vector<Trap> traps = m.traps();
    CHECK_EQ(traps.size(), 1u);                    // once
    CHECK_EQ(traps[0].cycle, 9u);
    CHECK_EQ(traps[0].cause, 0x80000000u);
    CHECK_EQ(traps[0].epc, 0x10u);
    CHECK_EQ(traps[0].tval, 0u);
    CHECK_EQ(m.core.mstatus, 0u);
    CHECK_EQ(m.core.x[9], 0u);
    CHECK_EQ(m.retired(0x100), 13u);
    CHECK_EQ(m.core.x[22], 3u);
}

// Spec, Traps: "Simultaneous interrupts: the NMI, then the machine external interrupt".
TEST(rtl_nmi_wins_over_the_external_interrupt) {
    A m;
    m.core.mtvec = 0x100; m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE;
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6)});
    m.run_to(7);
    m.bus.non_maskable = true; m.bus.external_interrupt = true;
    m.run_to(9);
    CHECK_EQ(m.core.traps, 1u);
    CHECK_EQ(m.core.mcause, 0x80000000u);
}

// Spec, Traps: "only by a core that has taken no trap since reset"; "the NMI that an exception raises through the
// tile's errors is then never taken".
TEST(rtl_nmi_is_never_taken_after_an_exception) {
    A m;
    m.core.mtvec = 0x100;
    m.prog({ECALL});
    m.prog({ADDI(20, 0, 1), ADDI(21, 0, 2), JAL(0, 0)}, 0x100);
    m.run_to(6);
    CHECK_EQ(m.core.traps, 1u);
    m.bus.non_maskable = true;
    m.run_to(60);
    CHECK_EQ(m.core.traps, 1u);
    CHECK_EQ(m.core.mcause, 11u);
    CHECK_EQ(m.core.x[21], 2u);
}

// ---------------------------------------------------------------------------------------------------------- 8. WFI
// The program of these tests: the WFI at 0x8 retires in cycle 7 (R0); 0xC is the instruction after it.
namespace {
void wfi_program(A& m) {
    m.core.mtvec = 0x100;
    m.prog({NOP, NOP, WFI, ADDI(6, 0, 1), ADDI(7, 0, 2), NOP, NOP, NOP, NOP, JAL(0, 0)});
    m.prog({ADDI(20, 0, 1)}, 0x100);
}
}  // namespace

// Spec, Core ports: `wfi_sleeping` "1 from the cycle after a WFI retires until the cycle after the wake condition
// first holds"; Lockstep regions, Resume timing: "In R the wake condition holds ...: `wfi_sleeping` is 0 from R + 1,
// and the instruction after the WFI ... with nothing else stalling, retires in R + 3"; WFI: "execution resumes at the
// WFI's address + 4 without a trap, whether or not the interrupt input stays high". (in_wfi after step(t) is the value
// of cycle t + 1.)
TEST(rtl_wfi_waits_and_resumes_3_cycles_after_the_wake) {
    for (int held = 0; held < 2; held++) {
        A m;
        wfi_program(m);
        m.core.mie = Rv32::MEIE;                   // mstatus.MIE clear
        m.run_to(6);
        CHECK(!m.core.in_wfi);                     // its value in cycle 7, where the WFI retires
        m.step();
        CHECK_EQ(m.retired(0x8), 7u);
        CHECK(m.core.in_wfi);                      // in cycle 8
        m.run_to(19);
        CHECK(m.core.in_wfi);                      // in cycle 20
        m.bus.external_interrupt = true;           // R = 20
        m.step();
        CHECK(!m.core.in_wfi);                     // in cycle 21
        m.bus.external_interrupt = held;
        m.run_to(24);
        CHECK_EQ(m.retired(0xC), 23u);
        CHECK_EQ(m.retired(0x10), 24u);
        CHECK_EQ(m.core.traps, 0u);
        CHECK_EQ(m.core.x[6], 1u);
    }
}

// Spec, WFI: "If the wake condition holds in the cycle the WFI retires, there is no wait: the instruction after it
// moves into execute in the next cycle and, with `mstatus.MIE` clear, retires three cycles after the WFI"; Core ports:
// `wfi_sleeping` "never 1 when that condition holds as the WFI retires".
TEST(rtl_wfi_does_not_wait_when_the_wake_condition_holds_as_it_retires) {
    A m;
    wfi_program(m);
    m.core.mie = Rv32::MEIE;
    m.run_to(6);
    m.bus.external_interrupt = true;               // from cycle 7, the WFI's
    for (int i = 0; i < 4; i++) { m.step(); CHECK(!m.core.in_wfi); }
    CHECK_EQ(m.retired(0x8), 7u);
    CHECK_EQ(m.retired(0xC), 10u);
    CHECK_EQ(m.core.traps, 0u);
}

// Spec, WFI: "An interrupt pending but not enabled in `mie` does not wake it".
TEST(rtl_wfi_is_not_woken_by_an_interrupt_mie_does_not_enable) {
    A m;
    wfi_program(m);
    m.core.mstatus = Rv32::MSTATUS_MIE;            // mie clear
    m.bus.external_interrupt = true;
    m.run_to(80);
    CHECK(m.core.in_wfi);
    CHECK_EQ(m.retired(0x8), 7u);
    CHECK_EQ(m.retired(0xC), 0u);
    CHECK_EQ(m.core.traps, 0u);
}

// Spec, WFI: "Let w be the cycle the WFI retires in if the wake condition holds then, or else the first cycle of the
// wait in which it holds. ... If `mstatus.MIE` is set in cycle w, or the wake is the NMI's, the interrupt can still be
// taken in cycle w + 2 ... in place of that instruction and before it has any effect, with `mepc` the WFI's address
// + 4, `mtval` 0 and the cause of the highest-priority interrupt pending in cycle w + 2".
TEST(rtl_wfi_waking_interrupt_is_taken_2_cycles_after_the_wake) {
    struct Case { bool nmi; uint64_t w; uint32_t cause; };
    const Case cases[] = {{false, 20, 0x8000000B}, {false, 7, 0x8000000B}, {true, 20, 0x80000000}, {true, 7, 0x80000000}};
    for (const Case& c : cases) {
        A m;
        wfi_program(m);
        if (!c.nmi) { m.core.mie = Rv32::MEIE; m.core.mstatus = Rv32::MSTATUS_MIE; }   // the NMI: mie 0, MIE 0
        m.run_to(c.w - 1);
        if (c.nmi) m.bus.non_maskable = true; else m.bus.external_interrupt = true;
        m.run_to(c.w + 6);
        const std::vector<Trap> traps = m.traps();
        CHECK_EQ(traps.size(), 1u);
        CHECK_EQ(traps[0].cycle, c.w + 2);
        CHECK_EQ(traps[0].cause, c.cause);
        CHECK_EQ(traps[0].epc, 0xCu);
        CHECK_EQ(traps[0].tval, 0u);
        CHECK_EQ(m.retired(0x8), 7u);
        CHECK_EQ(m.retired(0xC), 0u);
        CHECK_EQ(m.core.x[6], 0u);
        CHECK_EQ(m.retired(0x100), c.w + 6);       // (R1)
        CHECK(!m.core.in_wfi);
    }
}

// Spec, WFI: fetch "fills its six-halfword queue behind it and stops ... That is at most four requests from the cycle
// the WFI retires (the words up to 16 bytes past the WFI's own)"; Core ports: `core_status_canonical_wait` "1 in the
// cycle after each cycle in which `wfi_sleeping` is 1, `instruction_request_valid` is 0 and no fetch is outstanding
// (one accepted at an earlier edge whose response has not arrived before that cycle) ... it is 1 from the cycle after
// the wait begins with fetch idle through the first cycle in which `wfi_sleeping` is 0 again". With the last request
// in cycle f, answered in f + 1, fetch is idle from f + 2, and the wait begins in 8: the output is 1 in the cycles
// max(8, f + 2) + 1 .. R + 1. rv32.h, step: it returns kNever from the canonical wait until the wake.
namespace {
// The WFI at `wfi` retires in cycle `retires`, the wake is in cycle 30. False, with a failure reported: the canonical
// wait is not reported as the specification says.
bool check_canonical_wait(A& m, uint32_t wfi, uint64_t retires) {
    const uint64_t R = 30;
    bool ok = true;
    const auto expect = [&](bool cond, const char* what, uint64_t cycle = 0) {
        if (!cond && ok) { test::fail(__FILE__, __LINE__, "the WFI at 0x%x: %s (cycle %llu)", wfi, what, (unsigned long long)cycle); ok = false; }
    };
    std::vector<bool> canonical(R + 5), never(R + 5);        // by cycle t: the output's value in t + 1, step(t)'s return
    for (uint64_t t = 1; t <= R + 3; t++) {
        if (t == R) m.bus.external_interrupt = true;
        const uint64_t next = m.step();
        canonical[t] = m.core.core_status_canonical_wait;
        never[t] = next == Rv32::kNever;
        expect(never[t] || next == t + 1, "step's return", t);
    }
    expect(m.retired(wfi) == retires, "the WFI does not retire in its cycle");
    unsigned requests = 0;
    uint64_t f = 0;                                          // the last request before the wake
    for (const auto& [t, a] : m.bus.fetches) {
        if (t >= R) break;
        if (t >= retires) { requests++; expect(a <= wfi + 16, "a fetch more than 16 bytes past the WFI", t); }
        f = t;
    }
    expect(requests <= 4, "more than four requests from the cycle the WFI retires");
    expect(f < retires + 4, "a request after the four cycles", f);
    const uint64_t first = f + 2 > retires + 1 ? f + 2 : retires + 1;   // the first cycle of the canonical wait
    for (uint64_t t = 1; t <= R + 3; t++) {
        expect(canonical[t] == (t >= first && t <= R), "core_status_canonical_wait in the cycle after", t);
        expect(never[t] == (t >= first && t < R), "step's return: kNever from the canonical wait until the wake", t);
    }
    expect(m.retired(wfi + 4) == R + 3, "the instruction after the WFI does not retire in R + 3");
    return ok;
}
}  // namespace

TEST(rtl_canonical_wait) {
    {   // in straight-line code: the WFI retires in 7
        A m;
        wfi_program(m);
        m.core.mie = Rv32::MEIE;
        if (!check_canonical_wait(m, 0x8, 7)) return;
    }
    {   // the WFI is a jump's target: the JAL at 0x4 enters execute in 5, the WFI retires in 9 (R1), and fetch, restarted
        // in 5, is still filling its queue then
        A m;
        m.core.mie = Rv32::MEIE;
        m.prog({NOP, JAL(0, 0x3C)});
        m.prog({WFI, ADDI(6, 0, 1), NOP, NOP, NOP, NOP, NOP, JAL(0, 0)}, 0x40);
        if (!check_canonical_wait(m, 0x40, 9)) return;
    }
}

// ----------------------------------------------------------------------------------------- 9. fetch runs ahead
// Spec, Memory and reset interface details: "Without a redirect the core executes the words it has already fetched:
// falling through into rewritten code ... restart[s] nothing. So a word fetched before a write to it may execute in
// its old form"; "In J the core discards every instruction word fetched or requested before J ... Every instruction
// executed after the redirect comes from fetches raised in J or later". The word at 0x8 is rewritten at the edge that
// ends the cycle of its first fetch, cycles before its instruction is in decode; it runs in its old form, and after
// the branch back to it in its new one.
TEST(rtl_fetched_code_runs_in_its_old_form_until_a_redirect) {
    A m;
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2),
            ADDI(7, 0, 3),                         // 0x08: rewritten to addi x7, x0, 99
            ADDI(9, 9, 1), ADDI(10, 0, 2),
            BNE(9, 10, -0xC), JAL(0, 0)});                  // 0x14: back to 0x8 once
    uint64_t rewritten_at = 0;
    for (int i = 0; i < 30; i++) {
        m.step();
        if (rewritten_at == 0)
            for (const auto& [t, a] : m.bus.fetches)
                if (a == 0x8) { m.put32(0x8, ADDI(7, 0, 99)); rewritten_at = t; break; }
    }
    CHECK(rewritten_at != 0);
    CHECK_EQ(m.retired(0x8, 0), 7u);               // (R0): straight-line code
    CHECK(rewritten_at + 2 < 7);                   // rewritten before its instruction was in decode (cycle 5)
    CHECK_EQ(retired_value(m.tb.log, 0x8, 0), 3u); // the old form
    CHECK_EQ(m.retired(0x14, 0), 10u);
    CHECK_EQ(m.retired(0x8, 1), 14u);              // (R1): the taken branch
    CHECK_EQ(retired_value(m.tb.log, 0x8, 1), 99u);   // the new form
    CHECK_EQ(m.core.x[7], 99u);
}

// ------------------------------------------------------------------------------------------ 10. sleep equivalence
// rv32.h, step: "a core stepped every cycle and one stepped only when it asks end in the same state": one core is
// stepped in every cycle of a WFI's wait, the other skips the cycles after kNever and is stepped again at the wake.
TEST(rtl_sleeping_through_the_wait_equals_stepping_it) {
    const uint64_t R = 40, END = 60;
    A a, b;
    for (A* m : {&a, &b}) { wfi_program(*m); m->core.mie = Rv32::MEIE; }
    for (uint64_t t = 1; t <= END; t++) { if (t == R) a.bus.external_interrupt = true; a.step(); }
    unsigned steps = 0;
    for (uint64_t next = 1; next <= END;) {
        b.t = next - 1;
        if (next >= R) b.bus.external_interrupt = true;
        const uint64_t asked = b.step();
        steps++;
        next = asked != Rv32::kNever ? asked : next < R ? R : END + 1;   // asleep: nothing until its input changes
    }
    CHECK(steps < END - 15);                       // it did sleep
    CHECK_EQ(b.t, END);
    CHECK_EQ(a.core.digest(), b.core.digest());
    CHECK(format_body(a.tb.log) == format_body(b.tb.log));
    CHECK_EQ(a.core.mcycle, b.core.mcycle);
    CHECK_EQ(a.core.mcycle, END);
    CHECK_EQ(a.core.cycles_wfi, b.core.cycles_wfi);
    CHECK_EQ(a.retired(0xC), R + 3);
    CHECK_EQ(b.retired(0xC), R + 3);
    b.core.x[5] ^= 1;                              // the digest sees a register
    CHECK(a.core.digest() != b.core.digest());
}


// ======================================================================= B: the core on the tile's environment
// The first instruction leaves execute in cycle q = 4 (R0), where its load or store is raised ("issued", "accepted":
// the bus accepts in every cycle). x1 holds the base the accesses are relative to.
// --------------------------------------------------------------------------------------- 11. the register windows
// 6.4: "A load from a window ... takes `WINDOW_LOAD_LATENCY` = 2 cycles"; "A load issued in cycle t with latency L
// returns the state at the end of t + L - 2, its sample point"; register_bus: "A register-window access is registered
// at the end of t and presented on its owner's interface in t + 1 ... The owner answers a load in t + 1 with rdata
// from its registers, the state at the end of t ... the bus ... answers in t + 2". Raised in 4, the load of STATUS is
// made in 5, returns the state at the end of 4, and retires in 6.
TEST(tile_window_load_takes_two_cycles_and_reads_the_state_at_the_end_of_its_request_cycle) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE;
    m.env.regs[STATUS] = 0x10;                     // the state at the end of cycles 0 .. 3
    m.prog({LW(2, 1, 4), NOP});
    m.run_to(4);
    CHECK_EQ(m.env.asked, 1u);                     // asked what it is as it is raised
    CHECK_EQ(m.env.made.size(), 0u);
    m.env.regs[STATUS] = 0x30;                     // at the end of 4
    m.step();
    CHECK_EQ(m.env.made.size(), 1u);
    CHECK_EQ(m.env.made[0].t, 5u);
    CHECK(!m.env.made[0].store && m.env.made[0].a == TILE_WINDOW_BASE + 4);
    CHECK_EQ(m.core.instructions, 0u);
    m.env.regs[STATUS] = 0x50;                     // at the end of 5
    m.step();
    CHECK_EQ(m.retired(PMEM), 6u);
    CHECK_EQ(m.core.x[2], 0x30u);
    CHECK_STR(m.last(), "6 01000000 0040a103 x2=00000030 ld fffff804 4 00000030");
    m.step();
    CHECK_EQ(m.retired(PMEM + 4), 7u);
    CHECK_EQ(m.env.asked, 1u);
    CHECK_EQ(m.env.made.size(), 1u);
}

// 6.4: a load "takes its side effect (an `EVENTS` load's consumption, a pop) at the end of t + L - 1": the EVENTS load
// raised in 4 consumes in 5. The second load is raised in 6, as the first is answered (spec, Data), is made in 7 and
// retires in 8: register_bus, "register-window loads one every other cycle". Its sample point is the end of 6, after
// the consumption (register_bus: "Accesses take effect in program order"): EVENTS_PEEK no longer shows what the load
// of EVENTS consumed, the bit it returned set under the mask (7.2).
TEST(tile_events_load_consumes_the_cycle_after_its_request) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE;
    m.env.regs[EVENTS] = 0x300; m.env.regs[EVENTS_MASK] = 0x180;   // counters 2 and 3 done; the mask selects 1 and 2
    m.prog({LW(3, 1, 4 * EVENTS), LW(4, 1, 4 * EVENTS_PEEK)});
    m.run_to(4);
    CHECK_EQ(m.env.consumed.size(), 0u);
    m.run_to(8);
    CHECK_EQ(m.env.consumed.size(), 1u);
    CHECK_EQ(m.env.consumed[0].t, 5u);
    CHECK_EQ(m.env.consumed[0].events, 0x100u);
    CHECK_EQ(m.env.made.size(), 2u);
    CHECK_EQ(m.env.made[1].t, 7u);
    CHECK_EQ(m.env.made[1].a, TILE_WINDOW_BASE + 4 * EVENTS_PEEK);
    CHECK_EQ(m.core.x[3], 0x300u);
    CHECK_EQ(m.core.x[4], 0x200u);
    CHECK_EQ(m.retired(PMEM), 6u);
    CHECK_EQ(m.retired(PMEM + 4), 8u);
}

// 6.4: "a store issued in cycle t takes effect at the end of ... t + 1 in a window ..., and is answered in t + 1";
// register_bus: "Every other store to a register window is answered in t + 1, the cycle it is presented in and at
// whose end it takes effect"; "stores ... run one a cycle". Raised in 4 and 5: made, and retired, in 5 and 6. 6.4:
// "The state at the end of a cycle includes what takes effect then, so a load issued after a store sees it": the load
// raised in 6, whose sample point is the end of 6, returns the first store's value, and retires in 8.
TEST(tile_window_store_is_made_and_answered_the_cycle_after_its_request) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE; m.core.x[2] = 7; m.core.x[3] = 9;
    m.prog({SW(2, 1, 4 * EVENTS_MASK), SW(3, 1, 4 * COUNTER_1), LW(4, 1, 4 * EVENTS_MASK), NOP});
    m.run_to(4);
    CHECK_EQ(m.env.made.size(), 0u);
    m.step();
    CHECK_EQ(m.env.made.size(), 1u);
    CHECK_EQ(m.env.made[0].t, 5u);
    CHECK(m.env.made[0].store);
    CHECK_EQ(m.env.made[0].a, TILE_WINDOW_BASE + 4 * EVENTS_MASK);
    CHECK_EQ(m.env.made[0].v, 7u);
    m.run_to(9);
    CHECK_EQ(m.retired(PMEM), 5u);
    CHECK_EQ(m.retired(PMEM + 4), 6u);
    CHECK_EQ(m.retired(PMEM + 8), 8u);
    CHECK_EQ(m.retired(PMEM + 12), 9u);
    CHECK_EQ(m.env.made.size(), 3u);
    CHECK_EQ(m.env.made[1].t, 6u);
    CHECK(m.env.made[1].store);
    CHECK_EQ(m.env.made[1].a, TILE_WINDOW_BASE + 4 * COUNTER_1);
    CHECK_EQ(m.env.made[1].v, 9u);
    CHECK_EQ(m.env.made[2].t, 7u);
    CHECK_EQ(m.core.x[4], 7u);
}

// Machine-spec 5.1: DMEM is the core's; rv32.h's header: "DMEM ... made in q, answered in q + 1": a DMEM store and
// load retire one a cycle, from cycle 5 (spec, Data), and the environment is never asked.
TEST(tile_dmem_accesses_take_one_cycle) {
    M m;
    m.core.x[1] = 0x100; m.core.x[2] = 0xABCD;
    m.prog({SW(2, 1, 0), LW(3, 1, 0)});
    m.run_to(3);
    CHECK_EQ(m.dmem[0x40], 0u);
    m.step();
    CHECK_EQ(m.dmem[0x40], 0xABCDu);               // made in 4
    m.run_to(6);
    CHECK_EQ(m.retired(PMEM), 5u);
    CHECK_EQ(m.retired(PMEM + 4), 6u);
    CHECK_EQ(m.core.x[3], 0xABCDu);
    auto l = m.lines();
    CHECK_STR(l[0], "5 01000000 0020a023 st 00000100 4 0000abcd");
    CHECK_STR(l[1], "6 01000004 0000a183 x3=0000abcd ld 00000100 4 0000abcd");
    CHECK_EQ(m.env.asked, 0u);
    CHECK_EQ(m.env.made.size(), 0u);
}

// The tile window's W words (5.4: "W: ... a load reads 0"; register_bus: "a load of a W word ..., which reaches no
// owner, reads 0 after the same 2 cycles"): the loads of ARRIVE and DMA_START retire two cycles after they are raised,
// with 0, and reach no owner; the store to ARRIVE between them does.
TEST(tile_window_w_words_read_0_after_two_cycles_and_reach_no_owner) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE; m.core.x[2] = 7; m.core.x[5] = 0x55; m.core.x[6] = 0x66;
    m.env.regs[ARRIVE] = 0xFFFF; m.env.regs[DMA_START] = 0xFFFF;   // what an owner would return, were it asked
    m.prog({LW(5, 1, 4 * ARRIVE), SW(2, 1, 4 * ARRIVE), LW(6, 1, 4 * DMA_START), NOP});
    m.run_to(10);
    CHECK_EQ(m.retired(PMEM), 6u);                 // raised in 4
    CHECK_EQ(m.retired(PMEM + 4), 7u);             // raised in 6, made and answered in 7
    CHECK_EQ(m.retired(PMEM + 8), 9u);             // raised in 7
    CHECK_EQ(m.retired(PMEM + 12), 10u);
    CHECK_EQ(m.core.traps, 0u);
    CHECK_EQ(m.core.x[5], 0u);
    CHECK_EQ(m.core.x[6], 0u);
    CHECK_EQ(m.env.made.size(), 1u);
    CHECK(m.env.made[0].store && m.env.made[0].t == 7 && m.env.made[0].a == TILE_WINDOW_BASE + 4 * ARRIVE && m.env.made[0].v == 7);
}

// ------------------------------------------------------------------------------------------------- 12. the tables
// 6.4: "a load from a table `TABLE_LOAD_LATENCY` = 1"; a store "takes effect at the end of ... t in a table, and is
// answered in t + 1"; register_bus: "A table access is presented on its table's interface in cycle t ...: the table
// reads or writes at the end of t ... The bus answers in t + 1". The store raised in 4 is made in 4 and retires in 5;
// the load raised in 5, whose sample point is the end of 4, is made in 5, returns what the store wrote, and retires
// in 6. PTR's entry 5 and GROUP's word 3 (5.2).
TEST(tile_table_accesses_are_made_as_they_are_raised_and_a_load_sees_the_store_before_it) {
    for (uint32_t address : {uint32_t(PTR_BASE) + 0x14, uint32_t(GROUP_BASE) + 0xC}) {
        M m;
        m.core.x[1] = address; m.core.x[2] = 0x1ABCD;
        m.prog({SW(2, 1, 0), LW(4, 1, 0), NOP});
        m.run_to(3);
        CHECK_EQ(m.env.made.size(), 0u);
        m.step();
        CHECK_EQ(m.env.made.size(), 1u);
        CHECK(m.env.made[0].store && m.env.made[0].t == 4 && m.env.made[0].a == address && m.env.made[0].v == 0x1ABCD);
        m.run_to(7);
        CHECK_EQ(m.env.made.size(), 2u);
        CHECK(!m.env.made[1].store && m.env.made[1].t == 5 && m.env.made[1].a == address);
        CHECK_EQ(m.retired(PMEM), 5u);
        CHECK_EQ(m.retired(PMEM + 4), 6u);
        CHECK_EQ(m.retired(PMEM + 8), 7u);
        CHECK_EQ(m.core.x[4], 0x1ABCDu);
        CHECK_EQ(m.core.traps, 0u);
    }
}

// --------------------------------------------------------------------------------------------- 13. REQUEST_SEND
// 6.4: "A store that may stall (9, 11.1, 17.6) takes effect at the end of the cycle c its unit takes it, t + 1 at the
// earliest, and is answered in c + 1"; register_bus: "A store that may stall (... REQUEST_SEND ...) is presented from
// t + 1 in every cycle until its owner's ready is 1, and takes effect at the end of that cycle c; the bus answers in
// c + 1". Raised in 4 and presented from 5: with its owner ready it is made in 5 and retires in 6.
TEST(tile_request_send_is_answered_the_cycle_after_it_is_taken) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE; m.core.x[2] = 0xAB;
    m.prog({SW(2, 1, 4 * REQUEST_SEND), NOP});
    m.run_to(5);
    CHECK_EQ(m.env.made.size(), 1u);
    CHECK_EQ(m.env.made[0].t, 5u);
    CHECK_EQ(m.env.made[0].a, TILE_WINDOW_BASE + 4 * REQUEST_SEND);
    CHECK_EQ(m.core.instructions, 0u);
    m.run_to(7);
    CHECK_EQ(m.retired(PMEM), 6u);
    CHECK_EQ(m.retired(PMEM + 4), 7u);
    CHECK_EQ(m.core.stall_store, 0u);
}

// Its owner not ready in 5, 6 and 7 (a REQ-REFUSED line each, tests/TRACE-FORMAT.md), it is made in 8, the first
// cycle its owner takes it, and retires in 9.
TEST(tile_request_send_is_presented_until_its_owner_takes_it) {
    M m;
    m.core.x[1] = TILE_WINDOW_BASE; m.core.x[2] = 0xAB;
    m.env.ready = false;
    m.prog({SW(2, 1, 4 * REQUEST_SEND), NOP});
    m.run_to(4);
    CHECK(m.lines().empty());
    m.run_to(7);
    auto l = m.lines();
    CHECK_EQ(l.size(), 3u);
    CHECK_STR(l[0], "5 REQ-REFUSED");
    CHECK_STR(l[1], "6 REQ-REFUSED");
    CHECK_STR(l[2], "7 REQ-REFUSED");
    CHECK_EQ(m.core.stall_store, 3u);
    CHECK_EQ(m.env.made.size(), 0u);
    CHECK_EQ(m.core.instructions, 0u);
    m.env.ready = true;
    m.step();                                      // 8
    CHECK_EQ(m.env.made.size(), 1u);
    CHECK_EQ(m.env.made[0].t, 8u);
    CHECK_EQ(m.env.made[0].a, TILE_WINDOW_BASE + 4 * REQUEST_SEND);
    CHECK_EQ(m.env.made[0].v, 0xABu);
    CHECK_EQ(m.core.instructions, 0u);
    m.run_to(10);
    CHECK_EQ(m.retired(PMEM), 9u);
    CHECK_EQ(m.retired(PMEM + 4), 10u);
    CHECK_EQ(m.core.stall_store, 3u);
    CHECK_EQ(m.env.asked, 1u);                     // asked what it is once
}

// ------------------------------------------------------------------------------- 14. errors, the park loop, wakes
// rv32.h, cycle: "the pulses of a trap taken in t - 1 are the tile's ERRORS bits" (spec, Status: "in the cycle after
// the core takes an access fault (instruction, load or store) or an illegal-instruction exception"; machine-spec 7.4:
// ERRORS.illegal_instruction is bit 8): the illegal instruction traps in 5 and core_status pulses in 6.
TEST(tile_illegal_instruction_pulses_core_status_the_cycle_after_its_trap) {
    M m;
    const uint32_t ins = R(1, 2, 1, 4, 3);         // div x3, x1, x2: 0x0220c1b3
    m.prog({ins}); m.prog({NOP, NOP}, 0x40);
    m.run_to(5);
    CHECK_STR(m.last(), "5 TRAP mcause=00000002 mepc=01000000 mtval=0220c1b3 pc=01000100");
    CHECK_EQ(m.env.pulses.size(), 0u);
    m.run_to(9);
    CHECK_EQ(m.env.pulses.size(), 1u);
    CHECK_EQ(m.env.pulses[0].t, 6u);
    CHECK_EQ(m.env.pulses[0].bit, ILLEGAL_INSTRUCTION_BIT);
    CHECK_EQ(m.core.illegals, 1u);
    CHECK_EQ(m.retired(PMEM + 0x100), 9u);
    CHECK(m.core.stop == Stop::running);
}

// An instruction access fault: a fetch outside IMEM and the boot ROM (machine-spec 5.1) traps in 5, mcause 1, and
// core_status's access_fault (ERRORS bit 7) pulses in 6. The core has no halts of its own: ECALL and EBREAK trap, and
// pulse nothing.
TEST(tile_fetch_fault_pulses_core_status_and_ecall_traps) {
    const uint32_t outside[] = {0x00000100, PMEM + MEM_BYTES, BOOTROM_BASE + 4 * BOOTROM_MAX_WORDS, 0x30000000};
    for (uint32_t pc : outside) {
        M m;
        m.core.pc = pc; m.prog({NOP, NOP}, 0x40);
        m.run_to(9);
        const std::vector<Trap> traps = m.traps();
        CHECK_EQ(traps.size(), 1u);
        CHECK_EQ(traps[0].cycle, 5u);
        CHECK_EQ(traps[0].cause, 1u);
        CHECK_EQ(traps[0].epc, pc);
        CHECK_EQ(traps[0].tval, 0u);
        CHECK_EQ(m.env.pulses.size(), 1u);
        CHECK_EQ(m.env.pulses[0].t, 6u);
        CHECK_EQ(m.env.pulses[0].bit, ACCESS_FAULT_BIT);
        CHECK_EQ(m.core.halt, int(Rv32::RUN));
        CHECK_EQ(m.retired(PMEM + 0x100), 9u);
    }
    for (uint32_t ins : {ECALL, EBREAK}) {
        M m;
        m.prog({ins}); m.prog({NOP, NOP}, 0x40);
        m.run_to(9);
        CHECK_EQ(m.core.traps, 1u);
        CHECK_EQ(m.core.mcause, ins == ECALL ? 11u : 3u);
        CHECK_EQ(m.env.pulses.size(), 0u);
        CHECK(m.core.stop == Stop::running && !m.core.ub);
        CHECK_EQ(m.retired(PMEM + 0x100), 9u);
    }
}

// A load or store access fault: the access raised in 4 is answered with its error in 5 (6.4: "An access fault (5.1)
// is answered in t + 1, a load's or a store's"; register_bus: "An access fault is answered in t + 1, with error 1 and
// read data 0"; spec, Data-side address decoder: "An address in no window, IMEM's and the boot ROM's included, is ...
// answered with an error the next cycle"), where the trap is taken; core_status pulses in 6; nothing reaches an
// owner. The faults are machine-spec 5.1's and register_bus's: "any access to an unmapped word, any access of other
// than a whole word ..., and a store to an R word"; unmapped: "the rest of region 0xFF, window 11, an absent
// peripheral's window, a block a window does not use ... and a word a block does not define".
TEST(tile_data_faults_trap_the_cycle_after_they_are_raised) {
    struct Case { const char* name; uint32_t ins, base, cause, tval; };
    const Case cases[] = {
        {"a store to an R word (STATUS)", SW(2, 1, 4), TILE_WINDOW_BASE, 7, TILE_WINDOW_BASE + 4},
        {"a halfword store to the tile window", S(0x14, 2, 1, 1), TILE_WINDOW_BASE, 7, TILE_WINDOW_BASE + 0x14},
        {"a store to a word block 0 does not define", SW(2, 1, 0x1C), TILE_WINDOW_BASE, 7, TILE_WINDOW_BASE + 0x1C},
        {"a store to the tile window's block 7", SW(2, 1, 0x1C0), TILE_WINDOW_BASE, 7, TILE_WINDOW_BASE + 0x1C0},
        {"a store to an absent peripheral's window", SW(2, 1, 0x14), PERIPH_WINDOW_BASE, 7, PERIPH_WINDOW_BASE + 0x14},
        {"a store to window 11", SW(2, 1, 0), TABLE_END, 7, TABLE_END},
        {"a load from window 11", LW(2, 1, 0), TABLE_END, 5, TABLE_END},
        {"a load from region 0xFF below its mapped part", LW(2, 1, 0), 0xFF000000, 5, 0xFF000000},
        {"a byte load from a table", I(0, 1, 0, 2, 0x03), HIST_BASE, 5, HIST_BASE},
        {"a byte store to a table", S(1, 2, 1, 0), PTR_BASE, 7, PTR_BASE + 1},
        {"an unmapped address", LW(2, 1, 0), 0x30000000, 5, 0x30000000},
        {"a load from IMEM", LW(2, 1, 0), PMEM + 0x40, 5, PMEM + 0x40},
        {"a store to IMEM", SW(2, 1, 0), PMEM + 0x40, 7, PMEM + 0x40},
        {"a load from the boot ROM", LW(2, 1, 0), BOOTROM_BASE, 5, BOOTROM_BASE},
        {"a store to the boot ROM", SW(2, 1, 0), BOOTROM_BASE, 7, BOOTROM_BASE},
        {"a load past DMEM's end", LW(2, 1, 0), MEM_BYTES, 5, MEM_BYTES},
    };
    static const uint32_t rom[2] = {0x11, 0x22};
    for (const Case& c : cases) {
        M m;
        m.core.bootrom = rom; m.core.bootrom_words = 2;
        m.core.x[1] = c.base; m.core.x[2] = 0x77;
        m.prog({c.ins});
        m.run_to(7);
        const std::vector<Trap> traps = m.traps();
        const bool ok = traps.size() == 1 && traps[0].cycle == 5 && traps[0].cause == c.cause && traps[0].tval == c.tval &&
                        traps[0].epc == PMEM && m.core.bus_errors == 1 && m.env.pulses.size() == 1 &&
                        m.env.pulses[0].t == 6 && m.env.pulses[0].bit == ACCESS_FAULT_BIT && m.env.made.empty() &&
                        m.core.x[2] == 0x77;
        if (!ok) {
            test::fail(__FILE__, __LINE__, "%s: %zu traps (the first in cycle %llu, mcause %u), %zu pulses, %zu accesses made: one trap in cycle 5 with core_status's pulse in 6 and no access made is expected",
                       c.name, traps.size(), traps.empty() ? 0ull : (unsigned long long)traps[0].cycle, traps.empty() ? 0u : traps[0].cause, m.env.pulses.size(), m.env.made.size());
            return;
        }
    }
}

// A faulting LOAD from the register windows is answered the next cycle too, as every access fault is (6.4;
// register_bus.veryl, `request_answered_next = core_request_valid && (request_fault || ...)`): raised in 4, it traps in
// 5, not after the two cycles of a load that reaches its owner.
TEST(tile_window_load_faults_are_answered_the_next_cycle) {
    struct Case { const char* name; uint32_t ins, base; };
    const Case cases[] = {
        {"a byte load from the tile window", I(0, 1, 0, 2, 0x03), TILE_WINDOW_BASE},
        {"a halfword load from the tile window", I(4, 1, 1, 2, 0x03), TILE_WINDOW_BASE},
        {"a load of a word block 0 does not define", LW(2, 1, 0x1C), TILE_WINDOW_BASE},
        {"a load from the tile window's block 7", LW(2, 1, 0x1C0), TILE_WINDOW_BASE},
        {"an absent peripheral's window", LW(2, 1, 0), TILE_WINDOW_BASE + 0x400},
    };
    for (const Case& c : cases) {
        M m;
        m.core.x[1] = c.base;
        m.prog({c.ins});
        m.run_to(7);
        const std::vector<Trap> traps = m.traps();
        const bool ok = traps.size() == 1 && traps[0].cycle == 5 && traps[0].cause == 5 && traps[0].epc == PMEM &&
                        m.env.pulses.size() == 1 && m.env.pulses[0].t == 6 && m.env.pulses[0].bit == ACCESS_FAULT_BIT &&
                        m.env.made.empty();
        if (!ok) {
            test::fail(__FILE__, __LINE__, "%s: %zu traps (the first in cycle %llu): one load access fault in cycle 5 with core_status's pulse in 6 is expected",
                       c.name, traps.size(), traps.empty() ? 0ull : (unsigned long long)traps[0].cycle);
            return;
        }
    }
}

// A peripheral's window by the straps (machine-spec 17.1; register_bus, the map): peripheral 2 present, without a DMA.
// Its control block's words are a register window's: the load of STATUS made in 5 and answered in 6, the store to
// EVENTS_MASK made and answered in 7. "A peripheral without a DMA ... has no DMA block: every word of its block 1
// reads 0, reaching no owner", after a window load's two cycles; "and a store there ... faults". With the DMA
// strapped, block 1's DMA_DEST_ADDR is stored. Peripheral 1, absent, faults (above).
TEST(tile_peripheral_window_by_the_straps) {
    const uint32_t window = TILE_WINDOW_BASE + 2 * WINDOW_BYTES;   // peripheral n's: window 12 + n
    {
        M m;
        m.env.straps.peripherals = 0b010;
        m.env.periph[1][P_STATUS] = 0x3; m.env.periph[1][P_DMA_DEST_ADDR] = 0xFFFF;
        m.core.x[1] = window; m.core.x[2] = 5; m.core.x[4] = 0x44;
        m.prog({LW(3, 1, 4 * P_STATUS), SW(2, 1, 4 * P_EVENTS_MASK), LW(4, 1, 4 * P_DMA_DEST_ADDR), NOP});
        m.run_to(10);
        CHECK_EQ(m.core.traps, 0u);
        CHECK_EQ(m.retired(PMEM), 6u);
        CHECK_EQ(m.retired(PMEM + 4), 7u);
        CHECK_EQ(m.retired(PMEM + 8), 9u);
        CHECK_EQ(m.retired(PMEM + 12), 10u);
        CHECK_EQ(m.core.x[3], 0x3u);
        CHECK_EQ(m.core.x[4], 0u);
        CHECK_EQ(m.env.made.size(), 2u);
        CHECK(!m.env.made[0].store && m.env.made[0].t == 5 && m.env.made[0].a == window + 4 * P_STATUS);
        CHECK(m.env.made[1].store && m.env.made[1].t == 7 && m.env.made[1].a == window + 4 * P_EVENTS_MASK && m.env.made[1].v == 5);
    }
    {   // a store to the DMA-less block 1
        M m;
        m.env.straps.peripherals = 0b010;
        m.core.x[1] = window;
        m.prog({SW(2, 1, 4 * P_DMA_DEST_ADDR)});
        m.run_to(7);
        const std::vector<Trap> traps = m.traps();
        CHECK_EQ(traps.size(), 1u);
        CHECK_EQ(traps[0].cycle, 5u);
        CHECK_EQ(traps[0].cause, 7u);
        CHECK_EQ(m.env.made.size(), 0u);
    }
    {   // with its DMA
        M m;
        m.env.straps.peripherals = 0b010; m.env.straps.peripheral_dmas = 0b010;
        m.core.x[1] = window; m.core.x[2] = 0x1234;
        m.prog({SW(2, 1, 4 * P_DMA_DEST_ADDR), LW(4, 1, 4 * P_DMA_DEST_ADDR)});
        m.run_to(7);
        CHECK_EQ(m.core.traps, 0u);
        CHECK_EQ(m.retired(PMEM), 5u);
        CHECK_EQ(m.retired(PMEM + 4), 7u);
        CHECK_EQ(m.core.x[4], 0x1234u);
        CHECK_EQ(m.env.made.size(), 2u);
    }
}

// rv32.h, step: "parked (the handler's park loop): the run's end as recorded (stop, stop_cycle), but the core goes on
// running its loop every cycle". The trap in 5; the handler's `j .` retires in 9 (R1), and then every 3 cycles
// (spec, Control transfers: the target of a JAL "3 cycles after it retires").
TEST(tile_park_loop_in_the_handler) {
    M m;
    m.prog({R(1, 0, 0, 4, 1)});                    // illegal: to the handler, which parks
    m.prog({J(0, 0)}, 0x40);
    m.run_to(8);
    CHECK(!m.core.parked && m.core.stop == Stop::running);
    CHECK_EQ(m.step(), 10u);                       // the park loop goes on running
    CHECK(m.core.parked && m.core.stop == Stop::parked);
    CHECK_EQ(m.core.stop_cycle, 9u);
    CHECK_EQ(m.core.instructions, 1u);
    for (int i = 0; i < 12; i++) CHECK(m.step() != Rv32::kNever);
    CHECK_EQ(m.core.instructions, 5u);
    CHECK(m.core.parked && m.core.stop == Stop::parked && m.core.stop_cycle == 9u);
    CHECK_EQ(m.core.pc, PMEM + 0x100);
}

// rv32.h's header: "meip() ... the tile's wake". With mie = MEIE and mstatus.MIE clear the wake of cycle R = 20
// releases the WFI and is not taken (spec, Lockstep regions, "The release on `external_interrupt`"): the instruction
// after it retires in R + 3 (Resume timing), and the trace has the WAKE line of cycle R (tests/TRACE-FORMAT.md: "a
// WFI that slept is left at C: the wake term holds at C"). With mie clear it does not wake (spec, WFI).
TEST(tile_wake_is_meip_and_releases_a_wfi) {
    M m;
    m.core.mie = Rv32::MEIE;
    m.prog({NOP, NOP, WFI, ADDI(6, 0, 1), CSRR(7, 0x344)});
    m.run_to(19);
    CHECK(m.core.in_wfi);
    CHECK_EQ(m.core.instructions, 3u);
    m.env.meip_level = true;
    m.step();
    CHECK(!m.core.in_wfi);
    CHECK_STR(m.last(), "20 WAKE event");
    m.run_to(24);
    CHECK_EQ(m.retired(PMEM + 0xC), 23u);
    CHECK_EQ(m.core.x[6], 1u);
    CHECK_EQ(m.core.x[7], 0x800u);                 // mip.MEIP shows it
    CHECK_EQ(m.core.traps, 0u);
    M n;                                           // mie clear: no wake
    n.env.meip_level = true;
    n.prog({NOP, NOP, WFI, ADDI(6, 0, 1)});
    n.run_to(60);
    CHECK(n.core.in_wfi);
    CHECK_EQ(n.core.instructions, 3u);
    M k;                                           // mie and mstatus.MIE set: the wake is the external interrupt
    k.core.mie = Rv32::MEIE; k.core.mstatus = Rv32::MSTATUS_MIE;
    k.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6)});
    k.run_to(7);
    k.env.meip_level = true;
    k.run_to(9);
    CHECK_STR(k.last(), "9 TRAP mcause=8000000b mepc=01000010 mtval=00000000 pc=01000100");
}

// rv32.h's header: "nmi() ... the tile's errors" (machine-spec 6.2). Raised in 8, taken in 9 whatever mstatus and mie
// hold, once (spec, Traps; group A, 7); core_status pulses nothing for it.
TEST(tile_nmi_is_taken_once) {
    M m;
    m.prog({ADDI(5, 0, 1), ADDI(6, 0, 2), ADDI(7, 0, 3), ADDI(8, 0, 4), ADDI(9, 0, 5), ADDI(10, 0, 6)});
    m.prog({ADDI(20, 0, 1), ADDI(21, 0, 2), ADDI(22, 0, 3), ADDI(23, 0, 4), J(0, 0)}, 0x40);
    m.run_to(7);
    m.env.nmi_level = true;
    m.run_to(40);
    const std::vector<std::string> l = m.lines();
    CHECK(l.size() > 4);
    CHECK_STR(l[4], "9 TRAP mcause=80000000 mepc=01000010 mtval=00000000 pc=01000100");
    CHECK_EQ(m.traps().size(), 1u);
    CHECK_EQ(m.core.x[9], 0u);
    CHECK_EQ(m.retired(PMEM + 0x100), 13u);
    CHECK_EQ(m.core.x[23], 4u);
    CHECK_EQ(m.env.pulses.size(), 0u);
}

// The digest of a core that slept and of one stepped every cycle, on the tile's environment, through two waits.
TEST(tile_sleeping_through_the_wait_equals_stepping_it) {
    const uint64_t R = 30, END = 45;
    M a, b;
    for (M* m : {&a, &b}) { m->core.mie = Rv32::MEIE; m->prog({ADDI(5, 0, 1), WFI, ADDI(6, 0, 2), WFI}); }
    for (uint64_t t = 1; t <= END; t++) { a.env.meip_level = t == R; a.step(); }
    unsigned steps = 0;
    for (uint64_t next = 1; next <= END;) {
        b.t = next - 1;
        b.env.meip_level = next == R;
        const uint64_t asked = b.step();
        steps++;
        next = asked != Rv32::kNever ? asked : next < R ? R : END + 1;
    }
    CHECK(steps < 30);
    if (b.t < END) { b.t = END - 1; b.step(); }
    CHECK_EQ(a.core.digest(), b.core.digest());
    CHECK(a.lines() == b.lines());
    CHECK_EQ(a.core.x[6], 2u);
    CHECK(a.core.in_wfi && b.core.in_wfi);
}
