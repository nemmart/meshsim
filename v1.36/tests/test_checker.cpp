// test_checker.cpp: the checker (src/check.h, tests/checker.cpp) provoked on the coreless machine: packets that break
// a rule are sent from tiles' injection points, and the checker's report must name each violation once, at the tile
// that made it and at the tile that received it.
//
// The expectations are ref's (v1.3's Java simulator's CheckSelf, its first two cases), for the rules of the packet that
// v1.36's hardware keeps (machine-spec 4.2, 12.2 and chapter 20: invariants 5, 9, 10, 15 and 21): a packet for region
// 0xFF or 0xFE, or an AUGMENT at 128 bits, is a bad packet at its sender and at its receiver, which drops it and sets
// ERRORS.bad_packet; a size of 11 is undefined; a COUNT on counter 0 does nothing; a GATHER goes to the tile itself; a
// counted packet whose decrement finds its counter at 0 sets ERRORS.counter_underflow. The lines below are ref's,
// without their cycles: the cycles are the units' (the inject unit, the router, the director), which their own tests
// check. ref's section numbers in the texts ("3.1") are v1.35's; the invariants' numbers are v1.36's too. The names
// of the ERRORS bits are machine-spec 7.4's (bit 1 `bad_packet`, bit 2 `counter_underflow`, which ref called an
// unexpected arrival).
//
// What of ref's CheckSelf is not here: its other cases provoke v1.35's hardware (the write engine's input FIFO, the
// command and reply blocks, C&C's commands, the core's terminal traps).
//
// The checker records only in the builds with SIM_CHECKS (debug, suite); in the release build the report is empty, and
// the test requires that.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "harness.h"
#include "mach.h"
#include "units/linkword.h"

namespace {
namespace lw = units::lw;
using mach::Net;

// The report's messages without their cycles ("cycle T tile I: MSG" -> "tile I: MSG"), sorted.
std::vector<std::string> said(Net& n) {
    n.c.violation_report();   // (scans the overflows and closes the last cycle's facts)
    std::vector<std::string> v;
    for (const std::string& m : n.c.violation_messages()) {
        const size_t p = m.find(" tile ");
        v.push_back(p == std::string::npos ? m : m.substr(p + 1));
    }
    std::sort(v.begin(), v.end());
    return v;
}
bool same(std::vector<std::string> want, const std::vector<std::string>& got) {
    std::sort(want.begin(), want.end());
    if (want == got) return true;
    for (const std::string& s : want) if (!std::count(got.begin(), got.end(), s)) std::fprintf(stderr, "  missing: %s\n", s.c_str());
    for (const std::string& s : got) if (!std::count(want.begin(), want.end(), s)) std::fprintf(stderr, "  not expected: %s\n", s.c_str());
    return false;
}
}  // namespace

