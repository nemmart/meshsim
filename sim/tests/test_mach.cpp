// test_mach.cpp (WORKER20): the machine harness (mach.h). With the header only: mach::fields against ref's Fields.java
// (the values printed by ref's own Fields.java, 2026-11-10), and the header compiling with the units it names.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <string>

#include "base/fatal.h"
#include "harness.h"
#include "suite.h"
#include "java_random.h"
#include "mach.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/gather.h"
#include "units/linkword.h"
#include "units/tile_ctl.h"

using namespace mach::fields;
using mach::Mach;

TEST(mach_fields_as_refs_Fields) {
    CHECK_EQ(master_config(RUN_GROUPS, 0, 16, 0, 0, 1), 0x2001u);                // A2A.load's DMA_CONFIG
    CHECK_EQ(master_config(RUN_STREAMS, 255, 511, 7, 7, 8), 0x07FFFFFEu);
    CHECK_EQ(stream_src(0x1FFFFF, 7), 0x00FFFFFFu);
    CHECK_EQ(stream_count(0x1FFFF, SIZE_64, 1, 1, 63), 0x7E4BFFFFu);              // count, size 1, counter 1, M_SRC, d 63
    CHECK_EQ(group_lo(0xFFFFFFFF), 0x3FFFFu);
    CHECK_EQ(group_hi(4, 4, 0xFF), 0xFF304u);
    CHECK_EQ(hdr_bits(SIZE_128, 7, 7, 127), 0xFFFC0000u);
}

// ---- oracle/ref/MachSelf.java's sequence, line for line (fixture tests/mach/machself.out, oracle/ref/machself.sh) ----
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
std::string all(const Mach& m, const Mach::Image& im, const char* s, unsigned i) {
    const uint32_t v = m.word(0, im, s, i);
    for (unsigned k = 0; k < 64; k++) if (m.word(k, im, s, i) != v) return "differs (tile " + std::to_string(k) + ")";
    char b[16]; std::snprintf(b, sizeof b, "0x%x", v); return b;
}
void report(const std::string& name, Mach& m, bool ok) {
    say("%s: %s at cycle %lld; bootStart %lld runAt %lld; allExited %s allInWfi %s; halts [%s]; exit(0) %lld exit(63) %lld\n",
        name.c_str(), ok ? "stopped" : "NOT stopped", (long long)m.c.cycle(), (long long)m.boot_start, (long long)m.run_at,
        tf(m.all_exited()), tf(m.all_in_wfi()), m.halt_summary().c_str(), (long long)m.exit_value(0), (long long)m.exit_value(63));
}
const std::vector<std::string> kKit = {"cfg.linkLatency=4", "cfg.cmLatency=4", "cfg.goLatency=4", "cfg.runLatency=8", "cfg.areaQueueDepth=4",
                                       "cfg.sramReadLatency=3", "cfg.tsEngineLatency=2", "cfg.skewSeed=5", "cfg.checkDisjoint=false", "cfg.skew=true"};
