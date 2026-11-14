// test_mach.cpp: the machine harness (mach.h) on v1.36's hardware.
//
// WHAT IT CHECKS, AND AGAINST WHAT.
//   - mach::fields against machine-spec's bit positions (5.4, 8.1, 8.3, 8.4, 12.1, 13), written out here by hand.
//   - The coreless machine fed from the harness (Net) against v1.3's own run of the same sequences (the tail of
//     tests/mach/netself.out, ref's NetSelf.java: `packets`, `reads`, `a2a`): the network, the director, the write
//     engine, the address generator, the tables and the scratchpad, which are still v1.35's units (PENDING-v1.36.md,
//     section 4), configured through v1.36's layouts and back doors. The cycles are v1.3's.
//   - The machine as specified: the RTL's boot ROM's pull boot (machine-spec 6.5; rtl/blocks/core/src/boot_rom.S) on all
//     64 tiles, the master side's model standing where the master tile will: each tile's LOAD request, the image and the
//     COUNT on counter 7 sent to it, its ARRIVE, the release, the image entered. Expectations from 6.5, 7.2, 7.3, 11.5.
//   - The back doors, the model's wait_until, mem_idle over a wrapped queue, MachConfig's front end; and that the machine
//     with v1.35's C&C 1-3 as cores builds and steps as mach.h says it does.
//
// WHAT v1.35's TEST CHECKED THAT IS NOT HERE, AND WHY (PENDING-v1.36.md, section 2):
//   - ref's MachSelf.java sequence (tests/mach/machself.out): every case boots a v1.35 image through C&C's commands
//     (CMD_JUMP, the command block) or C&C 1-3 as cores with cc1.c, and reads SIM_EXIT, TRACE and v1.3's phase log:
//     hardware and firmware v1.36 does not have. The fixture stays; nothing runs it.
//   - ref's NetSelf.java cases other than the three above (tests/mach/netself.out, its first 47 lines): `fig12b`,
//     `figure12`, `core`, `hooks`, `ub`, `test peripheral` drive v1.35's register map through the core from the test
//     (a test's loads and stores: removed with the map); `drain` and `back-pressure` answer v1.35's RESULT requests
//     and read its 128-entry request FIFO (v1.36's queue is 4 deep, and op 3 is LOAD); `reqbus`, `on machine`,
//     `feed`, `phaseDone` and `controllers` print cycles that depend on v1.35's request FIFO depth, its attach tiles
//     (0, 1, 2) or the register block's view at the end of a cycle; they come back with the request tree (P7) and the
//     old suites (P11).
//   - the replayer's row hook (Chip::on_row): the row port comes with the replayer's stage (17.8).
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "base/fatal.h"
#include "harness.h"
#include "mach.h"
#include "rv32_asm.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/gather.h"
#include "units/cnc.h"
#include "units/linkword.h"
#include "units/table_store.h"
#include "units/tile_ctl.h"

using namespace mach::fields;
using mach::Mach;

