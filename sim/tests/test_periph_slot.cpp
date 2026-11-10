// test_periph_slot: the slot as a framework unit (WORKER15; AUDIT-M3 3.1): a stand-in for the core and the director puts
// window stores and consuming loads on S2/S3 (Link(1)) and mode-0 region writes on S1 (Link(3)), reads the window
// RegFile at chosen cycles (its loads' sample points), and takes the DMA's packets from the credit Fifo. The values and
// cycles checked are ref's (PeripheralSlot under Tile: an input at t acts from the end of t, a load at t reads the end
// of t - 1, a region write at t lands at the end of t + 2, the DMA's first packet the cycle after PULSE_DMA's).
// Run plain, with step-all, and shuffled; the debug builds' missed-wake detector is on throughout.
#include <cstdio>
#include <vector>

#include "harness.h"
#include "config.h"
#include "sim.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/replayer.h"
#include "statefp.h"
#include "units/test_peripheral.h"

using namespace units;

namespace {

struct Stand {
    enum : uint8_t { STORE, CONSUME_MASKED, REGION, READ };
    struct Ev { uint64_t t; uint8_t kind; uint32_t a, b; uint64_t lo, hi; };
    sim::LinkOut core, region;
    sim::RegFileIn window;
    sim::FifoIn dma;
    const Ev* ev = nullptr;
    unsigned n = 0, i = 0;
    bool with_dma = false;
    uint32_t reads[64] = {};
    unsigned n_reads = 0;
    uint64_t dma_t[16] = {}, dma_lo[16] = {}, dma_hi[16] = {}, dma_h[16] = {};
    unsigned n_dma = 0;

    Stand(sim::Ports& p, const Ev* e, unsigned count, bool dma_port) : ev(e), n(count), with_dma(dma_port) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
        if (dma_port) p.in_credit(dma, "dma", 3, 4);
    }
    sim::Wake step(sim::Ctx& c) {
        if (with_dma)
            while (c.ready(dma)) {
                const sim::Word* w = c.peek(dma);
                if (n_dma < 16) { dma_t[n_dma] = c.now(); dma_lo[n_dma] = w[0]; dma_h[n_dma] = w[1]; dma_hi[n_dma] = w[2]; }
                n_dma++;
                c.take(dma);
            }
        for (; i < n && ev[i].t == c.now(); i++) {
            const Ev& e = ev[i];
            switch (e.kind) {
                case STORE: c.put(core, CoreOp::store(1, e.a, e.b)); break;
                case CONSUME_MASKED: {
                    // a consuming load of EVENTS sampling now: the value under EVENTS_MASK, from the window
                    const uint32_t v = c.read(window, Win::reg(1, Win::R_STD + 5)), m = c.read(window, Win::reg(1, Win::R_STD + 7));
                    reads[n_reads++] = v;
                    if (v & m) c.put(core, CoreOp::consume(1, v & m));
                    break;
                }
                case REGION: c.put(region, e.lo, lw::hdr(7, 0, 0, 0, e.a, lw::SIZE_128), e.hi); break;
                case READ: reads[n_reads++] = c.read(window, e.a); break;
            }
        }
        if (i < n) return sim::Wake::at_cycle(ev[i].t);
        return with_dma ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { return i * 131 + n_reads * 7 + n_dma; }
};

struct Rig {
    sim::Sim m;
    Rig(const Stand::Ev* ev, unsigned n, const PeriphSlotConfig& pc, const sim::Options& o) : m(build(ev, n, pc, o)) {}
    static sim::Sim build(const Stand::Ev* ev, unsigned n, const PeriphSlotConfig& pc, const sim::Options& o) {
        sim::Config cfg(o);
        auto& st = cfg.add<Stand>("stand", ev, n, pc.controller);
        auto& sl = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(st.core, sl.core, 1);
        cfg.link(st.region, sl.region, 3);
        cfg.connect(sl.window, st.window);
        if (pc.controller) cfg.connect(sl.dma, st.dma);
        return cfg.build();
    }
    Stand& stand() { return m.unit<Stand>("stand"); }
    PeriphSlot& slot() { return m.unit<PeriphSlot>("slot"); }
};

const unsigned STATUS = Win::reg(1, Win::R_STD + 1), EVENTS_PEEK = Win::reg(1, Win::R_STD + 6), PARAMS = Win::reg(1, Win::R_STD + 0),
               W32 = Win::reg(1, Win::R_OWN + 0), W33 = Win::reg(1, Win::R_OWN + 1), DEF0 = Win::reg(1, Win::R_DEFINED);

sim::Options opts(int variant) {
    sim::Options o;
    if (variant == 1) o.step_all = true;
    if (variant == 2) o.shuffle_seed = 7;
    return o;
}

}  // namespace

