// suite_cond.cpp (WORKER23, P5, inventory C13): v1.3's CondTest (sim/ref/meshsim/test/CondTest.java) on W20's harness
// and whole-chip reset, check for check. run_tests.sh's steps `cond` (-1) and `cond-shuf` (4); its output equals ref's
// log (tests/ctlsuites/cond*.log). The checker's messages are part of it: the debug and suite builds.
//
// The errors end to end (spec 4.13, 6.1; v1.2), provoked from C (fw/tests/cond.c) on a booted machine: each case sets
// its ERRORS bit on the tiles that act; the hard interrupt (or, for a bus error or an illegal instruction, the trap) takes
// the tile to the boot ROM's handler, which sends its two reports (24 and 25) to C&C 1 over the request bus and parks;
// the tile's error line reaches C&C 1 (ERROR_LINES, the unit-error bit) and the toast pin rises. Then the outside world
// resets the chip and C&C 1 boots the tiles again (6.1, 6.2): every ERRORS, line and the toast pin clear. Errors 4, 5 and
// 6, which a program can't provoke in the model, are injected into ERRORS from the test.
//
// C&C 1 here is the Java model (Cnc): ref's cc1p on that machine is read from it: ERROR_LINES from its error lines
// (`err_line`), the unit-error bit (ERRORS [0]) and the toast pin from its sticky error bit (`error_bit`; ref's toast
// latches on the same condition, the unit's errors non-zero, and the reset clears both).
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../suite.h"
#include "ctl.h"

using namespace ctl;
using mach::Mach;