// The encoders against the specification's layouts. Each expected value is the fields placed by hand:
//   a header block (5.4)   size [1:0], counter [4:2], mode [7:5], dst [14:8]
//   STREAM_HDR (8.3)       the header block, count [31:15]
//   GROUP (8.4)            word 0: rotations [17:0], first_stream [25:18], streams - 1 [29:26]; word 1: gap [7:0]
//   DMA_CONFIG (8.1)       kind [0], start [8:1], count [17:9], gap [20:18]
//   RECEIVE_CONFIG (12.1)  bucket_digit [2:0], histogram_digit [5:3]
//   GATHER_CONFIG (13)     record_base [20:0], value_size (k - 1) [23:21]
TEST(mach_fields_are_the_specifications) {
    CHECK_EQ(hdr_block(SIZE_128, 7, 7, 127), 0x7FFEu);                 // 2 | 7 << 2 | 7 << 5 | 127 << 8
    CHECK_EQ(hdr_block(SIZE_64, 1, 1, 63), 0x3F25u);                   // 1 | 1 << 2 | 1 << 5 | 63 << 8
    CHECK_EQ(stream_hdr(0x1FFFF, SIZE_64, 1, 1, 63), 0xFFFFBF25u);     // the block above, count 0x1FFFF << 15
    CHECK_EQ(stream_hdr(1, SIZE_32, 0, 0, 0), 0x8000u);
    CHECK_EQ(stream_src(0xFFFFFFFFu), 0x1FFFFFu);
    CHECK_EQ(group_w0(0xFFFFFFFFu, 4, 4), 0x0C13FFFFu);                // 0x3FFFF | 4 << 18 | 3 << 26
    CHECK_EQ(group_w0(5, 255, 16), 0x3FFC0005u);                       // 5 | 255 << 18 | 15 << 26
    CHECK_EQ(group_w1(0x1FF), 0xFFu);
    CHECK_EQ(dma_config(RUN_GROUPS, 0, 16), 0x2001u);                  // 1 | 16 << 9
    CHECK_EQ(dma_config(RUN_STREAMS, 255, 511, 7), 0x1FFFFEu);         // 255 << 1 | 511 << 9 | 7 << 18
    CHECK_EQ(receive_config(4, 3), 0x1Cu);                             // 4 | 3 << 3
    CHECK_EQ(gather_config(0x1FFFFF, 8), 0xFFFFFFu);                   // 0x1FFFFF | 7 << 21
    CHECK_EQ(gather_config(0x40000, 1), 0x40000u);
}

namespace {
std::string out;
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    std::string b(size_t(n > 0 ? n : 0), '\0');
    std::vsnprintf(b.data(), b.size() + 1, fmt, ap);
    va_end(ap);
    out += b;
}
const char* tf(bool b) { return b ? "true" : "false"; }
std::string file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) FATAL("test_mach: cannot read %s (run from the version's folder)", path.c_str());
    std::string b;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) b.append(buf, n);
    std::fclose(f);
    return b;
}

