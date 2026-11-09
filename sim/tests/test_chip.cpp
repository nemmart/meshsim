// The chip (WORKER9, stage 4): build_chip with W8's G1 units in every tile (add_g1), the network and C&C's model, running
// M2Prog's regs (to its end since G3's mode-1 path, W11) and wfi exactly as CtrlTest runs them
// (Mach.boot's C&C sequence, then the program's), checked against golden-m2 every cycle: every unit of tile 0 and the
// chip-level units (the golden's 21 lines a point, `chip` included), from cycle 0 to the cycle every core has stopped.
// One thread, the debug options, and form 2 (the tiles in 2, 4 and 8 domains by bands of rows, on several threads).
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "golden_run.h"
#include "java_random.h"
#include "harness.h"
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "units/chip.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

// ---- the register block's line at the end of c (G2Chip::edge): W8's preview (tile_ctl.h, units::line_edge) -----------
fp::LineEdge line_edge(const void* ctx, unsigned k, uint64_t c) {
    const fp::G1Tile& g = static_cast<const fp::G1Tile*>(ctx)[k];
    const units::LineEdge e = units::line_edge(*g.sim, *g.uctl, c);
    fp::LineEdge le;
    le.put = e.put;
    le.ready = e.word & 1;
    le.level = e.word >> 1 & 1;
    return le;
}

// ---- a2a's input (ref/meshsim/test/A2A.java: Data(7, 2000), uniform; M2Prog pokes it into every tile's big SRAM) ----
void a2a_input(sim::Sim& m) {
    static const int G4[16][4] = {{1, 29, 35, 63}, {2, 30, 34, 62}, {3, 31, 33, 61}, {5, 23, 41, 59}, {6, 22, 42, 58},
        {7, 21, 43, 57}, {8, 28, 36, 56}, {9, 27, 37, 55}, {10, 26, 38, 54}, {11, 25, 39, 53}, {12, 24, 40, 52},
        {13, 15, 49, 51}, {14, 18, 46, 50}, {16, 20, 44, 48}, {17, 19, 45, 47}, {0, 4, 32, 60}};
    const int n = 2000;
    static int cnt[64][64];
    static std::vector<uint64_t> rec[64];
    std::memset(cnt, 0, sizeof cnt);
    test::JavaRandom r(7);
    for (int s = 0; s < 64; s++) {
        std::vector<int> dst(n);
        for (int k = 0; k < n; k++) { dst[k] = r.next_int_pow2(64); cnt[s][dst[k]]++; }   // weights all 1: d = x
        rec[s].assign(n, 0);
        int pos[64] = {};
        for (int d = 1; d < 64; d++) pos[d] = pos[d - 1] + cnt[s][d - 1];
        for (int k = 0; k < n; k++) rec[s][pos[dst[k]]++] = uint64_t(dst[k]) << 58 | uint64_t(s) << 52 | uint64_t(k);
    }
    int R[16] = {};
    for (int g = 0; g < 16; g++) for (int s = 0; s < 64; s++) for (int k : G4[g]) R[g] = std::max(R[g], cnt[s][(s + k) & 63]);
    for (unsigned s = 0; s < 64; s++) {
        BigSram& b = m.unit<BigSram>(tile_unit(s, "sram"));
        for (int k = 0; k < n; k++) b.poke64(uint32_t(2 * k), rec[s][k]);                  // A2A.SRC = 0
        const uint32_t base = 0xF0000;
        for (int d = 0; d < 64; d++) b.poke(base + d, uint32_t(cnt[s][d]));
        for (int src = 0; src < 64; src++) b.poke(base + 64 + src, uint32_t(cnt[src][s]));
        for (int g = 0; g < 16; g++) b.poke(base + 128 + g, uint32_t(R[g]));
        b.poke(base + 144, 0);
    }
}

// ---- one run ---------------------------------------------------------------------------------------------------------
struct Setup { unsigned parts = 1, threads = 1; sim::Options opt; bool scan_sum = false; };   // scan_sum: D31's path

