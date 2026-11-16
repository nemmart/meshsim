// test_segment: the segment on v1.36's chip, end to end, run by meshsim's firmware, v1.35's upgraded to v1.36: the
// RTL's chip test of the segment (Snarky One's rtl/chip/test/src/chip_trace_test.rs) on the simulator.
//
// A guest is traced by the tracer tile's Tracer, window by window; each window's record and streams are delivered over
// the NoC to its compute tile; every window is replayed there by the replayer. The master tile runs C&C 1's program
// (fw/cc/cc1.c), which loads the tracer tile's program (fw/tracer/tracer_ctl.c) and the replay controller
// (fw/replay/replay_ctl.c) on the tiles through their boot ROMs. tests/segment_bench.h is the run and its checks (run_chip's), and
// the test's model of the memory system on the Tracer's fill port, which is Open in the hardware (17.9.14).
//
// THE SEGMENTS are the RTL test's three, each from the fixture the RTL's test wrote (tests/segment/rtl/, its README):
// its generated guest and its models' expectations (and the RTL chip's own figures for the run, read and no longer
// compared). Nothing a segment is checked against comes from the simulator:
//   segment_small    chip_trace_test: 4 windows of 3,000 rows on tiles 0-3, the small guest;
//   segment_full     chip_trace_test_full: a window of 1,500 rows on each of the 64 compute tiles, a guest built to
//                    miss in the Tracer's caches and its jump LRU and to write lines back;
//   segment_window   chip_trace_test_window: one window of a real window's 65,536 rows on tile 0;
//   segment_small_split   the first again, the chip split over 2 and over 8 threads.
// THE CYCLES are printed: see segment_bench.h.
#include <cstdio>
#include <string>

#include "harness.h"
#include "segment_bench.h"

namespace {

void run_fixture(const char* name, unsigned parts = 1) {
    segment::Segment s;
    segment::Rtl rtl;
    std::string err;
    if (!segment::read_fixture(std::string("tests/segment/rtl/") + name + ".seg.gz", s, rtl, err)) {
        ::test::fail(__FILE__, __LINE__, "%s", err.c_str());
        return;
    }
    segment::Options o;
    o.parts = parts;
    const segment::Run run = segment::run_chip(s, &rtl, o);
    CHECK_EQ(run.failures, 0);
}

}  // namespace

TEST(segment_small) { run_fixture("chip_trace_test"); }
TEST(segment_full) { run_fixture("chip_trace_test_full"); }
TEST(segment_window) { run_fixture("chip_trace_test_window"); }

// ---- real workloads' windows (chip_replay_test.rs): a segment of 64 windows on the 64 compute tiles, read from the
// inputs' directory, outside the repository ($MESHSIM_INPUTS: tests/segment_real.h). Without it they
// print SKIPPED and pass. Every row, record and stream against the recorded files (run_chip's checks); the replay's
// cycles, from the windows' landing's release to each window's last row out of its tile's port, summed over the tiles,
// and those to each window's first row, are printed.
#include "segment_real.h"

namespace {

struct Real { const char* run; unsigned first; };
const Real kReal[] = {{"evm-zone-23", 0}, {"evm-zone-23", 64}, {"cyclone-22", 0}};

void run_real(const Real& rtl) {
    const std::string inputs = segment::inputs_directory();
    if (inputs.empty()) {
        std::printf("test_segment: %s windows %u-%u: SKIPPED: MESHSIM_INPUTS does not name a directory with the recorded runs (tests/segment_real.h)\n",
                    rtl.run, rtl.first, rtl.first + 63);
        return;
    }
    segment::Segment s;
    std::string err;
    if (!segment::load_real(inputs, rtl.run, rtl.first, 64, s, err)) { ::test::fail(__FILE__, __LINE__, "%s: %s", rtl.run, err.c_str()); return; }
    segment::Options o;
    o.quiet = true;
    const segment::Run run = segment::run_chip(s, nullptr, o);
    CHECK_EQ(run.failures, 0);
    const segment::ReplayCycles replay = segment::replay_cycles(run, 64);
    std::printf("%s: the replay, from the release to each window's last row, %llu cycles over %llu rows, %.4f a row; to the windows' "
                "first rows %llu\n", s.name.c_str(), (unsigned long long)replay.cycles, (unsigned long long)replay.rows,
                double(replay.cycles) / double(replay.rows), (unsigned long long)replay.to_first);
    CHECK_EQ(replay.rows, uint64_t(64) * 65536);
}

}  // namespace

TEST(real_evm_zone_0) { run_real(kReal[0]); }
TEST(real_evm_zone_64) { run_real(kReal[1]); }
TEST(real_cyclone_0) { run_real(kReal[2]); }

// The chip split over threads (src/units/chip.h, THE PARTITION), as acceptance B's split run: the small segment on 2 and
// on 8 threads, with every check of the one-domain run. (The fill port's model is
// called by the tracer tile's domain alone and draws by the cycle, whatever the order the domains run in.)
TEST(segment_small_split) {
    for (unsigned parts : {2u, 8u}) run_fixture("chip_trace_test", parts);
}
