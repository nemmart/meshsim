// The C&C units (WORKER16, M3 step 2; AUDIT-M3 3.4): the chip with C&C 1-3 as units 64-66 (ChipSpec::cc_cores), built
// from M2's units with the controller's router, the C&C links (Link(1)) to tiles 0-2's fifth ports and the host ports.
//   - The build: 67 units' worth of each tile unit, named t<k>.* and cc1.* .. cc3.*; C&C c in tile c's domain, so a
//     split chip's barrier interval stays 3.
//   - A boot through the host port and a program on C&C 1 (tests/m3-images/cc_ping.c, Mach.hostBoot's packets): its image
//     and first command (CMD_JUMP) land through CC_HOST_IN, the boot ROM jumps (a controller starts when its command
//     lands), and three sends exercise the controller router's paths: to tile 5 (the loopback out on the link, across
//     tile 0's fifth port and the mesh), to itself (the loopback ejected, with a token), and a broadcast (on the link,
//     rooted at tile 0, into all 64 tiles and never back to C&C 1); then SIM_EXIT. On one domain and on row bands, and
//     under the debug options (shuffle, both orders, step-all).
//   - ref's cycles (tests/m3-images/cc_ping.ref, from tests/ref/ccping.sh): SIM_EXIT's cycle and cc_ping's stamps (mcycle at
//     each send and when its STATUS poll sees send-busy clear). The first poll has nothing arriving at C&C 1: the
//     register block must step again after the core's own send (W16's fix in TileCtl; the missed-wake detector can't
//     see stale published state), else the poll never ends; ref's cycle is checked, not just the ending.
// The slot and the chip lines (C&C 1's READY, GO and error lines) come with W15's slot; nothing here reaches them.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.h"
#include "golden_run.h"
#include "harness.h"
#include "units/chip.h"
#include "units/host_port.h"
#include "units/inject.h"
#include "units/router.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

struct Out { std::string err; uint64_t cycle = 0, exit_at = 0; uint32_t exit = 0; uint32_t t5 = 0, self = 0, bc_min = 0, bc_max = 0, self_bc = 0; uint32_t stamp[6] = {}; };

// ref's figures (cc_ping.ref): "exit CYCLE VALUE", "stamp S0 .. S5"
struct Ref { uint64_t exit_at = 0; uint32_t exit = 0, stamp[6] = {}; bool ok = false; };
Ref load_ref() {
    Ref r;
    FILE* f = std::fopen("tests/m3-images/cc_ping.ref", "r");
    if (!f) return r;
    char line[256]; int got = 0;
    while (std::fgets(line, sizeof line, f)) {
        unsigned long long a; unsigned b;
        if (std::sscanf(line, "exit %llu %x", &a, &b) == 2) { r.exit_at = a; r.exit = b; got++; }
        if (std::sscanf(line, "stamp %u %u %u %u %u %u", &r.stamp[0], &r.stamp[1], &r.stamp[2], &r.stamp[3], &r.stamp[4], &r.stamp[5]) == 6) got++;
    }
    std::fclose(f);
    r.ok = got == 2;
    return r;
}