// `golden`: the .fp file (default tests/golden-m2/NAME.fp, tile 0 every cycle); `all_tiles`: its points cover every
// tile (the hash-only NAME-chip.fp, every 10 cycles), and cycles without a point are not compared.
std::string check(const char* name, uint64_t through, const Setup& su, const std::string& golden = "", bool all_tiles = false) {
    fp::FpGolden g = fp::FpGolden::load(golden.empty() ? std::string("tests/golden-m2/") + name + ".fp" : golden);
    if (!g.error.empty()) return g.error;
    // D12: what this run builds (the keys it reads; the rest are ref's defaults, which this build assumes)
    const std::pair<const char*, const char*> keys[] = {{"linkLatency", "3"}, {"queueDepth", "16"}, {"K", "8"}, {"cmLatency", "2"},
        {"goLatency", "8"}, {"readyLatency", "8"}, {"cncReaction", "1"}, {"cncGoSettle", "true"}, {"skew", "true"},
        {"skewSeed", "1"}, {"readTokens", "64"}, {"intraTileLatency", "1"}, {"doneGate", "true"}, {"memChunks", "1048576"},
        {"tsEngineLatency", "1"}, {"areaQueueDepth", "8"}, {"sramReadLatency", "2"}, {"writeCombining", "false"},
        {"fifoCountBeforePop", "true"}, {"weInDepth", "4"}, {"gatherDepth", "16"}, {"readPortDepth", "64"}};
    for (auto& kv : keys) {
        auto it = g.config.find(kv.first);
        if (it == g.config.end() || it->second != kv.second)
            return std::string("refused (D12): the golden's ") + kv.first + " is " + (it == g.config.end() ? "absent" : it->second) + ", this run builds " + kv.second;
    }
    // the images, the boot ROM, the hops table
    test::RunSpec rs; rs.name = name; rs.dir = "tests/golden-m2/images"; rs.image = name;
    test::Images im; std::string err;
    if (!test::load_images(rs, im, err)) return err;
    const Hops hops = load_hops("ref/meshsim/hops.txt");
    // C&C's program: Mach.boot, then M2Prog's sequence
    NetConfig nc;
    CncProgram prog;
    prog.load_words(lw::BROADCAST, 0x01000000, im.code.data(), im.code.size());
    prog.load_words(lw::BROADCAST, im.data_at, im.data.data(), im.data.size());
    const uint32_t entry = im.entry;
    prog.command(lw::BROADCAST, 0x4Au << 24, &entry, 1);
    prog.run_when_ready(0).call(1);                                       // runWhenReady's call, then Mach's
    if (std::string(name) == "a2a")                                       // M2Prog a2a (CtrlTest.allToAll's first set)
        prog.command(lw::BROADCAST, 1u << 24).go().command(lw::BROADCAST, 2u << 24).go().command(lw::BROADCAST, 3u << 24).go();
    if (std::string(name) == "wfi")
        prog.command(lw::BROADCAST, 1u << 24).go().command(lw::BROADCAST, 2u << 24).go().wait_cycles(700)
            .command(lw::BROADCAST, 3u << 24).go();
    prog.release();
    // G1's context (as W8's tests build it)
    TileConfig tc = test::tile_config(0);
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    G1Ctx gx;
    gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
    gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
    gx.cfg.sram_running_sum = !su.scan_sum;
    gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data();
    SramBlocks sram_blocks; gx.sram = &sram_blocks;

    sim::Options opt = su.opt;
    opt.observe_arrivals = true;
    opt.threads = su.threads;
    sim::Config cfg(opt);
    uint8_t dom[64];
    row_bands(su.parts, dom);
    ChipSpec spec;
    spec.net = nc; spec.hops = &hops; spec.program = &prog; spec.control = add_g1; spec.control_ctx = &gx;
    spec.domain = su.parts > 1 ? dom : nullptr;
    build_chip(cfg, spec);
    sim::Sim m = cfg.build();
    if (su.parts > 1 && m.partition().find("barrier every 3 cycles") == std::string::npos) return "the partition's barrier interval is not 3: " + m.partition();

    if (std::string(name) == "a2a") a2a_input(m);                         // before cycle 1, as M2Prog pokes it
    // the fingerprint: tile 0's units, the chip's
    std::vector<fp::G1Tile> g1(64);
    for (unsigned k = 0; k < 64; k++) { g1[k] = fp::g1_tile(m, k); g1[k].uinj = &m.unit<Injection>(tile_unit(k, "inj")); }
    std::vector<fp::G2Tile> g2(64);
    for (unsigned k = 0; k < 64; k++) g2[k] = fp::g2_tile(m, k);
    fp::G2Chip gc;
    gc.cnc = &m.unit<Cnc>("cnc");
    for (unsigned r = 0; r < 8; r++) gc.rows[r] = &m.unit<RowCollector>("row" + std::to_string(r));
    gc.ep[0] = &m.unit<Endpoint>("ep2"); gc.ep[1] = &m.unit<Endpoint>("ep3");
    gc.ready_latency = nc.ready_latency;
    gc.edge = line_edge; gc.edge_ctx = g1.data();
    units::reqbus::ReqBusUnits rbu;                                       // W14: the request bus's streams
    fp::attach_reqbus(m, g1.data(), 64, gc, rbu, spec.reqbus);
    fp::FpRegistry reg;
    std::vector<unsigned> tiles;
    for (unsigned k = 0; k < (all_tiles ? 64u : 1u); k++) { fp::register_g1(reg, g1[k]); fp::register_g2_tile(reg, g2[k]); tiles.push_back(k); }
    fp::register_g2(reg, gc);

    const uint64_t last = g.points.back().cycle;
    uint64_t c = 0;
    for (;; c++) {
        if (c > 0) m.run(1);
        if (c <= through) {
            auto pts = g.at(c);
            if (pts.empty() && !all_tiles && std::string(name) != "a2a") return std::string(name) + ": the golden has no point at cycle " + std::to_string(c);
            std::vector<std::string> lines;
            if (!pts.empty()) reg.point(fp::FpCtx{c, &m}, tiles, true, lines);
            std::vector<fp::FpLine> ours = fp::parse_lines(lines);
            for (const fp::FpPoint* p : pts) {
                fp::FpDiff d = fp::compare(*p, ours);
                if (d.any) {
                    if (std::getenv("CHIP_DUMP")) reg.dump(d.unit, fp::FpCtx{c, &m}, stderr);
                    return std::string(name) + ": cycle " + std::to_string(d.cycle) + ", " + d.unit + ": " + d.why;
                }
            }
        }
        if (c == through) break;                                          // regs: G3's path from here (PLAN-M2)
        bool stopped = true;                                              // Mach.allStopped, between cycles
        for (unsigned k = 0; k < 64 && stopped; k++) stopped = g1[k].core->halt != Rv32::RUN;
        if (stopped) break;
        if (c > last + 10) return std::string(name) + ": still running " + std::to_string(c - last) + " cycles past the golden's end";
    }
    const Cnc& cc = m.unit<Cnc>("cnc");
    std::fprintf(stderr, "%s (%u domain%s, %u thread%s): stopped at cycle %" PRIu64 " (the golden's last point %" PRIu64 "), checked through %" PRIu64
                 "; C&C: GOs %" PRIu64 ", packets %" PRIu64 ", ready_repulses %" PRIu64 ", stepped %" PRIu64
                 "; big SRAM: %zu KB allocated, %zu KB touched\n",
                 name, su.parts, su.parts > 1 ? "s" : "", su.threads, su.threads > 1 ? "s" : "", c, last,
                 std::min(c, through), cc.gos, cc.packets_sent, cc.ready_repulses, cc.stepped,
                 sram_blocks.allocated_bytes() >> 10, sram_blocks.touched_bytes() >> 10);
    if (cc.ready_repulses != 0) return std::string(name) + ": READY pulsed again while visible (D29), " + std::to_string(cc.ready_repulses) + " times";
    if (c != through && c != last) return std::string(name) + ": stopped at cycle " + std::to_string(c) + ", the golden at " + std::to_string(last);
    return "";
}

