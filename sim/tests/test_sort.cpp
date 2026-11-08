// The sort end to end (WORKER12, P5): the 64-tile chip running M2Sort's sequence, as M2Sort runs it (sim/tests/ref/
// M2Sort.java, SortTest.Machine), checked against golden-m2's sort16k. This piece goes up to the input load:
//   1. the boot (SortTest.Machine.boot: the image, CMD_JUMP, runWhenReady, without Mach's extra call), run cycle by
//      cycle until C&C is idle and every core waits in WFI (ref's runUntil(!cc.busy() && allWaiting())): cycle B;
//   2. the input load between cycles (SortTest.loadInput: big SRAM and S.n_in) and the sort's sequence released at B;
//   3. the state at B, every unit of every tile and the chip's, against the golden's first point (FpHooks takes it at B,
//      after the load and the queueing).
// make check runs (1) against sort16k.out's "boot N cycles". (2)-(3) need the key set and the hash-only golden:
//   SORT_KEYS=keys-16384/uniform.keys SORT_FP=sort16k-fp.fp test_sort
// (KeyGen's uniform set, checked against HASHES by its sha256; the golden from tests/golden-m2/regen.sh with KEEP=DIR).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "golden_run.h"
#include "sha256.h"
#include "harness.h"
#include "sort_input.h"
#include "statefp_g1.h"
#include "statefp_g2.h"
#include "units/chip.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

const char* kImages = "tests/golden-m2/images";

fp::LineEdge line_edge(const void* ctx, unsigned k, uint64_t c) {
    const fp::G1Tile& g = static_cast<const fp::G1Tile*>(ctx)[k];
    const units::LineEdge e = units::line_edge(*g.sim, *g.uctl, c);
    fp::LineEdge le;
    le.put = e.put; le.ready = e.word & 1; le.level = e.word >> 1 & 1;
    return le;
}

// A symbol of the image's .sym (nm's "ADDR [SIZE] TYPE NAME"); ~0 if absent.
uint32_t symbol(const std::string& name) {
    std::ifstream f(std::string(kImages) + "/sort.sym");
    for (std::string ln; std::getline(f, ln); ) {
        std::istringstream s(ln);
        std::vector<std::string> w; for (std::string x; s >> x; ) w.push_back(x);
        if (!w.empty() && w.back() == name) return uint32_t(std::stoul(w[0], nullptr, 16));
    }
    return ~0u;
}

// sort16k.out's "boot N cycles".
uint64_t golden_boot_cycles() {
    std::ifstream f("tests/golden-m2/sort16k.out");
    for (std::string ln; std::getline(f, ln); ) {
        const size_t a = ln.find("; boot ");
        if (ln.rfind("sort ", 0) == 0 && a != std::string::npos) return std::stoull(ln.substr(a + 7));
    }
    return 0;
}

// The chip with the sort's image and M2Sort's sequence, booted to B.
struct SortChip {
    test::Images im;
    Hops hops;
    CncProgram prog;
    std::vector<uint32_t> mem;
    G1Ctx gx;
    SramBlocks sram;
    std::unique_ptr<sim::Sim> m;
    std::vector<fp::G1Tile> g1;
    uint64_t B = 0;
    std::string err;

