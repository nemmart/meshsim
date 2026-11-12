// test_mach.cpp (WORKER20): the machine harness (mach.h). With the header only: mach::fields against ref's Fields.java
// (the values printed by ref's own Fields.java, 2026-11-10), and the header compiling with the units it names.
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <string>

#include "harness.h"
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
void a2a(int64_t seed) {
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
    m.c.cc().command(units::lw::BROADCAST, 1u << 24).go().command(units::lw::BROADCAST, 2u << 24).go().command(units::lw::BROADCAST, 3u << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 2000000);
    report("a2a seed " + std::to_string(seed), m, ok);
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
void machself() {
    out.clear();
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
void netself() {
    out.clear();
    core();
    controllers(-1); controllers(3);
    packets(-1); packets(5);
    reads();
    a2a_net(-1, 0); a2a_net(4, 0); a2a_net(-1, 8);
}
}  // namespace

TEST(mach_net_as_refs_Net) {
    netself();
    const std::string want = file("tests/mach/netself.out");
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/netself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}

// Without the checker's facts (the release builds) the violation counts and reports are left out of both sides.
std::string without_violations(const std::string& x) {
    if (sim::check::kOn) return x;
    std::string r;
    for (size_t at = 0; at < x.size();) {
        size_t e = x.find('\n', at);
        if (e == std::string::npos) e = x.size();
        std::string l = x.substr(at, e - at);
        at = e + 1;
        if (l.rfind("violations: ", 0) == 0 || l.rfind("  cycle ", 0) == 0) continue;
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

// ---- oracle/ref/CheckSelf.java's sequence (fixture tests/mach/checkself.out, oracle/ref/checkself.sh): the checker ----
namespace {
void checkself() {
    out.clear();
    {
        Net n(Net::cfg(-1));
        n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
        n.at(4, 11, 9, lw::M_WRITE, 0, Net::cm(0x900), lw::SIZE_UNDEF, 0x22, 0);
        n.at(5, 12, 9, lw::M_COUNT, 0, 0, lw::SIZE_32, 0x33, 0);
        n.at(6, 13, 9, lw::M_GATHER, 0, 0, lw::SIZE_64, 0x44, 0);
        n.at(7, 14, 9, lw::M_COUNT, 0, 0, lw::SIZE_UNDEF, 0x55, 0);
        n.at(8, 14, 9, lw::M_AUG, 1, 0, lw::SIZE_128, 0x66, 0x77);
        n.at(3, 40, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_64, 0x88, 0);
        const bool ok = n.run_all(1000);
        say("packets: %s at cycle %lld\n", ok ? "drained" : "NOT drained", (long long)n.c.cycle());
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_errors = true;
        Net n(k);
        n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
        n.at(4, 12, 12, lw::M_WRITE, 0, 0xFE000000u, lw::SIZE_32, 0x22, 0);
        n.at(5, 30, 9, lw::M_COUNT, 1, 0, lw::SIZE_32, 0x33, 0);
        n.at(6, 31, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x44, 0);
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.queue_depth = 2; k.we_in_depth = 1; k.measure_s1 = false;
        Net n(k);
        for (unsigned s = 20; s < 40; s++) for (unsigned i = 0; i < 4; i++)
            n.at(s, 10 + i, 9, lw::M_WRITE, 1, uint32_t(lw::R_SRAM) << 24 | (s * 16 + 2 * i) << 2, lw::SIZE_64, s * 100 + i, 0);
        n.run_all(2000);
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_unexpected = true;
        Net n(k);
        const uint32_t SR = uint32_t(lw::R_SRAM) << 24;
        n.at(3, 10, 9, lw::M_WRITE, 1, SR | 0x100, lw::SIZE_64, 1, 0);
        n.c.tile(12).set_remaining(1, 1);
        n.at(4, 10, 12, lw::M_WRITE, 1, SR | 0x200, lw::SIZE_64, 2, 0);
        n.at(4, 11, 12, lw::M_WRITE, 1, SR | 0x208, lw::SIZE_64, 3, 0);
        n.at(5, 12, 20, lw::M_SRC, 0, 0, lw::SIZE_64, 4, 0);
        n.at(5, 13, 21, lw::M_WRITE, 0, SR | 0x300, lw::SIZE_64, 5, 0);
        n.at(5, 14, 22, lw::M_WRITE, 0, Net::cm(0x900), lw::SIZE_32, 6, 0);
        n.at(6, 15, 23, lw::M_WRITE, 1, 8, lw::SIZE_64, 7, 0);
        n.at(6, 16, 24, lw::M_WRITE, 1, 0x7BC0 + 8 * 7, lw::SIZE_32, 8, 0);
        n.at(7, 17, 25, lw::M_WRITE, 0, 0x7FC0, lw::SIZE_32, 9, 0);
        n.at(7, 18, 26, lw::M_WRITE, 1, 0x7FF0 + 0x8000, lw::SIZE_128, 10, 11);
        n.at(8, 19, 27, lw::M_WRITE, 1, SR | (0x100000u << 2), lw::SIZE_64, 12, 0);
        n.cnc_send({30, lw::BROADCAST, lw::M_WRITE, 0x200, 13, lw::SIZE_32, 0, 0});
        n.cnc_send({31, 30, lw::M_WRITE, 0x7FC0, 14, lw::SIZE_32, 0, 0});
        n.run_all(2000);
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_errors = true;
        Net n(k);
        n.c.run(20);
        n.c.tile(5).set_errors(1u << 9);
        n.c.run(10);
        say("after tile 5's bits: %llu at cycle %lld\n", (unsigned long long)n.c.violations(), (long long)n.c.cycle());
        n.c.set_check_errors(false);
        n.c.tile(6).set_errors(1u << 10);
        n.c.run(10);
        n.c.set_check_errors(true);
        n.c.tile(7).set_errors(1u << 1);
        n.c.run(1);
        say("%s", n.c.violation_report().c_str());
        n.c.clear_violations(); n.c.set_max_violation_messages(1);
        n.c.tile(8).set_errors(1u << 2);
        n.c.tile(9).set_errors(1u << 3);
        n.c.step();
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.max_violation_messages = 3;
        Net n(k);
        for (unsigned i = 0; i < 6; i++) n.at(10 + i, 10 + i, 20, lw::M_COUNT, 0, 0, lw::SIZE_32, i, 0);
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
}
}  // namespace

TEST(mach_checker_as_refs) {
    if (!sim::check::kOn) { std::printf("  (the checker's facts are off in this build: MODE=debug or MODE=suite)\n"); return; }
    checkself();
    const std::string want = file("tests/mach/checkself.out");
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/checkself.out)\n%s", out.c_str(), want.c_str());
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
