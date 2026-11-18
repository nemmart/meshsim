// test_rtl_stream: the master tile's UART on the chip, through the master's core (what rtl/tile/master/test/src/
// master_tile_test.rs's part 6 adds to the unit's and the chip's tests), on v1.37's UART (17.6.9). The far end, the
// serial line's other end, is not chip hardware: tests/ie_far_end.h. v1.37's stage 3 removed v1.36's LVDS stream, and
// with it the RTL's chip test of the host interface (chip_stream_test.rs) that this file ran.
//
// The chip is tests/mach.h's (MachConfig::chip()). As the RTL's tests do, the test places a program at the master's
// IMEM base and its data in DMEM (the master's boot strap, machine-spec 6.5) and the memory tile's firmware
// (fw/images/cc/memory.elf, fw/cc/memory.c) in its own, before the first cycle; the program is written here with the
// simulator's assembler helper (tests/rv32_asm.h).
//
// CYCLES. Cycle 0 is the first cycle after the reset, as the RTL bench's count (the simulator's cycle 1). The far end
// is the RTL bench's own, cycle for cycle. The cycles asserted are the RTL chip's for v1.36's program with the same far
// end (Snarky One at 076e647; OPEN-v1.36.md, "Where the chip's reference figures came from"), moved by what v1.37's
// program changes, as the test says.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "harness.h"
#include "ie_far_end.h"
#include "mach.h"
#include "rtl_images.h"
#include "rv32_asm.h"
#include "units/ingress_egress.h"
#include "units/periph_slot.h"
#include "units/regmap.h"

namespace {

using namespace rv32_asm;
using Pins = units::IngressEgress::Pins;

// ---- verif::rv32i's encoders the programs use, on rv32_asm's fields ----
uint32_t addi(uint32_t rd, uint32_t rs1, int32_t imm) { return ADDI(rd, rs1, imm); }
uint32_t andi(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 7, rd); }
uint32_t lui(uint32_t rd, uint32_t upper) { return LUI(rd, upper << 12); }
uint32_t lw(uint32_t rd, int32_t offset, uint32_t rs1) { return LW(rd, rs1, offset); }
uint32_t sw(uint32_t rs2, int32_t offset, uint32_t rs1) { return SW(rs2, rs1, offset); }
uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t offset) { return B(offset, rs2, rs1, 0); }
uint32_t bne(uint32_t rs1, uint32_t rs2, int32_t offset) { return B(offset, rs2, rs1, 1); }
uint32_t jal(uint32_t rd, int32_t offset) { return J(offset, rd); }

// ---- region 0xFF's registers (5.5, 17.1, 17.6.1), each as an offset from x0 ----
constexpr int32_t IE = -0x600;                             // Ingress/Egress, peripheral 1, at 0xFFFF_FA00
constexpr int32_t IE_STATUS = IE + 0x004;
constexpr int32_t UART_TX = IE + 0x078, UART_RX = IE + 0x07C, UART_CONTROL = IE + 0x0A8;   // v1.37 (17.6.1)
constexpr uint32_t UART_ON = 1u << 16 | 1u << 17;          // UART_CONTROL's enable_read and enable_write (17.6.9)

constexpr uint32_t DONE = 0x444F4E45;                      // bench.rs's done word
constexpr uint32_t DONE_ADDRESS = 0x200;

struct Program {
    std::vector<uint32_t> words;
    void as(uint32_t instruction) { words.push_back(instruction); }
    int32_t here() const { return 4 * int32_t(words.size()); }
    void li(uint32_t rd, uint32_t value) {
        const int32_t low = int32_t(value << 20) >> 20;
        const uint32_t high = (value - uint32_t(low)) >> 12;
        if (high == 0) { as(addi(rd, 0, low)); return; }
        as(lui(rd, high));
        if (low != 0) as(addi(rd, rd, low));
    }
};

