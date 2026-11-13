// Rv32: the core's own logic (units/rv32.h) against a scripted environment, no framework: every instruction class, the
// CSR rules, traps and halts, the timing, the address decode, and the order of ref's checks.
#include <cstdint>
#include <string>
#include <vector>

#include "harness.h"
#include "rv32_asm.h"
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

struct Post { uint32_t op, arg, v; };
struct Env {
    uint32_t regs[16] = {};
    bool wk = false, interrupt = false, push = true;
    std::vector<Post> posts;
    uint32_t reg(unsigned r) const { return regs[r]; }
    bool wake() const { return wk; }
    bool irq() const { return interrupt; }
    bool can_push() const { return push; }
    void post(uint32_t op, uint32_t arg, uint32_t v) { posts.push_back({op, arg, v}); }
};

struct M {
    std::vector<uint32_t> dmem, imem;
    TraceBuffer tb;
    Rv32 core;
    Env env;
    uint64_t t = 0;
    M() : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0), tb(4096) {
        core.dmem = dmem.data(); core.imem = imem.data(); core.log = &tb.log;
        core.pc = 0x01000000;
    }
    void prog(std::initializer_list<uint32_t> ins, uint32_t at = 0) { for (uint32_t i : ins) imem[at++] = i; }
    uint64_t step() { return core.step(env, ++t); }
    void steps(int n) { for (int i = 0; i < n; i++) step(); }
    std::vector<std::string> lines() const { return format_body(tb.log); }
    std::string last() const { auto l = lines(); return l.empty() ? "" : l.back(); }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------- ALU
TEST(rv32_alu_immediates_and_registers) {
    M m;
    m.prog({ADDI(1, 0, -5), I(-4, 1, 2, 2), I(-4, 1, 3, 3), I(0x0F0, 1, 4, 4), I(1, 1, 5, 5) | 0x40000000u, I(1, 1, 5, 6),
            I(3, 1, 1, 7), R(0, 1, 1, 0, 8), R(0x20, 7, 1, 0, 9), R(0, 3, 1, 2, 10), R(0, 3, 1, 3, 11), R(0x20, 3, 1, 5, 12),
            R(0, 3, 1, 5, 13), LUI(14, 0xABCDE000), U(0x1000, 15, 0x17)});
    m.steps(15);
    CHECK_EQ(m.core.x[1], uint32_t(-5));
    CHECK_EQ(m.core.x[2], 1u);                     // slti -5 < -4
    CHECK_EQ(m.core.x[3], 1u);                     // sltiu 0xFFFFFFFB < 0xFFFFFFFC
    CHECK_EQ(m.core.x[4], uint32_t(-5) ^ 0x0F0u);
    CHECK_EQ(m.core.x[5], uint32_t(-3));           // srai -5 >> 1
    CHECK_EQ(m.core.x[6], 0x7FFFFFFDu);            // srli
    CHECK_EQ(m.core.x[7], uint32_t(-40));          // slli 3
    CHECK_EQ(m.core.x[8], uint32_t(-10));
    CHECK_EQ(m.core.x[9], uint32_t(35));           // -5 - -40
    CHECK_EQ(m.core.x[14], 0xABCDE000u);
    CHECK_EQ(m.core.x[15], 0x01000038u + 0x1000u); // auipc at the 15th word
}

TEST(rv32_slt_signedness) {
    M m;
    m.prog({ADDI(1, 0, -1), ADDI(2, 0, 1), R(0, 2, 1, 2, 3), R(0, 2, 1, 3, 4), I(1, 1, 2, 5), I(1, 1, 3, 6)});
    m.steps(6);
    CHECK_EQ(m.core.x[3], 1u);    // slt -1 < 1
    CHECK_EQ(m.core.x[4], 0u);    // sltu 0xFFFFFFFF < 1: no
    CHECK_EQ(m.core.x[5], 1u);
    CHECK_EQ(m.core.x[6], 0u);
}

