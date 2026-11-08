// The chip (WORKER9, stage 4): build_chip with W8's G1 units in every tile (add_g1), the network and C&C's model, running
// M2Prog's regs (to its end since G3's mode-1 path, W11) and wfi exactly as CtrlTest runs them
// (Mach.boot's C&C sequence, then the program's), checked against golden-m2 every cycle: every unit of tile 0 and the
// chip-level units (the golden's 21 lines a point, `chip` included), from cycle 0 to the cycle every core has stopped.
// One thread, the debug options, and form 2 (the tiles in 2, 4 and 8 domains by bands of rows, on several threads).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "golden_run.h"
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

// ---- one run ---------------------------------------------------------------------------------------------------------
struct Setup { unsigned parts = 1, threads = 1; sim::Options opt; };

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

    // the fingerprint: tile 0's units, the chip's
    std::vector<fp::G1Tile> g1(64);
    for (unsigned k = 0; k < 64; k++) g1[k] = fp::g1_tile(m, k);
    std::vector<fp::G2Tile> g2(64);
    for (unsigned k = 0; k < 64; k++) g2[k] = fp::g2_tile(m, k);
    fp::G2Chip gc;
    gc.cnc = &m.unit<Cnc>("cnc");
    for (unsigned r = 0; r < 8; r++) gc.rows[r] = &m.unit<RowCollector>("row" + std::to_string(r));
    gc.ep[0] = &m.unit<Endpoint>("ep2"); gc.ep[1] = &m.unit<Endpoint>("ep3");
    gc.ready_latency = nc.ready_latency;
    gc.edge = line_edge; gc.edge_ctx = g1.data();
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
            if (pts.empty() && !all_tiles) return std::string(name) + ": the golden has no point at cycle " + std::to_string(c);
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

// The hash-only goldens (every tile every 10 cycles), when CHIP_FP_DIR names a directory holding them
// (tests/golden-m2/regen.sh with KEEP=DIR): not in make check (the files are not committed).
TEST(chip_every_tile_when_asked) {
    const char* dir = std::getenv("CHIP_FP_DIR");
    if (!dir) return;
    Setup split; split.parts = 4; split.threads = 4;
    for (const Setup& su : {Setup{}, split})
        for (auto [name, through] : {std::pair<const char*, uint64_t>{"wfi", ~uint64_t(0)}, {"regs", fp::kRegsCheckedThrough}}) {
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