void a2a(int64_t seed, bool kit = false, bool s1 = true) {
    mach::MachConfig cfg = Mach::cfg(seed);
    cfg.measure_s1 = s1;
    std::string err;
    if (kit && !cfg.set_args(kKit, err)) FATAL("%s", err.c_str());
    Mach m(cfg);
    mach::A2A data(7, 2000);
    for (unsigned s = 0; s < 64; s++) {
        mach::TileRef p = m.c.tile(s);
        for (size_t k = 0; k < data.rec[s].size(); k++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(k), uint64_t(data.rec[s][k]));
        for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(data.cnt[s][d]));
        for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(data.cnt[src][s]));
        for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(data.R[g]));
        p.sram_poke(0xF0000 + 144, 0);
    }
    m.boot("a2a");
    m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go().command(units::lw::BROADCAST, 3u << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 2000000);
    report("a2a seed " + std::to_string(seed) + (kit ? " kit" : "") + (s1 ? "" : " no S1"), m, ok);
    {
        const std::string d = m.c.digest();                 // B25: its fp field, ref's StateFp of the chip
        say("  fp %s\n", d.substr(d.find(" fp ") + 4).c_str());
    }
    say("%s", m.c.ag_report().c_str());
    if (sim::check::kOn) say("  %s\n", m.c.s1_report().c_str());   // (S1: the debug and suite builds)
    std::string pl = "  phaseLog";
    for (const auto& e : m.c.cc().phase_log()) pl += " [" + std::to_string(e[0]) + ", " + std::to_string(e[1]) + "]";
    say("%s\n", pl.c_str());
    say("  misplaced %d; lastGoAt %lld lastAllReadyAt %lld; busy %s; sram t9 0x40000 %016llx t50 0x40100 %016llx\n", data.verify(m.c),
        (long long)m.c.cc().last_go_at(), (long long)m.c.cc().last_all_ready_at(), tf(m.c.cc().busy()),
        (unsigned long long)m.c.tile(9).sram_peek64(0x40000), (unsigned long long)m.c.tile(50).sram_peek64(0x40100));
}
void stageb(bool lone) {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.test_periph_tile = 0;
    if (lone) for (int t = 1; t < 64; t++) k.parked.push_back(t);
    mach::Chip c(k);
    const Mach::Image im = Mach::Image::load("regs", k);
    c.cc().set_expect(1);
    c.cc().load_words(0, 0x01000000, im.code).load_words(0, im.sym("_data_start"), im.data).command(0, 0x4Au << 24, {im.sym("_start")}).go();
    mach::TileRef p = c.tile(0);
    const bool ok = c.run_until([&] { return p.halt() != units::Rv32::RUN && p.halt() != 0; }, 100000);
    say("stageb lone %s: %s at cycle %lld; tile 0 halt %d pc 0x%x exit %lld; PARAMS t0 %x t1 %x; parkedUntouched %s; violations %llu\n",
        tf(lone), ok ? "stopped" : "NOT stopped", (long long)c.cycle(), p.halt(), p.pc(), (long long)p.exit_value(),
        *p.load(c.cycle() + 1, 0xFFFFF800u, 4), *c.tile(1).load(c.cycle() + 1, 0xFFFFF800u, 4), tf(c.parked_untouched()),
        (unsigned long long)c.violations());
    say("%s", c.violation_report().c_str());
}
void phases(int64_t seed) {
    Mach m(Mach::cfg(seed));
    mach::A2A data(7, 2000);
    for (unsigned s = 0; s < 64; s++) {
        mach::TileRef p = m.c.tile(s);
        for (size_t k = 0; k < data.rec[s].size(); k++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(k), uint64_t(data.rec[s][k]));
        for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(data.cnt[s][d]));
        for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(data.cnt[src][s]));
        for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(data.R[g]));
        p.sram_poke(0xF0000 + 144, 0);
    }
    m.c.phases().namer = [](int32_t w) -> std::string {
        switch (uint32_t(w) >> 24) { case 1: return "configure"; case 2: return "start (a2a)"; case 3: return "exit"; case 0x4A: return "boot"; default: return "?"; }
    };
    m.boot("a2a");
    m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go().command(units::lw::BROADCAST, 3u << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 2000000);
    m.c.phases().close();
    say("phases seed %lld: %s at cycle %lld\n", (long long)seed, ok ? "stopped" : "NOT stopped", (long long)m.c.cycle());
    say("%s", m.c.phases().report().c_str());
    say("%s", m.c.phases().csv().c_str());
}
void trace_case(bool rdpeak = false) {                       // H6: MachSelf.trace(); rdpeak: W24's F6 (PhaseLog's read-port peak)
    const int N = 4096;
    mach::MachConfig cfg = Mach::cfg(-1);
    if (rdpeak) cfg.sram_read_latency = 4;
    Mach m(cfg);
    if (rdpeak) m.c.phases();
    test::JavaRandom r(N);
    for (unsigned p = 0; p < 64; p++) {
        std::vector<int64_t> k(N);
        for (int i = 0; i < N; i++) k[size_t(i)] = int64_t(uint32_t(r.next(32)));
        std::sort(k.begin(), k.end());
        for (int i = 0; i < N; i++) m.c.tile(p).sram_poke(uint32_t(i), uint32_t(k[size_t(i)]));
        m.c.tile(p).sram_poke(0xF0000, N); m.c.tile(p).sram_poke(0xF0001, 0);
        for (unsigned i = 0; i < 64; i++) m.c.tile(p).sram_poke(0xF0002 + i, i == 0 ? 0u : i == 1 ? 0xFFFFFFFFu : uint32_t(r.next(32)));
    }
    m.boot("search");
    for (unsigned p = 0; p < 64; p++) m.c.tile(p).record_trace();
    m.c.cc().command(units::lw::BROADCAST, 1u << 24 | (rdpeak ? 1u : 0u) << 8 | 4u).go();
    m.run([&] { return !m.c.cc().busy(); }, 5000000);
    uint64_t d0[64];
    for (unsigned p = 0; p < 64; p++) d0[p] = m.c.tile(p).block().dones;
    const bool ok = m.run([&] { for (unsigned p = 0; p < 64; p++) if (m.c.tile(p).block().dones <= d0[p]) return false; return true; }, 5000000);
    if (rdpeak) {
        m.c.phases().close();
        say("rdpeak: %s at cycle %lld\n", ok ? "finished" : "NOT finished", (long long)m.c.cycle());
        say("%s", m.c.phases().csv().c_str());
        return;
    }
    say("trace: %s at cycle %lld\n", ok ? "finished" : "NOT finished", (long long)m.c.cycle());
    for (unsigned t : {0u, 7u, 63u}) {
        const auto l = m.c.tile(t).trace_log();
        std::string sb = "  tile " + std::to_string(t) + ": " + std::to_string(l.size()) + " TRACE stores";
        for (const auto& e : l) { char b[48]; std::snprintf(b, sizeof b, " %lld:%llx", (long long)e[0], (unsigned long long)uint64_t(e[1])); sb += b; }
        say("%s\n", sb.c_str());
    }
}
std::string fp_of(const Mach& m) { const std::string d = m.c.digest(); return d.substr(d.find(" fp ") + 4); }
void reset_case(int64_t seed) {                               // B20: MachSelf.resetCase
    Mach m(Mach::cfg(seed));
    mach::A2A data(7, 2000);
    for (unsigned s = 0; s < 64; s++) {
        mach::TileRef p = m.c.tile(s);
        for (size_t k = 0; k < data.rec[s].size(); k++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(k), uint64_t(data.rec[s][k]));
        for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(data.cnt[s][d]));
        for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(data.cnt[src][s]));
        for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(data.R[g]));
        p.sram_poke(0xF0000 + 144, 0);
    }
    m.boot("a2a");
    m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go();
    m.run([&] { return m.c.cycle() >= 5200; }, 100000);
    m.c.cc().reset(false);
    m.run([&] { return m.c.resets() == 1; }, 100000);
    std::string sb = "reset seed " + std::to_string(seed) + ": at " + std::to_string(m.c.last_reset_at()) + " fp " + fp_of(m);
    m.c.step(); sb += "; next fp " + fp_of(m);
    m.run([&] { return !m.c.cc().busy(); }, 100000);
    sb += "; queue done at " + std::to_string(m.c.cycle()) + " fp " + fp_of(m);
    m.boot("a2a");
    m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go().command(units::lw::BROADCAST, 3u << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    sb += std::string("; rerun ") + (ok ? "stopped" : "NOT stopped") + " at " + std::to_string(m.c.cycle()) + " fp " + fp_of(m) +
          " violations " + std::to_string(m.c.violations());
    say("%s\n", sb.c_str());
    for (const std::string& l : m.c.log()) if (l.find("reset") != std::string::npos) say("  %s\n", l.c_str());
}
void machself() {
    out.clear();
    reset_case(-1);
    reset_case(4);
    trace_case();
    if (sim::check::kOn) trace_case(true);                 // (PhaseLog: the debug and suite builds)
    if (sim::check::kOn) { phases(-1); phases(4); }        // (PhaseLog's FIFO peaks: the debug and suite builds)
    stageb(true);
    stageb(false);
    {
        Mach m(Mach::cfg(-1));
        Mach::Image im = m.boot("regs");
        const bool ok = m.run([&] { return m.all_stopped(); }, 40000);
        report("regs", m, ok);
        say("  r[35] %s; tile 5 pc 0x%x x10 %d\n", all(m, im, "r", 35).c_str(), m.c.tile(5).pc(), int32_t(m.c.tile(5).x(10)));
    }
    {
        Mach m(Mach::cfg(-1));
        Mach::Image im = m.boot("wfi");
        m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go().wait_cycles(700)
            .command(units::lw::BROADCAST, 3u << 24).go();
        m.c.run(1500);
        say("wfi after run(1500): cycle %lld runAt %lld busy %s allInWfi %s\n", (long long)m.c.cycle(), (long long)m.run_at,
            tf(m.c.cc().busy()), tf(m.all_in_wfi()));
        const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
        report("wfi", m, ok);
        say("  r[1] %s\n", all(m, im, "r", 1).c_str());
    }
    a2a(-1);
    a2a(4);
    a2a(-1, true);
    a2a(4, false, false);
    {
        Mach m(Mach::cores_cfg(-1));
        m.boot_cores("wfi");
        const bool ok = m.c.run_until([&] { return m.cores_booted(); }, 400000);
        say("cores: booted %s at cycle %lld; bootStart %lld; mbox %d %d %d %d; tile 0 pc 0x%x, cc1 pc 0x%x\n", tf(ok),
            (long long)m.c.cycle(), (long long)m.boot_start, int32_t(m.mbox(0)), int32_t(m.mbox(1)), int32_t(m.mbox(2)),
            int32_t(m.mbox(3)), m.c.tile(0).pc(), m.c.tile(64).pc());
    }
}
std::string file(const char* path) {
    std::string s;
    FILE* f = std::fopen(path, "rb");
    if (!f) return "(missing)";
    char b[4096]; size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}
}  // namespace