void run_all(const Setup& su) {
    for (auto [name, through] : {std::pair<const char*, uint64_t>{"wfi", ~uint64_t(0)}, {"regs", fp::kRegsCheckedThrough}}) {
        std::string e = check(name, through, su);
        if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    }
}

}  // namespace

TEST(chip_wfi_and_regs_every_cycle) { run_all({}); }

// D31: without BigSram's running sum (timing runs), a fingerprint scans the block: regs and a2a stay equal.
TEST(chip_sram_sum_by_scan_keeps_the_goldens) {
    Setup su; su.scan_sum = true;
    for (const char* name : {"regs", "a2a"}) {
        std::string e = check(name, ~uint64_t(0), su);
        if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    }
}

// a2a (W11 with W12's AG): every unit of tile 0 every 10 cycles (golden-m2/a2a.fp) to its end, one thread and split.
TEST(chip_a2a_every_10_cycles) {
    if (units::reg_info(units::PULSE_DMA).store == units::A_OUT) {   // W12's DMA start (sim-w12 83fb888): not on this branch
        std::fprintf(stderr, "a2a: skipped (PULSE_DMA not modelled on this branch yet)\n");
        return;
    }
    std::string e = check("a2a", ~uint64_t(0), {});
    if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
    Setup sp; sp.parts = 4; sp.threads = 4;
    e = check("a2a", ~uint64_t(0), sp);
    if (!e.empty()) test::fail(__FILE__, __LINE__, "%s", e.c_str());
}

