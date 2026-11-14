// test_receive.cpp: the receive unit's write engine and its tables (machine-spec 12.4, 12.5), each rule from the
// specification or, where it leaves the figure to the implementation, from the RTL's receive unit
// (rtl/blocks/receive_unit/README.md, "Timing"). For a packet the traffic director accepts in cycle a, at TABLE_LATENCY L:
//     PTR and HIST    read at the end of a + L, written at the end of a + L + 1
//     the scratchpad  written in a + 2L + 2, in place for every later read
// and every packet takes the same path, never held, so the writes reach the scratchpad in acceptance order (12.4).
// No expectation here comes from the simulator's output.
//
// THE RIG: the traffic director, the write engine, the table store, HIST's store and the scratchpad, wired as a tile
// wires them (tile_ctl.cpp), and a stand in the place of everything else: it puts packets on the director's eject
// input (a packet PUT in cycle p is ACCEPTED in p + 1 = a), makes the core's table stores (a store MADE in cycle s is
// put on the window in s), holds the configuration registers, and samples, every cycle, what a core load of that
// cycle would read of one PTR entry and one HIST entry, and whether the engine reads the table in that cycle.
// The test steps the machine a cycle at a time and looks at the memories between cycles.
#include <cstdint>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/big_sram.h"
#include "units/director.h"
#include "units/table_store.h"
#include "units/write_engine.h"

using namespace units;

