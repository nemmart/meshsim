// test_mach.cpp (WORKER20): the machine harness (mach.h). With the header only: mach::fields against ref's Fields.java
// (the values printed by ref's own Fields.java, 2026-11-10), and the header compiling with the units it names.
#include <cstdarg>
#include <cstdio>
#include <string>

#include "harness.h"
#include "mach.h"
#include "units/ag.h"
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

// ---- tests/ref/MachSelf.java's sequence, line for line (fixture tests/mach/machself.out, tests/ref/machself.sh) ----
namespace {
std::string out;
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
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
void machself() {
    out.clear();
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

// ---- tests/ref/NetSelf.java's sequence (fixture tests/mach/netself.out, tests/ref/netself.sh) ----
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
void netself() {
    out.clear();
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

TEST(mach_runs_as_refs_Mach) {
    machself();
    const std::string want = file("tests/mach/machself.out");
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/machself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}