// ---- oracle/ref/NetSelf.java's `packets`, `reads` and `a2a` (the tail of tests/mach/netself.out) ----
namespace lw = units::lw;
using mach::Net;
// ref's Net.cm: a v1.1 packed word address of the core's memories as a byte address (below 0x800 IMEM, else DMEM)
uint32_t cm(uint32_t w) { return w < 0x800 ? uint32_t(units::R_IMEM) << 24 | w << 2 : (w - 0x800) << 2; }
std::string hx(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }
std::string w(Net& n, unsigned tile, uint32_t word) { return hx(n.c.tile(tile).peek_local(4 * word)); }
void logs(Net& n, std::initializer_list<unsigned> tiles) {
    for (unsigned k : tiles) {
        const auto l = n.log(k);
        std::string sb = "  tile " + std::to_string(k) + ": " + std::to_string(l.size()) + " arrivals";
        for (size_t i = 0; i < std::min<size_t>(l.size(), 6); i++)
            sb += " [" + std::to_string(l[i][0]) + " " + hx(l[i][1]) + " " + hx(l[i][2]) + " " + hx(l[i][3]) + "]";
        say("%s\n", sb.c_str());
    }
}
void packets(int64_t seed) {
    Net n(Net::cfg(seed));
    uint64_t seq = 0;
    for (unsigned s = 0; s < 64; s += 7) n.at(s, 20, 9, lw::M_WRITE, cm(0x800 + 2 * s), Net::tagged(seq++, 20));
    for (int64_t T = 30; T < 40; T++) n.at(0, T, 63, lw::M_WRITE, cm(0x900 + 2 * uint32_t(T - 30)), Net::tagged(seq++, T));
    n.at(5, 50, lw::BROADCAST, lw::M_WRITE, 0, cm(0x880), lw::SIZE_128, 0x1111222233334444ull, 0x5555666677778888ull);
    n.at(9, 51, 9, lw::M_WRITE, 0, cm(0x881), lw::SIZE_32, 0xABCD, 0);
    for (unsigned i = 0; i < 3; i++) n.at(17, 60 + i, 17, lw::M_GATHER, 0, i, lw::SIZE_64, 0x40 + i, 0);
    const bool ok = n.run_all(100000);
    say("packets seed %lld: %s at cycle %lld; late %lld; router peak %u; t17 drained %llu; t9 dmem[0xe] %s dmem[0x80] %s; t63 dmem[0x10e] %s; "
        "t40 dmem[0x80..0x83] %s %s %s %s\n", (long long)seed, ok ? "quiescent" : "NOT quiescent", (long long)n.c.cycle(),
        (long long)n.late(), n.max_router_peak(), (unsigned long long)n.c.tile(17).unit<units::Gather>("gather").drained,
        w(n, 9, 0xe).c_str(), w(n, 9, 0x80).c_str(), w(n, 63, 0x10e).c_str(), w(n, 40, 0x80).c_str(), w(n, 40, 0x81).c_str(),
        w(n, 40, 0x82).c_str(), w(n, 40, 0x83).c_str());
    logs(n, {9, 63, 40, 5, 17});
}
void reads() {
    mach::MachConfig k = Net::cfg(-1);
    k.test_read_clients = true;
    Net n(k);
    for (uint32_t i = 0; i < 16; i++) n.c.tile(3).sram_poke(0x2000 + i, 0x1000 * i + 3);
    n.rd(3, 2, 10, 0x2000, lw::SIZE_64, lw::M_WRITE, 0, 12, cm(0x8A0));
    n.rd(3, 0, 10, 0x2004, lw::SIZE_128, lw::M_WRITE, 0, 12, cm(0x8A4));
    n.rd(3, 1, 11, 0x2008, lw::SIZE_32, lw::M_WRITE, 0, 3, cm(0x8A8));
    for (uint32_t i = 0; i < 70; i++) n.rd(3, 2, 12, 0x2000 + (i & 15), lw::SIZE_32, lw::M_WRITE, 0, 20 + (i & 7), cm(0x8C0 + (i & 31)));
    const bool ok = n.run_all(100000);
    say("reads: %s at cycle %lld; late %lld; t12 dmem[0xa0..0xa7] %s %s %s %s %s %s; t3 dmem[0xa8] %s\n", ok ? "quiescent" : "NOT quiescent",
        (long long)n.c.cycle(), (long long)n.late(), w(n, 12, 0xa0).c_str(), w(n, 12, 0xa1).c_str(), w(n, 12, 0xa4).c_str(),
        w(n, 12, 0xa5).c_str(), w(n, 12, 0xa6).c_str(), w(n, 12, 0xa7).c_str(), w(n, 3, 0xa8).c_str());
    logs(n, {12, 3, 21});
}
void a2a_net(int64_t seed, int gap) {
    mach::MachConfig k = mach::MachConfig::defaults();
    k.shuffle_seed = seed; k.check_errors = false;
    Net n(k);
    mach::A2A d(7, 2000);
    d.load(n.c, gap);
    const int64_t cyc = mach::A2A::run(n.c, 2000000);
    mach::TileRef t5 = n.c.tile(5);
    const units::AddrGen& ag = t5.unit<units::AddrGen>("ag");
    say("a2a seed %lld gap %d: %lld cycles (at cycle %lld); misplaced %d; t5 ag requests %llu done %s; t5 remaining[1] %u memIdle %s; t5 ptr[9] %s\n",
        (long long)seed, gap, (long long)cyc, (long long)n.c.cycle(), d.verify(n.c), (unsigned long long)ag.requests, tf(ag.done),
        t5.remaining(1), tf(t5.mem_idle()), hx(t5.ptr(9)).c_str());
    const bool ok = n.run_all(10000);
    say("  then %s at cycle %lld; t5 gather drained %llu\n", ok ? "quiescent" : "NOT quiescent", (long long)n.c.cycle(),
        (unsigned long long)t5.unit<units::Gather>("gather").drained);
}
}  // namespace