namespace {

constexpr unsigned ME = 5;

struct Stand {
    enum Kind { PKT, STORE_PTR, STORE_HIST };
    struct Ev { uint64_t at; Kind kind; uint64_t a, b, c; };
    // the director's surroundings
    sim::LinkOut eject;
    sim::LinkIn port_b, kline, bad;
    sim::FifoIn gather;
    sim::CountOut rp_stores;
    sim::RegFileOut dma;
    // the core's side of the tables
    sim::LinkOut ts_window, hist_window;
    sim::RegFileIn ptr_view, view, hist;
    std::vector<Ev> script;
    uint32_t receive_config = 0, gather_config = 0;
    unsigned watch_ptr = 0, watch_hist = 0;
    // per cycle (index = cycle): what a load made in that cycle reads, and the engines' read marks
    std::vector<uint32_t> ptr_seen, hist_seen;
    std::vector<uint8_t> ptr_read, hist_read;
    std::vector<uint64_t> kline_at;                      // the counters' delay line: (cycle << 8) | counter
    Stand(sim::Ports& p, std::vector<Ev> s) : script(std::move(s)) {
        p.out(eject, "eject", 3);
        p.in(port_b, "port_b", 3); p.in(kline, "kline", 1); p.in(bad, "bad", 0);
        p.in(gather, "gather", 3, 64);
        p.out(rp_stores, "rp_stores");
        p.out(dma, "dma", DMA_REGS);
        p.out(ts_window, "ts_window", 2); p.out(hist_window, "hist_window", 2);
        p.in(ptr_view, "ptr_view"); p.in(view, "view"); p.in(hist, "hist");
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (t == 1) { c.write(dma, DMA_R_CONFIG, 0); c.write(dma, DMA_R_RECEIVE, receive_config); c.write(dma, DMA_R_GATHER, gather_config); }
        if (c.ready(port_b)) c.take(port_b);
        if (c.ready(kline)) { kline_at.push_back(t << 8 | c.peek(kline)[0]); c.take(kline); }
        if (c.ready(bad)) c.take(bad);
        if (c.ready(gather)) c.take(gather);
        for (const Ev& e : script) {
            if (e.at != t) continue;
            if (e.kind == PKT) c.put(eject, e.a, e.b, e.c);
            else if (e.kind == STORE_PTR) c.put(ts_window, uint64_t(TBL_PTR) << 32 | e.a, e.b);
            else c.put(hist_window, e.a, e.b);
        }
        ptr_seen.resize(t + 1); hist_seen.resize(t + 1); ptr_read.resize(t + 1); hist_read.resize(t + 1);
        ptr_seen[t] = c.read(ptr_view, watch_ptr); hist_seen[t] = c.read(hist, watch_hist);
        ptr_read[t] = c.read(ptr_view, PTR_RMW_AT) == uint32_t(t + 1);
        hist_read[t] = c.read(hist, HIST_RMW_AT) == uint32_t(t + 1);
        return c.next();
    }
    uint64_t digest() const { return 0; }
};

// packets, as the router's eject gives them: {payload [63:0], header, payload [127:64]}
Stand::Ev pkt(uint64_t put, unsigned mode, unsigned size, uint64_t lo, uint32_t addr = 0, unsigned src = 9, unsigned counter = 0, uint64_t hi = 0) {
    return {put, Stand::PKT, lo, lw::hdr(src, ME, mode, counter, addr, size), hi};
}
Stand::Ev write_at(uint64_t put, uint32_t word, unsigned size, uint64_t lo, uint64_t hi = 0) { return pkt(put, lw::M_WRITE, size, lo, lw::sram_addr(word), 9, 0, hi); }
Stand::Ev store_ptr(uint64_t at, uint32_t i, uint32_t v) { return {at, Stand::STORE_PTR, i, v, 0}; }
Stand::Ev store_hist(uint64_t at, uint32_t i, uint32_t v) { return {at, Stand::STORE_HIST, i, v, 0}; }

struct Rig {
    SramBlocks blocks;
    sim::Sim m;
    unsigned L;
    Rig(std::vector<Stand::Ev> script, unsigned L_ = 1, uint32_t receive_config = 0, uint32_t gather_config = 0, unsigned watch_ptr = 9, unsigned watch_hist = 0)
        : m(build(blocks, std::move(script), L_, receive_config, gather_config, watch_ptr, watch_hist)), L(L_) {}
    static sim::Sim build(SramBlocks& blocks, std::vector<Stand::Ev> script, unsigned L, uint32_t rcfg, uint32_t gcfg, unsigned wp, unsigned wh) {
        sim::Config cfg;
        auto& st = cfg.add<Stand>("stand", std::move(script));
        st.receive_config = rcfg; st.gather_config = gcfg; st.watch_ptr = wp; st.watch_hist = wh;
        DirectorConfig dc;
        dc.tile = ME; dc.engine = true;
        auto& d = cfg.add<Director>("dir", dc);
        auto& we = cfg.add<WriteEngine>("we", L, 1u << 20);
        auto& ts = cfg.add<TableStore>("ts", true);
        auto& hs = cfg.add<HistStore>("hist");
        SramConfig sc;
        auto& sr = cfg.add<BigSram>("sram", sc, blocks.block(ME, sc.mem_chunks), false);
        cfg.link(st.eject, d.in, 1);
        cfg.link(d.port_b, st.port_b, 3);
        cfg.link(d.kline, st.kline, 9);
        cfg.link(d.bad, st.bad, 1);
        cfg.connect(d.gather, st.gather);
        cfg.connect(st.rp_stores, d.rp_stores);
        cfg.connect(st.dma, d.dma);
        cfg.connect(st.dma, we.dma);
        cfg.link(d.we_in, we.in, 1);
        cfg.link(d.ts_req, ts.req_we, L);                  // as tile_ctl.cpp
        cfg.link(ts.ret_we, we.ts_ret, L + 1);
        cfg.link(d.hist_req, hs.req, L);
        cfg.connect(we.to_sram, sr.wr);
        cfg.link(st.ts_window, ts.window, 1);
        cfg.link(st.hist_window, hs.window, 1);
        cfg.connect(ts.ptr_view, st.ptr_view);
        cfg.connect(ts.view, st.view);
        cfg.connect(hs.out, st.hist);
        return cfg.build();
    }
    uint64_t now = 0;
    void to(uint64_t cycle) { if (cycle > now) { m.run(cycle - now); now = cycle; } }   // run through `cycle`
    BigSram& sram() { return m.unit<BigSram>("sram"); }
    uint32_t ptr(unsigned i) { return m.unit<TableStore>("ts").m.ptr[i]; }
    uint32_t hist(unsigned i) { return m.unit<HistStore>("hist").m.hist[i]; }
    const Stand& stand() { return m.unit<Stand>("stand"); }
    const WriteEngine& we() { return m.unit<WriteEngine>("we"); }
};

}  // namespace

