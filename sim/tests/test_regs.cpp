// test_regs (WORKER19, P1): the register table behind mesh.h (tools/regs/regtable.h) against the committed files and
// against the simulator's own decode table (src/units/regmap.h), so header, document and simulator can't drift.
//   - fw/include/mesh.h and fw/REGISTERS.md are the generator's output byte for byte (v1.3's, from Registers.java,
//     MeshHeader.java and RegDoc.java; tools/regs/gen_regs rewrites them);
//   - every block word's load and store side, every name, the address map's constants and the event, status and error
//     bits agree between regtable.h and regmap.h.
#include <cstdio>
#include <string>

#include "../tools/regs/regs_gen.h"
#include "harness.h"
#include "units/regmap.h"

namespace {

std::string slurp(const char* path) {
    std::string s;
    FILE* f = fopen(path, "rb");
    if (!f) return "<cannot read>";
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

TEST(mesh_h_is_the_generators_output) {
    const std::string want = regs::mesh_h();
    CHECK(slurp("fw/include/mesh.h") == want);
}

TEST(registers_md_is_the_generators_output) {
    const std::string want = regs::registers_md();
    CHECK(slurp("fw/REGISTERS.md") == want);
}

TEST(every_block_word_agrees_with_regmap) {
    for (int i = 0; i < (int)regs::BLOCK_WORDS; i++) {
        const regs::Reg* st = regs::store_at(i);
        const regs::Reg* ld = regs::load_at(i);
        const units::RegInfo& m = units::reg_info(unsigned(i));
        CHECK_EQ(st != nullptr, m.store != units::A_NONE);
        CHECK_EQ(ld != nullptr, m.load != units::A_NONE);
        CHECK_EQ(regs::reserved_word(i), units::reserved_word(unsigned(i)));
        std::string name = !st && !ld ? "" : st && ld && st != ld ? std::string(st->name) + "/" + ld->name
                                                                  : std::string(st ? st->name : ld->name);
        CHECK(name == std::string(m.name ? m.name : ""));
    }
}

TEST(register_indices_agree_with_regmap) {
    struct { const char* name; unsigned idx; } named[] = {
        {"PARAMS", units::PARAMS}, {"STATUS", units::STATUS}, {"ERRORS", units::ERRORS},
        {"PULSE_DONE", units::PULSE_DONE}, {"PULSE_DMA", units::PULSE_DMA}, {"EVENTS", units::EVENTS},
        {"EVENTS_PEEK", units::EVENTS_PEEK}, {"EVENTS_MASK", units::EVENTS_MASK},
        {"EVENTS_REQUIRED", units::EVENTS_REQUIRED}, {"ROUTER_HDR", units::ROUTER_HDR},
        {"ROUTER_ADDR", units::ROUTER_ADDR}, {"ROUTER_SET_W0", units::ROUTER_SET_W0},
        {"ROUTER_SET_W1", units::ROUTER_SET_W1}, {"ROUTER_SET_W2", units::ROUTER_SET_W2},
        {"ROUTER_SEND_32", units::ROUTER_SEND_32}, {"ROUTER_SEND_64", units::ROUTER_SEND_64},
        {"ROUTER_SEND_128", units::ROUTER_SEND_128}, {"REQUEST_SET_W0", units::REQUEST_SET_W0},
        {"REQUEST_SET_W1", units::REQUEST_SET_W1}, {"REQUEST_SET_W2", units::REQUEST_SET_W2},
        {"REQUEST_SEND", units::REQUEST_SEND}, {"REQUEST_GET_W0", units::REQUEST_GET_W0},
        {"REQUEST_GET_W1", units::REQUEST_GET_W1}, {"REQUEST_GET_W2", units::REQUEST_GET_W2},
        {"REQUEST_RECEIVE", units::REQUEST_RECEIVE}, {"DMA_CONFIG", units::DMA_CONFIG},
        {"DMA_RECORD_BASE_ADDRESS", units::DMA_RECORD_BASE_ADDRESS}, {"WRITE_COUNTER_1", units::WRITE_COUNTER_1},
        {"WRITE_COUNTER_7", units::WRITE_COUNTER_7}, {"SRAM_RD_ADDR_QUEUE", units::SRAM_RD_ADDR_QUEUE},
        {"SRAM_RD_DEST_ADDR", units::SRAM_RD_DEST_ADDR}, {"SIM_EXIT", units::SIM_EXIT}, {"TRACE", units::TRACE},
    };
    for (const auto& n : named) CHECK_EQ(unsigned(regs::by_name(n.name).index), n.idx);
}

TEST(address_map_and_bits_agree_with_regmap) {
    CHECK_EQ(regs::DMEM_SIZE, uint32_t(units::MEM_BYTES));
    CHECK_EQ(regs::PMEM_SIZE, uint32_t(units::MEM_BYTES));
    CHECK_EQ(regs::BOOTROM_BASE, uint32_t(units::BOOTROM_BASE));
    CHECK_EQ(regs::DMEM_FIRST, uint32_t(units::DMEM_FIRST));
    CHECK_EQ(regs::TABLE_BASE, uint32_t(units::TABLE_BASE));
    CHECK_EQ(regs::PTR_BASE, uint32_t(units::PTR_BASE));
    CHECK_EQ(regs::TABLE_END, uint32_t(units::TABLE_END));
    CHECK_EQ(regs::REG_BASE, uint32_t(units::REG_BASE));
    CHECK_EQ(regs::BLOCK_WORDS, uint32_t(units::BLOCK_WORDS));
    CHECK_EQ(regs::window_base(1), uint32_t(units::WINDOW1_BASE));
    CHECK_EQ(regs::PMEM_BASE >> 24, uint32_t(units::R_PMEM));
    CHECK_EQ(regs::BOOTROM_BASE >> 24, uint32_t(units::R_BOOTROM));
    CHECK_EQ(uint32_t(regs::EV_COUNTERS_DONE), uint32_t(units::EV_COUNTERS_DONE));
    CHECK_EQ(uint32_t(regs::EV_GO), uint32_t(units::EV_GO));
    CHECK_EQ(uint32_t(regs::EV_COMMAND), uint32_t(units::EV_COMMAND));
    CHECK_EQ(uint32_t(regs::EV_RUN_DONE), uint32_t(units::EV_RUN_DONE));
    CHECK_EQ(uint32_t(regs::EV_REQUEST_ROOM), uint32_t(units::EV_REQUEST_ROOM));
    CHECK_EQ(uint32_t(regs::EV_REQUEST_PENDING), uint32_t(units::EV_REQUEST_PENDING));
    CHECK_EQ(uint32_t(regs::EV_PERIPH_1), uint32_t(units::EV_PERIPH_1));
    CHECK_EQ(regs::EVENTS_VALID, uint32_t(units::EVENTS_VALID));
    CHECK_EQ(regs::EVENTS_LIVE, uint32_t(units::EVENTS_LIVE));
    CHECK_EQ(regs::EVENTS_MASK_RESET, uint32_t(units::EVENTS_MASK_RESET));
    const uint32_t st[] = {units::ST_RD_RUNNING, units::ST_RD_DONE, units::ST_GATHER_BUSY, units::ST_SEND_BUSY,
                           units::ST_DONE, units::ST_LANDED, units::ST_REQUEST_BUSY};
    for (int s = 0; s < regs::N_ST; s++) CHECK_EQ(st[s], uint32_t(s));
    CHECK(std::string(regs::COND_NAME[units::COND_BUS_ERROR]) == "bus error");
    CHECK(std::string(regs::COND_NAME[units::COND_ILLEGAL]) == "illegal instruction");
    CHECK_EQ(uint32_t(regs::COND_RECV_UNDERFLOW), uint32_t(units::COND_RECV_UNDERFLOW));
    const regs::Field &periph = regs::field("PARAMS", "PERIPH_PRESENT"), &id = regs::field("PARAMS", "ID"),
                      &size = regs::field("PARAMS", "SIZE");
    const uint32_t p = units::params_value(5, 37, 0x100000);   // 4 MB: size field 15
    CHECK_EQ((p >> periph.lo) & periph.mask(), 5u);
    CHECK_EQ((p >> id.lo) & id.mask(), 37u);
    CHECK_EQ((p >> size.lo) & size.mask(), 15u);
}

}  // namespace