// ---- oracle/ref/NetSelf.java's sequence (fixture tests/mach/netself.out, oracle/ref/netself.sh) ----
namespace {
namespace lw = units::lw;
using mach::Net;
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
    for (unsigned s = 0; s < 64; s += 7) n.at(s, 20, 9, lw::M_WRITE, Net::cm(0x800 + 2 * s), Net::tagged(seq++, 20));
    for (int64_t T = 30; T < 40; T++) n.at(0, T, 63, lw::M_WRITE, Net::cm(0x900 + 2 * uint32_t(T - 30)), Net::tagged(seq++, T));
    n.at(5, 50, lw::BROADCAST, lw::M_WRITE, 0, Net::cm(0x880), lw::SIZE_128, 0x1111222233334444ull, 0x5555666677778888ull);
    n.at(9, 51, 9, lw::M_WRITE, 0, Net::cm(0x881), lw::SIZE_32, 0xABCD, 0);
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
    n.rd(3, 2, 10, 0x2000, lw::SIZE_64, lw::M_WRITE, 0, 12, Net::cm(0x8A0));
    n.rd(3, 0, 10, 0x2004, lw::SIZE_128, lw::M_WRITE, 0, 12, Net::cm(0x8A4));
    n.rd(3, 1, 11, 0x2008, lw::SIZE_32, lw::M_WRITE, 0, 3, Net::cm(0x8A8));
    for (uint32_t i = 0; i < 70; i++) n.rd(3, 2, 12, 0x2000 + (i & 15), lw::SIZE_32, lw::M_WRITE, 0, 20 + (i & 7), Net::cm(0x8C0 + (i & 31)));
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
std::string recv(Net& n, unsigned c) {
    const auto r = n.received(c);
    std::string sb = std::to_string(r.size()) + " [";
    for (const auto& e : r) sb += " " + std::to_string(e[0]) + ":" + hx(e[1]) + ":" + hx(e[2]);
    return sb + " ]";
}
void controllers(int64_t seed) {
    Net n(Net::cfg(seed));
    int64_t T0 = n.c.cycle() + 2;
    n.cnc_send({T0, lw::BROADCAST, lw::M_SRC, 0, Net::tagged(64, T0)});
    bool ok = n.run_all(500);
    int64_t lo = INT64_MAX, hi = 0, copies = 0;
    for (unsigned d = 0; d < 64; d++) for (const auto& e : n.log(d)) { lo = std::min(lo, int64_t(e[0])); hi = std::max(hi, int64_t(e[0])); copies++; }
    say("controllers seed %lld: broadcast %s at cycle %lld; %lld copies, first %lld last %lld; C&C 1 received %s\n", (long long)seed,
        ok ? "drained" : "NOT drained", (long long)n.c.cycle(), (long long)copies, (long long)(lo - T0), (long long)(hi - T0), recv(n, 0).c_str());
    n.clear_logs();
    T0 = n.c.cycle() + 2;
    n.cnc_send({T0, 9, lw::M_WRITE, 0x80 + 12, 0xCAFE0003ull, lw::SIZE_32, 0, 0});
    n.cnc_send({T0 + 1, 9, lw::M_WRITE, 0x80, 0x0000C0DEull, lw::SIZE_32, 0, 0});
    for (unsigned k = 0; k < 5; k++) n.cnc_send({T0 + 1, 20 + k, lw::M_WRITE, 0x100, k, lw::SIZE_64, 0, 2});
    ok = n.run_all(500);
    say("  command %s at cycle %lld; t9 dmem[0x20] %s dmem[0x23] %s\n", ok ? "drained" : "NOT drained", (long long)n.c.cycle(),
        w(n, 9, 0x20).c_str(), w(n, 9, 0x23).c_str());
    logs(n, {9, 20, 24});
    n.clear_logs();
    T0 = n.c.cycle() + 2;
    for (unsigned s : {0u, 2u, 37u, 63u}) for (unsigned c = 0; c < 3; c++) n.at(s, T0 + c, 64 + c, lw::M_WRITE, 3, 0x100, lw::SIZE_32, s, 0);
    ok = n.run_all(500);
    say("  to the controllers %s at cycle %lld\n", ok ? "drained" : "NOT drained", (long long)n.c.cycle());
    for (unsigned c = 0; c < 3; c++) say("  C&C %u received %s\n", c + 1, recv(n, c).c_str());
    n.clear_logs();
    const int64_t t0 = n.c.cycle();
    n.send(1, 37, lw::M_WRITE, 2, 0x100, lw::SIZE_32, 0x55, 0);
    n.send(0, 5, lw::M_WRITE, 2, 0x104, lw::SIZE_32, 0x56, 0);
    n.step();
    n.send(1, 37, lw::M_WRITE, 2, 0x108, lw::SIZE_32, 0x57, 0);
    n.send(2, 64, lw::M_WRITE, 2, 0x10C, lw::SIZE_64, 0x58, 0);
    ok = n.run_all(300);
    std::string sb = std::string("  direct sends ") + (ok ? "drained" : "NOT drained") + " at cycle " + std::to_string(n.c.cycle() - t0) + " after:";
    for (unsigned d : {37u, 5u}) for (const auto& e : n.log(d)) sb += " t" + std::to_string(d) + "@" + std::to_string(int64_t(e[0]) - t0) + ":" + hx(e[1]);
    for (const auto& e : n.received(0)) sb += " cc1@" + std::to_string(int64_t(e[0]) - t0) + ":" + hx(e[1]) + ":" + hx(e[2]);
    say("%s\n", sb.c_str());
    n.clear_logs();
    n.send(1, 66, lw::M_WRITE, 2, 0x200, lw::SIZE_64, 0x77, 0);
    n.run_all(300);
    const int src = n.received(2).size() == 1 ? int(lw::src(n.received(2)[0][2])) : -1;
    n.clear_logs();
    n.send(1, lw::BROADCAST, lw::M_COUNT, 1, 0, lw::SIZE_32, 9, 0);
    n.run_all(300);
    int reached = 0;
    for (unsigned d = 0; d < 64; d++) if (n.log(d).size() == 1 && lw::src(n.log(d)[0][2]) == 65) reached++;
    say("  C&C 2 -> C&C 3 from %d; C&C 2's broadcast reaches %d tiles once, controllers %zu %zu %zu\n", src, reached,
        n.received(0).size(), n.received(1).size(), n.received(2).size());
}
std::string hxo(const std::optional<uint32_t>& v) { return v ? hx(*v) : "null"; }
std::string qs(const std::string& r) { return r.empty() ? "null" : r; }
void core() {
    Net n(Net::cfg(-1));
    mach::TileRef p = n.c.tile(13);
    const uint32_t REG = 0xFFFFF800u;
    std::string sb = "core:";
    sb += " store dmem " + qs(p.store(n.c.cycle() + 1, 0x100, 0xBEEF));
    n.step();
    sb += "; load dmem " + hxo(p.load(n.c.cycle() + 1, 0x100, 4));
    sb += "; PARAMS " + hxo(p.load(n.c.cycle() + 1, REG, 4)) + " STATUS " + hxo(p.load(n.c.cycle() + 1, REG + 4, 4));
    sb += " ERRORS " + hxo(p.load(n.c.cycle() + 1, REG + 8, 4));
    p.set_remaining(2, 7);
    sb += "; WRITE_COUNTER_2 " + hxo(p.load(n.c.cycle() + 1, REG + 4 * 25, 4));
    sb += "; boot ROM [1] " + hxo(p.load(n.c.cycle() + 1, 0x02000004, 4));
    sb += "; halfword STATUS " + hxo(p.load(n.c.cycle() + 1, REG + 4, 2));
    sb += "; PTR " + hxo(p.load(n.c.cycle() + 1, 0xFFFF0400u, 4));
    say("%s\n", sb.c_str());
    sb = "  stores:";
    sb += " STATUS " + qs(p.store(n.c.cycle() + 1, REG + 4, 1));
    sb += "; byte ROUTER_SET_W1 " + qs(p.store(n.c.cycle() + 1, REG + 4 * 13, 1, 0xFF));
    sb += "; program " + qs(p.store(n.c.cycle() + 1, 0x01000000, 1));
    sb += "; boot ROM " + qs(p.store(n.c.cycle() + 1, 0x02000000, 1));
    sb += "; unmapped " + qs(p.store(n.c.cycle() + 1, 0x0F000000, 1));
    say("%s\n", sb.c_str());
    p.store(n.c.cycle() + 1, REG + 4 * 26, 5);
    n.step(); n.step(); n.step();
    sb = "  WRITE_COUNTER_3 after the store " + hxo(p.load(n.c.cycle() + 1, REG + 4 * 26, 4));
    p.store(n.c.cycle() + 1, REG + 4 * 10, 40u << 25); n.step();
    p.store(n.c.cycle() + 1, REG + 4 * 11, Net::cm(0x8E0)); n.step();
    const int64_t ts = n.c.cycle() + 1;
    p.store(ts, REG + 4 * 15, 0x1234);
    n.step();
    const bool ok = n.run_all(300);
    sb += std::string("; a send ") + (ok ? "drained" : "NOT drained");
    for (const auto& e : n.log(40)) sb += " t40@" + std::to_string(int64_t(e[0]) - ts) + ":" + hx(e[1]) + ":" + hx(e[2]);
    sb += " dmem[0xe0] " + w(n, 40, 0xe0);
    say("%s\n", sb.c_str());
}
void phase_done_probe(int64_t seed) {
    mach::MachConfig k = mach::MachConfig::defaults();
    k.shuffle_seed = seed; k.check_errors = false;
    Net n(k);
    mach::A2A d(7, 600);
    d.load(n.c, 0);
    const int64_t t0 = n.c.cycle();
    const std::vector<int> skew = n.c.start_skew();
    bool started[64] = {};
    int64_t first[4] = {-1, -1, -1, -1}, all = -1;
    const unsigned watch[4] = {0, 5, 40, 63};
    for (int i = 0; i < 100000 && all < 0; i++) {
        for (unsigned p = 0; p < 64; p++) if (!started[p] && n.c.cycle() >= t0 + skew[p]) { n.c.tile(p).pulse(mach::fields::PULSE_START); started[p] = true; }
        n.c.step();
        if (n.c.cycle() < t0 + 3) continue;
        for (int j = 0; j < 4; j++) if (first[j] < 0 && n.c.tile(watch[j]).phase_done()) first[j] = n.c.cycle() - t0;
        bool a = true;
        for (unsigned p = 0; p < 64 && a; p++) a = n.c.tile(p).phase_done();
        if (a) all = n.c.cycle() - t0;
    }
    say("phaseDone seed %lld: tiles 0 5 40 63 first at +%lld +%lld +%lld +%lld; all at +%lld\n", (long long)seed, (long long)first[0],
        (long long)first[1], (long long)first[2], (long long)first[3], (long long)all);
}
void feed_only_in_net() {
    mach::MachConfig k = Net::cfg(-1);
    k.test_read_clients = true;
    Net n(k);
    for (uint32_t i = 0; i < 16; i++) n.c.tile(3).sram_poke(0x2000 + i, 0x100 * i + 7);
    for (int64_t T = 10; T < 15; T++) n.at(4, T, 9, lw::M_WRITE, Net::cm(0x900 + 2 * uint32_t(T - 10)), Net::tagged(uint64_t(T), T));
    n.cnc_send({12, 20, lw::M_WRITE, 0x200, 0x77, lw::SIZE_32, 0, 0});
    n.rd(3, 0, 2, 0x2000, lw::SIZE_64, lw::M_WRITE, 0, 12, Net::cm(0x8A0));
    n.rd(3, 1, 30, 0x2004, lw::SIZE_32, lw::M_WRITE, 0, 12, Net::cm(0x8A4));
    n.step(); n.step(); n.step();
    n.c.run(40);
    std::string sb = "feed: after Chip.run(40) at " + std::to_string(n.c.cycle()) + ": t9 " + std::to_string(n.log(9).size()) + " t20 " +
                     std::to_string(n.log(20).size()) + " t12";
    for (const auto& e : n.log(12)) sb += " @" + std::to_string(e[0]);
    sb += "; late " + std::to_string(n.late()) + "; pending " + tf(n.pending());
    const bool ok = n.run_all(500);
    sb += std::string("; then runAll ") + (ok ? "drained" : "NOT drained") + " at " + std::to_string(n.c.cycle()) + ": late " + std::to_string(n.late()) + " t9";
    for (const auto& e : n.log(9)) sb += " @" + std::to_string(e[0]);
    sb += " t20"; for (const auto& e : n.log(20)) sb += " @" + std::to_string(e[0]);
    sb += " t12"; for (const auto& e : n.log(12)) sb += " @" + std::to_string(e[0]);
    say("%s\n", sb.c_str());
}
void on_machine(int periph) {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = periph;
    Net n(k);
    n.c.run(200);
    const int64_t T0 = n.c.cycle() + 2;
    for (unsigned s = 0; s < 64; s += 9) n.at(s, T0, 9, lw::M_WRITE, 0, Net::cm(0x900 + 2 * s), lw::SIZE_64, Net::tagged(s, T0), 0);
    n.at(5, T0 + 1, lw::BROADCAST, lw::M_WRITE, 0, Net::cm(0x880), lw::SIZE_32, 0xBEEF, 0);
    n.at(6, T0 + 2, 30, lw::M_WRITE, 1, uint32_t(lw::R_SRAM) << 24 | 0x40, lw::SIZE_64, 0x1234, 0);
    const bool ok = n.run_all(2000);
    std::string sb = "machine periph " + std::to_string(periph) + ": " + (ok ? "quiescent" : "NOT quiescent") + " at " + std::to_string(n.c.cycle()) +
                     "; late " + std::to_string(n.late());
    sb += "; t9 dmem[0x112] " + w(n, 9, 0x112) + " t40 dmem[0x80] " + w(n, 40, 0x80) + " t30 sram 0x10 " + hx(n.c.tile(30).sram_peek(0x10));
    sb += "; violations " + std::to_string(n.c.violations());
    say("%s\n", sb.c_str());
    logs(n, {9, 30});
}
void reqbus_case() {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.reqbus_host = true;
    Net n(k);
    n.c.run(200);
    mach::TileRef p = n.c.tile(5);
    const uint32_t REG = 0xFFFFF800u;
    const uint32_t st[4][2] = {{18, (0u << 7) | (0x21u << 10)}, {19, 0x1111}, {20, 0x2222}, {21, 0x3333}};
    for (const auto& x : st) { p.store(n.c.cycle() + 1, REG + 4 * x[0], x[1]); n.step(); }
    mach::ReqBus& b = n.c.reqbus();
    b.controller_send(n.c.cycle(), 1, (0u << 7) | (0x55u << 10), 1, 2, 3);
    std::string sb = "reqbus:";
    int64_t first = -1;
    for (int i = 0; i < 200 && b.queue_size(0) < 2; i++) { n.step(); if (first < 0 && b.pending(0, n.c.cycle() + 1)) first = n.c.cycle(); }
    sb += " first pending at " + std::to_string(first) + ", two queued at " + std::to_string(n.c.cycle());
    sb += "; head " + hx(b.get(0, n.c.cycle() + 1, 0)) + " " + hx(b.get(0, n.c.cycle() + 1, 3));
    sb += "; credits " + std::to_string(b.credits(0));
    { const uint32_t r = b.receive(0, n.c.cycle() + 1); sb += "; receive " + hx(r) + " size " + std::to_string(b.queue_size(0)) + " credits " + std::to_string(b.credits(0)); }
    n.step();
    sb += "; then credits " + std::to_string(b.credits(0)) + " head " + hx(b.get(0, n.c.cycle() + 1, 0));
    sb += "; receive " + hx(b.receive(0, n.c.cycle() + 1)); n.step();
    { const uint32_t r = b.receive(0, n.c.cycle() + 1); sb += "; receive on empty " + std::to_string(r) + " underflows " + std::to_string(b.underflows()); }
    const uint32_t st2[4][2] = {{18, (3u << 7) | (0x31u << 10)}, {21, 0x77}, {18, (5u << 7) | (0x32u << 10)}, {21, 0x78}};
    for (const auto& x : st2) { p.store(n.c.cycle() + 1, REG + 4 * x[0], x[1]); n.step(); }
    n.run_all(500);
    sb += "; dropped " + std::to_string(b.dropped()) + " delivered " + std::to_string(b.delivered()) + " credits " + std::to_string(b.credits(0)) +
          " at " + std::to_string(n.c.cycle());
    sb += "; violations " + std::to_string(n.c.violations());
    say("%s\n", sb.c_str());
}
void hooks_case() {
    const uint32_t REG = 0xFFFFF800u, EVENTS = REG + 4 * 6, PEEK = REG + 4 * 7, ERRORS = REG + 8, GET0 = REG + 4 * 18, RECV = REG + 4 * 21;
    {
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 0;
        Net n(k);
        n.c.run(200);
        mach::TileRef p = n.c.tile(3);
        std::string sb = "hooks:";
        const uint32_t c9 = 1u << units::EV_COUNTERS_DONE;
        p.set_events(c9);
        sb += " peek " + hxo(p.load(n.c.cycle() + 1, PEEK, 4));
        n.step();
        sb += " peek " + hxo(p.load(n.c.cycle() + 1, PEEK, 4));
        p.store(n.c.cycle() + 1, REG + 4 * 8, c9); n.step(); n.step(); n.step();
        sb += " EVENTS " + hxo(p.load(n.c.cycle() + 1, EVENTS, 4));
        n.step(); n.step(); n.step();
        sb += " then peek " + hxo(p.load(n.c.cycle() + 1, PEEK, 4));
        sb += "; GET0 " + hxo(p.load(n.c.cycle() + 1, GET0, 4));
        sb += " RECV " + hxo(p.load(n.c.cycle() + 1, RECV, 4));
        for (int i = 0; i < 4; i++) { sb += " ERRORS@" + std::to_string(n.c.cycle() + 1) + " " + hxo(p.load(n.c.cycle() + 1, ERRORS, 4)); n.step(); }
        const int64_t T = n.c.cycle() + 5;
        p.schedule_go(T);
        while (n.c.cycle() < T + 2) { sb += " peek@" + std::to_string(n.c.cycle() + 1) + " " + hxo(p.load(n.c.cycle() + 1, PEEK, 4)); n.step(); }
        say("%s\n", sb.c_str());
    }
    {
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 4;
        Net n(k);
        n.c.run(200);
        mach::TileRef p = n.c.tile(9);
        std::string sb = "  window 1:";
        for (uint32_t w = 0; w < 8; w++) sb += " " + hxo(p.load(n.c.cycle() + 1, REG + 0x200 + 4 * w, 4));
        say("%s\n", sb.c_str());
    }
    {
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 0; k.reqbus_host = true;
        Net n(k);
        n.c.run(200);
        mach::TileRef s = n.c.tile(12);
        std::string sb = "  req:";
        s.req_set_w(0, (0u << 7) | (0x44u << 10)); s.req_set_w(1, 0x44);
        sb += std::string(" canPush ") + tf(s.req_can_push(n.c.cycle() + 1));
        s.req_send(n.c.cycle(), 0x99);
        sb += " setW " + std::to_string(s.req_w(0) | s.req_w(1) | s.req_w(2));
        int64_t first = -1;
        for (int i = 0; i < 100 && first < 0; i++) { n.step(); if (n.c.reqbus().pending(0, n.c.cycle() + 1)) first = n.c.cycle(); }
        sb += " pending at " + std::to_string(first) + " head " + hx(n.c.reqbus().get(0, n.c.cycle() + 1, 0)) + " " + hx(n.c.reqbus().get(0, n.c.cycle() + 1, 3));
        say("%s\n", sb.c_str());
    }
    {
        Net n(Net::cfg(-1));
        mach::TileRef p = n.c.tile(6);
        for (uint32_t i = 0; i < 16; i++) p.sram_poke(0x1000 + i, 0x500 + i);
        std::string sb = std::string("  rdport: idle ") + tf(p.rp_idle()) + " all " + tf(p.rp_all_accepted());
        for (uint32_t i = 0; i < 10; i++) p.rp_host_queue(0x1000 + i, Net::cm(0x900 + i));
        sb += std::string("; queued: idle ") + tf(p.rp_idle()) + " all " + tf(p.rp_all_accepted());
        int64_t idle_at = -1, acc_at = -1;
        for (int i = 0; i < 200 && acc_at < 0; i++) { n.step(); if (idle_at < 0 && p.rp_idle()) idle_at = n.c.cycle(); if (p.rp_all_accepted()) acc_at = n.c.cycle(); }
        sb += "; idle at " + std::to_string(idle_at) + ", all accepted at " + std::to_string(acc_at);
        n.run_all(500);
        sb += "; dmem[0x100..0x109]";
        for (uint32_t i = 0; i < 10; i++) sb += " " + w(n, 6, 0x100 + i);
        say("%s\n", sb.c_str());
    }
}
void test_peripheral_case() {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.test_peripheral_tile = 7;
    Net n(k);
    n.c.run(200);
    const uint32_t REG = 0xFFFFF800u;
    std::string sb = "test peripheral: PARAMS t7 " + hxo(n.c.tile(7).load(n.c.cycle() + 1, REG, 4)) + " t8 " + hxo(n.c.tile(8).load(n.c.cycle() + 1, REG, 4)) +
                     "; t7 window 1:";
    for (uint32_t w = 0; w < 8; w++) sb += " " + hxo(n.c.tile(7).load(n.c.cycle() + 1, REG + 0x200 + 4 * w, 4));
    sb += "; t8 window 1 word 0 " + hxo(n.c.tile(8).load(n.c.cycle() + 1, REG + 0x200, 4));
    say("%s\n", sb.c_str());
}
void drain_case() {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.reqbus_host = true;
    Net n(k);
    n.c.run(200);
    const uint32_t REG = 0xFFFFF800u;
    n.c.tile(6).store(n.c.cycle() + 1, 0x900, 0x5555); n.step();
    n.c.cc().drain_reports(true);
    const uint32_t rq[4][5] = {{5, 1u << 10, 0x11, 0x12, 0x13}, {6, 3u << 10, 0x900, 0x22, 0x23}, {7, 2u << 10, 0x31, 0x32, 0x33}, {5, 1u << 10, 0x41, 0x42, 0x43}};
    for (const auto& r : rq) {
        mach::TileRef p = n.c.tile(r[0]);
        for (unsigned i = 0; i < 4; i++) { p.store(n.c.cycle() + 1, REG + 4 * (18 + i), r[1 + i]); n.step(); }
    }
    for (int i = 0; i < 300; i++) n.step();
    std::string sb = "drain:";
    for (const auto& e : n.c.cc().reports()) sb += " report@" + std::to_string(e[0]) + " " + hx(uint64_t(e[1])) + " " + hx(uint64_t(e[4]));
    for (const auto& e : n.c.cc().requests()) sb += " request@" + std::to_string(e[0]) + " " + hx(uint64_t(e[1]));
    for (const auto& e : n.c.cc().results()) sb += " result@" + std::to_string(e[0]) + " src " + std::to_string(e[1]) + " " + hx(uint64_t(e[2])) + " " + hx(uint64_t(e[3]));
    sb += "; reportsOf(5) " + std::to_string(n.c.cc().reports_of(5).size()) + " reportsOf(9) " + std::to_string(n.c.cc().reports_of(9).size());
    sb += "; t6 dmem[0x900] " + hxo(n.c.tile(6).load(n.c.cycle() + 1, 0x900, 4)) + " at " + std::to_string(n.c.cycle());
    sb += "; delivered";
    for (const auto& q : n.c.reqbus().delivered_log())
        sb += " " + std::to_string(q[0]) + ":" + std::to_string(q[1]) + ":" + hx(uint64_t(q[2])) + ":" + hx(uint64_t(q[5]));
    say("%s\n", sb.c_str());
}
void back_pressure_case() {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.reqbus_host = true;
    Net n(k);
    n.c.run(200);
    mach::TileRef s = n.c.tile(20);
    for (uint32_t i = 0; i < 128; i++) { s.req_set_w(0, (0u << 7) | (i << 10)); s.req_set_w(1, i); s.req_send(n.c.cycle(), 1000 + i); }
    const units::reqbus::ReqLeaf& l = n.c.unit<units::reqbus::ReqLeaf>("t20.reqleaf");
    std::string sb = std::string("back-pressure: canPush ") + tf(s.req_can_push(n.c.cycle() + 1)) + " stalled " + std::to_string(l.overflows);
    mach::ReqBus& b = n.c.reqbus();
    int got = 0;
    int64_t last = -1, sum = 0;
    for (int i = 0; i < 3000 && got < 128; i++) {
        if (b.pending(0, n.c.cycle() + 1)) { sum += b.receive(0, n.c.cycle() + 1); got++; last = n.c.cycle() + 1; }
        n.step();
    }
    sb += "; received " + std::to_string(got) + " last at " + std::to_string(last) + " sum " + std::to_string(sum) + " credits " + std::to_string(b.credits(0));
    sb += " delivered " + std::to_string(b.delivered()) + " violations " + std::to_string(n.c.violations());
    say("%s\n", sb.c_str());
}
void figure12_case(unsigned L) {
    mach::MachConfig k = mach::MachConfig::defaults();
    k.ts_engine_latency = L; k.K = std::max(k.K, k.k_min()); k.check_errors = false; k.check_unexpected = false;
    mach::Chip c(k);
    mach::TileRef p = c.tile(7), h = c.tile(20);
    p.set_ptr(9, 0x1000); h.hist_poke(3, 100);
    std::string sb = "figure 12, L = " + std::to_string(L) + ":";
    const units::TableStore& ts = p.unit<units::TableStore>("ts");
    const units::HistStore& hs = h.unit<units::HistStore>("hist");
    for (int r = 0; r < 3; r++) {
        const uint64_t before = ts.rmws;
        p.we_ts_req(c.cycle() + 1, units::mem::OP_ADD_RET, units::mem::IDX_PTR + 9, 2);
        int64_t tr = -1;
        for (int i = 0; i < 50 && tr < 0; i++) { c.step(); if (ts.rmws > before) tr = c.cycle(); }
        c.step(); c.step();
        char b[32]; std::snprintf(b, sizeof b, "%x", p.ptr(9));
        sb += " ptr@" + std::to_string(tr) + " " + b;
    }
    const uint64_t before = hs.rmws;
    h.we_hist_req(c.cycle() + 1, 0, 3, 1);
    int64_t tr = -1;
    for (int i = 0; i < 50 && tr < 0; i++) { c.step(); if (hs.rmws > before) tr = c.cycle(); }
    c.step(); c.step();
    sb += "; hist@" + std::to_string(tr) + " " + std::to_string(h.hist_peek(3));
    say("%s\n", sb.c_str());
}
void ub_case() {
    const std::string msg = suite::ub([] {
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 0;
        Net n(k);
        mach::TileRef p = n.c.tile(9);
        p.poke_local(0x01000000, 0x00102503);
        p.poke_local(0x01000004, 0x0000006F);
        n.c.cc().set_expect(1ull << 9);
        n.c.cc().command(9, 0x4Au << 24, {0x01000000}).go();
        n.c.run(2000);
    }, {"tile 9", "controller", "misaligned 4-byte load"}, "a misaligned load");
    say("ub: %s\n", msg.empty() ? "ran on" : msg.c_str());
}
template <class Takes>
int64_t rmw_case(mach::Chip& c, mach::TileRef& p, bool hist, uint32_t op, uint32_t idx, uint32_t add, Takes takes) {
    const uint64_t before = takes();
    if (hist) p.we_hist_req(c.cycle() + 1, op, idx, add); else p.we_ts_req(c.cycle() + 1, op, idx, add);
    for (int i = 0; i < 50; i++) { c.step(); if (takes() > before) return c.cycle(); }
    FATAL("RMW never taken");
}
void fig12b_case(unsigned L) {
    const uint32_t PTRB = units::PTR_BASE, HISTB = units::TABLE_BASE;   // (HIST is the window's first table)
    mach::MachConfig k = Net::cfg(-1);
    k.ts_engine_latency = L; k.K = std::max(k.K, k.k_min());
    std::string sb = "figure 12 values, L = " + std::to_string(L) + ":";
    {
        mach::Chip c(k);
        mach::TileRef p = c.tile(7);
        const units::TableStore& ts = p.unit<units::TableStore>("ts");
        p.set_ptr(9, 0x1000); p.set_ptr(5, 0x2000);
        int64_t tr = rmw_case(c, p, false, units::mem::OP_ADD_RET, units::mem::IDX_PTR + 9, 2, [&] { return ts.rmws; });
        p.store(tr + 1, PTRB + 4 * 5, 0x7777); c.step(); c.step();
        sb += " lost " + hx(p.ptr(5)) + "/" + std::to_string(ts.pipe.ctrl_writes_lost);
        c.run(4);
        tr = rmw_case(c, p, false, units::mem::OP_ADD_RET, units::mem::IDX_PTR + 9, 2, [&] { return ts.rmws; });
        p.store(tr, PTRB + 4 * 5, 0x3000);
        c.step(); c.step();
        sb += " read-cycle " + hx(p.ptr(5));
        p.store(c.cycle() + 1, PTRB + 4 * 5, 0x3004); c.step();
        sb += " peek " + hx(p.ptr(5)) + " ptr9 " + hx(p.ptr(9));
        tr = rmw_case(c, p, false, units::mem::OP_ADD_RET, units::mem::IDX_PTR + 9, 2, [&] { return ts.rmws; });
        p.store(tr, PTRB + 4 * 9, 0x5000); c.step(); c.step();
        sb += " same-entry " + hx(p.ptr(9));
    }
    {
        mach::Chip c(k);
        mach::TileRef p = c.tile(20);
        const units::HistStore& hs = p.unit<units::HistStore>("hist");
        p.hist_poke(3, 100); p.hist_poke(4, 7);
        int64_t tr = rmw_case(c, p, true, 0, 3, 1, [&] { return hs.rmws; });
        p.store(tr + 1, HISTB + 4 * 4, 55); c.step(); c.step();
        sb += "; hist " + std::to_string(p.hist_peek(3)) + " " + std::to_string(p.hist_peek(4)) + "/" + std::to_string(hs.pipe.ctrl_writes_lost);
        tr = rmw_case(c, p, true, 0, 3, 1, [&] { return hs.rmws; });
        { const auto v = p.load(tr + 1, HISTB + 4 * 4, 4); sb += " wb-load " + (v ? std::to_string(*v) : std::string("null")); }
        c.run(3);
        { const auto v = p.load(c.cycle() + 1, HISTB + 4 * 4, 4); sb += " later " + (v ? std::to_string(*v) : std::string("null")); }
        c.step();
        p.store(c.cycle() + 1, HISTB + 4 * 4, 55); c.step();
        sb += " peek " + std::to_string(p.hist_peek(4));
    }
    say("%s\n", sb.c_str());
}
void netself() {
    out.clear();
    fig12b_case(1);
    fig12b_case(3);
    ub_case();
    figure12_case(1);
    figure12_case(3);
    back_pressure_case();
    drain_case();
    test_peripheral_case();
    hooks_case();
    reqbus_case();
    on_machine(0);
    on_machine(4);
    feed_only_in_net();
    core();
    phase_done_probe(-1);
    phase_done_probe(4);
    controllers(-1); controllers(3);
    packets(-1); packets(5);
    reads();
    a2a_net(-1, 0); a2a_net(4, 0); a2a_net(-1, 8);
}
}  // namespace