TEST(rv32_multiplies_against_wide_arithmetic) {
    test::Rng rng(7);
    for (int k = 0; k < 2000; k++) {
        M m;
        uint32_t a = uint32_t(rng.next()), b = uint32_t(rng.next());
        if (k < 4) { a = k & 1 ? 0x80000000u : 0xFFFFFFFFu; b = k & 2 ? 0x80000000u : 0xFFFFFFFFu; }
        m.core.x[1] = a; m.core.x[2] = b;
        m.prog({R(1, 2, 1, 0, 3), R(1, 2, 1, 1, 4), R(1, 2, 1, 2, 5), R(1, 2, 1, 3, 6)});
        CHECK_EQ(m.step(), 4u);                     // mul_latency 3: two stall cycles
        CHECK_EQ(m.step(), 4u);
        CHECK_EQ(m.step(), 4u);
        m.steps(9);
        int64_t sa = int32_t(a), sb = int32_t(b);
        CHECK_EQ(m.core.x[3], a * b);
        CHECK_EQ(m.core.x[4], uint32_t(uint64_t(sa * sb) >> 32));
        CHECK_EQ(m.core.x[5], uint32_t(uint64_t((__int128)sa * (__int128)uint64_t(b) >> 32)));
        CHECK_EQ(m.core.x[6], uint32_t(uint64_t(a) * b >> 32));
        CHECK_EQ(m.core.instructions, 4u);
    }
}