// The test peripheral on a controller (C&C 1's id, 64): the window, PULSE_RUN, the scratch word, the DMA, a region write,
// EVENTS_MASK and a consuming load of DMA done.
TEST(slot_unit_test_peripheral_window_dma_region_and_events) {
    const Stand::Ev ev[] = {
        {2, Stand::READ, PARAMS, 0, 0, 0},              // 0: PARAMS: type 15, version 3
        {2, Stand::READ, DEF0, 0, 0, 0},                // 1: words 32-34 defined
        {3, Stand::STORE, 3, 1, 0, 0},                  //    PULSE_RUN at 3
        {4, Stand::READ, STATUS, 0, 0, 0},              // 2: STATUS [0] at 4 (from the end of 3)
        {4, Stand::READ, W32, 0, 0, 0},                 // 3: runs started 1
        {5, Stand::STORE, 33, 0xABC, 0, 0},             //    scratch
        {6, Stand::READ, W33, 0, 0, 0},                 // 4: 0xABC
        {7, Stand::STORE, 8, (1u << 25) | (1u << 22) | (0u << 12) | 3u, 0, 0},   // DMA_HDR: dst 1, mode 1, queue 0, 3 words
        {8, Stand::STORE, 9, 0x100, 0, 0},              //    DMA_ADDR
        {9, Stand::STORE, 7, 0x3, 0, 0},                //    EVENTS_MASK: run done, DMA done
        {10, Stand::STORE, 4, 1, 0, 0},                 //    PULSE_DMA at 10: packets at 11, 12, 13
        {11, Stand::READ, STATUS, 0, 0, 0},             // 5: STATUS [1] DMA busy at 11
        {12, Stand::REGION, 0x04000040, 0, 0x55, 0x66}, //    a region write at 12: lands at the end of 14
        {20, Stand::READ, EVENTS_PEEK, 0, 0, 0},        // 6: run done (the run ended at 13) and DMA done
        {20, Stand::READ, Win::R_LIVE, 0, 0, 0},        // 7: EVENTS [13] live (bit 0), no errors
        {21, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 8: the consuming load's value
        {23, Stand::READ, EVENTS_PEEK, 0, 0, 0},        // 9: both consumed
        {23, Stand::READ, Win::R_LIVE, 0, 0, 0},        // 10: the live bit down
    };
    PeriphSlotConfig pc;
    pc.tile = 64; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 11u);
        CHECK_EQ(s.reads[0], 15u | 3u << 8);
        CHECK_EQ(s.reads[1], 7u);
        CHECK_EQ(s.reads[2] & 1, 1u);
        CHECK_EQ(s.reads[3], 1u);
        CHECK_EQ(s.reads[4], 0xABCu);
        CHECK_EQ(s.reads[5] & 2, 2u);
        CHECK_EQ(s.reads[6], 3u);
        CHECK_EQ(s.reads[7], 1u);
        CHECK_EQ(s.reads[8], 3u);
        CHECK_EQ(s.reads[9], 0u);
        CHECK_EQ(s.reads[10], 0u);
        // the DMA: three packets put at 11, 12, 13 (seen by the stand the cycle after), at 0x100, 0x110, 0x120
        CHECK_EQ(s.n_dma, 3u);
        for (unsigned k = 0; k < 3 && k < s.n_dma; k++) {
            CHECK_EQ(s.dma_t[k], 12u + k);
            CHECK_EQ(s.dma_lo[k], uint64_t(k));
            CHECK_EQ(s.dma_hi[k], ~uint64_t(k));
            CHECK_EQ(lw::addr(s.dma_h[k]), 0x100u + 16 * k);
            CHECK_EQ(lw::dst(s.dma_h[k]), 1u);
            CHECK_EQ(lw::src(s.dma_h[k]), 72u);       // item 118: the controller's id + 8
        }
        TestPeripheral& x = r.slot().s.test_peripheral(1);
        CHECK_EQ(x.n_accepts, 1u);
        CHECK_EQ(x.accepts[0].land_t, 14);
        CHECK_EQ(x.accepts[0].offset, 0x40u);
        CHECK_EQ(x.accepts[0].lo, 0x55u);
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// The replayer on a mesh tile: PULSE_RUN with RUN_CONFIG's rows 0 (reset) ends the pass at once: STATUS [0] for one
// cycle, run done posted; a region write to the code queue lands; the slot sleeps when nothing is in progress.
TEST(slot_unit_replayer_pass_of_zero_rows_and_a_landing) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, 7, 0x1, 0, 0},                // EVENTS_MASK: run done
        {5, Stand::STORE, 3, 1, 0, 0},                  // PULSE_RUN at 5
        {6, Stand::READ, STATUS, 0, 0, 0},              // 0: running from the end of 5
        {7, Stand::READ, STATUS, 0, 0, 0},              // 1: and stopped at 6 (rows 0)
        {7, Stand::READ, EVENTS_PEEK, 0, 0, 0},         // 2: run done
        {8, Stand::CONSUME_MASKED, 0, 0, 0, 0},         // 3: taken
        {10, Stand::READ, EVENTS_PEEK, 0, 0, 0},        // 4: gone
        {12, Stand::REGION, 0x04100000, 0, 0x1234, 0x5678},   // the code queue (part 1)
        {16, Stand::READ, Win::reg(1, Win::R_OWN + 1), 0, 0, 0},   // 5: ROWS_RUN 0
    };
    PeriphSlotConfig pc;
    pc.tile = 9; pc.type[1] = PT_REPLAYER;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(200);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 6u);
        CHECK_EQ(s.reads[0] & 1, 1u);
        CHECK_EQ(s.reads[1] & 1, 0u);
        CHECK_EQ(s.reads[2] & 1, 1u);
        CHECK_EQ(s.reads[3] & 1, 1u);
        CHECK_EQ(s.reads[4] & 1, 0u);
        CHECK_EQ(s.reads[5], 0u);
        Replayer& rp = r.slot().s.replayer(1);
        CHECK_EQ(rp.code.occ, 1u);
        CHECK_EQ(rp.code.lo[rp.code.head], 0x1234u);
        CHECK_EQ(rp.passes, 1u);
        if (variant == 0) CHECK(r.m.steps("slot") < 40);   // asleep most of the 200 cycles
    }
}