TEST(mach_net_as_refs_Net) {
    out.clear();
    packets(-1); packets(5);
    reads();
    a2a_net(-1, 0); a2a_net(4, 0); a2a_net(-1, 8);
    const std::string all = file("tests/mach/netself.out");
    const size_t at = all.find("packets seed -1:");
    CHECK(at != std::string::npos);
    const std::string want = all.substr(at);                    // NetSelf's last three cases, to the fixture's end
    CHECK(want.find("a2a seed -1 gap 8: 3240 cycles") != std::string::npos);   // (the tail is what this test believes it is)
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/netself.out, from `packets seed -1`)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}

// The boot ROM's pull boot (6.5) on the machine as specified, the model in the master tile's place.
//   1. Each compute tile's ROM arms COUNTER_7 with 1 and sends a LOAD request (11.5: word 0's op 3 at [17:10], the
//      requester the tree's, the target 0, the rest 0; words 1-3 0), then waits for all_counters_done.
//   2. The harness, as the master would: the image written into every tile's IMEM, then a COUNT on counter 7 (4.1: it
//      counts and writes nothing), which drains the counter.
//   3. Each tile stores to ARRIVE and waits in the canonical wait (7.3); BARRIER_ARRIVE pulses; the model, with every
//      tile arrived, releases; the ROM consumes the release and enters the image at IMEM's base.
// The image: 0x5A to DMEM's word at byte 0x10, then EVENTS' value to byte 0x14, STATUS' to 0x18 and ERRORS' to 0x1C,
// then a WFI for ever.
TEST(mach_boots_as_the_boot_rom_pulls) {
    using namespace rv32_asm;
    mach::MachConfig k = Mach::cfg(-1);
    k.reqbus_host = true;
    Mach m(k);
    m.c.cc().drain_reports(true);
    // 1. the 64 LOAD requests
    CHECK(m.run([&] { return m.c.cc().requests().size() == 64; }, 5000));
    CHECK_EQ(m.c.cc().reports().size(), 0u);
    uint64_t seen = 0;
    for (const auto& r : m.c.cc().requests()) {
        const uint32_t w0 = uint32_t(r[1]);
        CHECK_EQ(w0 & ~0x7Fu, 3u << 10);                         // op LOAD, target 0, nothing else
        CHECK(r[2] == 0 && r[3] == 0 && r[4] == 0);
        seen |= uint64_t(1) << (w0 & 0x3F);
        CHECK((w0 & 0x7F) < 64);
    }
    CHECK_EQ(seen, ~uint64_t(0));                                // one from each tile
    for (unsigned t = 0; t < 64; t++) {
        CHECK_EQ(m.c.tile(t).remaining(7), 1u);                  // armed, not yet drained
        CHECK(m.c.tile(t).in_wfi());
        CHECK_EQ(m.c.tile(t).status() & 1u, 0u);                 // not arrived
    }
    // 2. the image and the COUNT; 3. the release
    const std::vector<uint32_t> image = {
        ADDI(5, 0, 0x5A), SW(5, 0, 0x10),                        // the mark
        LW(6, 0, -0x7F4), SW(6, 0, 0x14),                        // EVENTS (0xFFFF_F80C)
        LW(6, 0, -0x7FC), SW(6, 0, 0x18),                        // STATUS (0xFFFF_F804)
        LW(6, 0, -0x7F8), SW(6, 0, 0x1C),                        // ERRORS (0xFFFF_F808)
        WFI, J(-4, 0), NOP, NOP,
    };
    int64_t released_at = -1;
    m.c.cc().load_words(lw::BROADCAST, units::IMEM_BASE, image)
        .send(lw::BROADCAST, lw::M_COUNT, 7, 0, lw::SIZE_32, 0)
        .run_when_ready()
        .call([&](int64_t) { released_at = m.c.cc().last_run_at(); });
    CHECK(m.run([&] { return released_at >= 0; }, 20000));
    CHECK(m.run([&] { for (unsigned t = 0; t < 64; t++) if (m.word(t, 0x10) != 0x5A) return false; return m.all_in_wfi(); }, 5000));
    m.c.run(50);
    const auto pl = m.c.cc().phase_log();
    CHECK_EQ(pl.size(), 1u);
    if (pl.size() == 1) {
        CHECK(pl[0][0] > 0);                                     // every tile seen arrived,
        CHECK(pl[0][1] >= pl[0][0] + int64_t(k.cnc_reaction));   // then the release
    }
    for (unsigned t = 0; t < 64; t++) {
        mach::TileRef p = m.c.tile(t);
        CHECK_EQ(m.word(t, units::IMEM_BASE), image[0]);
        CHECK_EQ(m.word(t, 0x10), 0x5Au);
        CHECK_EQ(m.word(t, 0x14), 0u);                           // EVENTS: the ROM consumed the release; ARRIVE cleared
                                                                 //   the counter events (7.2)
        CHECK_EQ(m.word(t, 0x18), 0u);                           // STATUS: arrived fell at the release (7.3)
        CHECK_EQ(m.word(t, 0x1C), 0u);                           // ERRORS: none
        CHECK_EQ(p.remaining(7), 0u);
        CHECK_EQ(p.block().arrivals, 1u);
        CHECK_EQ(p.block().releases, 1u);
        CHECK_EQ(p.errors(), 0u);
        CHECK(p.in_wfi());
        CHECK(p.pc() >= units::IMEM_BASE && p.pc() < units::IMEM_BASE + 4 * image.size());
    }
    CHECK_EQ(m.c.violations(), 0u);
}

