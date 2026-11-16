// test_rtl_stream: the master tile's external IO on v1.36's chip, through the master's core: the RTL's chip test of the
// host interface (Snarky One's rtl/chip/test/src/chip_stream_test.rs, with its bench.rs) on the simulator, and the
// UART through the core (what rtl/tile/master/test/src/master_tile_test.rs's part 6 adds to the unit's and the chip's
// tests). The far ends, the LVDS IO block and the serial line's other end, are not chip hardware: tests/ie_far_end.h.
//
// The chip is tests/mach.h's (MachConfig::chip()). As the RTL's tests do, each test places a program at the master's
// IMEM base and its data in DMEM (the master's boot strap, machine-spec 6.5) and the memory tile's firmware
// (fw/images/cc/memory.elf, fw/cc/memory.c) in its own, before the first cycle; the program is written here with the
// simulator's assembler helper (tests/rv32_asm.h), instruction for instruction the RTL test's (verif::rv32i's).
//
// CYCLES. Cycle 0 is the first cycle after the reset, as the RTL bench's count (the simulator's cycle 1). The far ends here are the RTL
// bench's own, cycle for cycle (the stream test's from the same xorshift32 sequence, verif::Random), so the chip must
// do in every cycle what the RTL's chip does: the cycles asserted below are the RTL chip's, taken from runs of its
// model with the same far end (Snarky One at 076e647; OPEN-v1.36.md, "Where the chip's reference figures came from").
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
uint32_t xori(uint32_t rd, uint32_t rs1, int32_t imm) { return I(imm, rs1, 4, rd); }
uint32_t srli(uint32_t rd, uint32_t rs1, uint32_t shamt) { return I(int32_t(shamt), rs1, 5, rd); }
uint32_t lui(uint32_t rd, uint32_t upper) { return LUI(rd, upper << 12); }
uint32_t lw(uint32_t rd, int32_t offset, uint32_t rs1) { return LW(rd, rs1, offset); }
uint32_t sw(uint32_t rs2, int32_t offset, uint32_t rs1) { return SW(rs2, rs1, offset); }
uint32_t beq(uint32_t rs1, uint32_t rs2, int32_t offset) { return B(offset, rs2, rs1, 0); }
uint32_t bne(uint32_t rs1, uint32_t rs2, int32_t offset) { return B(offset, rs2, rs1, 1); }
uint32_t jal(uint32_t rd, int32_t offset) { return J(offset, rd); }

// ---- region 0xFF's registers (5.5, 17.1, 17.6), each as an offset from x0 ----
constexpr int32_t EVENTS_MASK = -0x800 + 0x014;            // the tile window's, and its peripheral_1 event (7.2)
constexpr uint32_t EVENT_PERIPHERAL_1 = 1u << 4;
constexpr int32_t IE = -0x600;                             // Ingress/Egress, peripheral 1, at 0xFFFF_FA00
constexpr int32_t IE_STATUS = IE + 0x004, IE_EVENTS_PEEK = IE + 0x010, IE_EVENTS_MASK = IE + 0x014;
constexpr int32_t UART_TX = IE + 0x0A8, UART_RX = IE + 0x0AC, UART_DIVISOR = IE + 0x0B0, UART_STATUS = IE + 0x0B4;
constexpr int32_t STREAM_OUT_W0 = IE + 0x100, STREAM_SEND = IE + 0x10C, STREAM_SEND_LAST = IE + 0x110;
constexpr int32_t STREAM_IN_W0 = IE + 0x114, STREAM_RECEIVE = IE + 0x120;
constexpr uint32_t STATUS_STREAM_LAST = 2;                 // STATUS [2]
constexpr uint32_t EVENT_STREAM_IN = 1u << 6, EVENT_STREAM_OUT = 1u << 7;

constexpr uint32_t DONE = 0x444F4E45;                      // bench.rs's done word
constexpr uint32_t OWN_BEAT_ADDRESS = 0x100, DONE_ADDRESS = 0x200;

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
    // With `event` Ingress/Egress's only event selected, until EVENTS_PEEK shows it, in WFI between its loads.
    void wait(uint32_t event) {
        li(5, event);
        as(sw(5, IE_EVENTS_MASK, 0));
        const int32_t top = here();
        as(lw(12, IE_EVENTS_PEEK, 0));
        as(andi(12, 12, int32_t(event)));
        as(bne(12, 0, 12));
        as(WFI);
        as(jal(0, top - here()));
    }
};