// PeripheralSlotTest's two undefined behaviours in the DMA (DECISIONS-v1.25 s8e): meshsim stops, naming them.
DEATH_TEST(slot_pulse_dma_while_a_transfer_runs, "DMA: PULSE_DMA while a transfer is in progress") {
    SlotCore s;
    s.tile = 10;
    s.install(1, PT_TEST);
    s.write(1, 1, 8, (1u << 12) | 7u);   // DMA_HDR: queue 1, 7 words
    s.write(2, 1, 4, 1);                 // PULSE_DMA
    s.run_cycle(2); s.end_cycle(2);
    s.write(3, 1, 4, 1);                 // again, while it runs
}
DEATH_TEST(slot_dma_naming_a_missing_queue, "output queue 3, which the peripheral doesn't have") {
    SlotCore s;
    s.tile = 10;
    s.install(1, PT_TEST);
    s.write(1, 1, 8, (3u << 12) | 4u);   // DMA_HDR: queue 3 (the test peripheral has two), 4 words
    s.write(2, 1, 4, 1);
}

// The replayer's canonical fingerprint through the slot (AUDIT-M3 6.8) equals a twin replayer's driven directly as ref's
// is (its inputs in its own lists): at the end of 13 a region write is still in S1 (put at 12) and the slot's helper
// reads it from the link; at the end of 5 the PULSE_RUN of 5 is still in S2.
TEST(slot_replayer_fingerprint_reads_the_links_inputs_as_refs_lists) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, 7, 0x1, 0, 0},
        {5, Stand::STORE, 3, 1, 0, 0},
        {8, Stand::CONSUME_MASKED, 0, 0, 0, 0},
        {12, Stand::REGION, 0x04100000, 0, 0x1234, 0x5678},
    };
    PeriphSlotConfig pc;
    pc.tile = 9; pc.type[1] = PT_REPLAYER;
    Rig r(ev, sizeof ev / sizeof ev[0], pc, sim::Options{});
    Replayer twin(9);
    auto twin_fp = [&](int64_t c) { fp::FpOut f; twin.fingerprint(f, c); return f.done(); };
    auto slot_fp = [&](int64_t c) { fp::FpOut f; r.slot().fingerprint_replayer(r.m, 1, f, c); return f.done(); };
    for (int64_t t = 0; t <= 14; t++) {
        if (t == 5) twin.run(5);
        if (t == 12) twin.accept(12, 7, 0x100000, 2, 0x1234, 0x5678);
        if (t == 9) twin.consume_events(1);   /* the consuming load of 8, applied at the end of 8 */
        twin.step(t);
        r.m.run_until(uint64_t(t));
        if (t == 5 || t == 12 || t == 13 || t == 14) CHECK_EQ(slot_fp(t), twin_fp(t));
    }
}