std::string without_violations(const std::string& x);   // (below: the release builds leave the checker's counts out)
TEST(mach_net_as_refs_Net) {
    netself();
    out = without_violations(out);
    const std::string want = without_violations(file("tests/mach/netself.out"));
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/netself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}

// Without the checker's facts (the release builds) the violation counts and reports are left out of both sides.
std::string without_violations(const std::string& x) {
    if (sim::check::kOn) return x;
    std::string r;
    bool skip = false;
    for (size_t at = 0; at < x.size();) {
        size_t e = x.find('\n', at);
        if (e == std::string::npos) e = x.size();
        std::string l = x.substr(at, e - at);
        at = e + 1;
        if (l.rfind("phases seed ", 0) == 0 || l.rfind("rdpeak: ", 0) == 0) skip = true;   // PhaseLog's: debug and suite only
        if (l.rfind("stageb ", 0) == 0 || (skip && l.rfind("phases seed ", 0) != 0 && l.rfind("a2a ", 0) == 0)) skip = false;
        if (skip) continue;
        if (l.rfind("violations: ", 0) == 0 || l.rfind("  cycle ", 0) == 0 || l.rfind("  S1: ", 0) == 0) continue;
        const size_t v = l.find("; violations ");
        if (v != std::string::npos) l = l.substr(0, v);
        r += l + "\n";
    }
    return r;
}