// The back doors into the tile control unit's registers: each is published at the unit's next step with its register's
// bits alone (regmap.h's masks, the specification's widths), and the units behind it read it.
TEST(mach_back_doors_reach_the_units) {
    mach::Chip c(mach::MachConfig::defaults());
    mach::TileRef p = c.tile(11);
    p.set_dma_config(0xFFFFFFFFu);
    p.set_receive_config(receive_config(4, 2));
    p.set_gather_config(0xFFFFFFFFu);
    p.set_remaining(3, 0xFFFFFFFFu);
    p.set_errors(1u << units::ERR_DRIFT);
    p.set_events(1u << units::EV_DMA_DONE);
    c.step();
    CHECK_EQ(p.reg(units::DMA_CONFIG), 0x1FFFFFu);               // 21 bits (8.1)
    CHECK_EQ(p.reg(units::RECEIVE_CONFIG), 0x14u);
    CHECK_EQ(p.reg(units::GATHER_CONFIG), 0xFFFFFFu);            // 24 bits (13)
    CHECK_EQ(p.remaining(3), 0xFFFFFu);                          // 20 bits (12.2)
    CHECK_EQ(p.reg(units::COUNTER_1 + 2), 0xFFFFFu);
    CHECK_EQ(p.errors(), 1u << units::ERR_DRIFT);
    CHECK(p.block().nmi());                                      // 7.4: any error raises the NMI and the error line
    CHECK_EQ(p.events(), 1u << units::EV_DMA_DONE);
    CHECK_EQ(c.tile(12).errors(), 0u);                           // another tile's: untouched
    CHECK_EQ(c.tile(12).reg(units::DMA_CONFIG), 0u);
    // the tables: a poke keeps the word's field bits alone (5.2)
    p.table_poke(units::TBL_PTR, 9, 0xFFFFFFFFu);
    p.table_poke(units::TBL_GROUP, 2 * 3, 0xFFFFFFFFu);
    p.table_poke(units::TBL_GROUP, 2 * 3 + 1, 0xFFFFFFFFu);
    p.table_poke(units::TBL_STREAM_HDR, 200, 0xDEADBEEFu);
    p.hist_poke(7, 0xFFFFFFFFu);
    CHECK_EQ(p.ptr(9), 0x1FFFFFu);                               // a 21-bit word address
    CHECK_EQ(p.table_peek(units::TBL_GROUP, 6), 0x3FFFFFFFu);    // GROUP word 0: 30 bits
    CHECK_EQ(p.table_peek(units::TBL_GROUP, 7), 0xFFu);          // word 1: the gap's 8
    CHECK_EQ(p.table_peek(units::TBL_STREAM_HDR, 200), 0xDEADBEEFu);
    CHECK_EQ(p.hist_peek(7), 0x1FFFFu);                          // 17 bits (12.5)
    CHECK_EQ(p.table_peek(units::TBL_GROUP, 8), 0u);
    // DMA_START's start: a stream run of one empty stream ends at once and sets dma_done (8.2)
    p.set_stream(0, stream_src(0x100), 0, stream_hdr(0, SIZE_64, 1, lw::M_WRITE, 0));
    p.set_dma_config(dma_config(RUN_STREAMS, 0, 1));
    c.step();
    CHECK_EQ(p.events(), 1u << units::EV_DMA_DONE);              // still the one set above
    p.dma_start();
    CHECK_EQ(p.events(), 0u);                                    // the start clears dma_done
    CHECK(c.run_until([&] { return (p.events() >> units::EV_DMA_DONE & 1) != 0; }, 200));
    CHECK_EQ(p.status() >> units::ST_DMA_BUSY & 1u, 0u);
}