// The chip with the master's program and data and the memory tile's firmware placed, and a far end attached.
struct Bench {
    mach::Mach m;
    IeFarEnd far;
    units::IngressEgress& ie;
    uint64_t cycle = 0;                                     // the cycles run
    Bench() : m(mach::Mach::chip_cfg(-1)), ie(m.c.unit<units::PeriphSlot>("cc1.slot").s.ingress_egress(1)) {
        far.attach(ie);
        far.origin = 1;                                     // cycle 0 is the first after the reset (header, CYCLES)
        m.c.touched("cc1.slot");
        m.place_elf(units::ID_MEMORY, rtl_images::fw_images() + "cc/memory.elf");
    }
    void run_cycle() { m.c.run(1); cycle++; }
    bool done() const { return m.word(units::ID_MASTER, DONE_ADDRESS) == DONE; }
};

}  // namespace

// The UART through the master's core, on the chip (what master_tile_test.rs's part 6 adds: bytes stored to UART_TX
// back to back, more than its FIFO holds, so that the later stores stall on the reg interface's ready; then bytes
// received, each polled in UART_RX, whose load pops it). The program: UART_CONTROL with a divider of 20 and both
// enables (8,681, 115,200 baud, would make a character 86,810 cycles; v1.37: the UART is off at the reset, so the
// store also turns it on); 20 bytes, 0x41 up, each stored to UART_TX and then to LED, whose pin, the byte's bit 0, so
// changes once each store is taken; STATUS then, stored to DMEM; four bytes polled in UART_RX until `valid`, each
// stored to DMEM; STATUS again; the done word. The line's far end sends four frames of 20 cycles a
// bit, their falling edges on uart_rx in cycles 4200, 4400 (back to back with the first), 4700 and 5000.
// Checked from 17.6: the 20 bytes on uart_tx in order, every bit of every frame 20 cycles (the decoder), the frames
// back to back, 200 cycles apart; the first 17 stores taken at the core's pace (one on the line, 16 in the FIFO) and
// the last three stalled, each taken only after a frame's end has made room, 200 cycles apart; STATUS after the
// stores, 16 characters in the TX FIFO ([12:8]); each byte received with `valid`; STATUS at the end, both FIFOs empty
// and no line error. And the cycles (header, CYCLES): the first start bit's cycle, every change of the led pin (so
// each store's cycle, the stalled ones' the cycle after their frame's pop made room), the cycles after which the first
// STATUS is in DMEM, and the done word's.
TEST(chip_uart_through_the_core_stalled_stores_and_polled_bytes_at_the_rtl_chips_cycles) {
    constexpr uint32_t D = 20;
    constexpr int32_t LED = IE + 0x0A4;
    Program p;
    p.li(5, UART_ON | D);                                   // v1.36: li 5, 20, one instruction; here lui and addi
    p.as(sw(5, UART_CONTROL, 0));
    p.li(6, 0x41);
    p.li(7, 20);
    const int32_t top = p.here();
    p.as(sw(6, UART_TX, 0));
    p.as(sw(6, LED, 0));
    p.as(addi(6, 6, 1));
    p.as(addi(7, 7, -1));
    p.as(bne(7, 0, top - p.here()));
    p.as(lw(8, IE_STATUS, 0));
    p.as(sw(8, 0x100, 0));
    p.li(9, 0x104);
    p.li(10, 4);
    const int32_t rx = p.here();
    p.as(lw(11, UART_RX, 0));
    p.as(andi(12, 11, 0x100));
    p.as(beq(12, 0, rx - p.here()));
    p.as(sw(11, 0, 9));
    p.as(addi(9, 9, 4));
    p.as(addi(10, 10, -1));
    p.as(bne(10, 0, rx - p.here()));
    p.as(lw(8, IE_STATUS, 0));
    p.as(sw(8, 0x114, 0));
    p.li(5, DONE);
    p.li(6, DONE_ADDRESS);
    p.as(sw(5, 0, 6));
    p.as(jal(0, 0));
    // v1.36's program, the one the RTL chip ran, as the RTL's encoders gave it, was these words with 0x1400293 (li 5, 20)
    // first; v1.37's has lui and addi there, and the UART's words at their v1.37 offsets
    CHECK_EQ(p.words.size(), size_t(28));
    CHECK_EQ(p.words[0], LUI(5, 0x30000u));
    CHECK_EQ(p.words[1], ADDI(5, 5, 20));
    Bench b;
    b.m.place(units::ID_MASTER, units::IMEM_BASE, p.words);
    b.m.place(units::ID_MASTER, 0x100, {0, 0, 0, 0, 0, 0, 0, 0});
    b.m.place(units::ID_MASTER, DONE_ADDRESS, {0});
    static const struct { int64_t start; uint8_t byte; } kFrames[] = {{4200, 0x5A}, {4400, 0xA5}, {4700, 0x01}, {5000, 0xFE}};
    for (const auto& f : kFrames) b.far.frame(f.start, f.byte, D);

    uint64_t status_after = 0;
    while (b.cycle < 8000 && !b.done()) {
        b.run_cycle();
        if (status_after == 0 && b.m.word(units::ID_MASTER, 0x100) != 0) status_after = b.cycle;
    }
    CHECK(b.done());
    // ---- the results ----
    CHECK_EQ(b.m.word(units::ID_MASTER, 0x100), 16u << 8);             // STATUS after the 20 stores: the TX FIFO full
    for (unsigned i = 0; i < 4; i++) CHECK_EQ(b.m.word(units::ID_MASTER, 0x104 + 4 * i), 0x100u | kFrames[i].byte);
    CHECK_EQ(b.m.word(units::ID_MASTER, 0x114), 0u);                   // at the end: both FIFOs empty, no line error, no word queued
    // ---- the line out ----
    std::string error;
    const std::vector<IeFarEnd::TxFrame> frames = b.far.decode_tx(0, int64_t(b.cycle), [](int64_t) { return D; }, error);
    CHECK(error.empty());
    if (!error.empty()) std::printf("test_rtl_stream: uart_tx: %s\n", error.c_str());
    CHECK_EQ(frames.size(), size_t(20));
    for (size_t k = 0; k < frames.size(); k++) {
        CHECK_EQ(unsigned(frames[k].byte), unsigned(0x41 + k));
        if (k > 0) CHECK_EQ(frames[k].start, frames[k - 1].start + 10 * D);   // back to back
    }
    CHECK(std::string(b.ie.serial_out, 20) == "ABCDEFGHIJKLMNOPQRST");   // the model's log of the bytes sent
    CHECK_EQ(b.ie.serial_n, 20u);
    // ---- the led pin's changes: each store's cycle ----
    std::vector<int64_t> led;
    for (int64_t t = 1; t < int64_t(b.cycle); t++) if (b.far.at(t).led != b.far.at(t - 1).led) led.push_back(t);
    CHECK_EQ(led.size(), size_t(20));
    for (size_t k = 18; k < led.size(); k++) CHECK_EQ(led[k], led[k - 1] + 10 * D);   // the stalled stores (the 18th on), a frame apart (17.6)
    std::printf("test_rtl_stream: chip_uart: the first start bit in cycle %lld; STATUS in DMEM after %llu cycles; done after %llu; led at",
                frames.empty() ? -1ll : (long long)frames[0].start, (unsigned long long)status_after, (unsigned long long)b.cycle);
    for (int64_t t : led) std::printf(" %lld", (long long)t);
    std::printf("\n");
    // ---- the RTL chip's cycles (chip_uart_ref, a scratch test of the RTL chip with v1.36's program and this line:
    // uart_tx falls first in cycle 34; led's changes; "status word after 643 cycles"; "done after 5210 cycles"), each one
    // cycle later here: v1.37's program has one instruction more before its first store (the setting is lui and addi),
    // and every figure follows the program; none moves by more ----
    constexpr int64_t kLater = 1;
    if (!frames.empty()) CHECK_EQ(frames[0].start, int64_t(34) + kLater);
    static const int64_t kLed[20] = {35, 44, 53, 62, 71, 80, 89, 98, 107, 116, 125, 134, 143, 152, 161, 170, 179, 237, 437, 637};
    for (size_t k = 0; k < led.size() && k < 20; k++) CHECK_EQ(led[k], kLed[k] + kLater);
    CHECK_EQ(status_after, uint64_t(643 + kLater));
    CHECK_EQ(b.cycle, uint64_t(5210 + kLater));
}
