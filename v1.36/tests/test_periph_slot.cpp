// test_periph_slot: the slot as a framework unit (AUDIT-M3 3.1): a stand-in for the core and the director puts
// window stores and consuming loads on S2/S3 (Link(1)) and mode-0 region writes on S1 (Link(3)), reads the window
// RegFile at chosen cycles (its loads' sample points), and takes the DMA's packets as a router's DMA input does (router.h,
// A DMA INPUT: `RouterSide` drives the input's room, the stand records each packet in the cycle it moved). The values and
// cycles checked are ref's (PeripheralSlot under Tile: an input at t acts from the end of t, a load at t reads the end
// of t - 1, a region write at t lands at the end of t + 2, the DMA's first packet the cycle after PULSE_DMA's).
// Run plain, with step-all, and shuffled; the debug builds' missed-wake detector is on throughout.
//
// v1.36: the control block and the DMA block are 17.1's, by its window words (regmap.h's PeriphReg: EVENTS 3, EVENTS_PEEK
// 4, EVENTS_MASK 5, START 6; DMA_HDR 16 with dst [14:8], count [26:15] and queue [28:27], DMA_DEST_ADDR 17, DMA_START
// 18), and the DMA's packets 17.4's and 16.4's: src the tile's own id, a WRITE at DMA_DEST_ADDR, which then advances by
// 16, a packet of any other mode at address 0 with DMA_DEST_ADDR left as it is, and a transfer of 0 packets busy for one
// cycle with dma_done posted at its end. Those expectations are the specification's; the cycles are the slot's as before.
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
    sim::LinkIn dma;                 // the DMA's packet, delivered the cycle after it moved
    const Ev* ev = nullptr;
    unsigned n = 0, i = 0;
    bool with_dma = false;
    unsigned pn = 1;                 // the peripheral the stores, the consuming loads and the region writes are for
    uint64_t hold_until = 0;         // (RouterSide's: the router's input is not ready before this cycle)
    uint32_t reads[64] = {};
    unsigned n_reads = 0;
    uint64_t dma_t[16] = {}, dma_lo[16] = {}, dma_hi[16] = {}, dma_h[16] = {};
    unsigned n_dma = 0;

    Stand(sim::Ports& p, const Ev* e, unsigned count, bool dma_port, unsigned peripheral = 1, uint64_t hold = 0)
        : ev(e), n(count), with_dma(dma_port), pn(peripheral), hold_until(hold) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
        if (dma_port) p.in(dma, "dma", 3);
    }
    sim::Wake step(sim::Ctx& c) {
        if (with_dma && c.ready(dma)) {                              // a packet that moved in the cycle before
            const sim::Word* w = c.peek(dma);
            if (n_dma < 16) { dma_t[n_dma] = c.now() - 1; dma_lo[n_dma] = w[0]; dma_h[n_dma] = w[1]; dma_hi[n_dma] = w[2]; }
            n_dma++;
            c.take(dma);
        }
        for (; i < n && ev[i].t == c.now(); i++) {
            const Ev& e = ev[i];
            switch (e.kind) {
                case STORE: c.put(core, CoreOp::store(pn, e.a, e.b)); break;
                case CONSUME_MASKED: {
                    // a consuming load of EVENTS sampling now: the value under EVENTS_MASK, from the window
                    const uint32_t v = c.read(window, Win::reg(pn, Win::R_STD + P_EVENTS)), m = c.read(window, Win::reg(pn, Win::R_STD + P_EVENTS_MASK));
                    reads[n_reads++] = v;
                    if (v & m) c.put(core, CoreOp::consume(pn, v & m));
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

// The router's side of the DMA's `packet` interface (16.4): its room for the packet, a Wire, every queue with room
// from cycle `from` on and none before (0: always).
struct RouterSide {
    sim::WireOut room;
    uint64_t from;
    RouterSide(sim::Ports& p, uint64_t ready_from) : from(ready_from) { p.out(room, "room", ready_from ? 0 : 0xFF); }
    sim::Wake step(sim::Ctx& c) {
        if (c.now() >= from) { c.drive(room, 0xFF); return sim::Wake::none(); }
        return sim::Wake::at_cycle(from);
    }
    uint64_t digest() const { return from; }
};

struct Rig {
    sim::Sim m;
    Rig(const Stand::Ev* ev, unsigned n, const PeriphSlotConfig& pc, const sim::Options& o, unsigned peripheral = 1, uint64_t hold = 0)
        : m(build(ev, n, pc, o, peripheral, hold)) {}
    static sim::Sim build(const Stand::Ev* ev, unsigned n, const PeriphSlotConfig& pc, const sim::Options& o, unsigned peripheral, uint64_t hold) {
        sim::Config cfg(o);
        auto& st = cfg.add<Stand>("stand", ev, n, pc.controller, peripheral, hold);
        auto& sl = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(st.core, sl.core, 1);
        cfg.link(st.region, sl.region, 3);
        cfg.connect(sl.window, st.window);
        if (pc.controller) {
            auto& rs = cfg.add<RouterSide>("router", hold);
            cfg.link(sl.dma, st.dma, 1);
            cfg.connect(rs.room, sl.dma_room);
        }
        return cfg.build();
    }
    Stand& stand() { return m.unit<Stand>("stand"); }
    PeriphSlot& slot() { return m.unit<PeriphSlot>("slot"); }
};

const unsigned W_STATUS = Win::reg(1, Win::R_STD + P_STATUS), W_EVENTS_PEEK = Win::reg(1, Win::R_STD + P_EVENTS_PEEK),
               W_PARAMS = Win::reg(1, Win::R_STD + P_PARAMS), W_DMA_DEST_ADDR = Win::reg(1, Win::R_STD + Win::std(P_DMA_DEST_ADDR)),
               W_EVENTS_MASK = Win::reg(1, Win::R_STD + P_EVENTS_MASK), W_DMA_HDR = Win::reg(1, Win::R_STD + Win::std(P_DMA_HDR)),
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
        {2, Stand::READ, W_PARAMS, 0, 0, 0},              // 0: PARAMS: type 15, version 3
        {2, Stand::READ, DEF0, 0, 0, 0},                // 1: words 32-34 defined
        {3, Stand::STORE, P_START, 1, 0, 0},            //    START at 3
        {4, Stand::READ, W_STATUS, 0, 0, 0},              // 2: STATUS [0] at 4 (from the end of 3)
        {4, Stand::READ, W32, 0, 0, 0},                 // 3: runs started 1
        {5, Stand::STORE, 33, 0xABC, 0, 0},             //    scratch
        {6, Stand::READ, W33, 0, 0, 0},                 // 4: 0xABC
        {7, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO | 3u << DMA_HDR_COUNT_LO | 0u << DMA_HDR_QUEUE_LO, 0, 0},   // dst 1, WRITE, queue 0, 3 packets
        {8, Stand::STORE, P_DMA_DEST_ADDR, 0x100, 0, 0},
        {9, Stand::STORE, P_EVENTS_MASK, 0x3, 0, 0},    //    EVENTS_MASK: done, dma_done
        {10, Stand::STORE, P_DMA_START, 1, 0, 0},       //    DMA_START at 10: packets at 11, 12, 13
        {11, Stand::READ, W_STATUS, 0, 0, 0},             // 5: STATUS [1] DMA busy at 11
        {12, Stand::REGION, 0x04000040, 0, 0x55, 0x66}, //    a region write at 12: lands at the end of 14
        {20, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},        // 6: run done (the run ended at 13) and DMA done
        {20, Stand::READ, Win::R_LIVE, 0, 0, 0},        // 7: EVENTS [13] live (bit 0), no errors
        {21, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 8: the consuming load's value
        {23, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},        // 9: both consumed
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
            CHECK_EQ(lw::src(s.dma_h[k]), 64u);       // 16.4: the special tile's own id (not v1.35's id + 8)
            CHECK_EQ(lw::mode(s.dma_h[k]), unsigned(lw::M_WRITE));
            CHECK_EQ(lw::size(s.dma_h[k]), unsigned(lw::SIZE_128));
        }
        TestPeripheral& x = r.slot().s.test_peripheral(1);
        CHECK_EQ(x.n_accepts, 1u);
        CHECK_EQ(x.accepts[0].land_t, 14);
        CHECK_EQ(x.accepts[0].offset, 0x40u);
        CHECK_EQ(x.accepts[0].lo, 0x55u);
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// 17.4, from the specification: a transfer of packets that are not WRITEs carries address 0 and leaves DMA_DEST_ADDR
// (4.1), each stamped with DMA_HDR's mode, counter and dst and the tile's own id as src (16.4); DMA_START leaves
// dma_done as it is, and a consuming load under EVENTS_MASK takes it (17.2); a transfer of 0 packets sends nothing,
// STATUS.dma_busy is 1 for one cycle, and dma_done is posted at its end.
TEST(slot_dma_block_other_modes_and_an_empty_transfer) {
    const uint32_t count_hdr = lw::M_COUNT << HDR_MODE_LO | 7u << HDR_COUNTER_LO | 5u << HDR_DST_LO;
    const Stand::Ev ev[] = {
        {3, Stand::STORE, P_DMA_HDR, count_hdr | 2u << DMA_HDR_COUNT_LO | 1u << DMA_HDR_QUEUE_LO, 0, 0},   // COUNT on 7 to tile 5, queue 1, 2
        {4, Stand::STORE, P_DMA_DEST_ADDR, 0x200, 0, 0},
        {5, Stand::STORE, P_DMA_START, 1, 0, 0},        //    DMA_START at 5: packets at 6 and 7
        {9, Stand::STORE, P_EVENTS_MASK, 0x2, 0, 0},    //    EVENTS_MASK: dma_done
        {12, Stand::READ, W_DMA_DEST_ADDR, 0, 0, 0},    // 0: DMA_DEST_ADDR as stored
        {12, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 1: dma_done, consumed
        {14, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 2: gone
        {14, Stand::STORE, P_DMA_HDR, count_hdr, 0, 0}, //    count 0
        {15, Stand::STORE, P_DMA_START, 1, 0, 0},       //    DMA_START at 15
        {16, Stand::READ, W_STATUS, 0, 0, 0},           // 3: dma_busy in 16
        {17, Stand::READ, W_STATUS, 0, 0, 0},           // 4: and not in 17
        {17, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 5: dma_done posted at the end of 16
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 6u);
        CHECK_EQ(s.reads[0], 0x200u);
        CHECK_EQ(s.reads[1], 2u);
        CHECK_EQ(s.reads[2], 0u);
        CHECK_EQ(s.reads[3] & 2, 2u);
        CHECK_EQ(s.reads[4] & 2, 0u);
        CHECK_EQ(s.reads[5], 2u);
        CHECK_EQ(s.n_dma, 2u);
        for (unsigned k = 0; k < 2 && k < s.n_dma; k++) {
            CHECK_EQ(s.dma_t[k], 7u + k);
            CHECK_EQ(lw::addr(s.dma_h[k]), 0u);
            CHECK_EQ(lw::mode(s.dma_h[k]), unsigned(lw::M_COUNT));
            CHECK_EQ(lw::counter(s.dma_h[k]), 7u);
            CHECK_EQ(lw::dst(s.dma_h[k]), 5u);
            CHECK_EQ(lw::src(s.dma_h[k]), 66u);
            CHECK_EQ(lw::size(s.dma_h[k]), unsigned(lw::SIZE_128));
        }
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// The replayer on a mesh tile: PULSE_RUN with RUN_CONFIG's rows 0 (reset) ends the pass at once: STATUS [0] for one
// cycle, run done posted; a region write to the code queue lands; the slot sleeps when nothing is in progress.
TEST(slot_unit_replayer_pass_of_zero_rows_and_a_landing) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, P_EVENTS_MASK, 0x1, 0, 0},    // EVENTS_MASK: done
        {5, Stand::STORE, P_START, 1, 0, 0},            // START at 5
        {6, Stand::READ, W_STATUS, 0, 0, 0},              // 0: running from the end of 5
        {7, Stand::READ, W_STATUS, 0, 0, 0},              // 1: and stopped at 6 (rows 0)
        {7, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},         // 2: run done
        {8, Stand::CONSUME_MASKED, 0, 0, 0, 0},         // 3: taken
        {10, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},        // 4: gone
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

// The RTL's cycles of the DMA and of a consuming load (rtl/blocks/peripheral/src/peripheral_dma.veryl and
// peripheral_control.veryl, their headers): DMA_START presented in p takes effect at the end of p; from p + 1 an entry a
// cycle; the transfer ends in the cycle its last packet leaves, the cycle after its last take: dma_done is set then
// and STATUS.dma_busy falls at its end. A counted event's count becomes one less on a consuming load, then one more,
// at most its depth, on its set: a set in the cycle of a consuming load survives it.
TEST(slot_dma_ends_the_cycle_after_its_last_take_and_a_set_survives_a_consuming_load) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO | 2u << DMA_HDR_COUNT_LO, 0, 0},   // dst 1, WRITE, queue 0, 2 packets
        {4, Stand::STORE, P_EVENTS_MASK, 0x2, 0, 0},    //    EVENTS_MASK: dma_done
        {5, Stand::STORE, P_DMA_START, 1, 0, 0},        //    DMA_START in 5: takes in 6 and 7, the end in 8
        {5, Stand::READ, W_STATUS, 0, 0, 0},            // 0: not busy in 5
        {6, Stand::READ, W_STATUS, 0, 0, 0},            // 1: busy in 6
        {7, Stand::READ, W_STATUS, 0, 0, 0},            // 2: in 7
        {8, Stand::READ, W_STATUS, 0, 0, 0},            // 3: and in 8, the cycle the last packet leaves
        {8, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},       // 4: dma_done not yet
        {9, Stand::READ, W_STATUS, 0, 0, 0},            // 5: busy fell at the end of 8
        {9, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},       // 6: dma_done set at the end of 8
        {12, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO, 0, 0},   // count 0
        {13, Stand::STORE, P_DMA_START, 1, 0, 0},       //    DMA_START in 13: the empty transfer ends in 14
        {14, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 7: a consuming load in 14 returns the first transfer's dma_done
        {15, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 8: the set of 14 survived the load of 14
        {16, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 9: taken now
        {18, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 10: gone
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 11u);
        CHECK_EQ(s.reads[0] & 2, 0u);
        CHECK_EQ(s.reads[1] & 2, 2u);
        CHECK_EQ(s.reads[2] & 2, 2u);
        CHECK_EQ(s.reads[3] & 2, 2u);
        CHECK_EQ(s.reads[4], 0u);
        CHECK_EQ(s.reads[5] & 2, 0u);
        CHECK_EQ(s.reads[6], 2u);
        CHECK_EQ(s.reads[7], 2u);
        CHECK_EQ(s.reads[8], 2u);
        CHECK_EQ(s.reads[9], 2u);
        CHECK_EQ(s.reads[10], 0u);
        CHECK_EQ(s.n_dma, 2u);
        for (unsigned k = 0; k < 2 && k < s.n_dma; k++) CHECK_EQ(s.dma_t[k], 7u + k);   // taken in 6 and 7
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// The same rule for a peripheral's own counted event, the test peripheral's done (depth 1; its work lasts 10 cycles
// from START's): the second work's end, in the cycle of a consuming load that returned the first's done, survives it.
TEST(slot_peripheral_event_set_in_a_consuming_loads_cycle_survives_it) {
    const Stand::Ev ev[] = {
        {2, Stand::STORE, P_EVENTS_MASK, 0x1, 0, 0},    //    EVENTS_MASK: done
        {3, Stand::STORE, P_START, 1, 0, 0},            //    START in 3: the work ends in 13
        {14, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 0: done
        {14, Stand::STORE, P_START, 1, 0, 0},           //    START in 14: the work ends in 24
        {24, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 1: a consuming load in 24: the first work's done
        {25, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 2: the set of 24 survived
        {26, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 3: taken
        {28, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 4: gone
    };
    PeriphSlotConfig pc;
    pc.tile = 64; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 5u);
        CHECK_EQ(s.reads[0], 1u);
        CHECK_EQ(s.reads[1], 1u);
        CHECK_EQ(s.reads[2], 1u);
        CHECK_EQ(s.reads[3], 1u);
        CHECK_EQ(s.reads[4], 0u);
        CHECK_EQ(r.slot().s.test_peripheral(1).runs, 2);
    }
}

// And for the replayer's done (17.8.1; replayer_pass.veryl: a pass of 0 rows is busy for the cycle after START's and
// ends in it): the second pass's end in the cycle of a consuming load.
TEST(slot_replayer_done_set_in_a_consuming_loads_cycle_survives_it) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, P_EVENTS_MASK, 0x1, 0, 0},    //    EVENTS_MASK: done
        {5, Stand::STORE, P_START, 1, 0, 0},            //    START in 5: busy in 6, done set at its end
        {7, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},       // 0: done
        {8, Stand::STORE, P_START, 1, 0, 0},            //    START in 8: done set at the end of 9
        {9, Stand::CONSUME_MASKED, 0, 0, 0, 0},         // 1: a consuming load in 9
        {10, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 2: the set of 9 survived
        {11, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 3: taken
        {13, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 4: gone
    };
    PeriphSlotConfig pc;
    pc.tile = 9; pc.type[1] = PT_REPLAYER;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(60);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 5u);
        CHECK_EQ(s.reads[0] & 1, 1u);
        CHECK_EQ(s.reads[1] & 1, 1u);
        CHECK_EQ(s.reads[2] & 1, 1u);
        CHECK_EQ(s.reads[3] & 1, 1u);
        CHECK_EQ(s.reads[4] & 1, 0u);
        CHECK_EQ(r.slot().s.replayer(1).passes, 2u);
    }
}

// 17.1, 5.4, from the specification and the RTL's blocks' headers (peripheral_control.veryl, peripheral_dma.veryl: "The
// bits above a register's last field read 0, and a store ignores them"): EVENTS_MASK is 0 at the reset (18) and has
// one bit per event, the test peripheral's five; DMA_HDR's size [1:0] is the DMA's, ignored by a store and read 0, and
// so are its bits above `queue`, [31:29]. A store to START or to DMA_START with bit 0 clear does nothing ("a store
// with bit 0 set starts ...; 0 does nothing"): no work, no transfer, no event. The packet's size is 128 bits whatever
// DMA_HDR's store held (17.4).
TEST(slot_reserved_bits_read_0_and_a_start_with_bit_0_clear_does_nothing) {
    const uint32_t hdr = 1u << HDR_DST_LO | 1u << DMA_HDR_COUNT_LO;    // dst 1, WRITE, queue 0, 1 packet
    const Stand::Ev ev[] = {
        {2, Stand::READ, W_EVENTS_MASK, 0, 0, 0},       // 0: 0 at the reset
        {2, Stand::STORE, P_EVENTS_MASK, 0xFFFFFFFF, 0, 0},
        {3, Stand::READ, W_EVENTS_MASK, 0, 0, 0},       // 1: its five events' bits
        {3, Stand::STORE, P_START, 2, 0, 0},            //    bit 0 clear
        {4, Stand::READ, W_STATUS, 0, 0, 0},            // 2: not busy
        {4, Stand::READ, W32, 0, 0, 0},                 // 3: no run started
        {4, Stand::STORE, P_DMA_HDR, 0xFFFFFFFF, 0, 0},
        {5, Stand::READ, W_DMA_HDR, 0, 0, 0},           // 4: [28:2]
        {5, Stand::STORE, P_DMA_HDR, hdr | 3u, 0, 0},   //    with the size's bits set
        {6, Stand::READ, W_DMA_HDR, 0, 0, 0},           // 5: without them
        {6, Stand::STORE, P_DMA_START, 2, 0, 0},        //    bit 0 clear
        {7, Stand::READ, W_STATUS, 0, 0, 0},            // 6: no transfer
        {9, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},       // 7: no event
        {10, Stand::STORE, P_DMA_START, 1, 0, 0},       //    and with bit 0 set: one packet, taken in 11
        {11, Stand::READ, W_STATUS, 0, 0, 0},           // 8: dma_busy
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(30);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 9u);
        CHECK_EQ(s.reads[0], 0u);
        CHECK_EQ(s.reads[1], 0x1Fu);
        CHECK_EQ(s.reads[2], 0u);
        CHECK_EQ(s.reads[3], 0u);
        CHECK_EQ(s.reads[4], 0x1FFFFFFCu);
        CHECK_EQ(s.reads[5], hdr);
        CHECK_EQ(s.reads[6], 0u);
        CHECK_EQ(s.reads[7], 0u);
        CHECK_EQ(s.reads[8], 2u);
        CHECK_EQ(s.n_dma, 1u);
        CHECK_EQ(s.dma_t[0], 12u);
        CHECK_EQ(lw::size(s.dma_h[0]), unsigned(lw::SIZE_128));
        CHECK_EQ(r.slot().s.test_peripheral(1).runs, 0);
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// 17.2, 7.2: dma_done is counted with depth 1: its cause adds 1, up to the depth, and a consuming load subtracts 1. Two
// transfers end with no load between them, and one consuming load takes the event.
TEST(slot_dma_done_of_depth_1_holds_one_count) {
    const Stand::Ev ev[] = {
        {2, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO, 0, 0},   // count 0
        {3, Stand::STORE, P_EVENTS_MASK, 0x2, 0, 0},    //    EVENTS_MASK: dma_done
        {4, Stand::STORE, P_DMA_START, 1, 0, 0},        //    ends in 5
        {7, Stand::STORE, P_DMA_START, 1, 0, 0},        //    ends in 8: dma_done is at its depth
        {10, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 0: dma_done, consumed at the end of 10
        {11, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 1: gone
        {13, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 2: and it stays gone
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant));
        r.m.run(30);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 3u);
        CHECK_EQ(s.reads[0], 2u);
        CHECK_EQ(s.reads[1], 0u);
        CHECK_EQ(s.reads[2], 0u);
        CHECK_EQ(r.slot().s.dma[1].transfers, 2u);
    }
}

// 17.4, Pace: the DMA sends at most one packet a cycle and waits while its router input has no room; the RTL's
// peripheral_dma.veryl: "in each cycle in which a packet is still to send, the register is empty or its packet leaves
// (packet_ready), and the queue holds an entry, it takes the entry at the head, and the packet is offered from the next
// cycle", DMA_DEST_ADDR advancing "at the end of the cycle the packet is taken". The router's input is not ready
// before cycle 20: of a transfer of 7 packets started by a store in 4, the first entry is taken in 5 into the packet
// register, which holds it, and no other is taken: STATUS.dma_busy 1, no dma_done, DMA_DEST_ADDR advanced once. From
// 20 a packet moves in every cycle, packet k in 20 + k, each entry taken as its predecessor leaves; the transfer ends
// in 26, the cycle the last packet leaves.
TEST(slot_dma_waits_while_its_router_input_has_no_room) {
    const Stand::Ev ev[] = {
        {2, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO | 7u << DMA_HDR_COUNT_LO, 0, 0},   // dst 1, WRITE, queue 0, 7 packets
        {3, Stand::STORE, P_DMA_DEST_ADDR, 0x400, 0, 0},
        {4, Stand::STORE, P_DMA_START, 1, 0, 0},        //    from 5
        {15, Stand::READ, W_STATUS, 0, 0, 0},           // 0: dma_busy: waiting
        {15, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 1: no dma_done
        {15, Stand::READ, W_DMA_DEST_ADDR, 0, 0, 0},    // 2: one WRITE on: the packet in the register
        {20, Stand::READ, W_STATUS, 0, 0, 0},           // 3: still
        {26, Stand::READ, W_STATUS, 0, 0, 0},           // 4: busy in 26, the cycle the last packet leaves
        {26, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 5: no dma_done yet
        {27, Stand::READ, W_STATUS, 0, 0, 0},           // 6: ended at the end of 26
        {27, Stand::READ, W_EVENTS_PEEK, 0, 0, 0},      // 7: dma_done
        {27, Stand::READ, W_DMA_DEST_ADDR, 0, 0, 0},    // 8: seven WRITEs on
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant), 1, 20);
        r.m.run_until(15);
        CHECK_EQ(r.slot().s.test_peripheral(1).taken[0], 1u);      // the packet register's one
        r.m.run_until(50);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 9u);
        CHECK_EQ(s.reads[0] & 2, 2u);
        CHECK_EQ(s.reads[1], 0u);
        CHECK_EQ(s.reads[2], 0x410u);
        CHECK_EQ(s.reads[3] & 2, 2u);
        CHECK_EQ(s.reads[4] & 2, 2u);
        CHECK_EQ(s.reads[5], 0u);
        CHECK_EQ(s.reads[6] & 2, 0u);
        CHECK_EQ(s.reads[7], 2u);
        CHECK_EQ(s.reads[8], 0x470u);
        CHECK_EQ(s.n_dma, 7u);
        for (unsigned k = 0; k < 7 && k < s.n_dma; k++) {
            CHECK_EQ(s.dma_lo[k], uint64_t(k));
            CHECK_EQ(lw::addr(s.dma_h[k]), 0x400u + 16 * k);
            CHECK_EQ(s.dma_t[k], 20u + k);
        }
        CHECK_EQ(r.slot().s.violation_total(), 0u);
    }
}

// The RTL's peripheral_dma.veryl: "DMA_START, presented in cycle p, takes effect at the end of p ... and the transfer
// takes DMA_HDR as it stands, so a store to DMA_HDR during a transfer changes nothing of it." Such a store is UB (17.4),
// which the simulator flags; the hardware's behaviour is the RTL's. A transfer of 3 WRITEs to tile 1 from queue 0,
// started in 4, with the router's input not ready before 20: in 10 DMA_HDR is stored with dst 9, mode COUNT, counter 5
// and a count of 1. The two packets taken after the store still go to tile 1 as WRITEs with no counter, at their
// addresses, three in all; DMA_HDR reads back what was stored.
TEST(slot_dma_takes_dma_hdr_at_the_start) {
    const uint32_t stored = 9u << HDR_DST_LO | lw::M_COUNT << HDR_MODE_LO | 5u << HDR_COUNTER_LO | 1u << DMA_HDR_COUNT_LO;
    const Stand::Ev ev[] = {
        {2, Stand::STORE, P_DMA_HDR, 1u << HDR_DST_LO | 3u << DMA_HDR_COUNT_LO, 0, 0},   // dst 1, WRITE, queue 0, 3 packets
        {3, Stand::STORE, P_DMA_DEST_ADDR, 0x800, 0, 0},
        {4, Stand::STORE, P_DMA_START, 1, 0, 0},
        {10, Stand::STORE, P_DMA_HDR, stored, 0, 0},    //    during the transfer
        {12, Stand::READ, W_DMA_HDR, 0, 0, 0},          // 0: DMA_HDR as stored
        {30, Stand::READ, W_STATUS, 0, 0, 0},           // 1: ended
    };
    PeriphSlotConfig pc;
    pc.tile = 66; pc.controller = true; pc.type[1] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant), 1, 20);
        r.m.run_until(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 2u);
        CHECK_EQ(s.reads[0], stored);
        CHECK_EQ(s.reads[1] & 2, 0u);
        CHECK_EQ(s.n_dma, 3u);                                     // the count taken at the start, not the stored 1
        for (unsigned k = 0; k < 3 && k < s.n_dma; k++) {
            CHECK_EQ(s.dma_t[k], 20u + k);
            CHECK_EQ(lw::dst(s.dma_h[k]), 1u);
            CHECK_EQ(lw::mode(s.dma_h[k]), unsigned(lw::M_WRITE));
            CHECK_EQ(lw::counter(s.dma_h[k]), 0u);
            CHECK_EQ(lw::addr(s.dma_h[k]), 0x800u + 16 * k);
        }
        CHECK_EQ(r.slot().s.violation_total(), 1u);                // the store during the transfer, flagged (UB)
    }
}

// 17.1: peripheral n has region 0x03 + n and window 12 + n, and the tile's EVENTS.peripheral_n is its own
// (EVENTS & EVENTS_MASK) != 0 (17.2; 7.2: bits 4-6); an absent peripheral's bits read 0. Peripheral 3 alone: its
// window's stores and loads, a WRITE into region 0x06, and the tile's third bit.
TEST(slot_peripheral_3_has_its_own_window_region_and_tile_bit) {
    const Stand::Ev ev[] = {
        {2, Stand::READ, Win::reg(3, Win::R_STD + P_PARAMS), 0, 0, 0},        // 0: PARAMS
        {3, Stand::STORE, P_START, 1, 0, 0},            //    START in 3: the work ends in 13
        {4, Stand::READ, Win::reg(3, Win::R_STD + P_STATUS), 0, 0, 0},        // 1: busy
        {5, Stand::REGION, 0x06000040, 0, 0x55, 0x66},  //    a WRITE into region 0x06: lands at the end of 7
        {6, Stand::STORE, P_EVENTS_MASK, 0x1, 0, 0},    //    EVENTS_MASK: done
        {20, Stand::READ, Win::reg(3, Win::R_STD + P_EVENTS_PEEK), 0, 0, 0},  // 2: done
        {20, Stand::READ, Win::R_LIVE, 0, 0, 0},        // 3: EVENTS.peripheral_3 alone
        {21, Stand::CONSUME_MASKED, 0, 0, 0, 0},        // 4: consumed
        {23, Stand::READ, Win::R_LIVE, 0, 0, 0},        // 5: the tile's bit down
    };
    PeriphSlotConfig pc;
    pc.tile = 9; pc.type[3] = PT_TEST;
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], pc, opts(variant), 3);
        r.m.run(40);
        Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 6u);
        CHECK_EQ(s.reads[0], 15u | 3u << 8);
        CHECK_EQ(s.reads[1] & 1, 1u);
        CHECK_EQ(s.reads[2], 1u);
        CHECK_EQ(s.reads[3], 1u << 2);
        CHECK_EQ(s.reads[4], 1u);
        CHECK_EQ(s.reads[5], 0u);
        TestPeripheral& x = r.slot().s.test_peripheral(3);
        CHECK_EQ(x.runs, 1);
        CHECK_EQ(x.n_accepts, 1u);
        CHECK_EQ(x.accepts[0].land_t, 7);
        CHECK_EQ(x.accepts[0].offset, 0x40u);
        CHECK_EQ(x.accepts[0].hi, 0x66u);
    }
}

// PeripheralSlotTest's two undefined behaviours in the DMA (DECISIONS-v1.25 s8e): meshsim stops, naming them.
DEATH_TEST(slot_dma_start_while_a_transfer_runs, "DMA: DMA_START while a transfer is in progress") {
    SlotCore s;
    s.tile = 10;
    s.install(1, PT_TEST);
    s.write(1, 1, P_DMA_HDR, 1u << DMA_HDR_QUEUE_LO | 7u << DMA_HDR_COUNT_LO);   // queue 1, 7 packets
    s.write(2, 1, P_DMA_START, 1);
    s.run_cycle(2); s.end_cycle(2);
    s.write(3, 1, P_DMA_START, 1);       // again, while it runs
}
DEATH_TEST(slot_dma_naming_a_missing_queue, "output queue 3, which the peripheral doesn't have") {
    SlotCore s;
    s.tile = 10;
    s.install(1, PT_TEST);
    s.write(1, 1, P_DMA_HDR, 3u << DMA_HDR_QUEUE_LO | 4u << DMA_HDR_COUNT_LO);   // queue 3 (the test peripheral has two), 4 packets
    s.write(2, 1, P_DMA_START, 1);
}

// The replayer's canonical fingerprint through the slot (AUDIT-M3 6.8) equals a twin replayer's driven directly as ref's
// is (its inputs in its own lists): at the end of 13 a region write is still in S1 (put at 12) and the slot's helper
// reads it from the link; at the end of 5 the PULSE_RUN of 5 is still in S2.
TEST(slot_replayer_fingerprint_reads_the_links_inputs_as_refs_lists) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, P_EVENTS_MASK, 0x1, 0, 0},
        {5, Stand::STORE, P_START, 1, 0, 0},
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
