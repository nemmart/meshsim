// test_core_m2.cpp: the core's M2 paths (WORKER8; Controller.java's store/load/window, TileControl.sendLo/Hi/Header):
// a send store's packet on the send holding register, a refused put retried with the core stalled, the table window's
// stores and one-cycle HIST loads, the M2 registers' loads through the block's view. A scripted environment, no framework.
#include <cstdint>
#include <vector>

#include "harness.h"
#include "rv32_asm.h"
#include "units/rv32.h"

using namespace units;
using namespace rv32_asm;

namespace {

struct Packet { uint64_t lo, hdr, hi; };
struct TStore { uint32_t table, i, v; };
struct Env {
    uint32_t regs[64] = {};
    uint32_t hist_mem[256] = {};
    bool refused = false;
    std::vector<Packet> sent;
    std::vector<TStore> tstores;
    std::vector<uint32_t> stores;   // BUS_STORE register numbers
    uint32_t reg(unsigned r) const { return regs[r]; }
    bool wake() const { return false; }
    bool irq() const { return false; }
    bool can_push() const { return true; }
    void post(uint32_t op, uint32_t arg, uint32_t) { if (op == BUS_STORE) stores.push_back(arg); }
    void send(uint64_t lo, uint64_t hdr, uint64_t hi) { sent.push_back({lo, hdr, hi}); }
    bool send_refused() const { return refused; }
    void table_store(uint32_t table, uint32_t i, uint32_t v) { tstores.push_back({table, i, v}); }
    uint32_t hist(uint32_t i) const { return hist_mem[i]; }
};

struct M {
    std::vector<uint32_t> dmem, imem;
    Rv32 core;
    Env env;
    uint64_t t = 0;
    M() : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0) { core.dmem = dmem.data(); core.imem = imem.data(); core.pc = 0x01000000; }
    void prog(std::initializer_list<uint32_t> ins) { uint32_t at = 0; for (uint32_t i : ins) imem[at++] = i; }
    uint64_t step() { return core.step(env, ++t); }
};
constexpr uint32_t hdr_reg(uint32_t dst, uint32_t mode, uint32_t ctr) { return dst << 25 | mode << 22 | ctr << 19; }

}  // namespace

TEST(send_64_packet_from_the_registers_as_they_stand) {
    M m;
    m.env.regs[ROUTER_HDR] = hdr_reg(5, 0, 3); m.env.regs[ROUTER_ADDR] = 0x7BC0; m.env.regs[ROUTER_SET_W0] = 0x11;
    m.core.x[1] = REG_BASE; m.core.x[2] = 0x22;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_64)});
    m.step();
    CHECK_EQ(m.env.sent.size(), size_t(1));
    CHECK_EQ(m.env.sent[0].lo, uint64_t(0x22) << 32 | 0x11);
    CHECK_EQ(m.env.sent[0].hdr, lw::hdr(0, 5, 0, 3, 0x7BC0, lw::SIZE_64));
    CHECK_EQ(m.env.sent[0].hi, uint64_t(0));
    CHECK_EQ(m.env.stores.size(), size_t(1));
    CHECK_EQ(m.env.stores[0], uint32_t(ROUTER_SEND_64));            // the block advances ROUTER_ADDR, clears W0-W2
    CHECK_EQ(m.core.pending_t, uint64_t(1));
}

TEST(send_32_and_128_payload_layout) {
    M m;
    m.env.regs[ROUTER_HDR] = hdr_reg(63, 1, 2); m.env.regs[ROUTER_ADDR] = 0x100;
    m.env.regs[ROUTER_SET_W0] = 0xA0; m.env.regs[ROUTER_SET_W1] = 0xA1; m.env.regs[ROUTER_SET_W2] = 0xA2;
    m.core.x[1] = REG_BASE; m.core.x[2] = 0xBB;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_32), SW(2, 1, 4 * ROUTER_SEND_128)});
    m.step(); m.step();
    CHECK_EQ(m.env.sent.size(), size_t(2));
    CHECK_EQ(m.env.sent[0].lo, uint64_t(0xBB));                     // 32 bits: the store's word alone
    CHECK_EQ(m.env.sent[0].hdr, lw::hdr(0, 63, 1, 2, 0x100, lw::SIZE_32));
    CHECK_EQ(m.env.sent[1].lo, uint64_t(0xA1) << 32 | 0xA0);        // 128 bits: W1 W0 below, the store and W2 above
    CHECK_EQ(m.env.sent[1].hi, uint64_t(0xBB) << 32 | 0xA2);
}