TEST(mach_runs_as_refs_Mach) {
    machself();
    out = without_violations(out);
    const std::string want = without_violations(file("tests/mach/machself.out"));
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/machself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}

// Cc::wait_until in W23's form (approved, 2026-11-11; no ref twin: ref checks inside C&C's cycle): the harness checks the
// condition between cycles once C&C is at the step, and C&C moves on in the next cycle.
TEST(mach_wait_until_releases_the_next_cycle) {
    Mach m(Mach::cfg(-1));
    m.boot("wfi");
    int64_t first_true = -1, ran = -1, checks_before = 0;
    m.c.cc().wait_until([&] {
        const bool b = m.all_in_wfi();
        if (!b) checks_before++;
        if (b && first_true < 0) first_true = m.c.cycle();
        return b;
    }).call([&](int64_t t) { ran = t; });
    CHECK(m.c.run_until([&] { return ran >= 0; }, 20000));
    CHECK(first_true > m.run_at);                             // checked only once C&C reached it (after the boot's GO)
    CHECK_EQ(ran, first_true + 1);
}

// W25's F6: a row hook attached, the machine destroyed: the slot frees only the buffer it allocated (ASan in debug).
TEST(mach_row_hook_destroyed_cleanly) {
    mach::MachConfig k = mach::MachConfig::machine();
    k.periph_type = 0; k.test_periph_tile = 0;
    {
        mach::Chip c(k);
        uint64_t rows = 0;
        c.on_row(0, [&](const uint64_t*, int64_t) { rows++; });
        c.run(10);
        CHECK_EQ(rows, 0u);
    }
    CHECK(true);
}

