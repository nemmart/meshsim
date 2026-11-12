// stageb.h (WORKER25, P7b; inventory E3): v1.3's StageB on the C++ machine (W20's tests/mach.h), line for line. The
// replay controller (fw/replay/replay_ctl.c, prebuilt) on tile 0 feeding its replayer through the peripheral slot,
// against a delivery's window (or Stage A's synthetic windows): the window loaded into tile 0's big SRAM as
// replay_layout.h lays it out, C&C 1's model loading replay_ctl and starting it (only tile 0 expected at GO), the run
// watched for starvation (CL4), then the rows, the streams' consumption, the end state, the replayer's lists, the
// machine's violations and rp_results, as ref reports them.
//   run_window   StageB.runWindow (a delivered window; its line printed; the result for replay_shard's stages B and F)
//   synthetic    StageB.synthetic (run_tests.sh's step stageb)
#pragma once

#include <cstdint>
#include <string>

#include "stage_a.h"
#include "mach.h"

namespace stageb {

struct Options {
    bool lone = false;               // ref's StageB.LONE (-Dstageb.lone; ReplayShard's stage B): tiles 1-63 parked
    int64_t norow = 50000;           // ref's NO_ROW_LIMIT (-Dstageb.norow; run_shard.sh -g)
    unsigned logq = 256;             // ref's -Dreplayer.logq (run_shard.sh -q): the replayer's log queues
    bool profile = false;            // ref's -Dstageb.profile: the controller's pc during new-expansion stalls
    int64_t shuffle = -1;            // the debug shuffle's seed (>= 0; MachConfig::shuffle_seed): the output must not change
    bool step_all = false, instance_major = false;   // the framework's other debug options (MachConfig): the same
};

// The replay controller's image (ref's StageB.Image: DIR/replay_ctl.{code.bin,data.bin,sym}).
mach::Mach::Image image(const std::string& dir);

// One delivered window on the tile (StageB.runWindow): prints its line (and detail lines), returns the result; the
// summary is ref's `out.summary` (replay_shard's CSV detail for an OK window).
stagea::Result run_window(const mach::Mach::Image& im, const units::tracer::ReplayTables& tb, const std::string& pd, const stagea::Entry& we,
                          const std::string& variant, const std::string& prog, const Options& o);

// StageB.synthetic: Stage A's synthetic windows on the tile; prints every line; returns the exit code (0 if none failed).
// image_dir holds replay_ctl, oldmap_dir the old-queue-map controller (ref's sw/replay/build and build/oldmap).
int synthetic(const std::string& image_dir, const std::string& oldmap_dir, const Options& o);

// Java's String.format("%.Nf", v): the shortest decimal that reads back as v, rounded HALF_UP (printf rounds the binary value).
std::string java_fixed(double v, int digits);

}  // namespace stageb
