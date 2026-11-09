// test_tile_slot: the tile's paths to the peripheral slot (WORKER15; AUDIT-M3 3.1), G1Config::periph_slot on, the test
// peripheral as peripheral 1. A program in the boot ROM does what PeripheralSlotTest's core does through the window: a
// PARAMS load, a store and load of a read/write word, PULSE_RUN, STATUS right after it, EVENTS_MASK (the slot's and the
// block's EVENTS [13]) and a WFI the run's end wakes, the peek and consuming EVENTS loads, a reserved word, a DMA
// transfer (recorded: a mesh tile has no DMA port) and a store to a read-only word (a bus error); a stand-in for the
// router puts a region-0x04 write. Each result is stored to data memory and checked; the cycle checks are ref's rules.
#include <vector>

#include "base/fatal.h"
#include "harness.h"
#include "mem_stand.h"
#include "units/test_peripheral.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

// ---- RV32 encoders ----
uint32_t LUI(unsigned rd, uint32_t imm20) { return imm20 << 12 | rd << 7 | 0x37; }
uint32_t ADDI(unsigned rd, unsigned rs1, int imm) { return uint32_t(imm & 0xFFF) << 20 | rs1 << 15 | rd << 7 | 0x13; }
uint32_t LW(unsigned rd, unsigned rs1, int off) { return uint32_t(off & 0xFFF) << 20 | rs1 << 15 | 2u << 12 | rd << 7 | 0x03; }
uint32_t SW(unsigned rs2, unsigned rs1, int off) { return uint32_t((off >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | 2u << 12 | uint32_t(off & 31) << 7 | 0x23; }
uint32_t CSRR(unsigned rd, unsigned csr) { return csr << 20 | 2u << 12 | rd << 7 | 0x73; }
constexpr uint32_t WFI = 0x10500073, JSELF = 0x0000006f;
constexpr unsigned MCYCLE = 0xB00;

struct Stand {   // W9's side: the eject at chosen cycles; the rest idle
    sim::LinkOut eject, go;
    sim::HoldIn send;
    sim::RegOut send_busy;
    sim::LinkIn line;
    sim::CountOut ovf;
    sim::LinkOut ovf_ev;
    uint64_t at = 0, lo = 0, hdr = 0;
    Stand(sim::Ports& p) {
        p.out(eject, "eject", 3); p.out(go, "go", 0); p.in(send, "send", 3); p.out(send_busy, "send_busy", 1);
        p.in(line, "line", 1); p.out(ovf, "ovf"); p.out(ovf_ev, "ovf_ev", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(send)) c.take(send);
        if (c.ready(line)) c.take(line);
        if (c.now() == at) c.put(eject, lo, hdr, 0);
        return c.now() < at ? sim::Wake::at_cycle(at) : sim::Wake::none();
    }
    uint64_t digest() const { return at; }
};

const std::vector<uint32_t>& program() {
    static const std::vector<uint32_t> p = {
        ADDI(1, 0, -0x600),                           // x1: peripheral 1's window, 0xFFFFFA00
        ADDI(7, 0, -0x800),                           // x7: the register block, 0xFFFFF800
        LW(2, 1, 0), SW(2, 0, 0x100),                 // PARAMS
        ADDI(3, 0, 0x55), SW(3, 1, 4 * 33), LW(4, 1, 4 * 33), SW(4, 0, 0x104),   // word 33: read/write
        LW(5, 1, 4 * 32), SW(5, 0, 0x108),            // word 32: runs started (0)
        ADDI(6, 0, 1), SW(6, 1, 4 * 7),               // the slot's EVENTS_MASK: run done
        LUI(8, 0x2), SW(8, 7, 4 * 8),                 // the block's EVENTS_MASK: EVENTS [13] (peripheral 1)
        CSRR(9, MCYCLE), SW(6, 1, 4 * 3),             // PULSE_RUN (its cycle: x9 + 1)
        LW(10, 1, 4 * 1), SW(10, 0, 0x10C),           // STATUS right after
        WFI, CSRR(11, MCYCLE),                        // woken by the run's end (EVENTS [13] live)
        LW(12, 1, 4 * 6), SW(12, 0, 0x110),           // EVENTS_PEEK: run done
        LW(13, 1, 4 * 5), SW(13, 0, 0x114),           // EVENTS, consuming
        LW(14, 1, 4 * 6), SW(14, 0, 0x118),           // EVENTS_PEEK: gone
        LW(15, 1, 4 * 32), SW(15, 0, 0x11C),          // runs started: 1
        SW(9, 0, 0x120), SW(11, 0, 0x124),
        LW(16, 1, 4 * 16), SW(16, 0, 0x128),          // a reserved word: reads 0, flagged
        ADDI(17, 0, 2), SW(17, 1, 4 * 8), SW(6, 1, 4 * 4),   // DMA_HDR: queue 0, 2 words; PULSE_DMA
        LW(18, 7, 4 * 7), SW(18, 0, 0x12C),           // the block's EVENTS_PEEK (no live bit: DMA done not masked yet)
        SW(3, 1, 4 * 34),                             // word 34 is read-only: a bus error (store access fault), the end
        JSELF,
    };
    return p;
}

}  // namespace

TEST(tile_slot_window_paths_from_the_core) {
    for (int variant = 0; variant < 3; variant++) {
        const std::vector<uint32_t>& rom = program();
        std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
        G1Ctx g;
        g.bootrom = rom.data(); g.bootrom_words = unsigned(rom.size()); g.mem = mem.data();
        static SramBlocks sram_blocks; g.sram = &sram_blocks;
        g.cfg.periph_slot = true; g.cfg.periph1 = PT_TEST;
        sim::Options o;
        if (variant == 1) o.step_all = true;
        if (variant == 2) o.shuffle_seed = 11;
        sim::Config cfg(o);
        auto& s = cfg.add<Stand>("stand");
        s.at = 20; s.lo = 0x77; s.hdr = lw::hdr(64, 0, lw::M_WRITE, 0, 0x04000030, lw::SIZE_128);   // a region-0x04 write
        ControlIO io = add_g1(cfg, 0, &g);
        cfg.link(s.eject, io.eject, 1);
        cfg.link(s.go, io.go, 1);
        cfg.connect(io.send, s.send);
        cfg.connect(s.send_busy, io.send_busy);
        cfg.link(io.line, s.line, 6);
        cfg.connect(s.ovf, io.router_ovf);
        cfg.link(s.ovf_ev, io.router_ovf_ev, 1);
        wire_mem_stand(cfg, io);
        sim::Sim m = cfg.build();
        m.run(400);
        const TileCore& core = m.unit<TileCore>("t0.core");
        auto d = [&](uint32_t a) { return core.rv.dmem[a >> 2]; };
        CHECK_EQ(d(0x100), 15u | 3u << 8);                  // PARAMS: type 15, version 3
        CHECK_EQ(d(0x104), 0x55u);
        CHECK_EQ(d(0x108), 0u);
        CHECK_EQ(d(0x10C) & 1, 1u);                         // running: the store's effect from the end of its cycle
        CHECK_EQ(d(0x110), 1u);                             // run done
        CHECK_EQ(d(0x114), 1u);
        CHECK_EQ(d(0x118), 0u);                             // consumed
        CHECK_EQ(d(0x11C), 1u);
        CHECK_EQ(d(0x128), 0u);
        // the run: PULSE_RUN at x9 + 1 = s; run done posted at s + 10 (run_length), visible at s + 11: the WFI's wake
        const uint32_t s0 = d(0x120) + 1, woke = d(0x124);
        CHECK(woke >= s0 + 11);
        CHECK(woke <= s0 + 11 + 4);
        CHECK_EQ(core.rv.reserved_accesses, uint64_t(1));   // the reserved word, flagged by the core
        PeriphSlot& sl = m.unit<PeriphSlot>("t0.slot");
        TestPeripheral& x = sl.s.test_peripheral(1);
        CHECK_EQ(x.runs, 1);
        CHECK_EQ(x.scratch, 0x55u);
        CHECK_EQ(x.n_accepts, 1u);                          // the region write: put at 20, accepted by the director at 21
        CHECK_EQ(x.accepts[0].offset, 0x30u);
        CHECK_EQ(x.accepts[0].lo, 0x77u);
        CHECK_EQ(sl.dma_recorded, uint64_t(2));             // the DMA's two packets, recorded (no DMA port on a tile)
        CHECK_EQ(sl.dma_record[0].lo, 0u);
        CHECK_EQ(sl.dma_record[1].lo, 1u);
        CHECK_EQ(sl.s.violation_total(), uint64_t(0));
        CHECK_EQ(d(0x12C) >> 13 & 1, 0u);                  // the block's EVENTS [13]: DMA done not under the slot's mask
        CHECK_EQ(core.rv.mcause, 7u);                       // the read-only word's store: a store access fault at once
        CHECK_EQ(core.rv.mtval, 0xFFFFFA00u + 4 * 34);
        CHECK_EQ(x.scratch, 0x55u);
    }
}