// W22's F2: mem_idle reads an area queue as it wraps, at its configured depth (8), not at the array's (16): a write
// queued in the slot after the wrap holds mem_idle low; a stale write past the depth doesn't.
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

// MachConfig's front end (ref's cfg.<field>=value): ref's names and types, the dropped fields, the errors.
TEST(mach_config_front_end) {
    mach::MachConfig k = mach::MachConfig::machine();
    std::string err;
    CHECK(k.set_args({"--x", "cfg.goLatency=10", "cfg.skewSeed=-3", "cfg.writeCombining=true", "cfg.replayerLogEntries=512"}, err));
    CHECK_EQ(k.go_latency, 10u);
    CHECK_EQ(k.skew_seed, -3);
    CHECK(k.write_combining);
    CHECK_EQ(k.replayer_log_entries, 512u);
    CHECK(k.set("checkDisjoint", "TRUE", err) && k.check_disjoint);   // Boolean.parseBoolean: case-insensitive
    CHECK(k.set("checkDisjoint", "yes", err) && !k.check_disjoint);
    CHECK(!k.set("noSuchField", "1", err) && err == "no Config field noSuchField");
    CHECK(!k.set("idleSkip", "false", err) && err.find("dropped") != std::string::npos);
    CHECK(!k.set("K", "eight", err) && err.find("not a number") != std::string::npos);
    CHECK(!k.set_args({"cfg.K"}, err));
    CHECK(!mach::MachConfig::dropped().empty());
}

