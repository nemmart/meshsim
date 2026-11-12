#!/bin/bash
# run_tests.sh (WORKER20, P2, row C0): v1.3's runner (sim/ref/meshsim/run_tests.sh) for the suites ported to C++. Same
# step names, same log names (results/<step>.log), same summary lines; a step not yet ported prints and logs
# "PENDING (<who or what>)" as v1.3's own pending steps did.
#
#   ./run_tests.sh [all|quick] [--check] [--no-build]
#     all | quick   v1.3's modes: quick leaves out the long steps (v1.3's `if [ "$MODE" != quick ]` block) and passes
#                   `quick` to the steps that take it
#     --check       also compare each log with its reference log (v1.3's, or the oracle's regenerated one: the step's
#                   REF), timing masked (mask() below); a difference fails the run as a FAIL line would
#     --no-build    use the binaries as they are
#   Environment: CC_NAME (default clang); the suites build with MODE=suite (release flags, the checker's facts on:
#   src/check.h), into build/$CC_NAME-suite/.
#
# ADDING A STEP (wave 2 and on: each worker adds its own lines, in v1.3's order, and its Makefile include):
#     step LOG TAIL "description" REF -- command args...
#   LOG the log's name (v1.3's), TAIL the lines shown (v1.3's TAIL), REF the reference log ("-" for none), then the
#   command: normally $S/suite_<name> with v1.3's arguments ($S is the suites' build directory; $Q is `quick` or empty).
#   The step's suite program must be in SUITES below (built before the steps run). A step still waiting:
#     pending LOG "description" "who or what it waits for"
set -u
cd "$(dirname "$0")"
MODE=all CHECK=0 BUILD=1
for a in "$@"; do
  case "$a" in
    all|quick) MODE=$a;;
    --check) CHECK=1;;
    --no-build) BUILD=0;;
    *) echo "usage: ./run_tests.sh [all|quick] [--check] [--no-build]"; exit 2;;
  esac
done
CC_NAME=${CC_NAME:-clang}
S=build/$CC_NAME-suite
Q=; [ "$MODE" = quick ] && Q=quick
mkdir -p results
: > results/.differs

# The suite programs the steps run (each worker adds its own; targets of sim/Makefile).
SUITES="suite_memsys suite_rvc suite_tracer"

if [ "$BUILD" = 1 ]; then
  echo "== building the suites (MODE=suite CC_NAME=$CC_NAME)"
  make -s -j"${JOBS:-4}" MODE=suite CC_NAME="$CC_NAME" $(for x in $SUITES; do echo "$S/$x"; done) > results/.build.log 2>&1 \
    || { cat results/.build.log; echo "== the build failed"; exit 1; }
fi

# Timing masked for --check: wall-clock figures (seconds, ms, rates) vary by machine; cycles never do.
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(ms|s|sec|seconds|us|µs|ns)\b/<time>/g; s/[0-9.]+ ?[kKMG]?(cycles|instructions|packets|steps)\/s/<rate>/g'; }

step() {
  local log=$1 tail=$2 what=$3 ref=$4; shift 4
  [ "$1" = -- ] && shift
  echo "== $what"
  "$@" > "results/$log.log" 2>&1
  tail -"$tail" "results/$log.log"
  if [ "$CHECK" = 1 ] && [ "$ref" != - ]; then
    if [ ! -f "$ref" ]; then echo "LOG DIFFERS ($log): no reference log $ref" | tee -a results/.differs
    elif ! diff -q <(mask < "$ref") <(mask < "results/$log.log") > /dev/null; then
      echo "LOG DIFFERS ($log): results/$log.log against $ref" | tee -a results/.differs
    fi
  fi
}
pending() { echo "== $2"; echo "PENDING ($3)" | tee "results/$1.log"; }

V13=ref/meshsim/results                    # v1.3's committed logs (some stale: the oracle regenerates those)

