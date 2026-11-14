// test_core_m2_rtl.cpp: the M2 ports of the tile's environment (rv32.h's header: send, send_refused, table_store, hist,
// and the M2 registers through reg()) under the RTL's core (v135_platform off): a send store's packet on the send
// holding register, a refused put retried with the core frozen, the table window's stores and one-cycle HIST loads,
// the M2 registers' loads through the block's view. A scripted environment, no framework.
//
// The cycles (rv32.h's: step(env, 1) is the first after reset) rest on:
//   (R0) the first instruction is in execute in cycle 4 and retires in 5, and straight-line code retires one a cycle
//        (the core specification's §1, Pipeline); a load or store is raised in the cycle q its instruction leaves
//        execute (rv32.h's header), 4 for the first;
//   (data) "one outstanding operation. The next request may be raised in the cycle the previous response arrives" (§1,
//        Data);
//   (bus) the register bus's timing (Snarky One's rtl/blocks/register_bus/README.md, "Timing"), for a request of
//        cycle t: "A table load or store | in t, from the request | t + 1"; "A register-window load | in t + 1 |
//        t + 2"; "A store that may stall (`WRITE_SEND_32`, `_64`, `_128`, `REQUEST_SEND` ...) | from t + 1, until its
//        owner's ready | the cycle after its owner takes it" (the tile's ROUTER_SEND_* here).
#include <cstdint>
#include <string>
#include <vector>

#include "harness.h"
#include "rv32_asm.h"
#include "units/rv32.h"

using namespace units;
using namespace rv32_asm;

namespace {

struct Packet { uint64_t t, lo, hdr, hi; };
struct TStore { uint64_t t; uint32_t table, i, v; };
struct Env {
    uint32_t regs[64] = {};
    uint32_t hist_mem[256] = {};
    bool refused = false;
    uint64_t now = 0;
    std::vector<Packet> sent;
    std::vector<TStore> tstores;
    std::vector<uint32_t> stores;   // BUS_STORE register numbers
    uint32_t reg(unsigned r) const { return regs[r]; }
    bool wake() const { return false; }
    bool irq() const { return false; }
    bool can_push() const { return true; }
    void post(uint32_t op, uint32_t arg, uint32_t) { if (op == BUS_STORE) stores.push_back(arg); }
    void send(uint64_t lo, uint64_t hdr, uint64_t hi) { sent.push_back({now, lo, hdr, hi}); }
    bool send_refused() const { return refused; }
    void table_store(uint32_t table, uint32_t i, uint32_t v) { tstores.push_back({now, table, i, v}); }
    uint32_t hist(uint32_t i) const { return hist_mem[i]; }
};

constexpr uint32_t PMEM = 0x01000000;
struct M {
    std::vector<uint32_t> dmem, imem;
    TraceBuffer tb;
    Rv32 core;
    Env env;
    uint64_t t = 0;
    M() : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0), tb(1024) {
        core.v135_platform = false;
        core.dmem = dmem.data(); core.imem = imem.data(); core.log = &tb.log;
        core.pc = PMEM; core.mtvec = PMEM + 0x100;
    }
    void prog(std::initializer_list<uint32_t> ins) { uint32_t at = 0; for (uint32_t i : ins) imem[at++] = i; }
    uint64_t step() { env.now = ++t; return core.step(env, t); }
    void run_to(uint64_t cycle) { while (t < cycle) step(); }
    uint64_t retired(uint32_t pc) const {                    // the cycle the instruction at pc retires in; 0: it did not
        for (uint32_t i = 0; i < tb.log.n; i++)
            if (tb.log.rec[i].kind == T_RETIRE && tb.log.rec[i].w[0] == pc) return tb.log.rec[i].cycle;
        return 0;
    }
};
constexpr uint32_t hdr_reg(uint32_t dst, uint32_t mode, uint32_t ctr) { return dst << 25 | mode << 22 | ctr << 19; }

}  // namespace

