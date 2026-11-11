// test_tileslotconf: PeripheralSlotTest's groups (the window, events, region writes, the DMA, the replayer's window) as
// conformance against ref (WORKER15; owner 2026-11-09). Each tests/tileslotconf/<group>.prog (oracle/ref/
// tileslotconf_progs.py) is a boot-ROM program doing the group's loads and stores from the core, storing its results and
// mcycle stamps to data memory, with pokes on the test peripheral between cycles and region writes into the tile;
// <group>.obs is ref's run of it (oracle/ref/TileSlotConf.java: its tile in Config.machine()). Here: the same program on a
// tile from add_g1 with the slot (G1Config::periph_slot), the pokes at the same cycles (between runs, the slot woken with
// Sim::wake), each region write put in the tile's eject one cycle before ref's director accepted it (its "dir" line).
// The data-memory image, the test peripheral's accepts, the bad packets and the DMA's packets must equal ref's.
#include <cstdio>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "harness.h"
#include "mem_stand.h"
#include "portconf.h"
#include "units/test_peripheral.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

const std::string kDir = "tests/tileslotconf/";

struct Prog {
    unsigned tile = 0, cycles = 0, dump_a = 0, dump_n = 0;
    bool test = false;
    std::vector<uint32_t> rom;
    struct Poke { uint64_t c; std::string op; };
    std::vector<Poke> pokes;
};
struct Inject { uint64_t t; unsigned src; uint32_t addr; uint64_t lo, hi; };

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> r;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(' ', p);
        if (q == std::string::npos) q = s.size();
        if (q > p) r.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return r;
}
std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> r;
    size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find('\n', p);
        if (q == std::string::npos) q = s.size();
        r.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return r;
}
uint64_t hex(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }

Prog parse(const std::string& text) {
    Prog p;
    for (const std::string& l : lines(text)) {
        if (l.empty() || l[0] == '#') continue;
        std::vector<std::string> f = split(l);
        if (f[0] == "tile") p.tile = unsigned(std::stoul(f[1]));
        else if (f[0] == "periph") p.test = f[1] == "test";
        else if (f[0] == "cycles") p.cycles = unsigned(std::stoul(f[1]));
        else if (f[0] == "rom") for (size_t i = 1; i < f.size(); i++) p.rom.push_back(uint32_t(hex(f[i])));
        else if (f[0] == "poke") p.pokes.push_back({std::stoull(f[1]), f[2]});
        else if (f[0] == "dump") { p.dump_a = unsigned(hex(f[1])); p.dump_n = unsigned(std::stoul(f[2])); }
        else if (f[0] != "inject") FATAL("tileslotconf: %s", l.c_str());
    }
    return p;
}

struct Stand {   // W9's side: the region writes into the tile's eject; the rest idle
    sim::LinkOut eject, go;
    sim::HoldIn send;
    sim::RegOut send_busy;
    sim::LinkIn line;
    sim::CountOut ovf;
    sim::LinkOut ovf_ev;
    const Inject* in = nullptr;
    unsigned n = 0, i = 0, tile = 0;
    Stand(sim::Ports& p, const Inject* x, unsigned count, unsigned t) : in(x), n(count), tile(t) {
        p.out(eject, "eject", 3); p.out(go, "go", 0); p.in(send, "send", 3); p.out(send_busy, "send_busy", 1);
        p.in(line, "line", 1); p.out(ovf, "ovf"); p.out(ovf_ev, "ovf_ev", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(send)) c.take(send);
        if (c.ready(line)) c.take(line);
        for (; i < n && in[i].t == c.now(); i++)
            c.put(eject, in[i].lo, lw::hdr(in[i].src, tile, lw::M_WRITE, 0, in[i].addr, lw::SIZE_128), in[i].hi);
        return i < n ? sim::Wake::at_cycle(in[i].t) : sim::Wake::none();
    }
    uint64_t digest() const { return i; }
};