namespace {
int64_t shuffle = -1;
units::Cnc& cnc(Mach& m) { return m.c.unit<units::Cnc>("cnc"); }
bool error_line(Mach& m, unsigned k) { return (cnc(m).err_line >> k) & 1; }
bool unit_error(Mach& m) { return cnc(m).error_bit; }
bool toast(Mach& m) { return cnc(m).error_bit; }

// C&C 1's model drains target 0's receive queue (its handler would, 6.1) until tile p has parked and the ROM's two
// reports are in (v1.25: sequence 24 and 25, spec 4.13)
std::vector<std::array<uint32_t, 4>> drain_until_parked(Mach& m, unsigned p) {
    std::vector<std::array<uint32_t, 4>> got;
    mach::ReqBus& rb = m.c.reqbus();
    m.run([&] {
        const int64_t now = m.c.cycle() + 1;
        while (rb.pending(0, now)) { std::array<uint32_t, 4> r{}; rb.peek_request(0, now, r.data()); rb.receive(0, now); if ((r[0] & 0x7F) == p) got.push_back(r); }
        return core(m.c, p).parked && got.size() >= 2;
    }, 200000);
    m.c.run(10);
    return got;
}
void check(Mach& m, unsigned p, int k, int64_t cause, uint32_t bits, const std::vector<std::array<uint32_t, 4>>& reports) {
    const std::string tl = "error " + std::to_string(k) + ", tile " + std::to_string(p) + ": ";
    const char* t = tl.c_str();
    const uint32_t cond = m.c.tile(p).block().conditions;
    suite::eq(bits, cond & bits, "%sits ERRORS bit", t);
    suite::check(core(m.c, p).parked && core(m.c, p).in_handler, "%sthe handler ran and parked", t);
    suite::eq(cause, core(m.c, p).trap_cause, "%smcause", t);
    suite::eq(2, int64_t(reports.size()), "%sthe boot ROM's two reports reached C&C 1", t);
    bool in_order = true; for (size_t i = 0; i < reports.size(); i++) if (((reports[i][0] >> 21) & 0x7FF) != 24 + i) in_order = false;
    suite::check(in_order, "%sreports 24 and 25, in order (spec 4.13)", t);
    if (reports.size() == 2) {
        suite::eq(int32_t(cond), int32_t(reports[0][1]), "%sreport 24 carries ERRORS", t);
        suite::eq(cause, int32_t(reports[0][2]), "%sand mcause", t);
    }
    suite::eq(1, error_line(m, p), "%sits error line at C&C 1's ERROR_LINES", t);
    suite::eq(1, unit_error(m), "%sC&C 1's unit-error bit", t);
    suite::check(toast(m), "%sthe toast pin", t);
}
// The outside world's soft reset, then C&C 1 boots the tiles again (the image is still in memory): 6.1, 6.2.
void reset_and_boot(Mach& m, const Mach::Image& im) {
    const uint64_t r0 = m.c.resets();
    m.c.cc().reset(false).command(BROADCAST, CMD_JUMP << 24, {im.sym("_start")}).run_when_ready();
    const bool ok = m.run([&] { return m.c.resets() > r0 && !m.c.cc().busy(); }, 20000);
    m.c.run(50);
    uint32_t e = 0; for (unsigned q = 0; q < 64; q++) e |= m.c.tile(q).block().conditions;
    suite::check(ok && e == 0 && !toast(m) && !unit_error(m), "the reset cleared every ERRORS, line and the toast pin, and the tiles booted again");
    bool waiting = true; for (unsigned q = 0; q < 64; q++) if (core(m.c, q).in_handler || core(m.c, q).halt != RUN) waiting = false;
    suite::check(waiting, "every tile back in its command loop");
}
void run_case(Mach& m, const Mach::Image& im, int k, std::vector<unsigned> acting, unsigned erring, int64_t cause, uint32_t bits) {
    suite::group("error " + std::to_string(k));
    uint32_t lo = 0, hi = 0; for (unsigned a : acting) { if (a < 32) lo |= 1u << a; else hi |= 1u << (a - 32); }
    m.c.cc().command(BROADCAST, uint32_t(k) << 24, {lo, hi}).go();
    const auto reports = drain_until_parked(m, erring);
    check(m, erring, k, cause, bits, reports);
    reset_and_boot(m, im);
}
void injected(Mach& m, const Mach::Image& im, int bit) {
    suite::group("error " + std::to_string(bit) + " (injected)");
    const unsigned p = 30;
    m.c.tile(p).set_errors(1u << bit);                       // as if the unit's FIFO had overflowed
    const auto reports = drain_until_parked(m, p);
    check(m, p, bit, CAUSE_EXTERNAL, 1u << bit, reports);
    reset_and_boot(m, im);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    mach::MachConfig k = Mach::cfg(shuffle); k.max_violation_messages = 1000; k.reqbus_host = true;
    Mach m(k);
    const Mach::Image im = m.boot("cond");
    suite::check(m.run([&] { return m.c.cc().last_run_at() >= 0 && !m.c.cc().busy(); }, 20000), "booted");
    // {case, tiles that act..., the tile whose ERRORS rises, its mcause}
    const int64_t ext = CAUSE_EXTERNAL;
    const int64_t cases[][4] = {{1, 5, 5, ext}, {2, 6, 6, ext}, {3, 7, 7, ext}, {7, 12, 12, ext}, {8, 13, 13, ext},
                                {9, 20, 20, CAUSE_STORE_FAULT}, {10, 21, 21, CAUSE_ILLEGAL}, {14, 22, 22, ext}};
    for (const auto& c : cases) run_case(m, im, int(c[0]), {unsigned(c[1])}, unsigned(c[2]), c[3], 1u << c[0]);
    run_case(m, im, 0, {8, 10, 1, 17, 9}, 9, ext, 1u << 0);
    for (int bit : {4, 5, 6}) injected(m, im, bit);
    // meshsim's own flags for the deliberate bugs: each names its invariant (5, 9, 3, 8, 4, 1), nothing else
    const char* want[] = {"invariant 5", "invariant 9", "invariant 3", "invariant 8", "invariant 4", "invariant 1", "invariant 15"};
    const auto& msgs = m.c.violation_messages();
    bool named = true;
    for (const std::string& v : msgs) { bool any = false; for (const char* w : want) if (v.find(w) != std::string::npos) any = true; if (!any) named = false; }
    suite::check(named && m.c.violations() == msgs.size(), "every violation flags a deliberate bug: %s", m.c.violation_report().c_str());
    for (const char* w : want) { bool any = false; for (const std::string& v : msgs) if (v.find(w) != std::string::npos) any = true; suite::check(any, "meshsim flagged %s", w); }
    std::printf("errors [meas: CondTest]: errors 0, 1, 2, 3, 7, 8, 9, 10 and 14 provoked from C, 4, 5 and 6 injected: each sets its ERRORS bit and takes the tile to the boot ROM's handler (the hard interrupt, or the trap for 9 and 10), which sends its 24 reports to C&C 1 in order and parks; the error line reaches ERROR_LINES, the unit-error bit and the toast pin rise; a soft reset clears them all and the tiles boot again\n");
    return suite::done(shuffle >= 0 ? "CondTest (shuffled, seed " + std::to_string(shuffle) + ")" : "CondTest");
}