// Cc::wait_until: the harness checks the condition between cycles once the model is at the step, and the model moves on
// in the next cycle.
TEST(mach_wait_until_releases_the_next_cycle) {
    mach::Chip c(mach::MachConfig::defaults());
    int64_t first_true = -1, ran = -1, reached = -1, checks_before = 0;
    c.cc().wait_cycles(7).call([&](int64_t t) { reached = t; }).wait_until([&] {
        const bool b = c.cycle() >= 30;
        if (!b) checks_before++;
        if (b && first_true < 0) first_true = c.cycle();
        return b;
    }).call([&](int64_t t) { ran = t; });
    CHECK(c.run_until([&] { return ran >= 0; }, 1000));
    CHECK(reached >= 7);                                      // the wait before it was served first
    CHECK(checks_before > 0 && checks_before <= 30 - reached + 1);   // checked only once the model reached the step
    CHECK_EQ(first_true, 30);
    CHECK_EQ(ran, first_true + 1);
}

// mem_idle reads an area queue as it wraps, at its configured depth (8), not at the array's (16): a write queued in the
// slot after the wrap holds mem_idle low; a stale write past the depth doesn't.
TEST(mach_mem_idle_sees_a_wrapped_queue) {
    mach::Chip c(mach::MachConfig::defaults());
    mach::TileRef p = c.tile(3);
    units::BigSram& m = p.unit<units::BigSram>("sram");
    CHECK_EQ(m.cfg.area_queue_depth, 8u);
    CHECK(p.mem_idle());
    m.head[5] = 7; m.count[5] = 2;                           // entries in slots 7 and 0
    m.q[5][7].write = false; m.q[5][0].write = true; m.q[5][8].write = false;
    CHECK(!p.mem_idle());                                    // the write in slot 0 (not slot 8)
    m.q[5][0].write = false; m.q[5][8].write = true;         // a stale write past the depth
    CHECK(p.mem_idle());
    m.count[5] = 0;
}