# ---- v1.3's steps, in v1.3's order -------------------------------------------------------------------------------
echo "== the layout (v1.3: tools/check_layout.sh; v1.35: tools/check_copies.sh)"; sh tools/check_copies.sh > results/layout.log 2>&1 && tail -1 results/layout.log || { cat results/layout.log; echo "FAIL the layout"; }
pending sw-build      "building the tile programs (sw/build.sh; regenerates sw/mesh.h)" "v1.35: fw/build.sh (W19)"
pending sort-build    "building the sort program (sw/ext/sort/build.sh: one image, the split, the planner)" "v1.35: fw/build.sh (W19)"
pending bus           "bus primitives" "W20, C2"
pending net           "network, packets, director" "P4"
pending net-shuf      "shuffled child order (seed 5)" "P4"
pending net-shuf2     "shuffled child order (seed 11)" "P4"
pending mem           "big SRAM, read and write queues" "P4"
pending mem-shuf      "shuffled child order (seed 3)" "P4"
pending mem-shuf2     "shuffled child order (seed 8)" "P4"
pending engine        "address generator, write engine, all-to-all (small)" "P4"
pending eng-shuf      "shuffled child order (seed 4)" "P4"
pending ctrl          "control and the controller (from C)" "P5"
pending ctrl-shuf     "shuffled child order (seed 4)" "P5"
pending riscv         "riscv-tests rv32ui/rv32um on the controller" "P5"
pending rdport        "read port, GATHER (Java; the C parts PENDING (phase 2))" "P4"
pending accounting    "per-phase accounting (from C)" "P6"
pending searchbench   "search steps through the read port (microbenchmark, from C)" "P6"
pending rdport-shuf   "shuffled child order (seed 4)" "P4"
pending agtest        "address generator read-ahead, figure 12's PTR/HIST ports" "P4"
pending agtest-shuf   "shuffled child order (seed 4)" "P4"
pending cc-build      "building the controllers' firmware (sw/build_cc.sh: sw/cc/cc1.c, cc23.c; WORKER14)" "v1.35: fw/build.sh (W19)"
pending recipes-build "building the v1.2 recipes (sw/build_recipes.sh: wip/sw/recipes/, unedited)" "v1.35: fw/build.sh (W19)"
pending recipes       "the v1.2 recipes as suites, and the book's code blocks against their sources" "P5"
pending recipes-shuf  "shuffled child order (seed 4)" "P5"
pending cond          "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" "P5"
pending cond-shuf     "shuffled child order (seed 4)" "P5"
pending regdoc        "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" "P5"
pending hooks         "the register map, the send port, the pulses (from Java)" "P5"
pending hooks-shuf    "shuffled child order (seed 4)" "P5"
pending periph        "the peripheral framework: the standard window, events, region writes, the DMA" "P5"
pending periph-shuf   "shuffled child order (seed 4)" "P5"
pending reqbus        "the request port, the request bus and the receive queues in the tree" "P5"
pending reqbus-shuf   "shuffled child order (seed 4)" "P5"
pending cccores       "C&C 1-3 as cores (Config.ccCores, WORKER14): the boot ROM's controller branch, the link, the request tree" "P5"
pending cccores-shuf  "shuffled child order (seed 4)" "P5"
pending replay-build  "the replay controller (sw/replay/build.sh)" "v1.35: fw/build.sh (W19)"
pending stageb        "Stage B: replay_ctl.c on a tile against Stage A, synthetic windows" "P7b"
pending replayer      "the replayer model (v1.25): the opcode table, the ALU against vec.tsv at 821e1bd, the unit tests" "P7b"
step memsys 1 "the Tracer's memory system (v1.3, B1): FileStore's trees and statuses, the shared cache's timing" tests/tracersuites/memsys.log -- $S/suite_memsys
step rvc    1 "the Tracer's RVC decompressor against gold's, every 16-bit encoding (v1.3, B1)" tests/tracersuites/rvc.log -- $S/suite_rvc
step tracer 1 "the Tracer (v1.3, B1): execution, memory, the map, errors, cuts, controller-fed expansions, the output path" tests/tracersuites/tracer.log -- $S/suite_tracer
pending sort-p1       "the sort's local sorts (L1, L2)" "P6"
pending sort          "the sort, every built-in set at 64 x <= 2,000 (several minutes)" "P6"
pending sort-shuf     "shuffled child order (seed 4)" "P6"
if [ "$MODE" != quick ]; then
  pending sort-bt-kv32    "BT, forced, trace hot 10 / 10 % at 64 x 16,384, 32-bit values (a few minutes)" "P6"
  pending sort-bt-k128    "the same, 128-bit values" "P6"
  pending rdport-full     "gather 1 at full size (Java; the C parts PENDING (phase 2))" "P4"
  pending accounting-full "per-phase accounting, full-size all-to-all from C" "P6"
  pending agbench         "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" "P6"
  pending s1bench         "item S1: K's margin over the write path, L = 1-4, and the table-window case (a few minutes)" "P6"
  pending ccbench         "C&C 1-3 as cores (WORKER14): the boot and a phase against the Java model" "P6"
  pending sweep-quick     "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" "P6"
  pending ctrl-full       "the all-to-all from C on the four data sets" "P5"
  pending engine-full     "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" "P4"
  pending skewtable       "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" "P4"
fi
# ---- v1.3's summary ------------------------------------------------------------------------------------------------
grep -h "PENDING" results/*.log | sort | uniq -c
FAILED=0
grep -h "FAIL" results/*.log && FAILED=1
[ -s results/.differs ] && { cat results/.differs; FAILED=1; }
[ "$FAILED" = 1 ] && { echo "== FAILURES"; exit 1; } || echo "== all passed (PENDING suites aside); logs in results/"