TEST(a_refused_send_is_retried_and_the_core_stalls) {
    M m;
    m.core.x[1] = REG_BASE; m.core.x[2] = 7;
    m.prog({SW(2, 1, 4 * ROUTER_SEND_32), ADDI(3, 0, 1), ADDI(3, 3, 1)});
    m.step();                                                         // t = 1: the send store
    m.env.refused = true;
    CHECK_EQ(m.step(), uint64_t(3));                                  // t = 2: refused: the same packet again, stall
    CHECK_EQ(m.env.sent.size(), size_t(2));
    CHECK_EQ(m.env.sent[1].lo, m.env.sent[0].lo);
    CHECK_EQ(m.env.sent[1].hdr, m.env.sent[0].hdr);
    CHECK_EQ(m.core.pending_t, uint64_t(2));
    CHECK_EQ(m.core.x[3], 0u);
    CHECK_EQ(m.core.stall_send, uint64_t(1));
    m.env.refused = false;
    m.step();                                                         // t = 3: accepted at t = 2: the next instruction
    CHECK_EQ(m.core.x[3], 1u);
    CHECK_EQ(m.env.sent.size(), size_t(2));
}

TEST(table_window_stores_and_hist_loads) {
    M m;
    m.env.hist_mem[5] = 0x1ABCD;
    m.core.x[1] = 0xFFFF2000; m.core.x[2] = 0xFFFFFFFF; m.core.x[6] = 0xFFFF2800; m.core.x[7] = 0xFFFF3400;
    m.prog({SW(2, 1, 0x404), SW(2, 6, 0xC), SW(2, 7, 8 * 63 + 4), LW(4, 1, 0x14), LW(5, 1, 0x404)});   // 12-bit offsets
    for (int i = 0; i < 4; i++) m.step();
    CHECK_EQ(m.env.tstores.size(), size_t(3));
    CHECK(m.env.tstores[0].table == TBL_PTR && m.env.tstores[0].i == 1 && m.env.tstores[0].v == 0x1FFFFFu);   // 21 bits
    CHECK(m.env.tstores[1].table == TBL_STREAM_SRC && m.env.tstores[1].i == 3 && m.env.tstores[1].v == 0xFFFFFFFFu);
    CHECK(m.env.tstores[2].table == TBL_GROUP && m.env.tstores[2].i == 127);
    CHECK_EQ(m.core.x[4], 0x1ABCDu);                                  // a one-cycle load (no register-block latency)
    m.step();                                                         // a PTR load: a bus error, as in ref
    CHECK_EQ(m.core.bus_errors, uint64_t(1));
}

TEST(m2_registers_load_through_the_view) {
    M m;
    m.env.regs[ROUTER_ADDR] = 0x1234; m.env.regs[WRITE_COUNTER_1 + 2] = 77; m.env.regs[DMA_CONFIG] = 0xBCDEF;
    m.core.x[1] = REG_BASE;
    m.prog({LW(2, 1, 4 * ROUTER_ADDR), LW(3, 1, 4 * (WRITE_COUNTER_1 + 2)), LW(4, 1, 4 * DMA_CONFIG)});
    for (int i = 0; i < 6; i++) m.step();                             // each a two-cycle register-block load
    CHECK_EQ(m.core.x[2], 0x1234u);
    CHECK_EQ(m.core.x[3], 77u);
    CHECK_EQ(m.core.x[4], 0xBCDEFu);
}