// 12.4 and the RTL's timing: a WRITE to the scratchpad accepted in a is written in a + 2L + 2, at its address, with its
// size's words alone; it reads no PTR entry and moves none. L = 1, 2, 3.
TEST(a_write_lands_in_the_scratchpad_2L_plus_2_cycles_after_its_acceptance) {
    for (unsigned L = 1; L <= 3; L++) {
        const uint64_t a = 5;                                // put in 4
        Rig r({store_ptr(1, 9, 0x700), write_at(a - 1, 0x1002, lw::SIZE_64, 0x2222222211111111ull)}, L);
        for (uint32_t j = 0; j < 4; j++) r.sram().poke(0x1000 + j, 0xEE00 + j);
        r.to(a + 2 * L + 1);
        CHECK_EQ(r.sram().peek(0x1002), 0xEE02u);            // not yet
        r.to(a + 2 * L + 2);
        CHECK_EQ(r.sram().peek(0x1002), 0x11111111u);
        CHECK_EQ(r.sram().peek(0x1003), 0x22222222u);
        CHECK_EQ(r.sram().peek(0x1000), 0xEE00u);            // the row's other words kept
        CHECK_EQ(r.sram().peek(0x1001), 0xEE01u);
        r.to(a + 12);
        CHECK_EQ(r.ptr(9), 0x700u);                          // no PTR entry moved
        CHECK_EQ(r.stand().ptr_read[a + L], uint8_t(0));     // nor read
        CHECK_EQ(r.sram().writes_served, uint64_t(1));
    }
}

// 12.4, 12.5 and the RTL's timing: an APPEND accepted in a writes at PTR[src] and advances it by its size. PTR is read
// at the end of a + L and written at the end of a + L + 1, so a core load made in a + L + 1 still reads the old
// pointer and one made in a + L + 2 the new; a load made in a + L, the cycle the engine reads, is undefined (the store
// marks that cycle and no other). The scratchpad's write is in a + 2L + 2.
TEST(an_append_reads_ptr_at_a_plus_L_and_writes_it_the_cycle_after) {
    for (unsigned L = 1; L <= 3; L++) {
        const uint64_t a = 6;
        Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_128, 0xB000000Aull, 0, 9, 0, 0xD000000Cull)}, L);
        r.to(a + L);
        CHECK_EQ(r.ptr(9), 0x700u);
        r.to(a + L + 1);
        CHECK_EQ(r.ptr(9), 0x704u);                          // += 4 words for 128 bits
        r.to(a + 2 * L + 1);
        CHECK_EQ(r.sram().peek(0x700), 0u);
        r.to(a + 2 * L + 2);
        CHECK_EQ(r.sram().peek(0x700), 0xB000000Au);         // at the OLD pointer
        CHECK_EQ(r.sram().peek(0x701), 0u);
        CHECK_EQ(r.sram().peek(0x702), 0xD000000Cu);
        r.to(a + 12);
        const Stand& s = r.stand();
        CHECK_EQ(s.ptr_seen[a + L + 1], 0x700u);             // a load made in the cycle of the write: the old value
        CHECK_EQ(s.ptr_seen[a + L + 2], 0x704u);
        for (uint64_t t = 2; t <= a + 12; t++) CHECK_EQ(unsigned(s.ptr_read[t]), t == a + L ? 1u : 0u);
    }
}

// 12.4: every write passes the pointer stage in arrival order: writes of every mode accepted one a cycle land one a
// cycle, in acceptance order, each 2L + 2 after its own acceptance. BUCKET writes at PTR[the bucket digit], byte
// bucket_digit - 1 of the payload's low 32 bits; AUGMENT writes the record (key, ordinal), the ordinal (the record's
// word address - record_base) / 2, and advances PTR by a record.
TEST(writes_of_every_mode_land_in_acceptance_order_one_a_cycle) {
    for (unsigned L = 1; L <= 3; L++) {
        const uint64_t a = 8;                                // the first acceptance
        // bucket_digit 2 (byte 1); record_base 0x2000. PTR[9] = 0x700 (APPEND from src 9), PTR[0x5A] = 0x900 (the
        // bucket of 0x5A in byte 1), PTR[3] = 0x2010 (AUGMENT from src 3).
        Rig r({store_ptr(1, 9, 0x700), store_ptr(2, 0x5A, 0x900), store_ptr(3, 3, 0x2010),
               write_at(a - 1, 0x1000, lw::SIZE_32, 0xA1),
               pkt(a, lw::M_SRC, lw::SIZE_32, 0xA2),
               pkt(a + 1, lw::M_BUCKET, lw::SIZE_64, 0x0000000700005A01ull),
               pkt(a + 2, lw::M_AUG, lw::SIZE_32, 0xDEAD0077, 0, 3),
               pkt(a + 3, lw::M_SRC, lw::SIZE_32, 0xA5),
               write_at(a + 4, 0x1004, lw::SIZE_32, 0xA6)},
              L, /* RECEIVE_CONFIG: bucket_digit */ 2, /* GATHER_CONFIG: record_base */ 0x2000);
        const uint32_t where[6] = {0x1000, 0x700, 0x900, 0x2010, 0x701, 0x1004};
        const uint32_t what[6] = {0xA1, 0xA2, 0x00005A01, 0xDEAD0077, 0xA5, 0xA6};
        for (unsigned i = 0; i < 6; i++) {
            r.to(a + i + 2 * L + 1);
            CHECK_EQ(r.sram().writes_served, uint64_t(i));   // one a cycle, none early
            CHECK_EQ(r.sram().peek(where[i]), 0u);
            r.to(a + i + 2 * L + 2);
            CHECK_EQ(r.sram().writes_served, uint64_t(i + 1));
            CHECK_EQ(r.sram().peek(where[i]), what[i]);
        }
        CHECK_EQ(r.sram().peek(0x901), 7u);                  // the BUCKET's second word
        CHECK_EQ(r.sram().peek(0x2011), (0x2010u - 0x2000u) / 2);   // the AUGMENT's ordinal
        CHECK_EQ(r.ptr(9), 0x702u);
        CHECK_EQ(r.ptr(0x5A), 0x902u);
        CHECK_EQ(r.ptr(3), 0x2012u);
    }
}

