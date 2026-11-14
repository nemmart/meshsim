// segment_real.h: a real workload's segment for the chip (tests/segment_bench.h), from its recorded files, as the RTL's
// chip_replay_test.rs builds one (Snarky One's rtl/chip/test/src): windows of meshsim's recorded runs of EVM Zone
// (`evm-zone-23`) and Cyclone (`cyclone-22`), traced by the tracer tile's Tracer from the guest's own memory, map and
// advice, delivered to the compute tiles and replayed there.
//
// THE FILES are outside the repository, under the inputs' directory ($MESHSIM_INPUTS), read only:
//   w26-jumpmap/<run>/   the Tracer's host-side inputs: proof.json (log2 K, the lowest address, the map's constants),
//                        map.lines (the jump-target map), initial.pages (the guest's memory at row 0), seg/NNN.pages (the
//                        pages each host segment writes), segments.seg (each host segment's start, advice and update list);
//   gold-v1.3/<run>/     gold's segment file, which tests/tracerblock/gold.h reads;
//   v1261/<run>/         each window's files: rows.bin (the reference rows), state.bin and end.bin (its start and end
//                        records), state-pick.bin with the streams as the Tracer's pick codes them.
//
// A SEGMENT here starts at a host segment's start: window 64 k of the run, on tile 0. The RTL's test starts a segment
// at any window, by running its model of the Tracer's execution from row 0 to it; the simulator's tests have no such
// model (and take none from the simulator), so the start is one the files give: host segment k's start state, the
// guest's memory at row 0 with the update lists of the segments before k, the pages any host segment writes writable
// and the others not_owned (17.9.5), and the run's advice from segment k's on. These are the RTL test's inputs for the
// same windows: its three segments of 64 windows (EVM Zone's 0-63 and 64-127, Cyclone's 0-63) are host segments.
//
// WHAT EACH WINDOW MUST BE, from the files alone (the RTL's test checks its model against the same files first):
// its rows, rows.bin's; its record as it lands, words 0 and 2-65 state.bin's, word 66 the files word as the test
// writes it, words 1 and 67 its run's totals: the stream word's counts and flags the recorded pick's (state-pick.bin),
// its advice field the advice left after the window's advice rows (counted from rows.bin), and the bitmap the recorded
// one's inline templates; its streams, the recorded ones as the pick codes them; the Tracer's last record, the last
// window's end.bin with its run's totals. (tests/tracerblock/gold.h's Account is the reader: the RTL's
// tracer_acceptance_test's, ported.)
#pragma once

#include <string>

#include "segment_bench.h"

namespace segment {

// The inputs' directory, $MESHSIM_INPUTS; empty if it is not set or holds no recorded run (the runner gives it its
// default, ~/meshsim-inputs; a test binary run by hand or by `make check` runs the real segments only when asked).
std::string inputs_directory();

// Windows `first` to `first + windows - 1` of `run`, `first` a multiple of 64 and at most 64 windows. False and `err`
// if a file is missing or not as its format says, or the files disagree with each other.
bool load_real(const std::string& inputs, const std::string& run, unsigned first, unsigned windows, Segment& s, std::string& err);

}  // namespace segment