std::string run(const Prog& p, const std::vector<Inject>& inj, const sim::Options& o) {
    std::vector<uint32_t> mem(64 * 2 * MEM_WORDS, 0);
    G1Ctx g;
    g.bootrom = p.rom.data(); g.bootrom_words = unsigned(p.rom.size()); g.mem = mem.data();
    static SramBlocks sram_blocks; g.sram = &sram_blocks;
    g.cfg.periph_slot = true; g.cfg.periph1 = p.test ? PT_TEST : PT_REPLAYER;
    sim::Config cfg(o);
    auto& s = cfg.add<Stand>("stand", inj.data(), unsigned(inj.size()), p.tile);
    ControlIO io = add_g1(cfg, p.tile, &g);
    cfg.link(s.eject, io.eject, 1);
    cfg.link(s.go, io.go, 1);
    cfg.connect(io.send, s.send);
    cfg.connect(s.send_busy, io.send_busy);
    cfg.link(io.line, s.line, 6);
    cfg.connect(s.ovf, io.router_ovf);
    cfg.link(s.ovf_ev, io.router_ovf_ev, 1);
    wire_mem_stand(cfg, io);
    units::reqbus::tie_off(cfg, "t" + std::to_string(p.tile) + ".reqtie", io.req);      // W14: no request bus in a lone tile
    sim::Sim m = cfg.build();
    const std::string t = "t" + std::to_string(p.tile) + ".";
    PeriphSlot& sl = m.unit<PeriphSlot>(t + "slot");
    for (const Prog::Poke& k : p.pokes) {
        if (m.now() < k.c) m.run_until(k.c - 1);
        TestPeripheral& x = sl.s.test_peripheral(1);
        if (k.op == "post") x.post(int64_t(k.c));
        else if (k.op == "credit") x.credit(int64_t(k.c));
        else if (k.op == "level1") x.level(true);
        else if (k.op == "level0") x.level(false);
        else if (k.op == "fault") x.fault();
        else FATAL("tileslotconf: poke %s", k.op.c_str());
        m.wake(t + "slot");
    }
    m.run_until(p.cycles - 1);
    const TileCore& core = m.unit<TileCore>(t + "core");
    std::string out;
    char b[200];
    for (unsigned i = 0; i < p.dump_n; i++) {
        const uint32_t a = p.dump_a + 4 * i;
        std::snprintf(b, sizeof b, "dmem %x %x\n", a, core.rv.dmem[a >> 2]);
        out += b;
    }
    std::snprintf(b, sizeof b, "bad %llu\n", (unsigned long long)m.unit<Director>(t + "dir").bad_packets);
    out += b;
    if (p.test) {
        const TestPeripheral& x = sl.s.test_peripheral(1);
        for (unsigned i = 0; i < x.n_accepts && i < TestPeripheral::MAXA; i++) {
            const auto& e = x.accepts[i];
            std::snprintf(b, sizeof b, "acc %lld %d %x %llx %llx\n", (long long)e.land_t, e.src, e.offset, (unsigned long long)e.lo, (unsigned long long)e.hi);
            out += b;
        }
    }
    for (uint64_t i = 0; i < sl.dma_recorded && i < PeriphSlot::DMA_RECORD; i++) {
        const DmaPacket& d = sl.dma_record[i];
        std::snprintf(b, sizeof b, "dma %lld %u %u %u %x %llx %llx\n", (long long)d.t, unsigned(lw::dst(d.h)), unsigned(lw::mode(d.h)),
                      unsigned(lw::counter(d.h)), unsigned(lw::addr(d.h)), (unsigned long long)d.lo, (unsigned long long)d.hi);
        out += b;
    }
    return out;
}

bool check(const char* group, const sim::Options& o) {
    bool ok1 = false, ok2 = false;
    const Prog p = parse(portconf::read_command("cat " + kDir + group + ".prog", &ok1));
    const std::string ref = portconf::read_command("cat " + kDir + group + ".obs", &ok2);
    if (!ok1 || !ok2) { std::fprintf(stderr, "  %s: fixture missing\n", group); return false; }
    std::vector<Inject> inj;
    std::string want;
    for (const std::string& l : lines(ref)) {
        if (l.empty() || l[0] == '#') continue;
        std::vector<std::string> f = split(l);
        if (f[0] == "dir") { inj.push_back({std::stoull(f[1]) - 1, unsigned(std::stoul(f[2])), uint32_t(hex(f[3])), hex(f[4]), hex(f[5])}); continue; }
        want += l + "\n";
    }
    const std::string got = run(p, inj, o);
    if (got == want) return true;
    std::vector<std::string> a = lines(want), b = lines(got);
    for (size_t i = 0; i < a.size() || i < b.size(); i++) {
        const std::string x = i < a.size() ? a[i] : "(none)", y = i < b.size() ? b[i] : "(none)";
        if (x != y) std::fprintf(stderr, "  %s: ref %s | ours %s\n", group, x.c_str(), y.c_str());
    }
    return false;
}

const char* kGroups[] = {"window", "events", "landing", "dma", "replayer"};

}  // namespace

TEST(peripheral_slot_test_groups_equal_ref) {
    for (int variant = 0; variant < 3; variant++) {
        sim::Options o;
        if (variant == 1) o.step_all = true;
        if (variant == 2) o.shuffle_seed = 3;
        for (const char* g : kGroups) CHECK(check(g, o));
    }
}