TEST(rv32_divide_opcodes_are_illegal) {
    for (uint32_t f3 = 4; f3 < 8; f3++) {
        M m;
        m.core.mtvec = 0x01000100;
        uint32_t ins = R(1, 2, 1, f3, 3);
        m.prog({ins});
        m.step();
        CHECK_EQ(m.core.mcause, 2u);
        CHECK_EQ(m.core.mtval, ins);
        CHECK_EQ(m.core.mepc, 0x01000000u);
        CHECK_EQ(m.core.pc, 0x01000100u);
        CHECK_EQ(m.env.posts.size(), 1u);
        CHECK_EQ(m.env.posts[0].op, uint32_t(BUS_ERROR));
        CHECK_EQ(m.env.posts[0].arg, uint32_t(COND_ILLEGAL));
        CHECK(m.last().find("TRAP mcause=00000002") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------------------------------- loads and stores
TEST(rv32_loads_and_stores_extend) {
    M m;
    m.core.x[1] = 0x100; m.core.x[2] = 0x8081FF80;
    m.prog({SW(2, 1, 0), I(0, 1, 0, 3, 0x03), I(0, 1, 4, 4, 0x03), I(2, 1, 1, 5, 0x03), I(2, 1, 5, 6, 0x03),
            S(5, 2, 1, 0), S(6, 2, 1, 1), LW(7, 1, 4)});
    m.steps(8);
    CHECK_EQ(m.core.x[3], 0xFFFFFF80u);
    CHECK_EQ(m.core.x[4], 0x80u);
    CHECK_EQ(m.core.x[5], 0xFFFF8081u);
    CHECK_EQ(m.core.x[6], 0x8081u);
    CHECK_EQ(m.core.x[7], 0xFF808000u);          // sb 0x80 at byte 5, sh 0xFF80 at bytes 6-7
    auto l = m.lines();
    CHECK_STR(l[1], "2 01000004 00008183 x3=ffffff80 ld 00000100 1 00000080");
    CHECK_STR(l[5], "6 01000014 002082a3 st 00000105 1 00000080");
}

TEST(rv32_memory_aliases_past_32k) {
    M m;
    m.dmem[0] = 0x1234;
    m.prog({LUI(1, 0x8000), LW(2, 1, 0)});
    m.steps(2);
    CHECK_EQ(m.core.x[2], 0x1234u);
    CHECK_EQ(m.core.alias_accesses, 1u);
}

TEST(rv32_misaligned_access_is_ub) {
    M m;
    m.core.x[1] = 0x102;
    m.prog({LW(2, 1, 0)});
    CHECK_EQ(m.step(), Rv32::kNever);
    CHECK(m.core.stop == Stop::ub);
    CHECK_EQ(m.core.pc, 0x01000000u);
    CHECK_STR(m.last(), "1 UB controller: a misaligned 4-byte load from 0x102 at pc 0x1000000");
    CHECK_EQ(m.step(), Rv32::kNever);              // nothing more
    CHECK_EQ(m.core.mcycle, 1u);
}

TEST(rv32_bus_errors) {
    struct Case { uint32_t ins, base, cause; };
    const Case cases[] = {
        {LW(2, 1, 0), 0x30000000, 5},                // unmapped
        {SW(2, 1, 0), 0x01000040, 7},                // program memory
        {SW(2, 1, 0), 0x02000000, 7},                // the boot ROM
        {LW(2, 1, 0), 0x02000100, 5},                // past the boot ROM's words (none loaded here)
        {LW(2, 1, 0), 0xFFFF2400, 5},                // PTR (a 1-cycle table-window load)
    };
    for (int k = 0; k < 5; k++) {
        M m;
        m.core.x[1] = cases[k].base;
        m.prog({cases[k].ins});
        m.step();
        CHECK_EQ(m.core.mcause, cases[k].cause);
        CHECK_EQ(m.core.mtval, cases[k].base);
        CHECK_EQ(m.core.bus_errors, 1u);
        CHECK_EQ(m.env.posts[0].arg, uint32_t(COND_BUS_ERROR));
    }
    M m;                                             // a sub-word load from the table window: bus error, 1 cycle
    m.core.x[1] = 0xFFFF2000;
    m.prog({I(0, 1, 0, 2, 0x03)});
    m.step();
    CHECK_EQ(m.core.mcause, 5u);
}

// ------------------------------------------------------------------------------------------------- the register block
TEST(rv32_block_load_takes_two_cycles_and_samples_in_the_second) {
    M m;
    m.core.x[1] = REG_BASE;
    m.env.regs[STATUS] = 0x10;
    m.prog({LW(2, 1, 4), NOP});
    CHECK_EQ(m.step(), 2u);
    CHECK_EQ(m.core.instructions, 0u);
    CHECK(m.lines().empty());
    m.env.regs[STATUS] = 0x30;                      // what the block shows at the sampling cycle
    m.step();
    CHECK_EQ(m.core.x[2], 0x30u);
    CHECK_STR(m.last(), "2 01000000 0040a103 x2=00000030 ld fffff804 4 00000030");
}

TEST(rv32_block_load_waits_before_its_checks) {
    {   // a sub-word load from the block: the wait, then a bus error
        M m; m.core.x[1] = REG_BASE; m.prog({I(0, 1, 0, 2, 0x03)});
        m.step();
        CHECK_EQ(m.core.traps, 0u);
        m.step();
        CHECK_EQ(m.core.mcause, 5u);
        CHECK_EQ(m.core.mepc, 0x01000000u);
    }
    {   // a reserved load width: the wait, then illegal
        M m; m.core.x[1] = REG_BASE; m.prog({I(0, 1, 3, 2, 0x03)});
        m.step();
        CHECK_EQ(m.core.traps, 0u);
        m.step();
        CHECK_EQ(m.core.mcause, 2u);
    }
    {   // misaligned: the wait, then UB
        M m; m.core.x[1] = REG_BASE + 2; m.prog({LW(2, 1, 0)});
        m.step();
        CHECK(m.core.stop == Stop::running);
        m.step();
        CHECK(m.core.stop == Stop::ub);
    }
    {   // an absent peripheral's window (2): the wait, then a bus error
        M m; m.core.x[1] = REG_BASE + 0x400; m.prog({LW(2, 1, 0)});
        m.steps(2);
        CHECK_EQ(m.core.mcause, 5u);
    }
}

TEST(rv32_block_words) {
    M m;
    m.core.x[1] = REG_BASE; m.core.x[2] = 7;
    m.env.regs[EVENTS] = 0x300; m.env.regs[EVENTS_MASK] = 0x180;
    m.prog({LW(3, 1, 0x18), LW(4, 1, 0x1C), LW(5, 1, 0x0C), LW(6, 1, 0x14), SW(2, 1, 0x14), SW(2, 1, 0x20)});
    m.steps(8);
    CHECK_EQ(m.core.x[3], 0x300u);
    CHECK_EQ(m.env.posts.size(), 1u);                // EVENTS consumes what it returned under the mask: [8]
    CHECK_EQ(m.env.posts[0].op, uint32_t(BUS_CONSUME));
    CHECK_EQ(m.env.posts[0].v, 0x100u);
    CHECK_EQ(m.core.x[4], 0x300u);                   // EVENTS_PEEK: no consume
    CHECK_EQ(m.core.x[5], 0u);                       // PULSE_DONE is write-only: reads 0, no violation
    CHECK_EQ(m.core.x[6], 0u);                       // word 5 reserved: reads 0, flagged
    CHECK_EQ(m.core.reserved_accesses, 1u);
    m.steps(2);
    CHECK_EQ(m.core.reserved_accesses, 2u);          // a store to it: ignored, flagged
    CHECK_EQ(m.env.posts.size(), 2u);
    CHECK_EQ(m.env.posts[1].op, uint32_t(BUS_STORE));
    CHECK_EQ(m.env.posts[1].arg, uint32_t(EVENTS_MASK));
}

TEST(rv32_block_store_faults) {
    {   // a read-only register
        M m; m.core.x[1] = REG_BASE; m.prog({SW(2, 1, 4)});
        m.step();
        CHECK_EQ(m.core.mcause, 7u);
        CHECK_EQ(m.core.mtval, REG_BASE + 4);
    }
    {   // a sub-word store
        M m; m.core.x[1] = REG_BASE; m.prog({S(0x34, 2, 1, 1)});
        m.step();
        CHECK_EQ(m.core.mcause, 7u);
    }
}

TEST(rv32_request_send_refused_is_reexecuted) {
    M m;
    m.core.x[1] = REG_BASE;
    m.core.x[2] = 0xAB;
    m.env.push = false;
    m.prog({SW(2, 1, 0x54)});
    CHECK_EQ(m.step(), 2u);
    CHECK_STR(m.last(), "1 REQ-REFUSED");
    CHECK_EQ(m.core.pc, 0x01000000u);
    CHECK_EQ(m.core.instructions, 0u);
    m.env.push = true;
    m.step();
    CHECK_EQ(m.core.pc, 0x01000004u);
    CHECK_EQ(m.env.posts.size(), 1u);
    CHECK_EQ(m.env.posts[0].arg, uint32_t(REQUEST_SEND));
}

TEST(rv32_sim_exit_halts_after_its_retire_line) {
    M m;
    m.core.x[1] = REG_BASE; m.core.x[2] = 7;
    m.prog({SW(2, 1, 0x1F8)});
    CHECK_EQ(m.step(), Rv32::kNever);
    auto l = m.lines();
    CHECK_EQ(l.size(), 2u);
    CHECK_STR(l[0], "1 01000000 1e20ac23 st fffff9f8 4 00000007");
    CHECK_STR(l[1], "1 HALT 5 pc=01000000");
    CHECK(m.core.stop == Stop::halt);
    CHECK_EQ(m.core.pc, 0x01000004u);
}

// M2: ROUTER_* and the write counters are modelled; PULSE_DMA since G4: a store reaches the block.
TEST(rv32_pulse_dma_store_reaches_the_block) {
    M m; m.core.x[1] = REG_BASE; m.core.x[2] = 1; m.prog({SW(2, 1, 0x10)});
    m.step();
    CHECK_EQ(m.core.pc, 0x01000004u);
    CHECK_EQ(m.env.posts.size(), 1u);
    if (!m.env.posts.empty()) CHECK_EQ(m.env.posts[0].arg, uint32_t(PULSE_DMA));
}
DEATH_TEST(rv32_read_port_needs_a_read_port, "SRAM_RD_ADDR_QUEUE at 0xfffff87c: this bench has no read port") {
    M m; m.core.x[1] = REG_BASE; m.prog({SW(2, 1, 0x7C)});
    m.step();
}
DEATH_TEST(rv32_request_receive_not_modelled, "REQUEST_RECEIVE.*not modelled yet") {
    M m; m.core.x[1] = REG_BASE; m.prog({LW(2, 1, 0x54)});
    m.steps(2);
}
DEATH_TEST(rv32_peripheral_window_not_modelled, "peripheral 1's window at 0xfffffa00: this bench has no peripheral slot")   /* M3's message */ {
    M m; m.core.x[1] = REG_BASE + 0x200; m.prog({LW(2, 1, 0)});
    m.steps(2);
}
DEATH_TEST(rv32_hist_without_a_hist, "HIST in the table window at 0xffff2000: this bench has no HIST") {
    M m; m.core.x[1] = 0xFFFF2000; m.prog({LW(2, 1, 0)});
    m.step();
}
DEATH_TEST(rv32_table_store_without_a_table_store, "table window at 0xffff2800: this bench has no table store") {
    M m; m.core.x[1] = 0xFFFF2800; m.prog({SW(2, 1, 0)});
    m.step();
}

// ------------------------------------------------------------------------------------------------- branches and jumps
TEST(rv32_branch_timing) {
    M m;
    m.core.x[1] = 1;
    m.prog({B(8, 0, 1, 0), B(8, 0, 1, 1), NOP, J(-8, 5)});   // beq not taken; bne taken to +8; jal back to beq
    CHECK_EQ(m.step(), 2u);                         // not taken: no penalty
    CHECK_EQ(m.step(), 4u);                         // taken: +1
    CHECK_EQ(m.core.pc, 0x0100000Cu);
    CHECK_EQ(m.step(), 4u);                         // a step in the stall cycle: just the stall
    CHECK_EQ(m.core.instructions, 2u);
    CHECK_EQ(m.step(), 6u);                         // jal: +1
    CHECK_EQ(m.core.x[5], 0x01000010u);
    CHECK_EQ(m.core.pc, 0x01000004u);
}

TEST(rv32_jalr_clears_bit0_and_misaligned_target_halts) {
    M m;
    m.core.x[1] = 0x01000009;
    m.prog({I(0, 1, 0, 2, 0x67)});
    m.step();
    CHECK_EQ(m.core.pc, 0x01000008u);
    CHECK_EQ(m.core.x[2], 0x01000004u);
    M n;
    n.core.x[1] = 0x01000006;
    n.prog({I(0, 1, 0, 2, 0x67)});
    n.step();
    CHECK(n.core.stop == Stop::halt);
    CHECK_EQ(n.core.halt, 7);
    CHECK_STR(n.last(), "1 HALT 7 pc=01000000");
}

TEST(rv32_halts) {
    M a; a.prog({ECALL}); a.step(); CHECK_EQ(a.core.halt, 2);
    M b; b.prog({EBREAK}); b.step(); CHECK_EQ(b.core.halt, 3);
    M c; c.core.pc = 0x00000100; c.step(); CHECK_EQ(c.core.halt, 6);
    CHECK_STR(c.last(), "1 HALT 6 pc=00000100");
    M d; d.core.pc = 0x02000000; d.step(); CHECK_EQ(d.core.halt, 6);      // no boot ROM words here
}

// ----------------------------------------------------------------------------------------------------------- the CSRs
TEST(rv32_csrs) {
    M m;
    m.core.x[1] = 0xFFFFFFFF;
    m.prog({CSR(1, 0x340, 1, 2), CSR(2, 0x340, 0, 3), CSR(2, 0x300, 1, 4), CSR(2, 0x300, 0, 5), CSR(2, 0x301, 0, 6),
            CSR(1, 0x305, 1, 0), CSR(2, 0x305, 0, 7), CSR(1, 0x304, 1, 0), CSR(2, 0x304, 0, 8), CSR(1, 0x341, 1, 0),
            CSR(2, 0x341, 0, 9), CSR(2, 0xB00, 0, 10)});
    m.steps(12);
    CHECK_EQ(m.core.x[2], 0u);
    CHECK_EQ(m.core.x[3], 0xFFFFFFFFu);
    CHECK_EQ(m.core.x[4], 0x1800u);                 // MPP 3 on read
    CHECK_EQ(m.core.x[5], 0x1888u);                 // MIE | MPIE kept
    CHECK_EQ(m.core.x[6], 0x40001100u);
    CHECK_EQ(m.core.x[7], 0xFFFFFFFCu);             // mtvec: direct only
    CHECK_EQ(m.core.x[8], 0x800u);                  // mie: MEIE only
    CHECK_EQ(m.core.x[9], 0xFFFFFFFCu);             // mepc low bits 0
    CHECK_EQ(m.core.x[10], 12u);                    // mcycle read at t returns t
}

TEST(rv32_mcycle_write_and_mtime) {
    M m;
    m.core.x[1] = 1000;
    m.prog({CSR(1, 0xB00, 1, 0), CSR(2, 0xB00, 0, 2), CSR(2, 0xC01, 0, 3), CSR(2, 0xC81, 0, 4)});
    m.steps(4);
    CHECK_EQ(m.core.x[2], 1001u);                   // written at 1, read at 2: the value + 1
    CHECK_EQ(m.core.x[3], 1002u / 40);
    CHECK_EQ(m.core.x[4], 0u);
    M n;
    n.core.mtvec = 0x01000100;
    n.prog({CSR(1, 0xC01, 1, 2)});                  // a write to mtime: illegal
    n.step();
    CHECK_EQ(n.core.mcause, 2u);
    M k;
    k.prog({CSR(2, 0xC00, 0, 2)});                  // cycle: not implemented, illegal
    k.step();
    CHECK_EQ(k.core.mcause, 2u);
}

TEST(rv32_mip_reads_the_interrupt_term) {
    M m;
    m.env.interrupt = true;
    m.prog({CSR(2, 0x344, 0, 2), CSR(1, 0x344, 1, 3)});
    m.steps(2);
    CHECK_EQ(m.core.x[2], 0x800u);
    CHECK_EQ(m.core.traps, 0u);                    // a write to mip is ignored
}

// ------------------------------------------------------------------------------------------------- traps and interrupts
TEST(rv32_trap_inside_the_handler_halts) {
    M m;
    m.core.mtvec = 0x01000100;
    m.prog({R(1, 0, 0, 4, 1)});
    m.prog({R(1, 0, 0, 4, 1)}, 0x40);
    m.step();
    CHECK(m.core.in_handler);
    m.step();
    CHECK_EQ(m.core.halt, 8);
    CHECK_EQ(m.core.illegals, 2u);                  // counted (ERRORS) before the stop
    CHECK_EQ(m.env.posts.size(), 2u);
    CHECK_STR(m.last(), "2 HALT 8 pc=01000100");
}

TEST(rv32_park_loop_in_the_handler) {
    M m;
    m.core.mtvec = 0x01000100;
    m.prog({R(1, 0, 0, 4, 1)});                     // illegal: to the handler, which parks
    m.prog({J(0, 0)}, 0x40);
    m.step();
    CHECK(m.step() != Rv32::kNever);                // the park loop goes on running (ref's runCycle: only a halt stops it)
    CHECK(m.core.stop == Stop::parked);             // the run's end recorded at the park
    CHECK_EQ(m.core.stop_cycle, 2u);
    const uint32_t pc = m.core.pc;
    const uint64_t n = m.core.instructions;
    for (int i = 0; i < 6; i++) m.step();
    CHECK(m.core.parked && m.core.pc == pc && m.core.instructions > n && m.core.stop_cycle == 2u);
}

TEST(rv32_interrupt_waits_for_a_stall_but_not_a_block_load) {
    {   // after a multiply: taken when the stall ends
        M m;
        m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE; m.core.mtvec = 0x01000100;
        m.prog({R(1, 0, 0, 0, 1), NOP});
        m.step();
        m.env.interrupt = true;
        m.steps(2);
        CHECK_EQ(m.core.traps, 0u);
        m.step();
        CHECK_EQ(m.core.mcause, 0x8000000Bu);
        CHECK_EQ(m.core.mepc, 0x01000004u);
        CHECK_EQ(m.core.mstatus, uint32_t(Rv32::MSTATUS_MPIE));
        CHECK_STR(m.last(), "4 TRAP mcause=8000000b mepc=01000004 mtval=00000000 pc=01000100");
    }
    {   // a block load's first cycle: preempted, mepc the load
        M m;
        m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE; m.core.mtvec = 0x01000100;
        m.core.x[1] = REG_BASE;
        m.prog({LW(2, 1, 0)});
        m.step();
        m.env.interrupt = true;
        m.step();
        CHECK_EQ(m.core.mepc, 0x01000000u);
        CHECK_EQ(m.core.x[2], 0u);
    }
    {   // a refused REQUEST_SEND: preempted
        M m;
        m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE; m.core.mtvec = 0x01000100;
        m.core.x[1] = REG_BASE; m.env.push = false;
        m.prog({SW(2, 1, 0x54)});
        m.step();
        m.env.interrupt = true;
        m.step();
        CHECK_EQ(m.core.mepc, 0x01000000u);
    }
    {   // MIE set at t: pending at t + 1
        M m;
        m.core.mie = Rv32::MEIE; m.core.mtvec = 0x01000100; m.env.interrupt = true;
        m.prog({CSR(6, 0x300, 8, 0), NOP});
        m.step();
        CHECK_EQ(m.core.traps, 0u);
        m.step();
        CHECK_EQ(m.core.traps, 1u);
    }
}

// ------------------------------------------------------------------------------------------------------- WFI and sleep
TEST(rv32_wfi_sleeps_and_wakes) {
    M m;
    m.env.regs[EVENTS] = 1u << EV_GO;
    m.prog({WFI, ADDI(1, 0, 1)});
    CHECK_EQ(m.step(), Rv32::kNever);               // asleep from cycle 2
    CHECK(m.core.in_wfi);
    CHECK_EQ(m.step(), Rv32::kNever);               // a step with no wake changes nothing visible
    m.env.wk = true;
    m.t = 9;
    m.step();                                       // cycle 10: the WAKE line, and the next instruction
    auto l = m.lines();
    CHECK_EQ(l.size(), 3u);
    CHECK_STR(l[1], "10 WAKE go");
    CHECK_STR(l[2], "10 01000004 00100093 x1=00000001");
    CHECK_EQ(m.core.mcycle, 10u);                   // the skipped cycles caught up
    CHECK_EQ(m.core.cycles_wfi, 8u);
}

TEST(rv32_wfi_falls_through_when_the_term_holds) {
    M m;
    m.env.wk = true;
    m.prog({WFI, NOP});
    CHECK_EQ(m.step(), 2u);
    CHECK(!m.core.in_wfi);
}

TEST(rv32_interrupt_in_wfi_traps_without_a_wake_line) {
    M m;
    m.core.mstatus = Rv32::MSTATUS_MIE; m.core.mie = Rv32::MEIE; m.core.mtvec = 0x01000100;
    m.prog({WFI});
    m.step();
    m.env.interrupt = true;
    m.step();
    auto l = m.lines();
    CHECK_EQ(l.size(), 2u);
    CHECK(l[1].find("TRAP mcause=8000000b mepc=01000004") != std::string::npos);
    CHECK(!m.core.in_wfi);
}

TEST(rv32_sleeping_through_a_stall_equals_stepping_it) {
    M a, b;
    for (M* m : {&a, &b}) { m->core.x[1] = 3; m->prog({R(1, 1, 1, 0, 2), B(-4, 0, 1, 1)}); }
    for (int i = 0; i < 20; i++) a.step();          // every cycle
    for (uint64_t next = 1; next <= 20;) {          // only when asked
        b.t = next - 1;
        next = b.step();
    }
    if (b.t < 20) { b.t = 19; b.step(); }           // the last cycle, asked or not
    CHECK_EQ(a.core.mcycle, b.core.mcycle);
    CHECK_EQ(a.core.pc, b.core.pc);
    CHECK_EQ(a.core.instructions, b.core.instructions);
    CHECK_EQ(a.core.cycles_stalled, b.core.cycles_stalled);
    CHECK(a.lines() == b.lines());
}

TEST(rv32_digest_is_the_same_slept_or_stepped) {
    M a, b;
    for (M* m : {&a, &b}) m->prog({R(1, 0, 0, 0, 1), WFI});
    a.steps(20);                                    // every cycle: the multiply's stall, then WFI from cycle 4
    b.step();                                       // only when asked: 1, then 4 (the WFI)
    b.t = 3;
    CHECK_EQ(b.step(), Rv32::kNever);
    CHECK(a.core.mcycle != b.core.mcycle);          // b's counters lag until it next runs ...
    CHECK_EQ(a.core.digest(), b.core.digest());     // ... which the digest accounts for
    M c;
    c.prog({R(1, 0, 0, 0, 1), WFI});
    c.step(); c.t = 3; c.step();
    c.core.x[5] = 1;
    CHECK(c.core.digest() != b.core.digest());
}