// (bus): the send store, raised in 4, is taken in 5 (the packet on the holding register, the block's BUS_STORE) and
// answered in 6.
TEST(send_64_packet_from_the_registers_as_they_stand) {
    M m;
    m.env.regs[ROUTER_HDR] = hdr_reg(5, 0, 3); m.env.regs[ROUTER_ADDR] = 0x7BC0; m.env.regs[ROUTER_SET_W0] = 0x11;
    m.core.x[1] = REG_BASE; m.core.x[2] = 0x22;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_64), NOP});
    m.run_to(4);
    CHECK_EQ(m.env.sent.size(), size_t(0));
    m.env.regs[ROUTER_SET_W0] = 0x12;                                // the registers as they stand in cycle 5
    m.step();
    CHECK_EQ(m.env.sent.size(), size_t(1));
    CHECK_EQ(m.env.sent[0].t, uint64_t(5));
    CHECK_EQ(m.env.sent[0].lo, uint64_t(0x22) << 32 | 0x12);
    CHECK_EQ(m.env.sent[0].hdr, lw::hdr(0, 5, 0, 3, 0x7BC0, lw::SIZE_64));
    CHECK_EQ(m.env.sent[0].hi, uint64_t(0));
    CHECK_EQ(m.env.stores.size(), size_t(1));
    CHECK_EQ(m.env.stores[0], uint32_t(ROUTER_SEND_64));            // the block advances ROUTER_ADDR, clears W0-W2
    CHECK_EQ(m.core.pending_t, uint64_t(5));
    CHECK_EQ(m.core.instructions, uint64_t(0));
    m.run_to(7);
    CHECK_EQ(m.retired(PMEM), uint64_t(6));
    CHECK_EQ(m.retired(PMEM + 4), uint64_t(7));
    CHECK_EQ(m.env.sent.size(), size_t(1));
    CHECK_EQ(m.core.sends, uint64_t(1));
}

// (data), (bus): the second send is raised in 6, as the first is answered, taken in 7 and answered in 8.
TEST(send_32_and_128_payload_layout) {
    M m;
    m.env.regs[ROUTER_HDR] = hdr_reg(63, 1, 2); m.env.regs[ROUTER_ADDR] = 0x100;
    m.env.regs[ROUTER_SET_W0] = 0xA0; m.env.regs[ROUTER_SET_W1] = 0xA1; m.env.regs[ROUTER_SET_W2] = 0xA2;
    m.core.x[1] = REG_BASE; m.core.x[2] = 0xBB;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_32), SW(2, 1, 4 * ROUTER_SEND_128)});
    m.run_to(8);
    CHECK_EQ(m.env.sent.size(), size_t(2));
    CHECK_EQ(m.env.sent[0].t, uint64_t(5));
    CHECK_EQ(m.env.sent[0].lo, uint64_t(0xBB));                     // 32 bits: the store's word alone
    CHECK_EQ(m.env.sent[0].hdr, lw::hdr(0, 63, 1, 2, 0x100, lw::SIZE_32));
    CHECK_EQ(m.env.sent[1].t, uint64_t(7));
    CHECK_EQ(m.env.sent[1].lo, uint64_t(0xA1) << 32 | 0xA0);        // 128 bits: W1 W0 below, the store and W2 above
    CHECK_EQ(m.env.sent[1].hi, uint64_t(0xBB) << 32 | 0xA2);
    CHECK_EQ(m.retired(PMEM), uint64_t(6));
    CHECK_EQ(m.retired(PMEM + 4), uint64_t(8));
}

// rv32.h's header: "send_refused(): the send put of t - 1 was refused: the core puts the same packet again and
// stalls"; step: "nothing moves in t". The put of 5 refused: the packet again in 6, a cycle in which nothing moves, so
// the store is answered in 7 instead of 6, and the instructions behind it retire in 8 and 9 instead of 7 and 8.
TEST(a_refused_send_is_retried_and_the_core_stalls) {
    M m;
    m.core.x[1] = REG_BASE; m.core.x[2] = 7;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_32), ADDI(3, 0, 1), ADDI(3, 3, 1)});
    m.run_to(5);                                                      // t = 5: the send store's put
    CHECK_EQ(m.env.sent.size(), size_t(1));
    m.env.refused = true;
    CHECK_EQ(m.step(), uint64_t(7));                                  // t = 6: refused: the same packet again, stall
    CHECK_EQ(m.env.sent.size(), size_t(2));
    CHECK_EQ(m.env.sent[1].t, uint64_t(6));
    CHECK_EQ(m.env.sent[1].lo, m.env.sent[0].lo);
    CHECK_EQ(m.env.sent[1].hdr, m.env.sent[0].hdr);
    CHECK_EQ(m.core.pending_t, uint64_t(6));
    CHECK_EQ(m.core.instructions, uint64_t(0));
    CHECK_EQ(m.core.stall_send, uint64_t(1));
    m.env.refused = false;
    m.run_to(9);                                                      // accepted at t = 6
    CHECK_EQ(m.retired(PMEM), uint64_t(7));
    CHECK_EQ(m.retired(PMEM + 4), uint64_t(8));
    CHECK_EQ(m.retired(PMEM + 8), uint64_t(9));
    CHECK_EQ(m.core.x[3], 2u);
    CHECK_EQ(m.env.sent.size(), size_t(2));
    CHECK_EQ(m.env.stores.size(), size_t(1));                         // the block's store once
    CHECK_EQ(m.core.mcycle, uint64_t(9));                             // the frozen cycle is a cycle (§1: mcycle "counts every cycle")
}