// MachConfig's front end (`cfg.<field>=value`): v1.3's names and types, the dropped fields, the errors; and the defaults
// that are the specification's (19): REQUEST_QUEUE_DEPTH 4, GATHER_QUEUE_DEPTH 64, LAND_LATENCY 8, WRITE_PORT_LATENCY 2.
TEST(mach_config_front_end) {
    mach::MachConfig k = mach::MachConfig::machine();
    CHECK_EQ(k.request_queue_depth, 4u);
    CHECK_EQ(k.gather_depth, 64u);
    CHECK_EQ(k.K, 8u);
    CHECK_EQ(k.cm_latency, 2u);
    std::string err;
    CHECK(k.set_args({"--x", "cfg.goLatency=10", "cfg.skewSeed=-3", "cfg.writeCombining=true", "cfg.replayerLogEntries=512",
                      "cfg.requestFifoDepth=8"}, err));
    CHECK_EQ(k.go_latency, 10u);
    CHECK_EQ(k.skew_seed, -3);
    CHECK(k.write_combining);
    CHECK_EQ(k.replayer_log_entries, 512u);
    CHECK_EQ(k.request_queue_depth, 8u);
    CHECK(k.set("checkDisjoint", "TRUE", err) && k.check_disjoint);   // Boolean.parseBoolean: case-insensitive
    CHECK(k.set("checkDisjoint", "yes", err) && !k.check_disjoint);
    CHECK(!k.set("noSuchField", "1", err) && err == "no Config field noSuchField");
    CHECK(!k.set("idleSkip", "false", err) && err.find("dropped") != std::string::npos);
    // v1.36: the parameters of hardware that is gone are refused, each by name, never silently taken
    for (const char* gone : {"doneGate", "stageDelay", "runLatency", "counterStoreClearsLatch", "strictReplies", "checkCommandRead", "logTrace"})
        CHECK(!k.set(gone, "true", err) && err.find("dropped") != std::string::npos && err.find(gone) != std::string::npos);
    CHECK(!k.set("K", "eight", err) && err.find("not a number") != std::string::npos);
    CHECK(!k.set_args({"cfg.K"}, err));
    CHECK(!mach::MachConfig::dropped().empty());
}

// The machine with v1.35's C&C 1-3 as cores (mach.h, THE MASTER SIDE): it builds and steps with the special tiles'
// straps and the RTL's boot ROM, and no image. Units 64 and 65 have boot_local (6.5), so their ROM enters an empty
// IMEM: the all-zero word is an illegal instruction, which sets ERRORS.illegal_instruction (7.4). Unit 66 and the
// compute tiles have armed COUNTER_7 and wait for a load nobody sends. The host port's write lands in unit 64's IMEM.
TEST(mach_cores_machine_builds_and_steps) {
    Mach m(Mach::cores_cfg(-1));
    m.c.run(20);
    m.c.host_load(0, units::IMEM_BASE + 0x100, {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u});
    m.c.run(400);
    for (unsigned i = 0; i < 4; i++) CHECK_EQ(m.word(64, units::IMEM_BASE + 0x100 + 4 * i), 0x11111111u * (i + 1));
    CHECK_EQ(m.word(65, units::IMEM_BASE + 0x100), 0u);
    for (unsigned u : {64u, 65u}) {
        CHECK(m.c.tile(u).errors() >> units::ERR_ILLEGAL_INSTRUCTION & 1);
        CHECK_EQ(m.c.tile(u).reg(units::PARAMS) >> 18 & 1u, 1u);            // boot_local
        CHECK_EQ(m.c.tile(u).reg(units::PARAMS) >> 17 & 1u, 0u);            // no barrier
    }
    for (unsigned u : {66u, 0u, 37u, 63u}) {
        CHECK_EQ(m.c.tile(u).errors(), 0u);
        CHECK_EQ(m.c.tile(u).remaining(7), 1u);
        CHECK(m.c.tile(u).in_wfi());
        CHECK_EQ(m.c.tile(u).reg(units::PARAMS) >> 18 & 1u, 0u);
    }
    CHECK_EQ(m.c.tile(66).reg(units::PARAMS) & 0x7Fu, 66u);
}