Out run(unsigned parts, sim::Options opt) {
    Out o;
    test::RunSpec rs; rs.name = "cc_ping"; rs.dir = "tests/m3-images"; rs.image = "cc_ping";
    test::Images im;
    if (!test::load_images(rs, im, o.err)) return o;
    const Hops hops = load_hops("ref/meshsim/hops.txt");
    NetConfig nc;
    TileConfig tc = test::tile_config(0);
    std::vector<uint32_t> mem(67 * 2 * MEM_WORDS, 0);
    G1Ctx gx;
    gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
    gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
    gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data(); gx.units = 67;
    SramBlocks sram_blocks; gx.sram = &sram_blocks;
    opt.threads = parts;
    sim::Config cfg(opt);
    uint8_t dom[64];
    row_bands(parts, dom);
    ChipSpec spec;
    spec.net = nc; spec.hops = &hops; spec.control = add_g1; spec.control_ctx = &gx;
    spec.domain = parts > 1 ? dom : nullptr;
    spec.cc_cores = true; spec.cc3_dma = false;
    build_chip(cfg, spec);
    sim::Sim m = cfg.build();
    if (parts > 1 && m.partition().find("barrier every 3 cycles") == std::string::npos) { o.err = "barrier interval: " + m.partition(); return o; }
    // Mach.hostBoot: C&C 1's program and data, then CMD_JUMP to _start, through the host port
    HostPort& h = m.unit<HostPort>("cc1.host");
    h.load(0x01000000, im.code.data(), im.code.size());
    if (!im.data.empty()) h.load(im.data_at, im.data.data(), im.data.size());
    const uint32_t entry = im.entry;
    h.command(0x4Au << 24, &entry, 1);
    m.wake("cc1.host");
    const TileCore& core = m.unit<TileCore>("cc1.core");
    for (uint64_t c = 0; c < 20000 && core.rv.halt == Rv32::RUN; c++) m.run(1);
    o.cycle = m.now();
    m.run(200);                                                // the broadcast's last copies land
    o.exit = m.unit<TileCtl>("cc1.ctl").s.sim_exit_value;
    o.exit_at = m.unit<TileCtl>("cc1.ctl").s.sim_exit_at;
    for (unsigned i = 0; i < 6; i++) o.stamp[i] = mem[size_t(64) * 2 * MEM_WORDS + 0x10 / 4 + i];   // cc_ping.sym: stamp at 0x10
    auto dmem = [&](unsigned k, uint32_t byte) { return mem[size_t(k) * 2 * MEM_WORDS + byte / 4]; };
    o.t5 = dmem(5, 0x6000); o.self = dmem(64, 0x6000); o.self_bc = dmem(64, 0x6004);
    o.bc_min = ~0u; o.bc_max = 0;
    for (unsigned k = 0; k < 64; k++) { o.bc_min = std::min(o.bc_min, dmem(k, 0x6004)); o.bc_max = std::max(o.bc_max, dmem(k, 0x6004)); }
    if (core.rv.halt != Rv32::HALT_EXIT) o.err = "C&C 1 did not exit (halt " + std::to_string(core.rv.halt) + ")";
    const Router& r = m.unit<Router>("cc1.router");
    if (!o.err.empty()) return o;
    if (r.eject_errors || r.route_errors) o.err = "C&C 1's router: eject errors " + std::to_string(r.eject_errors) + ", route errors " + std::to_string(r.route_errors);
    if (m.unit<Router>("t0.router").from_cnc_words != 2) o.err = "tile 0's fifth port took " + std::to_string(m.unit<Router>("t0.router").from_cnc_words) + " words from C&C 1, want 2";
    return o;
}

void check(unsigned parts, sim::Options opt) {
    Out o = run(parts, opt);
    if (!o.err.empty()) { test::fail(__FILE__, __LINE__, "%s", o.err.c_str()); return; }
    CHECK_EQ(o.exit, 0x5EEu);
    CHECK_EQ(o.t5, 0xC0FFEE05u);
    CHECK_EQ(o.self, 0xC0FFEE40u);
    CHECK_EQ(o.bc_min, 0xB0B0CA57u);
    CHECK_EQ(o.bc_max, 0xB0B0CA57u);
    CHECK_EQ(o.self_bc, 0u);                                   // a controller's broadcast never reaches itself
    const Ref r = load_ref();
    CHECK(r.ok);
    CHECK_EQ(o.exit_at, r.exit_at);                            // ref's cycles
    CHECK_EQ(o.exit, r.exit);
    for (unsigned i = 0; i < 6; i++) CHECK_EQ(o.stamp[i], r.stamp[i]);
    std::fprintf(stderr, "cc_ping (%u domain%s): C&C 1 exited at cycle %" PRIu64 "\n", parts, parts > 1 ? "s" : "", o.cycle);
}

}  // namespace

TEST(cc_units_names_and_router_variants) {
    CHECK(unit_prefix(0) == "t0.");
    CHECK(unit_prefix(63) == "t63.");
    CHECK(unit_prefix(64) == "cc1.");
    CHECK(tile_unit(66, "router") == "cc3.router");
}

TEST(cc_units_boot_and_ping_one_domain) { check(1, {}); }
TEST(cc_units_boot_and_ping_row_bands) { check(4, {}); }

TEST(cc_units_under_debug_options) {
    sim::Options a; a.shuffle_seed = 11; check(1, a);
    sim::Options b; b.order = sim::Options::Order::instance_major; check(1, b);
    sim::Options c; c.step_all = true; check(1, c);
}