    explicit SortChip(int fmt, const sim::Options& opt0, unsigned parts = 1) : hops(load_hops("ref/meshsim/hops.txt")) {
        test::RunSpec rs; rs.name = "sort"; rs.dir = kImages; rs.image = "sort";
        if (!test::load_images(rs, im, err)) return;
        // SortTest.Machine.boot
        prog.load_words(lw::BROADCAST, 0x01000000, im.code.data(), im.code.size());
        prog.load_words(lw::BROADCAST, im.data_at, im.data.data(), im.data.size());
        const uint32_t entry = im.entry;
        prog.command(lw::BROADCAST, 0x4Au << 24, &entry, 1);
        prog.run_when_ready(0);
        prog.release();
        // M2Sort's sequence (queued after the boot; released at B)
        using namespace sortin;
        std::vector<uint32_t> cmd = {CMD_L1 << 24 | uint32_t(fmt), CMD_SPLIT << 24, CMD_XB << 24, CMD_RED << 24, CMD_XC << 24,
                                     CMD_TH << 24, CMD_SWEEP << 24, CMD_DESC << 24, CMD_K << 24};
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K1 << 24 | l);
        for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_K2 << 24 | l);
        if (fmt == FMT_K128) {
            cmd.push_back(CMD_V << 24);
            for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V1 << 24 | l);
            for (uint32_t l = 0; l < 5; l++) cmd.push_back(CMD_V2 << 24 | l);
        }
        cmd.push_back(CMD_L2 << 24);
        prog.command(lw::BROADCAST, cmd[0]).go();
        for (size_t i = 1; i <= cmd.size(); i++) {
            if (cmd[i - 1] >> 24 == CMD_SPLIT)
                for (int b = 0; b < 66; b++) prog.command(lw::BROADCAST, CMD_BARRIER << 24).at_all_ready(0).go();
            prog.command(lw::BROADCAST, i < cmd.size() ? cmd[i] : CMD_EXIT << 24).at_all_ready(0).go();
        }
        prog.release();

        TileConfig tc = test::tile_config(0);
        mem.assign(64 * 2 * MEM_WORDS, 0);
        gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
        gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
        gx.bootrom = im.bootrom.data(); gx.bootrom_words = uint32_t(im.bootrom.size()); gx.mem = mem.data();
        gx.sram = &sram;
        sim::Options opt = opt0;
        opt.observe_arrivals = true;
        opt.threads = parts;
        sim::Config cfg(opt);
        uint8_t dom[64];
        row_bands(parts, dom);
        ChipSpec spec;
        spec.net = NetConfig{}; spec.hops = &hops; spec.program = &prog; spec.control = add_g1; spec.control_ctx = &gx;
        spec.domain = parts > 1 ? dom : nullptr;
        build_chip(cfg, spec);
        m = std::make_unique<sim::Sim>(cfg.build());
        g1.resize(64);
        for (unsigned k = 0; k < 64; k++) { g1[k] = fp::g1_tile(*m, k); g1[k].uinj = &m->unit<Injection>(tile_unit(k, "inj")); }
    }
    bool all_waiting() const {
        for (unsigned k = 0; k < 64; k++) if (!g1[k].core->in_wfi || g1[k].core->halt != Rv32::RUN) return false;
        return true;
    }
    // Run to the boot's end (ref's runUntil, checked between cycles); B is that cycle.
    bool boot(uint64_t max = 100000) {
        const Cnc& cc = m->unit<Cnc>("cnc");
        for (uint64_t c = 1; c <= max; c++) {
            m->run(1);
            if (!cc.busy() && all_waiting()) { B = c; return true; }
        }
        err = "boot: not every tile waits for its first command by cycle " + std::to_string(max);
        return false;
    }
    // SortTest.loadInput, then the sequence released (M2Sort queues it before its run starts).
    void load(const sortin::Keys& k, int fmt) {
        const uint32_t s = symbol("S");
        std::vector<BigSram*> sr(64);
        for (unsigned t = 0; t < 64; t++) sr[t] = &m->unit<BigSram>(tile_unit(t, "sram"));
        sortin::load_input(k, fmt, s,
                           [&](uint32_t t, uint32_t c, uint32_t v) { sr[t]->poke(c, v); },
                           [&](uint32_t t, uint32_t a, uint32_t v) { mem[size_t(t) * 2 * MEM_WORDS + ((a & 0x7FFF) >> 2)] = v; });
        m->unit<Cnc>("cnc").release_next();
        m->wake("cnc");
    }
};

std::string sha256_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return test::sha256_hex(data.data(), data.size());
}

}  // namespace

TEST(sort_boot_ends_at_refs_cycle) {
    SortChip s(sortin::FMT_K128, {});
    CHECK(s.err.empty());
    if (!s.err.empty()) { std::fprintf(stderr, "%s\n", s.err.c_str()); return; }
    CHECK(symbol("S") == 0x1778u);
    CHECK(s.boot());
    const uint64_t want = golden_boot_cycles();
    CHECK(want != 0);
    CHECK_EQ(s.B, want);
    std::fprintf(stderr, "sort: the boot ends at cycle %" PRIu64 " (sort16k.out: %" PRIu64 ")\n", s.B, want);
}