// ref's first case: seven packets, each breaking one rule, from tiles 3 to 8 to tile 9.
TEST(each_bad_packet_is_reported_at_its_sender_and_at_its_receiver) {
    Net n(Net::cfg(-1));
    n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
    n.at(4, 11, 9, lw::M_WRITE, 0, 0x400, lw::SIZE_UNDEF, 0x22, 0);
    n.at(5, 12, 9, lw::M_COUNT, 0, 0, lw::SIZE_32, 0x33, 0);
    n.at(6, 13, 9, lw::M_GATHER, 0, 0, lw::SIZE_64, 0x44, 0);
    n.at(7, 14, 9, lw::M_COUNT, 0, 0, lw::SIZE_UNDEF, 0x55, 0);
    n.at(8, 14, 9, lw::M_AUG, 1, 0, lw::SIZE_128, 0x66, 0x77);
    n.at(3, 40, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_64, 0x88, 0);
    CHECK(n.run_all(1000));
    const std::vector<std::string> got = said(n);
    if (!sim::check::kOn) { CHECK(got.empty()); CHECK_EQ(n.c.violations(), 0u); return; }
    CHECK_EQ(n.c.violations(), 13u);
    CHECK(same({
        "tile 3: bad packet sent (region 0xff not reachable by a packet; 3.1, invariant 5): src=0 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000011",
        "tile 4: packet with size 11, undefined, sent (3.1, invariant 5): src=0 dst=9 mode=0(write@addr) ctr=0 addr=0x00000400 size=11(undefined) payload=0x0000000000000000_0000000000000022",
        "tile 5: count-only packet on counter 0 sent: it does nothing (3.1, invariant 21): src=0 dst=9 mode=5(count-only) ctr=0 addr=0x00000000 size=32 payload=0x00000033",
        "tile 6: mode-4 packet to another unit (invariant 10): src=0 dst=9 mode=4(gather) ctr=0 addr=0x00000000 size=64 payload=0x0000000000000044",
        "tile 7: count-only packet on counter 0 sent: it does nothing (3.1, invariant 21): src=0 dst=9 mode=5(count-only) ctr=0 addr=0x00000000 size=11(undefined) payload=0x0000000000000000_0000000000000055",
        "tile 8: bad packet sent (mode 3 at 128 bits; 3.1, invariant 5): src=0 dst=9 mode=3(augmented) ctr=1 addr=0x00000000 size=128 payload=0x0000000000000077_0000000000000066",
        "tile 9: bad packet dropped, uncounted, ERRORS [1] (mode 3 at 128 bits; 3.1, invariant 5): src=8 dst=9 mode=3(augmented) ctr=1 addr=0x00000000 size=128 payload=0x0000000000000077_0000000000000066",
        "tile 9: bad packet dropped, uncounted, ERRORS [1] (region 0xff not reachable by a packet; 3.1, invariant 5): src=3 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000011",
        "tile 9: packet with size 11 (undefined, 3.1; invariant 5) arrived and was dropped: src=7 dst=9 mode=5(count-only) ctr=0 addr=0x00000000 size=11(undefined) payload=0x0000000000000000_0000000000000055",
        "tile 9: packet with size 11 (undefined, 3.1; invariant 5) arrived and was dropped: src=4 dst=9 mode=0(write@addr) ctr=0 addr=0x00000400 size=11(undefined) payload=0x0000000000000000_0000000000000022",
        "tile 9: count-only packet on counter 0: it does nothing (3.1, invariant 21): src=5 dst=9 mode=5(count-only) ctr=0 addr=0x00000000 size=32 payload=0x00000033",
        "tile 3: bad packet sent (region 0xff not reachable by a packet; 3.1, invariant 5): src=0 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=64 payload=0x0000000000000088",
        "tile 9: bad packet dropped, uncounted, ERRORS [1] (region 0xff not reachable by a packet; 3.1, invariant 5): src=3 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=64 payload=0x0000000000000088",
    }, got));
}

// ref's second case, with invariant 15 on: a tile's ERRORS bit is reported once, when it is first set (a second bad
// packet at tile 9 adds no ERRORS line), and a COUNT on counter 1 at a tile that armed nothing underflows the counter
// at its decrement, LAND_LATENCY after its acceptance (12.2, 12.3).
TEST(errors_bits_are_reported_once_when_they_are_set) {
    mach::MachConfig k = Net::cfg(-1);
    k.check_errors = true;
    Net n(k);
    n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
    n.at(4, 12, 12, lw::M_WRITE, 0, 0xFE000000u, lw::SIZE_32, 0x22, 0);
    n.at(5, 30, 9, lw::M_COUNT, 1, 0, lw::SIZE_32, 0x33, 0);
    n.at(6, 31, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x44, 0);
    CHECK(n.run_all(1000));
    n.c.run(40);   // (past the last decrement and its error line)
    const std::vector<std::string> got = said(n);
    if (!sim::check::kOn) { CHECK(got.empty()); CHECK_EQ(n.c.violations(), 0u); return; }
    CHECK_EQ(n.c.violations(), 9u);
    CHECK(same({
        "tile 3: bad packet sent (region 0xff not reachable by a packet; 3.1, invariant 5): src=0 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000011",
        "tile 4: bad packet sent (region 0xfe not reachable by a packet; 3.1, invariant 5): src=0 dst=12 mode=0(write@addr) ctr=0 addr=0xfe000000 size=32 payload=0x00000022",
        "tile 12: ERRORS [10] set: bad packet (invariant 15)",
        "tile 12: bad packet dropped, uncounted, ERRORS [1] (region 0xfe not reachable by a packet; 3.1, invariant 5): src=4 dst=12 mode=0(write@addr) ctr=0 addr=0xfe000000 size=32 payload=0x00000022",
        "tile 9: ERRORS [10] set: bad packet (invariant 15)",
        "tile 9: bad packet dropped, uncounted, ERRORS [1] (region 0xff not reachable by a packet; 3.1, invariant 5): src=3 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000011",
        "tile 6: bad packet sent (region 0xff not reachable by a packet; 3.1, invariant 5): src=0 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000044",
        "tile 9: bad packet dropped, uncounted, ERRORS [1] (region 0xff not reachable by a packet; 3.1, invariant 5): src=6 dst=9 mode=0(write@addr) ctr=0 addr=0xff000000 size=32 payload=0x00000044",
        "tile 9: ERRORS [100] set: counter underflow (invariant 15)",
    }, got));
}