// verif::Random: xorshift32 from a non-zero state.
struct Random {
    uint32_t state;
    uint32_t next() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    uint32_t below(uint32_t bound) { return next() % bound; }
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

// chip_stream_test.rs. The master's program: its own beat, with TLAST, then BEATS beats echoed, each inverted, with
// its TLAST; then a done word. The LVDS IO block, as the RTL bench plays it: on lvds_in a transmitter of BEATS beats
// at random gaps, each held until its handshake, some with TLAST; on lvds_out a receiver whose TREADY is random.
// Checked, as the RTL test checks: every beat on lvds_out, in order, its TDATA and TLAST; every beat offered on
// lvds_in taken; a valid held until its ready, its beat unchanged meanwhile, on both ports; all in fewer than
// CYCLE_LIMIT cycles. And beyond it: every handshake's cycle and the done word's, the RTL chip's.
TEST(chip_stream_the_master_echoes_beats_at_the_rtl_chips_cycles) {
    constexpr unsigned BEATS = 12, CYCLE_LIMIT = 20000;
    constexpr unsigned BEAT_WORDS = 1;                     // STREAM_BYTES 4 (the RTL's Veryl.toml): a beat is its top word
    constexpr uint32_t OWN_BEAT = 0x11110000;              // OWN_BEAT's word 0
    // ---- the master's program (master_program) ----
    Program p;
    p.li(2, OWN_BEAT_ADDRESS);
    p.li(5, EVENT_PERIPHERAL_1);
    p.as(sw(5, EVENTS_MASK, 0));
    const int32_t TOP = BEAT_WORDS - 1;
    for (int32_t word = 0; word < TOP; word++) { p.as(lw(6, 4 * word, 2)); p.as(sw(6, STREAM_OUT_W0 + 4 * word, 0)); }
    p.as(lw(6, 4 * TOP, 2));
    p.as(sw(6, STREAM_SEND_LAST, 0));
    p.li(11, BEATS);                                       // the echo: x11 counts the beats left
    const int32_t next = p.here();
    p.wait(EVENT_STREAM_IN);
    p.as(lw(14, IE_STATUS, 0));
    p.as(srli(14, 14, STATUS_STREAM_LAST));
    p.as(andi(14, 14, 1));
    for (int32_t word = 0; word < TOP; word++) {
        p.as(lw(6, STREAM_IN_W0 + 4 * word, 0));
        p.as(xori(6, 6, -1));
        p.as(sw(6, STREAM_OUT_W0 + 4 * word, 0));
    }
    p.as(lw(15, STREAM_RECEIVE, 0));
    p.as(xori(15, 15, -1));
    p.wait(EVENT_STREAM_OUT);
    p.as(beq(14, 0, 12));
    p.as(sw(15, STREAM_SEND_LAST, 0));
    p.as(jal(0, 8));
    p.as(sw(15, STREAM_SEND, 0));
    p.as(addi(11, 11, -1));
    p.as(bne(11, 0, next - p.here()));
    p.li(5, DONE);
    p.li(6, DONE_ADDRESS);
    p.as(sw(5, 0, 6));
    p.as(jal(0, 0));

    // ---- the beats (the test's Random, as the RTL's draws them: four words each, the first the highest) ----
    Random random{0x5EED57EA};
    struct Beat { uint32_t data; bool last; };
    std::vector<Beat> beats;
    for (unsigned index = 0; index < BEATS; index++) {
        uint32_t low = 0;
        for (unsigned k = 0; k < 4; k++) low = random.next();          // TDATA is the low 32 bits: the last word drawn
        beats.push_back({low, index % 5 == 4 || index == BEATS - 1});
    }

    Bench b;
    CHECK_EQ(b.ie.stream_bytes, 4u);
    b.m.place(units::ID_MASTER, units::IMEM_BASE, p.words);
    b.m.place(units::ID_MASTER, OWN_BEAT_ADDRESS, {OWN_BEAT});
    b.m.place(units::ID_MASTER, DONE_ADDRESS, {0});

    // ---- the LVDS IO block (Bench::cycle), from the same sequence, in the RTL bench's order of draws ----
    size_t to_send = 0;
    bool offering = false;
    Beat offered{};
    unsigned taken = 0;
    b.far.drive = [&](int64_t, const Pins::Out& o, Pins::In& in) {
        if (!offering && random.below(2) == 0 && to_send < beats.size()) { offered = beats[to_send++]; offering = true; }
        in.lvds_out_tready = random.below(3) == 0;
        in.lvds_in_tvalid = offering; in.lvds_in_tdata[0] = offering ? offered.data : 0; in.lvds_in_tlast = offering && offered.last;
        in.uart_rx = true;
        if (offering && o.lvds_in_tready) { offering = false; taken++; }
    };

    // ---- the run, until the master's done word and every beat out ----
    uint64_t done_after = 0;
    while (b.cycle < CYCLE_LIMIT && !(b.far.got.size() == BEATS + 1 && b.done())) {
        b.run_cycle();
        if (done_after == 0 && b.done()) done_after = b.cycle;
    }
    CHECK(b.done());

    // ---- both ports ----
    CHECK_EQ(taken, BEATS);                                // the beats taken on lvds_in
    CHECK_EQ(b.far.taken_at.size(), size_t(BEATS));
    CHECK_EQ(b.far.got.size(), size_t(BEATS + 1));         // the beats on lvds_out
    for (size_t i = 0; i < b.far.got.size() && i < BEATS + 1; i++) {
        const uint32_t want = i == 0 ? OWN_BEAT : ~beats[i - 1].data;
        const bool last = i == 0 ? true : beats[i - 1].last;
        CHECK_EQ(b.far.got[i].w[0], want);                 // lvds_out's beat i: TDATA
        CHECK_EQ(b.far.got[i].last, last);                 // and TLAST
    }
    CHECK_EQ(b.far.out_violations, uint64_t(0));           // lvds_out: a valid held until its ready, its beat unchanged
    // (lvds_in's is the far end's own: the beat offered is held until its handshake by construction)
    CHECK(!b.far.at(0).lvds_in_tready);                    // 0 in the first cycle after the reset
    CHECK(b.far.at(1).lvds_in_tready);
    CHECK(b.far.at(0).uart_tx);                            // the line idle
    std::printf("test_rtl_stream: chip_stream: %llu cycles; the done word after %llu\n", (unsigned long long)b.cycle, (unsigned long long)done_after);
    std::printf("test_rtl_stream: chip_stream: lvds_in's handshakes at");
    for (int64_t t : b.far.taken_at) std::printf(" %lld", (long long)t);
    std::printf("\ntest_rtl_stream: chip_stream: lvds_out's handshakes at");
    for (const auto& g : b.far.got) std::printf(" %lld", (long long)g.t);
    std::printf("\n");

    // ---- the RTL chip's cycles with this far end (header, CYCLES): chip_stream_test at Snarky One 076e647, each
    // handshake's cycle printed from its Bench::cycle, and the cycles after which the done word is in DMEM ----
    static const int64_t kIn[BEATS] = {1, 2, 3, 4, 52, 92, 132, 172, 212, 251, 291, 331};
    static const int64_t kOut[BEATS + 1] = {43, 70, 112, 150, 192, 227, 275, 312, 349, 390, 427, 468, 508};
    for (size_t i = 0; i < BEATS && i < b.far.taken_at.size(); i++) CHECK_EQ(b.far.taken_at[i], kIn[i]);
    for (size_t i = 0; i < BEATS + 1 && i < b.far.got.size(); i++) CHECK_EQ(b.far.got[i].t, kOut[i]);
    CHECK_EQ(done_after, uint64_t(513));
    CHECK_EQ(b.cycle, uint64_t(513));                      // "chip_stream_test: 513 cycles"
    // the levels between: lvds_in_tready 0 in cycle 0 and from the fourth beat in until the master's first pop
    for (int64_t t = 5; t <= 51; t++) CHECK(!b.far.at(t).lvds_in_tready);
    CHECK(b.far.at(52).lvds_in_tready);
}

// The UART through the master's core, on the chip (what master_tile_test.rs's part 6 adds: bytes stored to UART_TX
// back to back, more than its FIFO holds, so that the later stores stall on the reg interface's ready; then bytes
// received, each polled in UART_RX, whose load pops it). The program: UART_DIVISOR 20 (the chip's reset divisor, 8681,
// would make a frame 86,810 cycles); 20 bytes, 0x41 up, each stored to UART_TX and then to LED, whose pin, the byte's
// bit 0, so changes once each store is taken; UART_STATUS then, stored to DMEM; four bytes polled in UART_RX until
// `valid`, each stored to DMEM; UART_STATUS again; the done word. The line's far end sends four frames of 20 cycles a
// bit, their falling edges on uart_rx in cycles 4200, 4400 (back to back with the first), 4700 and 5000.
// Checked from 17.6: the 20 bytes on uart_tx in order, every bit of every frame 20 cycles (the decoder), the frames
// back to back, 200 cycles apart; the first 17 stores taken at the core's pace (one on the line, 16 in the FIFO) and
// the last three stalled, each taken only after a frame's end has made room, 200 cycles apart; UART_STATUS after the
// stores, 16 bytes in the TX FIFO; each byte received with `valid`; UART_STATUS at the end, both FIFOs empty and no
// line error. And the cycles, the RTL chip's with the same program and line (header, CYCLES): the first start bit's
// cycle, every change of the led pin (so each store's cycle, the stalled ones' the cycle after their frame's pop made
// room), the cycles after which the first UART_STATUS is in DMEM, and the done word's.
TEST(chip_uart_through_the_core_stalled_stores_and_polled_bytes_at_the_rtl_chips_cycles) {
    constexpr uint32_t D = 20;
    constexpr int32_t LED = IE + 0x0A4;
    Program p;
    p.li(5, D);
    p.as(sw(5, UART_DIVISOR, 0));
    p.li(6, 0x41);
    p.li(7, 20);
    const int32_t top = p.here();
    p.as(sw(6, UART_TX, 0));
    p.as(sw(6, LED, 0));
    p.as(addi(6, 6, 1));
    p.as(addi(7, 7, -1));
    p.as(bne(7, 0, top - p.here()));
    p.as(lw(8, UART_STATUS, 0));
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
    p.as(lw(8, UART_STATUS, 0));
    p.as(sw(8, 0x114, 0));
    p.li(5, DONE);
    p.li(6, DONE_ADDRESS);
    p.as(sw(5, 0, 6));
    p.as(jal(0, 0));
    // the program is the one the RTL chip ran: its words as the RTL's encoders gave them
    static const uint32_t kWords[] = {0x1400293, 0xaa502823, 0x4100313, 0x1400393, 0xaa602423, 0xaa602223, 0x130313, 0xfff38393, 0xfe0398e3,
                                      0xab402403, 0x10802023, 0x10400493, 0x400513, 0xaac02583, 0x1005f613, 0xfe060ce3, 0xb4a023, 0x448493,
                                      0xfff50513, 0xfe0514e3, 0xab402403, 0x10802a23, 0x444f52b7, 0xe4528293, 0x20000313, 0x532023, 0x6f};
    CHECK_EQ(p.words.size(), sizeof kWords / sizeof kWords[0]);
    for (size_t i = 0; i < p.words.size() && i < sizeof kWords / sizeof kWords[0]; i++) CHECK_EQ(p.words[i], kWords[i]);

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
    CHECK_EQ(b.m.word(units::ID_MASTER, 0x100), 16u);                  // UART_STATUS after the 20 stores: the TX FIFO full
    for (unsigned i = 0; i < 4; i++) CHECK_EQ(b.m.word(units::ID_MASTER, 0x104 + 4 * i), 0x100u | kFrames[i].byte);
    CHECK_EQ(b.m.word(units::ID_MASTER, 0x114), 0u);                   // at the end: both FIFOs empty, no line error
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
    std::printf("test_rtl_stream: chip_uart: the first start bit in cycle %lld; UART_STATUS in DMEM after %llu cycles; done after %llu; led at",
                frames.empty() ? -1ll : (long long)frames[0].start, (unsigned long long)status_after, (unsigned long long)b.cycle);
    for (int64_t t : led) std::printf(" %lld", (long long)t);
    std::printf("\n");
    // ---- the RTL chip's cycles (chip_uart_ref, a scratch test of the RTL chip with this program and line: uart_tx
    // falls first in cycle 34; led's changes; "status word after 643 cycles"; "done after 5210 cycles") ----
    if (!frames.empty()) CHECK_EQ(frames[0].start, int64_t(34));
    static const int64_t kLed[20] = {35, 44, 53, 62, 71, 80, 89, 98, 107, 116, 125, 134, 143, 152, 161, 170, 179, 237, 437, 637};
    for (size_t k = 0; k < led.size() && k < 20; k++) CHECK_EQ(led[k], kLed[k]);
    CHECK_EQ(status_after, uint64_t(643));
    CHECK_EQ(b.cycle, uint64_t(5210));
}
