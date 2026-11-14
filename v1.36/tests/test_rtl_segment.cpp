// test_rtl_segment: ACCEPTANCE C. The segment on v1.36's chip, end to end, run by the RTL's own firmware, unmodified:
// the RTL's chip test of the segment (Snarky One's rtl/chip/test/src/chip_trace_test.rs) on the simulator.
//
// A guest is traced by the tracer tile's Tracer, window by window; each window's record and streams are delivered over
// the NoC to its compute tile; every window is replayed there by the replayer. The master tile runs the trace run's
// firmware (fw/rtl/sort/trace/master.elf), which loads the tracer tile's image and the replay image (fw/rtl/sort/
// segment/) on the tiles through their boot ROMs. tests/segment_bench.h is the run and its checks (run_chip's), and
// the test's model of the memory system on the Tracer's fill port, which is Open in the hardware (17.9.14).
//
// THE SEGMENTS are the RTL test's three, each from the fixture the RTL's test wrote (tests/segment/rtl/, its README):
// its generated guest, its models' expectations, and the RTL chip's own figures for the run. Nothing a segment is
// checked against comes from the simulator:
//   segment_small    chip_trace_test: 4 windows of 3,000 rows on tiles 0-3, the small guest;
//   segment_full     chip_trace_test_full: a window of 1,500 rows on each of the 64 compute tiles, a guest built to
//                    miss in the Tracer's caches and its jump LRU and to write lines back;
//   segment_window   chip_trace_test_window: one window of a real window's 65,536 rows on tile 0.
// THE CYCLES are checked equal to the RTL chip's in each: see segment_bench.h.
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
// inputs' directory, outside the repository ($MESHSIM_INPUTS, ~/meshsim-inputs: tests/segment_real.h). Without it they
// print SKIPPED and pass. THE FIGURES are the RTL chip's for the same windows (Snarky One's rtl/chip/README.md, Real
// workloads' windows), written here: the replay's cycles, from the windows' landing's release to each window's last
// row out of its tile's port, summed over the tiles; those to each window's first row; the passes' end from the
// release; and the Tracer's own cycles, its first START to its last done, under the test's memory system.
#include "segment_real.h"

namespace {

struct RtlReal { const char* run; unsigned first; uint64_t replay, to_first; uint32_t passes_end, tracer_own; };
const RtlReal kRtlReal[] = {
    {"evm-zone-23", 0, 13045233, 13235, 230435, 23108239},
    {"evm-zone-23", 64, 12854434, 8938, 210878, 22055425},
    {"cyclone-22", 0, 13106834, 12151, 228441, 22586242},
};

void run_real(const RtlReal& rtl) {
    const std::string inputs = segment::inputs_directory();
    if (inputs.empty()) {
        std::printf("test_rtl_segment: %s windows %u-%u: SKIPPED: no recorded runs in $MESHSIM_INPUTS or ~/meshsim-inputs (tests/segment_real.h)\n",
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
    // the tracer tile's report (tracer.rs, `report`): word 16 + 3 n, run n's START; word 18 + 3 n, its done
    const uint32_t trace = run.report.size() >= 16 + 3 * 64 ? run.report[16 + 3 * 63 + 2] - run.report[16] : 0;
    std::printf("%s: the replay, from the release to each window's last row, %llu cycles over %llu rows, %.4f a row (the RTL chip's: %llu); "
                "to the windows' first rows %llu (%llu); the passes' end %u cycles from the release (%u); the Tracer's own %u (%u)\n",
                s.name.c_str(), (unsigned long long)replay.cycles, (unsigned long long)replay.rows, double(replay.cycles) / double(replay.rows),
                (unsigned long long)rtl.replay, (unsigned long long)replay.to_first, (unsigned long long)rtl.to_first, run.replay, rtl.passes_end,
                trace, rtl.tracer_own);
    CHECK_EQ(replay.rows, uint64_t(64) * 65536);
    CHECK_EQ(replay.cycles, rtl.replay);
    CHECK_EQ(replay.to_first, rtl.to_first);
    CHECK_EQ(run.replay, rtl.passes_end);
    CHECK_EQ(trace, rtl.tracer_own);
}

}  // namespace

TEST(real_evm_zone_0) { run_real(kRtlReal[0]); }
TEST(real_evm_zone_64) { run_real(kRtlReal[1]); }
TEST(real_cyclone_0) { run_real(kRtlReal[2]); }