// The hash-only goldens (every tile every 10 cycles), when CHIP_FP_DIR names a directory holding them
// (tests/golden-m2/regen.sh with KEEP=DIR): not in make check (the files are not committed).
TEST(chip_every_tile_when_asked) {
    const char* dir = std::getenv("CHIP_FP_DIR");
    if (!dir) return;
    Setup split; split.parts = 4; split.threads = 4;
    for (const Setup& su : {Setup{}, split})
        for (auto [name, through] : {std::pair<const char*, uint64_t>{"wfi", ~uint64_t(0)}, {"regs", fp::kRegsCheckedThrough},
                                     {"a2a", ~uint64_t(0)}}) {
            if (std::string(name) == "a2a" && units::reg_info(units::PULSE_DMA).store == units::A_OUT) continue;
            std::string e = check(name, through, su, std::string(dir) + "/" + name + "-chip.fp", true);
            if (!e.empty()) { test::fail(__FILE__, __LINE__, "%s", e.c_str()); return; }
        }
}

TEST(chip_under_debug_options) {
    Setup a; a.opt.step_all = true; run_all(a);
    Setup b; b.opt.shuffle_seed = 5; run_all(b);
    Setup c; c.opt.order = sim::Options::Order::instance_major; run_all(c);
}

TEST(chip_form2_row_bands) {
    for (unsigned parts : {2u, 4u, 8u}) { Setup s; s.parts = parts; s.threads = parts; run_all(s); }
}

namespace {
// The tile's surroundings for one tile alone: floods its four link inputs with packets for it, sinks its outputs,
// records its error line.
struct Flood {
    sim::LinkOut out[4], go;
    sim::LinkIn in[4], line;
    unsigned tile; uint64_t until;
    uint64_t line_at = 0; sim::Word line_w = 0;
    Flood(sim::Ports& p, unsigned k, uint64_t n) : tile(k), until(n) {
        for (unsigned d = 0; d < 4; d++) { p.out(out[d], "out", 3); p.in(in[d], "in", 3); }
        p.out(go, "go", 0); p.in(line, "line", 1);
    }
    sim::Wake step(sim::Ctx& c) {
        for (unsigned d = 0; d < 4; d++) if (c.ready(in[d])) c.take(in[d]);
        if (c.ready(line)) { line_at = c.now(); line_w = c.peek(line)[0]; c.take(line); }
        if (c.now() > until) return sim::Wake::none();
        // uncounted 32-bit writes into data memory, src = the tile itself: ejected here from any input
        for (unsigned d = 0; d < 4; d++) c.put(out[d], c.now(), lw::hdr(tile, tile, lw::M_WRITE, 0, 0x1000 + 4 * d, lw::SIZE_32), 0);
        return c.next();
    }
    uint64_t digest() const { return line_at; }
};
}  // namespace

