// suite_accounting.cpp (WORKER23, P5, inventory C10): v1.3's AccountingTest (sim/ref/meshsim/test/AccountingTest.java)
// on W20's harness and PhaseLog (tests/phase_log.h), check for check. run_tests.sh's steps `accounting` (-1) and
// `accounting-full` (-1 full); its output equals ref's log (tests/ctlsuites/accounting*.log) and the CSV it writes,
// results/phases-a2a[-full].csv, ref's (tests/ctlsuites/phases-a2a[-full].csv), byte for byte. The write-engine input's
// and the GATHER queue's peaks exist in the debug and suite builds only (phase_log.h): run it there. Run from sim/.
// Usage: suite_accounting [shuffleSeed] [full]
//
// Per-phase accounting (PhaseLog): the all-to-all from C (configure, start, exit); the phase table against C&C's own
// record and the run's known counts, then the tables.
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "base/fatal.h"
#include "../phase_log.h"
#include "../suite.h"
#include "ctl.h"

using mach::Mach;
using mach::PhaseLog;

namespace {
int idx(const char* const* names, int n, const char* name) { for (int i = 0; i < n; i++) if (std::strcmp(names[i], name) == 0) return i; FATAL("accounting: no column %s", name); }
}  // namespace

int main(int argc, char** argv) {
    const int64_t shuffle = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : -1;
    const bool full = argc > 2 && std::string(argv[2]) == "full";
    const int n = full ? 65536 : 2000;
    suite::group("all-to-all phases");
    const mach::A2A a(7, n);
    Mach m(Mach::cfg(shuffle));
    PhaseLog& phases = m.c.phases();
    phases.namer = [](int32_t w) -> std::string {
        switch (uint32_t(w) >> 24) { case 1: return "configure"; case 2: return "start (a2a)"; case 3: return "exit"; case ctl::CMD_JUMP: return "boot"; default: return "?"; }
    };
    for (unsigned s = 0; s < 64; s++) {
        mach::TileRef p = m.c.tile(s);
        for (size_t k = 0; k < a.rec[s].size(); k++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(k), uint64_t(a.rec[s][k]));
        for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(a.cnt[s][d]));
        for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(a.cnt[src][s]));
        for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(a.R[g]));
        p.sram_poke(0xF0000 + 144, 0);
    }
    m.boot("a2a");
    m.c.cc().command(ctl::BROADCAST, 1 << 24).go().command(ctl::BROADCAST, 2 << 24).go().command(ctl::BROADCAST, 3 << 24).go();
    suite::check(m.run([&] { return m.all_stopped(); }, 3000000) && m.all_exited(), "the run finished: %s", m.halt_summary().c_str());
    phases.close();
    suite::eq(0, a.verify(m.c), "every record landed");
    const auto& ph = phases.phases;
    const auto cl = m.c.cc().phase_log();
    suite::eq(4, int64_t(ph.size()), "four phases: the boot (the ROM's GO, 6.2), configure, start, exit");
    for (size_t i = 0; i + 1 < ph.size(); i++) suite::eq(cl.at(i + 1)[0] - cl.at(i)[1], ph[i].cycles(), "phase %zu: GO -> all READY as C&C saw it", i);
    int64_t sent = 0; for (int s = 0; s < 64; s++) for (int d = 0; d < 64; d++) sent += a.cnt[s][d];
    const int AGREQ = idx(PhaseLog::COUNTS, PhaseLog::NC, "agReq"), EJ = idx(PhaseLog::COUNTS, PhaseLog::NC, "ejected"),
              INJ = idx(PhaseLog::COUNTS, PhaseLog::NC, "injected"), LATE = idx(PhaseLog::COUNTS, PhaseLog::NC, "agLate"),
              LINK = idx(PhaseLog::PEAKS, PhaseLog::NP, "link");
    suite::eq(sent, ph.at(2).n[AGREQ], "start phase: address-generator requests = records sent");
    suite::eq(sent + 64, ph.at(2).n[EJ], "start phase: ejected packets = records + the exit command (one broadcast packet per tile, sent right after GO)");
    suite::eq(64, ph.at(1).n[EJ], "configure phase: the start command, one packet per tile");
    suite::eq(sent, ph.at(2).n[INJ], "start phase: injected packets = records");
    suite::eq(0, ph.at(2).n[LATE], "start phase: never late");
    suite::check(ph.at(2).peak[LINK] > ph.at(1).peak[LINK], "peaks restart per phase (start %lld > configure %lld)", (long long)ph.at(2).peak[LINK],
                 (long long)ph.at(1).peak[LINK]);
    std::printf("phase accounting, 16-group all-to-all from C (Data(7, %d)) [meas: AccountingTest]:\n%s\n", n, phases.report().c_str());
    mkdir("results", 0777);
    std::string err;
    if (!phases.csv(std::string("results/phases-a2a") + (full ? "-full" : "") + ".csv", err)) FATAL("accounting: %s", err.c_str());
    return suite::done(std::string("AccountingTest") + (full ? " full" : ""));
}