TEST(sort_state_after_the_input_load_equals_refs) {
    const char* keys = std::getenv("SORT_KEYS");
    const char* golden = std::getenv("SORT_FP");
    if (!keys || !golden) return;                     // on demand (header)
    const std::string kh = sha256_file(keys);
    CHECK(kh == "3db703f3e170ee6fa337eb7ee539800b371a3d6fcf924225baade1a8386aeb40");   // HASHES: keys-16384/uniform.keys
    std::string err;
    const sortin::Keys k = sortin::read_keys(keys, err);
    CHECK(err.empty());
    if (!err.empty()) return;
    fp::FpGolden g = fp::FpGolden::load(golden);
    CHECK(g.error.empty());
    if (!g.error.empty()) { std::fprintf(stderr, "%s\n", g.error.c_str()); return; }
    // D12: the golden's configuration must be the one built here (test_chip's keys)
    const std::pair<const char*, const char*> cfgkeys[] = {{"linkLatency", "3"}, {"queueDepth", "16"}, {"K", "8"}, {"cmLatency", "2"},
        {"goLatency", "8"}, {"readyLatency", "8"}, {"cncReaction", "1"}, {"cncGoSettle", "true"}, {"skew", "true"},
        {"skewSeed", "1"}, {"readTokens", "64"}, {"intraTileLatency", "1"}, {"doneGate", "true"}, {"memChunks", "1048576"},
        {"tsEngineLatency", "1"}, {"areaQueueDepth", "8"}, {"sramReadLatency", "2"}, {"writeCombining", "false"},
        {"fifoCountBeforePop", "true"}, {"weInDepth", "4"}, {"gatherDepth", "16"}, {"readPortDepth", "64"}};
    for (auto& kv : cfgkeys) {
        auto it = g.config.find(kv.first);
        if (it == g.config.end() || it->second != kv.second) {
            test::fail(__FILE__, __LINE__, "refused (D12): the golden's %s is %s, this run builds %s", kv.first,
                       it == g.config.end() ? "absent" : it->second.c_str(), kv.second);
            return;
        }
    }
    for (unsigned parts : {1u, 4u}) {
        SortChip s(sortin::FMT_K128, {}, parts);
        CHECK(s.err.empty() && s.boot());
        s.load(k, sortin::FMT_K128);
        auto pts = g.at(s.B);
        CHECK(!pts.empty());
        if (pts.empty()) return;
        std::vector<fp::G2Tile> g2(64);
        for (unsigned t = 0; t < 64; t++) g2[t] = fp::g2_tile(*s.m, t);
        fp::G2Chip gc;
        gc.cnc = &s.m->unit<Cnc>("cnc");
        for (unsigned r = 0; r < 8; r++) gc.rows[r] = &s.m->unit<RowCollector>("row" + std::to_string(r));
        gc.ep[0] = &s.m->unit<Endpoint>("ep2"); gc.ep[1] = &s.m->unit<Endpoint>("ep3");
        gc.ready_latency = NetConfig{}.ready_latency;
        gc.edge = line_edge; gc.edge_ctx = s.g1.data();
        fp::FpRegistry reg;
        std::vector<unsigned> tiles;
        for (unsigned t = 0; t < 64; t++) { fp::register_g1(reg, s.g1[t]); fp::register_g2_tile(reg, g2[t]); tiles.push_back(t); }
        fp::register_g2(reg, gc);
        std::vector<std::string> lines;
        reg.point(fp::FpCtx{s.B, s.m.get()}, tiles, true, lines);
        const std::vector<fp::FpLine> ours = fp::parse_lines(lines);
        for (const fp::FpPoint* p : pts) {
            fp::FpDiff d = fp::compare(*p, ours);
            if (d.any) {
                if (std::getenv("SORT_DUMP")) reg.dump(d.unit, fp::FpCtx{s.B, s.m.get()}, stderr);
                test::fail(__FILE__, __LINE__, "sort, %u domain(s): at B = %" PRIu64 ", %s: %s", parts, s.B, d.unit.c_str(), d.why.c_str());
                return;
            }
        }
        std::fprintf(stderr, "sort (%u domain%s): the state at B = %" PRIu64 ", after the input load, equals ref's (%zu units)\n",
                     parts, parts > 1 ? "s" : "", s.B, ours.size());
    }
}
