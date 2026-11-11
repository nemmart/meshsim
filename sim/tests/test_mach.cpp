// test_mach.cpp (WORKER20): the machine harness (mach.h). With the header only: mach::fields against ref's Fields.java
// (the values printed by ref's own Fields.java, 2026-11-10), and the header compiling with the units it names.
#include <cstdarg>
#include <cstdio>
#include <string>

#include "harness.h"
#include "mach.h"
#include "units/ag.h"
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

TEST(mach_runs_as_refs_Mach) {
    machself();
    const std::string want = file("tests/mach/machself.out");
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/machself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}