// (bus): table accesses are made in the cycle they are raised in and answered in the next: the three stores are made
// in 4, 5 and 6 and retire in 5, 6 and 7; the HIST load is made in 7 and retires in 8; the PTR load (a bus error in
// v1.35's map) traps in 9.
TEST(table_window_stores_and_hist_loads) {
    M m;
    m.env.hist_mem[5] = 0x1ABCD;
    m.core.x[1] = 0xFFFF2000; m.core.x[2] = 0xFFFFFFFF; m.core.x[6] = 0xFFFF2800; m.core.x[7] = 0xFFFF3400;
    m.prog({SW(2, 1, 0x404), SW(2, 6, 0xC), SW(2, 7, 8 * 63 + 4), LW(4, 1, 0x14), LW(5, 1, 0x404)});   // 12-bit offsets
    m.run_to(8);
    CHECK_EQ(m.env.tstores.size(), size_t(3));
    CHECK(m.env.tstores[0].table == TBL_PTR && m.env.tstores[0].i == 1 && m.env.tstores[0].v == 0x1FFFFFu);   // 21 bits
    CHECK(m.env.tstores[1].table == TBL_STREAM_SRC && m.env.tstores[1].i == 3 && m.env.tstores[1].v == 0xFFFFFFFFu);
    CHECK(m.env.tstores[2].table == TBL_GROUP && m.env.tstores[2].i == 127);
    for (uint32_t k = 0; k < 3; k++) {
        CHECK_EQ(m.env.tstores[k].t, uint64_t(4 + k));
        CHECK_EQ(m.retired(PMEM + 4 * k), uint64_t(5 + k));
    }
    CHECK_EQ(m.retired(PMEM + 0xC), uint64_t(8));                     // a one-cycle load (no register-block latency)
    CHECK_EQ(m.core.x[4], 0x1ABCDu);
    CHECK_EQ(m.core.bus_errors, uint64_t(0));
    m.step();                                                         // a PTR load: a bus error, as in ref
    CHECK_EQ(m.core.bus_errors, uint64_t(1));
    CHECK_EQ(m.core.mcause, 5u);
    CHECK_EQ(m.core.mtval, 0xFFFF2404u);
    CHECK_EQ(m.core.trap_at, int64_t(9));
}

// (bus), (data): each a two-cycle register-block load, the next raised as the last is answered: they retire in 6, 8
// and 10, each returning the view of the cycle after its request (5, 7 and 9).
TEST(m2_registers_load_through_the_view) {
    M m;
    m.env.regs[ROUTER_ADDR] = 0x1234; m.env.regs[WRITE_COUNTER_1 + 2] = 77; m.env.regs[DMA_CONFIG] = 0xBCDEF;
    m.core.x[1] = REG_BASE;
    m.prog({LW(2, 1, 4 * ROUTER_ADDR), LW(3, 1, 4 * (WRITE_COUNTER_1 + 2)), LW(4, 1, 4 * DMA_CONFIG)});
    m.run_to(6);
    m.env.regs[WRITE_COUNTER_1 + 2] = 78;                             // cycle 7's view
    m.step();
    m.env.regs[WRITE_COUNTER_1 + 2] = 79;                             // cycle 8's
    m.run_to(10);
    CHECK_EQ(m.retired(PMEM), uint64_t(6));
    CHECK_EQ(m.retired(PMEM + 4), uint64_t(8));
    CHECK_EQ(m.retired(PMEM + 8), uint64_t(10));
    CHECK_EQ(m.core.x[2], 0x1234u);
    CHECK_EQ(m.core.x[3], 78u);
    CHECK_EQ(m.core.x[4], 0xBCDEFu);
}

// SIM_EXIT's store (v1.35's register, which ends a run): a register-window store that cannot stall, raised in 4, made
// and answered in 5 (register_bus: "A register-window store | in t + 1 | t + 1"): it retires there, once, and the run
// ends with it.
TEST(sim_exit_store_retires_once_and_halts) {
    M m;
    m.core.x[1] = REG_BASE; m.core.x[2] = 7;
    m.prog({SW(2, 1, 4 * SIM_EXIT), NOP});
    m.run_to(5);
    CHECK_EQ(m.core.halt, int(Rv32::HALT_EXIT));
    CHECK(m.core.stop == Stop::halt);
    CHECK_EQ(m.core.stop_cycle, uint64_t(5));
    CHECK_EQ(m.core.instructions, uint64_t(1));
    uint32_t retires = 0;
    for (uint32_t i = 0; i < m.tb.log.n; i++) retires += m.tb.log.rec[i].kind == T_RETIRE;
    CHECK_EQ(retires, 1u);
    const std::vector<std::string> lines = format_body(m.tb.log);
    CHECK_EQ(lines.size(), size_t(2));
    CHECK(lines[1].find("HALT 5") != std::string::npos);     // the retire line, then HALT
    CHECK_EQ(m.step(), uint64_t(Rv32::kNever));              // nothing more
}