// 12.5: the update has a bypass, so no update to one entry is lost at any spacing, and no queue holds one back: APPENDs
// to one entry accepted every cycle, every second and every third each write at its own address and leave PTR advanced
// by all of them. 12.4: the engine never refuses, and holds at most the packets of its 2L + 1 stages.
TEST(the_bypass_loses_no_update_at_any_spacing) {
    for (unsigned L = 1; L <= 3; L++)
        for (unsigned gap = 1; gap <= 3; gap++) {
            const unsigned N = 40;
            std::vector<Stand::Ev> s{store_ptr(1, 9, 0x700)};
            for (unsigned i = 0; i < N; i++) s.push_back(pkt(5 + i * gap, lw::M_SRC, lw::SIZE_32, 0xC000 + i));
            Rig r(s, L);
            r.to(5 + N * gap + 20);
            for (unsigned i = 0; i < N; i++) CHECK_EQ(r.sram().peek(0x700 + i), 0xC000u + i);
            CHECK_EQ(r.ptr(9), 0x700u + N);
            CHECK_EQ(r.we().written, uint64_t(N));
            CHECK(r.we().stage_peak <= 2 * L + 1);
            CHECK_EQ(r.we().count, uint8_t(0));
        }
}

// 12.4: count-on-arrival. With a histogram digit, every APPEND, BUCKET or AUGMENT payload the engine writes increments
// HIST at that digit, a WRITE none; the bucket and the histogram updates are for different digits in one cycle. HIST is
// read at the end of a + L and written at the end of a + L + 1, with the same bypass.
TEST(count_on_arrival_increments_hist_at_the_histogram_digit) {
    for (unsigned L = 1; L <= 3; L++) {
        const uint64_t a = 8;
        // bucket_digit 2 (byte 1), histogram_digit 1 (byte 0): RECEIVE_CONFIG = 2 | 1 << 3
        std::vector<Stand::Ev> s{store_ptr(1, 9, 0x700), store_ptr(2, 0x5A, 0x900), store_hist(3, 0x33, 100),
                                 pkt(a - 1, lw::M_SRC, lw::SIZE_32, 0x00000033),
                                 pkt(a, lw::M_BUCKET, lw::SIZE_32, 0x00005A33),
                                 write_at(a + 1, 0x1000, lw::SIZE_32, 0x33),
                                 pkt(a + 2, lw::M_SRC, lw::SIZE_32, 0x00000033),
                                 pkt(a + 3, lw::M_SRC, lw::SIZE_32, 0x00000044)};
        Rig r(s, L, 2 | 1u << 3, 0, 9, 0x33);
        r.to(a + L);
        CHECK_EQ(r.hist(0x33), 100u);
        r.to(a + L + 1);
        CHECK_EQ(r.hist(0x33), 101u);                        // the first APPEND's
        r.to(a + L + 2);
        CHECK_EQ(r.hist(0x33), 102u);                        // the BUCKET's, the cycle after: the bypass
        r.to(a + L + 3);
        CHECK_EQ(r.hist(0x33), 102u);                        // the WRITE counts nothing
        r.to(a + 20);
        CHECK_EQ(r.hist(0x33), 103u);
        CHECK_EQ(r.hist(0x44), 1u);
        CHECK_EQ(r.ptr(0x5A), 0x901u);                       // the BUCKET's pointer moved in the cycle its HIST counted
        const Stand& st = r.stand();
        CHECK_EQ(st.hist_seen[a + L + 1], 100u);             // a load made in the cycle of the write: the old count
        CHECK_EQ(st.hist_seen[a + L + 2], 101u);
        for (uint64_t t = 2; t <= a + 20; t++) {
            const bool reads = t == a + L || t == a + L + 1 || t == a + L + 3 || t == a + L + 4;   // the four counted packets
            CHECK_EQ(unsigned(st.hist_read[t]), reads ? 1u : 0u);
        }
    }
}
// Without a histogram digit HIST is untouched.
TEST(no_histogram_digit_no_count) {
    Rig r({store_ptr(1, 9, 0x700), pkt(5, lw::M_SRC, lw::SIZE_32, 0x33)}, 1, 0, 0, 9, 0x33);
    r.to(30);
    CHECK_EQ(r.hist(0x33), 0u);
    CHECK_EQ(r.m.unit<HistStore>("hist").rmws, uint64_t(0));
    for (uint64_t t = 2; t <= 30; t++) CHECK_EQ(unsigned(r.stand().hist_read[t]), 0u);
}