// D29: a router FIFO's overflow reaches the tile's register block as an event. The tile's four inputs flood its eject
// output; the overflow of a put made at t is counted at t + 1 (D28) and its event put then (Link(1)), so the block,
// asleep (its core waits in the boot ROM), steps at t + 2 and its endCycle(t + 1) sets ERRORS [0]; its error line then
// reaches the collector's side ready_latency - 2 later.
TEST(tile_router_overflow_steps_the_register_block) {
    test::RunSpec rs; rs.name = "wfi"; rs.dir = "tests/golden-m2/images"; rs.image = "wfi";
    test::Images im; std::string err;
    CHECK(test::load_images(rs, im, err));
    const Hops hops = load_hops("ref/meshsim/hops.txt");
    TileConfig tc = test::tile_config(0);
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    G1Ctx gx;
    gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
    gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
    gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data();
    SramBlocks sram_blocks; gx.sram = &sram_blocks;
    CncProgram prog;
    prog.release();
    ChipSpec spec;
    spec.hops = &hops; spec.program = &prog; spec.control = add_g1; spec.control_ctx = &gx;
    const unsigned k = 3;
    sim::Config cfg;
    TileIO io = tile_cycle(cfg, spec, k);
    units::reqbus::tie_off(cfg, "t" + std::to_string(k) + ".reqtie", io.req);   // W14: no request bus in a lone tile
    auto& f = cfg.add<Flood>("flood", k, 60u);
    for (unsigned d = 0; d < 4; d++) { cfg.link(f.out[d], io.link_in[d], 1); cfg.link(io.link_out[d], f.in[d], 1); }
    cfg.link(f.go, io.go, 1);
    cfg.link(io.line, f.line, spec.net.ready_latency - 2);
    sim::Sim m = cfg.build();
    const Router& r = m.unit<Router>(tile_unit(k, "router"));
    const TileCtl& b = m.unit<TileCtl>(tile_unit(k, "ctl"));
    uint64_t first = 0, set_at = 0;
    for (uint64_t c = 1; c <= 100; c++) {
        m.run(1);
        if (!first)
            for (const auto& q : r.q) if (q.overflows && (!first || q.first_overflow < first)) first = q.first_overflow;
        if (!set_at && (b.s.conditions & 1u << RegBlockState::COND_ROUTER_OVF)) set_at = c;
    }
    CHECK(first != 0);                                     // the flood overflowed a FIFO
    CHECK_EQ(set_at, first + 2);                           // stepped at t + 2: endCycle(t + 1) set ERRORS [0]
    const Flood& fl = m.unit<Flood>("flood");
    CHECK_EQ(fl.line_at, first + 2 + spec.net.ready_latency - 2);   // put at t + 2, ready_latency - 2 on its way
    CHECK_EQ(fl.line_w, sim::Word(2));                     // the error line high, no READY
}

