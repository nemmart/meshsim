// suite_hooks.cpp (WORKER23, P5, inventory C15): v1.3's HooksTest (sim/ref/meshsim/test/HooksTest.java) on W20's
// harness, in Java's order and words. run_tests.sh's steps `hooks` (-1) and `hooks-shuf` (4); its output equals ref's
// log (tests/ctlsuites/hooks*.log). Usage: suite_hooks [shuffleSeed]
//
// The v1.2 register map and the hooks a test can drive without a tile image (the C side is CtrlTest's): the map (8.1),
// the pulses (8.6), the send port (8.4), a tile with no peripheral, the load latencies (4.11) and the sampling rule.
//
// ONE ADAPTATION (NOTES.md): ref's `core.store(t, ...)` from Java applies the store at once; the harness's
// TileRef::store applies it in the core's step at t. Where ref counts a store's effect before its next cycle (the
// reserved words' "both flagged"), the suite steps that cycle first.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "base/fatal.h"
#include "units/ag.h"
#include "units/director.h"
#include "../../tools/regs/regtable.h"
#include "../suite.h"
#include "ctl.h"

using namespace ctl;
using mach::Chip;
using mach::MachConfig;
using mach::Net;
namespace F = mach::fields;
namespace lw = units::lw;

namespace {

int64_t shuffle = -1;

uint32_t addr(const char* name) { for (const regs::Reg& g : regs::ALL) if (std::string(g.name) == name) return regs::REG_BASE + 4u * uint32_t(g.index); FATAL("no register %s", name); }
const regs::Reg* by_name(const char* name) { for (const regs::Reg& g : regs::ALL) if (std::string(g.name) == name) return &g; return nullptr; }
uint32_t place(const char* word, const char* name, uint32_t v) { const regs::Field& f = regs::field(word, name); return (v & f.mask()) << f.lo; }
uint32_t pfield(const char* name, uint32_t v) { const regs::Field& f = regs::field("PARAMS", name); return (v >> f.lo) & f.mask(); }
constexpr uint32_t PTR_BASE = 0xFFFF2400, STREAM_SRC_BASE = 0xFFFF2800, STREAM_DEST_BASE = 0xFFFF2C00, STREAM_COUNT_BASE = 0xFFFF3000,
                   GROUP_BASE = 0xFFFF3400, HIST_BASE = 0xFFFF2000, TABLE_END = 0xFFFF3600, REPLY_BASE = 0x7BC0;
constexpr uint32_t TS_GROUP = 0x400, TS_STREAM_SRC = 0x1000, TS_STREAM_DEST = 0x1100, TS_STREAM_COUNT = 0x1200;
uint32_t window_base(int n) { return regs::window_base(n); }

// ref's tile.ctl.violations: the block's own and the core's (ref counts both in TileControl)
uint64_t ctl_violations(Chip& c, unsigned k) { return c.tile(k).block().violations + core(c, k).violations; }

// Every core has reached the boot ROM's WFI (done pulsed): the test may now drive the registers without racing the ROM.
void settle(Net& n) {
    for (int i = 0; i < 200; i++) {
        bool all = true;
        for (unsigned k = 0; k < 64; k++) if (n.c.cfg().coreless ? false : !core(n.c, k).in_wfi) all = false;
        if (all) break;
        n.step();
    }
}
// A specified machine (controllers on, TILE_STATUS computed) whose tiles all sit in a WFI loop at the loader.
// periph_type 0: no peripheral on any tile (these hooks test the tile's side); 4: the replayer.
MachConfig machine_cfg(int periph_type) { MachConfig k = MachConfig::machine(); k.shuffle_seed = shuffle; k.periph_type = periph_type; return k; }
int32_t load(Net& n, unsigned k, uint32_t a) {
    const std::optional<uint32_t> v = n.c.tile(k).load(n.c.cycle() + 1, a, 4);
    if (!v) FATAL("hooks: tile %u: a load from 0x%x is a bus error (ref: a NullPointerException)", k, a);
    return int32_t(*v);
}
std::optional<uint32_t> try_load(Chip& c, unsigned k, uint32_t a, unsigned width = 4) { return c.tile(k).load(c.cycle() + 1, a, width); }
std::string try_store(Chip& c, unsigned k, uint32_t a, uint32_t v, uint32_t mask = 0xFFFFFFFFu) { return c.tile(k).store(c.cycle() + 1, a, v, mask); }
void store(Net& n, unsigned k, uint32_t a, uint32_t v) {
    const std::string e = try_store(n.c, k, a, v);
    if (!e.empty()) FATAL("hooks: tile %u: %s (ref: IllegalStateException)", k, e.c_str());
    n.c.step();
}

// ================================================================ the pulses (8.6)
void pulses() {
    suite::group("pulses");
    Net n(machine_cfg(0)); settle(n);
    const unsigned p = 7;
    suite::eq(0xFFFFF80CLL, int64_t(addr("PULSE_DONE")), "PULSE_DONE at 0x80C");
    suite::eq(0xFFFFF810LL, int64_t(addr("PULSE_DMA")), "PULSE_DMA at 0x810");
    const uint64_t v0 = ctl_violations(n.c, p), dones = n.c.tile(p).block().dones;
    store(n, p, addr("PULSE_DONE"), 0); store(n, p, addr("PULSE_DMA"), 0);
    n.c.run(5);
    const units::AddrGen& ag = n.c.unit<units::AddrGen>(unit_name(p, "ag"));
    suite::check(n.c.tile(p).block().dones == dones && ag.started_at < 0, "a store of 0 does nothing");
    suite::eq(int64_t(v0), int64_t(ctl_violations(n.c, p)), "and isn't flagged");
    // the old PULSE_GO word (0x814) is reserved: stores ignored, flagged
    store(n, p, regs::REG_BASE + 0x14, 1);
    suite::eq(int64_t(v0 + 1), int64_t(ctl_violations(n.c, p)), "a store to 0x814 (reserved; PULSE_GO moved to C&C 1's peripheral) is flagged");
    suite::eq(0, load(n, p, regs::REG_BASE + 0x14), "and the word reads 0");
    // PULSE_DMA starts the run and clears EVENTS [10]; a start while running is UB (v1.25)
    n.c.tile(p).set_master_config(F::master_config(F::RUN_STREAMS, 0, 0, 0, 0, 1));
    store(n, p, addr("PULSE_DMA"), 1);
    n.c.run(10);
    suite::check(ag.started_at >= 0 && ag.done, "PULSE_DMA starts the run (a count-0 run ends at once)");
    const int32_t rd = 1 << regs::EV_RUN_DONE;
    suite::eq(rd, load(n, p, addr("EVENTS_PEEK")) & rd, "DMA run done ([10]) set when rd_done rose");
    n.c.tile(p).set_stream(0, F::stream_src(0), 0, F::stream_count(400, F::SIZE_32, 1, lw::M_COUNT, 0));
    n.c.tile(p).set_master_config(F::master_config(F::RUN_STREAMS, 0, 1, 0, 0, 1));
    n.c.tile(p).set_remaining(1, 400);
    store(n, p, addr("PULSE_DMA"), 1);
    suite::eq(0, load(n, p, addr("EVENTS_PEEK")) & rd, "PULSE_DMA cleared [10]");
    n.c.run(10);
    n.c.run_until([&] { return ag.done && n.c.tile(p).remaining(1) == 0; }, 5000);
    suite::eq(0, int64_t(n.c.tile(p).block().conditions), "no error");
    suite::eq(0, load(n, p, addr("PULSE_DMA")), "PULSE_DMA reads 0");
    // v1.25 (DECISIONS-v1.25 s8e): PULSE_DMA while the run runs is undefined behavior (v1.2 ignored it): meshsim stops
    n.c.tile(p).set_remaining(1, 400);
    store(n, p, addr("PULSE_DMA"), 1); n.c.run(10);
    store(n, p, addr("PULSE_DMA"), 1);
    suite::ub([&] { n.c.run(2); }, {"tile 7", "address generator", "a start while a run is running"}, "PULSE_DMA while the run runs");
}

// ================================================================ the map and the bus errors (8.1, 4.11)
void bus_errors() {
    suite::group("register map and window");
    Net n(machine_cfg(0)); settle(n);
    const unsigned p = 2;
    const char* names[] = {"PARAMS", "STATUS", "ERRORS", "PULSE_DONE", "PULSE_DMA", nullptr, "EVENTS", "EVENTS_PEEK", "EVENTS_MASK", "EVENTS_REQUIRED",
                           "ROUTER_HDR", "ROUTER_ADDR", "ROUTER_SET_W0", "ROUTER_SET_W1", "ROUTER_SET_W2", "ROUTER_SEND_32", "ROUTER_SEND_64", "ROUTER_SEND_128",
                           "REQUEST_SET_W0", "REQUEST_SET_W1", "REQUEST_SET_W2", "REQUEST_SEND", "DMA_CONFIG", "DMA_RECORD_BASE_ADDRESS",
                           "WRITE_COUNTER_1", "WRITE_COUNTER_2", "WRITE_COUNTER_3", "WRITE_COUNTER_4", "WRITE_COUNTER_5", "WRITE_COUNTER_6", "WRITE_COUNTER_7",
                           "SRAM_RD_ADDR_QUEUE", "SRAM_RD_DEST_ADDR"};
    for (int i = 0; i < int(sizeof names / sizeof names[0]); i++) {
        if (!names[i]) continue;
        const regs::Reg* r = by_name(names[i]);
        suite::check(r && regs::REG_BASE + 4u * uint32_t(r->index) == 0xFFFFF800u + 4u * uint32_t(i), "%s at offset 0x%x", names[i], 0x800 + 4 * i);
    }
    const char* loads[] = {"REQUEST_GET_W0", "REQUEST_GET_W1", "REQUEST_GET_W2", "REQUEST_RECEIVE"};
    for (int i = 0; i < 4; i++)
        suite::check(regs::load_at(18 + i) == by_name(loads[i]) && regs::store_at(18 + i) == by_name(names[18 + i]),
                     "offset 0x%x: a store reaches %s, a load %s", 0x848 + 4 * i, names[18 + i], loads[i]);
    // reserved words: read 0, stores ignored, flagged (8.1)
    for (uint32_t off : {0x814u, 0x884u, 0x900u, 0x9F4u}) {
        const uint32_t a = 0xFFFFF000u | off; const uint64_t v0 = core(n.c, p).reserved_accesses;
        suite::check(try_load(n.c, p, a) == std::optional<uint32_t>(0), "offset 0x%x reserved: reads 0", off);
        suite::check(try_store(n.c, p, a, 5).empty(), "a store there is ignored, not a bus error");
        n.c.step();                                                // the adaptation: the store applies in this cycle
        suite::eq(int64_t(v0 + 2), int64_t(core(n.c, p).reserved_accesses), "both flagged");
    }
    for (const char* r : {"PARAMS", "STATUS", "ERRORS", "EVENTS", "EVENTS_PEEK"})
        suite::check(!try_store(n.c, p, addr(r), 0).empty(), "a store to the read-only %s is a bus error", r);
    for (const char* r : {"PULSE_DONE", "PULSE_DMA", "ROUTER_SET_W0", "ROUTER_SEND_128", "SIM_EXIT", "TRACE"})
        suite::eq(0, load(n, p, addr(r)), "the write-only %s reads 0", r);
    suite::check(!try_load(n.c, p, addr("STATUS"), 2), "a halfword load from a register is a bus error");
    suite::check(!try_store(n.c, p, addr("ROUTER_SET_W1"), 1, 0xFF).empty(), "a byte store to a register is a bus error");
    // region 0xFF outside the block, the windows and the table window; other regions
    for (uint32_t a : {0xFFFF0000u, 0xFFFF3600u, 0xFFFFF7FCu, 0x03000000u, 0x05000000u, 0x80000000u}) {
        suite::check(!try_load(n.c, p, a), "a load from 0x%x is a bus error", a);
        suite::check(!try_store(n.c, p, a, 0).empty(), "a store to 0x%x is a bus error", a);
    }
    suite::check(!try_store(n.c, p, PMEM_BASE + 0x100, 0).empty(), "a store to program memory is a bus error");
    suite::check(bool(try_load(n.c, p, PMEM_BASE + 0x100)), "a load from program memory is not");
    // the windows (8.1): peripheral 1 present (the replayer), 2 and 3 absent
    Net q(machine_cfg(4)); settle(q);
    suite::check(bool(try_load(q.c, 2, window_base(1))), "peripheral 1's window: present");
    suite::check(!try_load(q.c, 2, window_base(2)) && !try_store(q.c, 2, window_base(3) + 0x1FC, 0).empty(), "peripherals 2 and 3's windows: absent, bus errors");
    // the table window: only HIST loads; PTR, STREAM and GROUP are write-only
    for (uint32_t a : {PTR_BASE, STREAM_SRC_BASE, STREAM_DEST_BASE, STREAM_COUNT_BASE, GROUP_BASE})
        suite::check(!try_load(n.c, p, a), "a load from 0x%x (write-only table) is a bus error", a);
    store(n, p, STREAM_SRC_BASE + 4 * 255, 0x12345); store(n, p, STREAM_DEST_BASE + 4 * 3, 0x2ABCDE); store(n, p, STREAM_COUNT_BASE + 4 * 7, 0x7777);
    store(n, p, GROUP_BASE, 0x111); store(n, p, TABLE_END - 4, 0x222); store(n, p, HIST_BASE + 8, 0x1234);
    suite::eq(0x12345, n.c.tile(p).ts_peek(TS_STREAM_SRC + 255), "0xFFFF2BFC: STREAM_SRC[255]");
    suite::eq(0x2ABCDE, n.c.tile(p).ts_peek(TS_STREAM_DEST + 3), "0xFFFF2C0C: STREAM_DEST[3]");
    suite::eq(0x7777, n.c.tile(p).ts_peek(TS_STREAM_COUNT + 7), "0xFFFF301C: STREAM_COUNT[7]");
    suite::eq(0x111, n.c.tile(p).ts_peek(TS_GROUP), "0xFFFF3400: GROUP[0] low");
    suite::eq(0x222, n.c.tile(p).ts_peek(TS_GROUP + 127), "0xFFFF35FC: GROUP[63] high");
    n.c.run(10);
    suite::eq(0x1234, load(n, p, HIST_BASE + 8), "HIST[2] loads back");
    suite::check(n.c.tile(p).ts_peek(TS_STREAM_SRC) == 0 && n.c.tile(p).ts_peek(TS_STREAM_COUNT + 255) == 0, "no stray writes");
    const uint32_t par = uint32_t(load(n, p, addr("PARAMS")));
    suite::eq(2, pfield("ID", par), "PARAMS [26:20]: the endpoint id, tile 2");
    suite::eq(15, pfield("SIZE", par), "PARAMS [31:27]: 4 MB = 16 units of 256 KB - 1");
    suite::eq(0, pfield("PERIPH_PRESENT", par), "PARAMS [2:0]: no peripheral on this machine");
    suite::eq(1, pfield("PERIPH_PRESENT", q.c.tile(2).block().params), "and 0b001 with the replayer (every mesh tile's, 4.17.1)");
    suite::eq(0, int32_t(par & 0x000FFFF8u), "PARAMS [19:3] spare, read 0");
}

// ================================================================ the send port (8.4)
void send_port() {
    suite::group("send port");
    Net n(machine_cfg(0)); settle(n);
    const unsigned s = 6, r = 7;
    n.clear_logs();                                                // ref: r.dir.log = new ArrayList<>() here
    const uint32_t HDR = addr("ROUTER_HDR"), ADDR = addr("ROUTER_ADDR"), W0 = addr("ROUTER_SET_W0"), W1 = addr("ROUTER_SET_W1"), W2 = addr("ROUTER_SET_W2");
    const uint32_t S32 = addr("ROUTER_SEND_32"), S64 = addr("ROUTER_SEND_64"), S128 = addr("ROUTER_SEND_128");
    const uint32_t dmem = 0x400;
    n.c.tile(r).set_remaining(2, 3); n.c.tile(r).set_remaining(3, 2); n.c.tile(r).set_remaining(1, 2); n.c.tile(r).set_ptr(6, 0x30000);
    // mode 0 into data memory: + 16, + 8, + 4
    store(n, s, HDR, place("HDR", "DST", 7) | place("HDR", "MODE", lw::M_WRITE) | place("HDR", "CTR", 2));
    store(n, s, ADDR, dmem);
    store(n, s, W0, 1); store(n, s, W1, 2); store(n, s, W2, 3); store(n, s, S128, 4);
    suite::eq(dmem + 16, n.c.tile(s).block().router_addr, "mode 0, ROUTER_SEND_128: ROUTER_ADDR + 16");
    { const units::RegBlockState b = n.c.tile(s).block(); suite::check(b.router_w[0] == 0 && b.router_w[1] == 0 && b.router_w[2] == 0, "W0-W2 cleared by the send"); }
    store(n, s, W0, 5); store(n, s, W1, 0xEE); store(n, s, S64, 6);
    suite::eq(dmem + 24, n.c.tile(s).block().router_addr, "ROUTER_SEND_64: + 8");
    store(n, s, W0, 0xEE); store(n, s, S32, 7);
    suite::eq(dmem + 28, n.c.tile(s).block().router_addr, "ROUTER_SEND_32: + 4");
    suite::eq(dmem + 28, load(n, s, ADDR), "a load of ROUTER_ADDR returns the next packet's address");
    // no-increment: replies to the same address
    store(n, s, HDR, place("HDR", "DST", 7) | place("HDR", "MODE", lw::M_WRITE) | place("HDR", "CTR", 3) | place("ROUTER_HDR", "NOINC", 1));
    store(n, s, ADDR, REPLY_BASE + 8 * 6);
    store(n, s, W0, 0x99); store(n, s, S64, 0x77);
    store(n, s, S32, 0x55);
    suite::eq(REPLY_BASE + 8 * 6, n.c.tile(s).block().router_addr, "no-increment: ROUTER_ADDR stays put");
    suite::eq(int32_t(place("HDR", "DST", 7) | place("HDR", "MODE", 0) | place("HDR", "CTR", 3) | 1), load(n, s, HDR), "ROUTER_HDR reads back, no-increment set until cleared");
    // modes 1-5 leave ROUTER_ADDR alone
    store(n, s, HDR, place("HDR", "DST", 7) | place("HDR", "MODE", lw::M_SRC) | place("HDR", "CTR", 1));
    store(n, s, ADDR, 0x0300ABC0);
    store(n, s, W0, 0x8); store(n, s, W1, 0x9); store(n, s, W2, 0xA); store(n, s, S128, 0xB);
    store(n, s, HDR, place("HDR", "DST", 7) | place("HDR", "MODE", lw::M_COUNT) | place("HDR", "CTR", 1));
    store(n, s, S32, 0);
    suite::eq(0x0300ABC0, n.c.tile(s).block().router_addr, "modes 1 and 5: ROUTER_ADDR unchanged");
    n.c.run(60);
    const auto l = n.log(r);
    suite::eq(7, int64_t(l.size()), "seven packets at tile 7");
    if (l.size() == 7) {
        suite::check(lw::size(l[0][2]) == lw::SIZE_128 && l[0][1] == (2ull << 32 | 1) && l[0][3] == (4ull << 32 | 3) && lw::addr(l[0][2]) == dmem &&
                         lw::counter(l[0][2]) == 2 && lw::mode(l[0][2]) == 0,
                     "ROUTER_SEND_128: size 128, payload {data, W2, W1, W0}, its address, counter and mode");
        suite::check(lw::size(l[1][2]) == lw::SIZE_64 && l[1][1] == (6ull << 32 | 5) && l[1][3] == 0 && lw::addr(l[1][2]) == dmem + 16,
                     "ROUTER_SEND_64: size 64, payload {data, W0}, W1 not sent");
        suite::check(lw::size(l[2][2]) == lw::SIZE_32 && l[2][1] == 7 && l[2][3] == 0 && lw::addr(l[2][2]) == dmem + 24, "ROUTER_SEND_32: size 32, the data only");
        suite::eq(REPLY_BASE + 48, lw::addr(l[3][2]), "the first reply at REPLY + 8 x 6");
        suite::eq(REPLY_BASE + 48, lw::addr(l[4][2]), "the second at the same address");
    }
    bool words = true; for (uint32_t i = 0; i < 7; i++) if (n.c.tile(r).peek_local(4 * (0x100 + i)) != i + 1) words = false;
    suite::check(words, "mode 0 at 128, 64, 32 bits: seven consecutive words at 0x400");
    suite::eq(0x55, n.c.tile(r).peek_local(4 * ((REPLY_BASE >> 2) + 12)), "the 32-bit reply over the 64-bit one: word 0");
    suite::eq(0x77, n.c.tile(r).peek_local(4 * ((REPLY_BASE >> 2) + 13)), "word 1 from the 64-bit reply");
    suite::check(n.c.tile(r).sram_peek(0x30000) == 8 && n.c.tile(r).sram_peek(0x30001) == 9 && n.c.tile(r).sram_peek(0x30002) == 0xA &&
                     n.c.tile(r).sram_peek(0x30003) == 0xB,
                 "mode 1 at 128 bits: one row at PTR[6]");
    suite::eq(0x30004, n.c.tile(r).ptr(6), "PTR[6] += 4");
    suite::eq(2, int64_t(n.c.tile(s).block().sends_by_size[lw::SIZE_128]), "two 128-bit sends (modes 0 and 1)");
    suite::eq(0, int64_t(n.c.tile(r).block().conditions | n.c.tile(s).block().conditions), "no errors");
    suite::eq(0, int64_t(n.c.tile(r).remaining(1)) + n.c.tile(r).remaining(2) + n.c.tile(r).remaining(3), "every counted packet counted once");
    suite::eq(0, int64_t(n.c.violations()), "no violations %s", n.c.violation_report().c_str());
}

// ================================================================ a tile with no peripheral
void empty_slot() {
    suite::group("no peripheral");
    Net n(machine_cfg(0)); settle(n);
    const unsigned p = 12;
    suite::eq(0, n.c.unit<units::Director>(unit_name(p, "dir")).cfg.periph_present, "PARAMS [2:0] = 0");
    n.c.tile(p).set_remaining(4, 2);
    n.at(3, n.c.cycle() + 2, 12, lw::M_WRITE, 4, 0x04000400, lw::SIZE_128, 1, 2);
    n.send(0, 12, lw::M_WRITE, 4, 0x04000410, lw::SIZE_128, 3, 4);   // from C&C 1 too
    n.run_all(200);
    suite::eq(2, int64_t(n.c.tile(p).block().bad_total), "two mode-0 packets to region 0x04: bad packets, dropped");
    suite::eq(2, n.c.tile(p).remaining(4), "uncounted");
    suite::check((n.c.tile(p).block().conditions & 1u << units::RegBlockState::COND_BAD) != 0, "ERRORS [1]");
    suite::check(!try_load(n.c, p, window_base(1)), "peripheral 1's window: a bus error");
    n.c.tile(p).set_remaining(4, 0);
}

// ================================================================ register-load latency and the sampling rule (4.11)
uint32_t lw_ins(int rd, int off) { return uint32_t((off & 0xFFF) << 20 | 0 << 15 | 2 << 12 | rd << 7 | 0x03); }
void load_latency() {
    const int L = 2;
    suite::group("register-load latency");
    Net n(machine_cfg(0));
    const unsigned p = 3;
    const uint32_t B = PMEM_BASE;
    n.c.tile(p).poke_local(B, 0x00000013);                            // nop
    n.c.tile(p).poke_local(B + 4, lw_ins(10, 0x818));                 // lw a0, EVENTS  (issued at R + 1)
    n.c.tile(p).poke_local(B + 8, lw_ins(11, 0x81C));                 // lw a1, EVENTS_PEEK
    n.c.tile(p).poke_local(B + 12, 0x0000006F);                       // j . (the end)
    n.c.unit<units::Cnc>("cnc").expect = 1ull << 3;                   // only tile 3 starts (6.2): its command, then GO
    n.c.touched("cnc");
    n.c.cc().command(3, CMD_JUMP << 24, {B}).go();
    while (core(n.c, p).pc != B + 4) n.step();                        // cycle R: the ROM has jumped and the nop has run
    const uint64_t s0 = core(n.c, p).stall_reg_load;
    const int64_t R = n.c.cycle(), issue = R + 1, last = issue + L - 1;   // the load's sample point is the end of issue + L - 2
    const uint32_t c9 = 1u << regs::EV_COUNTERS_DONE, go = 1u << regs::EV_GO;
    n.c.tile(p).set_events(c9);                                       // pending from the end of R: before the sample point
    n.c.tile(p).schedule_go(last);                                    // GO seen in the load's last cycle: after its sample point
    while (n.c.cycle() < last) n.step();
    suite::eq(c9, core(n.c, p).x[10] & (c9 | go), "L = %d: the load returns counters done (pending at its sample point), not GO (set in its last cycle)", L);
    suite::eq(go, n.c.tile(p).block().events & (c9 | go), "L = %d: its side effect at the end of its last cycle cleared counters done; GO, set in that cycle, survives", L);
    while (n.c.cycle() < last + L) n.step();
    suite::eq(go, core(n.c, p).x[11] & (c9 | go), "L = %d: the next load (EVENTS_PEEK) sees GO, not counters done", L);
    suite::eq(2LL * (L - 1), int64_t(core(n.c, p).stall_reg_load - s0), "L = %d: each register-block load costs %d cycles", L, L);
    suite::eq(0, int64_t(n.c.violations()), "no violations %s", n.c.violation_report().c_str());
    // the table window: a HIST load takes 1 cycle (4.11)
    const uint64_t s1 = core(n.c, p).stall_reg_load;
    suite::check(bool(try_load(n.c, p, HIST_BASE)), "a HIST load");
    suite::eq(int64_t(s1), int64_t(core(n.c, p).stall_reg_load), "a table-window load stalls nothing: 1 cycle");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    pulses();
    bus_errors();
    send_port();
    empty_slot();
    load_latency();
    return suite::done(shuffle >= 0 ? "HooksTest (shuffled, seed " + std::to_string(shuffle) + ")" : "HooksTest");
}
