// test_g1_tile.cpp: G1's part of a tile (add_g1, a ControlBuilder) on the engine with a stand-in for W9's side: eject
// packets at chosen cycles, a GO, a send sink, the send-busy Reg. Checks port B's landing (and command landed), and the
// K-line: a counted packet nothing armed for is judged unexpected at endCycle(t + K) (ERRORS [2]), by D27's rule.
#include <vector>

#include "harness.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

struct Stand {
    sim::LinkOut eject, go;
    sim::HoldIn send;
    sim::RegOut send_busy;
    sim::LinkIn line;
    sim::CountOut ovf;
    struct Ev { uint64_t t; uint64_t lo, hdr, hi; bool go; };
    const Ev* ev; unsigned n, i = 0;
    uint64_t sends = 0, lines = 0;
    Stand(sim::Ports& p, const Ev* e, unsigned count) : ev(e), n(count) {
        p.out(eject, "eject", 3); p.out(go, "go", 0); p.in(send, "send", 3); p.out(send_busy, "send_busy", 1);
        p.in(line, "line", 1); p.out(ovf, "ovf");
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(send)) { c.take(send); sends++; }
        if (c.ready(line)) { c.take(line); lines++; }
        for (; i < n && ev[i].t == c.now(); i++) {
            if (ev[i].go) c.put(go); else c.put(eject, ev[i].lo, ev[i].hdr, ev[i].hi);
        }
        return i < n ? sim::Wake::at_cycle(ev[i].t) : sim::Wake::none();
    }
    uint64_t digest() const { return i * 31 + sends * 7 + lines; }
};

}  // namespace

TEST(g1_tile_port_b_landing_and_the_k_line) {
    static const uint32_t rom[] = {0x10500073, 0x0000006f};          // wfi; j . (a stand-in boot ROM)
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    G1Ctx g;
    g.bootrom = rom; g.bootrom_words = 2; g.mem = mem.data();
    const Stand::Ev ev[] = {
        {5, 0x1111222233334444ull, lw::hdr(64, 0, lw::M_WRITE, 0, 0x100, lw::SIZE_64), 0, false},      // data memory
        {6, 0xABCD, lw::hdr(64, 0, lw::M_WRITE, 0, 0x7FC0, lw::SIZE_32), 0, false},                    // command slot 0
        {7, 0x5, lw::hdr(3, 0, lw::M_WRITE, 2, 0x200, lw::SIZE_32), 0, false},                         // counted, unarmed
        {30, 0x6, lw::hdr(3, 0, lw::M_WRITE, 0, 0x02000010, lw::SIZE_32), 0, false},                   // bad: the boot ROM
    };
    sim::Config cfg;
    auto& s = cfg.add<Stand>("stand", ev, 4u);
    ControlIO io = add_g1(cfg, 0, &g);
    cfg.link(s.eject, io.eject, 1);
    cfg.link(s.go, io.go, 1);
    cfg.connect(io.send, s.send);
    cfg.connect(s.send_busy, io.send_busy);
    cfg.link(io.line, s.line, 6);
    cfg.connect(s.ovf, io.router_ovf);
    sim::Sim m = cfg.build();
    m.run_until(8);                                                   // eject at 5 (arrives 6): port B at 6 + 3 = 9
    const TileCore& core = m.unit<TileCore>("t0.core");
    CHECK_EQ(core.rv.dmem[0x100 >> 2], 0u);
    m.run_until(9);
    CHECK_EQ(core.rv.dmem[0x100 >> 2], 0x33334444u);                  // seen by a load at 9: ref writes at 8
    CHECK_EQ(core.rv.dmem[0x104 >> 2], 0x11112222u);
    m.run_until(10);
    CHECK_EQ(core.rv.dmem[0x7FC0 >> 2], 0xABCDu);
    const TileCtl& ctl = m.unit<TileCtl>("t0.ctl");
    CHECK(ctl.s.landed);                                              // endCycle(9): latched (ref: landedAt = 9)
    CHECK(ctl.s.events >> EV_COMMAND & 1);
    // the counted packet: accepted at 8, decrement due at 8 + K = 16, judged at endCycle(16) (level 0 of 17)
    m.run_until(16);
    CHECK_EQ(ctl.s.unexpected_total, uint64_t(0));
    m.run_until(17);
    CHECK_EQ(ctl.s.unexpected_total, uint64_t(1));
    CHECK(ctl.s.conditions >> 2 & 1);                                 // ERRORS [2]
    CHECK_EQ(ctl.s.remaining[2], 0u);                                 // stays 0
    CHECK(ctl.s.error_line);
    // A condition with nothing else reaching the block (PORTS.md 8): the bad packet arrives at 31, the director drops it
    // at 31, ref's endCycle(31) sets ERRORS [1]; here the director's event steps the block at level 0 of 32.
    CHECK(!(ctl.s.conditions >> 1 & 1));
    m.run_until(31);
    CHECK(!(ctl.s.conditions >> 1 & 1));
    m.run_until(32);
    CHECK(ctl.s.conditions >> 1 & 1);
    CHECK_EQ(m.unit<Director>("t0.dir").bad_packets, uint64_t(1));
    CHECK_EQ(ctl.s.bad_total, uint64_t(1));
}