// 12.5: the core shares the tables' two ports, and in a cycle the engine uses a port the engine wins: a core store made
// in the cycle of the engine's write (a + L + 1) is lost. A store made the cycle before lands and is then overwritten
// by the engine's write (which read the entry before it); one made the cycle after lands and stays.
TEST(a_core_store_in_the_cycle_of_the_engines_write_is_lost) {
    const unsigned L = 1;
    const uint64_t a = 6;
    {
        Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_32, 1), store_ptr(a + L + 1, 9, 0x5000)}, L);
        r.to(a + 12);
        CHECK_EQ(r.ptr(9), 0x701u);                          // the engine's; the core's store lost
        CHECK_EQ(r.m.unit<TableStore>("ts").pipe.ctrl_writes_lost, uint64_t(1));
    }
    {
        Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_32, 1), store_ptr(a + L, 9, 0x5000)}, L);
        r.to(a + L);
        r.to(a + L + 1);
        CHECK_EQ(r.ptr(9), 0x701u);                          // the engine read 0x700 in a + L and writes 0x701 after the store
        CHECK_EQ(r.m.unit<TableStore>("ts").pipe.ctrl_writes_lost, uint64_t(0));
    }
    {
        Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_32, 1), store_ptr(a + L + 2, 9, 0x5000)}, L);
        r.to(a + 12);
        CHECK_EQ(r.ptr(9), 0x5000u);
        CHECK_EQ(r.sram().peek(0x700), 1u);
    }
    {                                                        // HIST the same
        Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_32, 0x33), store_hist(a + L + 1, 0x33, 500)}, L, 1u << 3, 0, 9, 0x33);
        r.to(a + 12);
        CHECK_EQ(r.hist(0x33), 1u);
        CHECK_EQ(r.m.unit<HistStore>("hist").pipe.ctrl_writes_lost, uint64_t(1));
    }
}

// 12.3: the counter's number enters the delay line at the acceptance, whatever the packet's path: on the rig's line of
// LAND_LATENCY + 1 = 9 it is at the tile control unit's edge 9 cycles after a.
TEST(a_counted_write_enters_the_delay_line_at_its_acceptance) {
    const uint64_t a = 6;
    Rig r({store_ptr(1, 9, 0x700), pkt(a - 1, lw::M_SRC, lw::SIZE_32, 1, 0, 9, 4), write_at(a, 0x1000, lw::SIZE_32, 2)});
    r.to(a + 12);
    CHECK_EQ(r.stand().kline_at.size(), size_t(1));          // the WRITE's counter field is 0: uncounted
    CHECK_EQ(r.stand().kline_at[0], (a + 9) << 8 | 4);
}

// 12.4: a TABLE_LATENCY whose pipeline would miss LAND_LATENCY is refused (the RTL's unit refuses 4 as it elaborates).
DEATH_TEST(table_latency_4_is_refused, "TABLE_LATENCY 4") {
    sim::Config cfg;
    cfg.add<WriteEngine>("we", 4u, 1u << 20);
}