namespace {
// A tile's surroundings that do nothing: its four links, GO and its line, sunk.
struct Quiet {
    sim::LinkOut out[4], go;
    sim::LinkIn in[4], line;
    explicit Quiet(sim::Ports& p) {
        for (unsigned d = 0; d < 4; d++) { p.out(out[d], "out", 3); p.in(in[d], "in", 3); }
        p.out(go, "go", 0); p.in(line, "line", 1);
    }
    sim::Wake step(sim::Ctx& c) {
        for (unsigned d = 0; d < 4; d++) if (c.ready(in[d])) c.take(in[d]);
        if (c.ready(line)) c.take(line);
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};
// The read port end to end in one tile (W11, P2b): the core's boot ROM sets SRAM_RD_DEST_ADDR to 0x200 (32 bits,
// uncounted), queues reads of chunks 0x40 and 0x44, loads SRAM_RD_ADDR_QUEUE (free entries) into x4 and
// SRAM_RD_DEST_ADDR into x5, then waits. The results go through the read queue, the injection unit, the router's
// loopback and the director to data memory at 0x200 and 0x204.
struct RpRun { uint32_t x4, x5, d0, d1, errors; uint64_t stored, dropped, accepted, injected; };
RpRun read_port_run(unsigned depth) {
    static const uint32_t rom[] = {0x80000093, 0x20000113, 0x0820a023, 0x04000193, 0x0630ae23, 0x04400193, 0x0630ae23,
                                   0x07c0a203, 0x0800a283, 0x10500073, 0x0000006f};
    const Hops hops = load_hops("ref/meshsim/hops.txt");
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    SramBlocks blocks;
    G1Ctx gx;
    gx.cfg.read_port_depth = depth;
    gx.bootrom = rom; gx.bootrom_words = sizeof rom / 4; gx.mem = mem.data(); gx.sram = &blocks;
    CncProgram prog;
    prog.release();
    ChipSpec spec;
    spec.hops = &hops; spec.program = &prog; spec.control = add_g1; spec.control_ctx = &gx;
    const unsigned k = 3;
    sim::Config cfg;
    TileIO io = tile_cycle(cfg, spec, k);
    units::reqbus::tie_off(cfg, "t" + std::to_string(k) + ".reqtie", io.req);   // W14: no request bus in a lone tile
    auto& q = cfg.add<Quiet>("quiet");
    for (unsigned d = 0; d < 4; d++) { cfg.link(q.out[d], io.link_in[d], 1); cfg.link(io.link_out[d], q.in[d], 1); }
    cfg.link(q.go, io.go, 1);
    cfg.link(io.line, q.line, spec.net.ready_latency - 2);
    sim::Sim m = cfg.build();
    m.unit<BigSram>(tile_unit(k, "sram")).poke(0x40, 0xABCD0040);
    m.unit<BigSram>(tile_unit(k, "sram")).poke(0x44, 0xABCD0044);
    m.run(100);
    const TileCore& c = m.unit<TileCore>(tile_unit(k, "core"));
    const TileCtl& b = m.unit<TileCtl>(tile_unit(k, "ctl"));
    RpRun r;
    r.x4 = c.rv.x[4]; r.x5 = c.rv.x[5];
    r.d0 = c.rv.dmem[0x200 >> 2]; r.d1 = c.rv.dmem[0x204 >> 2];
    r.errors = b.s.conditions;
    r.stored = c.rp_stored; r.dropped = c.rp_dropped;
    r.accepted = m.unit<Director>(tile_unit(k, "dir")).rp_accepted;
    r.injected = m.unit<Injection>(tile_unit(k, "inj")).rs.rp_injected_n;
    return r;
}
}  // namespace

// Depth 64: both reads queued and their results written; the load at its sampling cycle sees 62 free (no result
// injected yet: the second read was queued two cycles before, the first's row is not back); the destination advanced
// twice (0x208). Depth 1: the second store is dropped (READ_PORT: ERRORS [7]), the destination still advances.
TEST(tile_read_port_end_to_end) {
    RpRun a = read_port_run(64);
    CHECK_EQ(a.d0, 0xABCD0040u);
    CHECK_EQ(a.d1, 0xABCD0044u);
    CHECK_EQ(a.x4, 62u);
    CHECK_EQ(a.x5, 0x208u);
    CHECK_EQ(a.stored, uint64_t(2));
    CHECK_EQ(a.accepted, uint64_t(2));
    CHECK_EQ(a.injected, uint64_t(2));
    CHECK_EQ(a.errors, 0u);
    RpRun b = read_port_run(1);
    CHECK_EQ(b.d0, 0xABCD0040u);
    CHECK_EQ(b.d1, 0u);
    CHECK_EQ(b.dropped, uint64_t(1));
    CHECK_EQ(b.x4, 0u);
    CHECK_EQ(b.x5, 0x208u);
    CHECK_EQ(b.errors, 1u << 7);
}
