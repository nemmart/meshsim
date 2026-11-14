#!/bin/bash
# run_tests.sh: v1.3's runner (sim/ref/meshsim/run_tests.sh) for the suites ported to C++. Same
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
# v1.36: the steps whose program is on v1.35's hardware or firmware and is not ported yet are pending this way
# (PENDING-v1.36.md, section 3; the reasons are R_* below); each keeps its v1.3 `step` line under it as a comment, so
# that un-pending it is one edit (and its program back in SUITES or EQUIVS). The summary counts them.
#   A v1.3 suite whose C++ equivalent is a make-check test (not a port of its log): pass or fail, named in the log
#     equiv LOG "description" "the equivalent" -- $S/test_x
#   (its test in EQUIVS below).
# A v1.36 test with no v1.3 step and no reference log, pass or fail by its exit status (its program in SUITES):
#     check LOG "description" -- $S/test_x
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
# STEP_TIMES=1 (off by default): each step's, each equivalent's and the build's wall time, "LOG N s", in
# results/.step-times (no step's log changes)
[ -z "${STEP_TIMES:-}" ] || : > results/.step-times

# The suite programs the steps run (each worker adds its own; targets of sim/Makefile).
# v1.36: only what runs is built. The pending steps' programs (not built): suite_net suite_engine suite_search suite_ag
# suite_rdport suite_agbench suite_reqbus (P4), suite_ctrl suite_recipes suite_cond suite_hooks suite_regdoc
# suite_accounting suite_s1bench suite_cccores suite_ccbench (P5), stageb, sort_test, sweep; the equivalents test_golden
# and test_tileslotconf.
SUITES="suite_memsys suite_rvc suite_tracer"
EQUIVS=""
SUITES="$SUITES suite_replayer"
SUITES="$SUITES suite_mem"                           # P4: MemTest
SUITES="$SUITES test_rtl_tile"                       # v1.36: acceptance A (the rtl-tile step)

if [ "$BUILD" = 1 ]; then
  echo "== building the suites (MODE=suite CC_NAME=$CC_NAME)"
  t_build=$SECONDS
  make -s -j"${JOBS:-4}" MODE=suite CC_NAME="$CC_NAME" $(for x in $SUITES $EQUIVS; do echo "$S/$x"; done) > results/.build.log 2>&1 \
    || { cat results/.build.log; echo "== the build failed"; exit 1; }
  # BusTest's throws checks are the framework's debug assertions, so its step runs suite_bus's debug build (tiny)
  make -s -j"${JOBS:-4}" MODE=debug CC_NAME="$CC_NAME" "build/$CC_NAME-debug/suite_bus" > results/.build-bus.log 2>&1 \
    || { cat results/.build-bus.log; echo "== the build failed (suite_bus, MODE=debug)"; exit 1; }
  [ -z "${STEP_TIMES:-}" ] || echo "(build) $((SECONDS - t_build)) s" >> results/.step-times
fi

# Timing masked for --check: wall-clock figures (seconds, ms, rates) vary by machine; cycles never do.
# (approved): an `inputs:` line's first field names the simulator (v1.3's meshsim.jar=(classes)): metadata, masked.
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(ms|s|sec|seconds|us|µs|ns)\b/<time>/g; s/[0-9.]+ ?[kKMG]?(cycles|instructions|packets|steps)\/s/<rate>/g; s/^(inputs: )[^ ]+/\1<simulator>/'"${MASK_EXTRA:+; $MASK_EXTRA}"; }
# A step's own approved mask (MASK_EXTRA, a sed expression, set for that step only): a recorded deliberate difference.
# D44 (D33 reached): CcCoresTest's error path prints C&C 1's UART dump end, one cycle later here (C&C 1's peripheral
# applies stores at t + 1); that one figure is masked.
D44_MASK='s/dump done at \+[0-9]+/dump done at +<D44>/'

step() {
  local log=$1 tail=$2 what=$3 ref=$4; shift 4
  [ "$1" = -- ] && shift
  echo "== $what"
  local t0=$SECONDS
  "$@" > "results/$log.log" 2>&1
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
  tail -"$tail" "results/$log.log"
  if [ "$CHECK" = 1 ] && [ "$ref" != - ]; then
    if [ ! -f "$ref" ]; then echo "LOG DIFFERS ($log): no reference log $ref" | tee -a results/.differs
    elif ! diff -q <(mask < "$ref") <(mask < "results/$log.log") > /dev/null; then
      echo "LOG DIFFERS ($log): results/$log.log against $ref" | tee -a results/.differs
    fi
  fi
}
NPENDING=0
pending() { echo "== $2"; echo "PENDING ($3)" | tee "results/$1.log"; NPENDING=$((NPENDING + 1)); }
# check: a test with no reference log; a failing one (its own FAIL lines, or a non-zero exit) fails the run. Lines of its
# own that begin with PENDING (what the test itself leaves pending) are shown, and counted in the summary.
check() {
  local log=$1 what=$2; shift 2
  [ "$1" = -- ] && shift
  echo "== $what"
  local t0=$SECONDS
  "$@" > "results/$log.log" 2>&1 || echo "FAIL $log: exit status $?" >> "results/$log.log"
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
  grep "^PENDING" "results/$log.log"; tail -1 "results/$log.log"
}
equiv() {
  local log=$1 what=$2 eq=$3; shift 3
  [ "$1" = -- ] && shift
  echo "== $what"
  local t0=$SECONDS
  if "$@" > "results/$log.out" 2>&1; then echo "$log: equal to v1.3's by its C++ equivalent, $eq: $(tail -1 "results/$log.out" | sed 's|.*/||')" | tee "results/$log.log"
  else { cat "results/$log.out"; echo "FAIL $log: its C++ equivalent, $eq, failed"; } | tee "results/$log.log" | tail -3; fi
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
}
# one part of fw/build.sh, checked against the committed images (v1.3's build_cc.sh and build_recipes.sh steps).
# Without the RISC-V toolchain (the runner has none), the committed images are checked against the hash lists committed
# beside them instead (fw/check_images.sh); with it, after the build, that check's image names against the build's.
fw_part() {
  if command -v riscv64-unknown-elf-gcc > /dev/null; then PARTS="$*" bash fw/build.sh --check && bash fw/check_images.sh --built fw/build/out "$@"
  else echo "fw/build.sh ($*): no riscv64-unknown-elf-gcc, so the committed images against their hash lists"; bash fw/check_images.sh "$@"; fi
}

# ---- why a step is pending (v1.36; PENDING-v1.36.md, section 3) ----------------------------------------------------
R_FW="v1.35's firmware: a build check of images that v1.36's tile does not run; the RTL's images are vendored (fw/rtl/); the sort end to end, P8, and the firmware headers, P12"
R_NET="suite_net on tests/mach.h: NetTest checks v1.35's unexpected-packet and command conditions and its C&C attach tiles 0-2; the NoC and topology, P6, then the old suites, P11"
R_ENG="its suite drives v1.35's DMA_CONFIG, record base, pulses and read port through harness hooks that went with them; the units' behaviour, P9, then the old suites, P11"
R_CTL="its suite runs v1.35 firmware under C&C's commands (GO, DONE, the command block); the sort end to end, P8, then the old suites, P11"
R_CC="v1.35's C&C 1-3 as cores with cc1.c and cc23.c; the special tiles, P7"
R_REGS="REGISTERS.md and mesh.h are v1.35's register map; the register tables and firmware headers, P12"
R_CORE="test_golden: the M1 goldens of v1.35's core (ISA, traps, map); the core stage, v1.36-core"
R_SLOT="test_tileslotconf: boot-ROM programs on v1.35's map and peripheral window; the peripherals, P7"
R_REQ="suite_reqbus: v1.35's 128-entry request FIFO and its receive protocol through the harness; the request tree, P7"
R_STAGEB="v1.35's replay controller (replay_ctl.c) on v1.35's replayer and map; the replayer, 17.8"
R_SORT="v1.35's sort firmware under C&C's commands; the sort end to end, P8"
# ---- v1.3's steps, in v1.3's order -------------------------------------------------------------------------------
echo "== the layout (v1.3: tools/check_layout.sh; v1.35: tools/check_copies.sh)"; sh tools/check_copies.sh > results/layout.log 2>&1 && tail -1 results/layout.log || { cat results/layout.log; echo "FAIL the layout"; }
pending sw-build "building the tile programs (fw/build.sh: the boot ROM, fw/tests' programs, the M1, M3, replay and Tracer programs)" "$R_FW"
#   step sw-build   2 "building the tile programs (fw/build.sh: the boot ROM, fw/tests' programs, the M1, M3, replay and Tracer programs)" - -- fw_part boot tests m1 m3 replay tracer
pending sort-build "building the sort program (fw/build.sh's sort part: one image, the split, the planner)" "$R_FW"
#   step sort-build 2 "building the sort program (fw/build.sh's sort part: one image, the split, the planner)" - -- fw_part sort
step bus 1 "bus primitives (on the framework's port kinds; the debug build, its throws checks being debug assertions)" tests/p2suites/bus.log -- build/$CC_NAME-debug/suite_bus
check rtl-tile "acceptance A: the RTL's tile programs on one compute tile (tests/test_rtl_tile.cpp; v1.36's, no v1.3 step)" -- $S/test_rtl_tile
NETREF=tests/p4suites/net.log; [ "$MODE" = quick ] && NETREF=tests/p4suites/net-quick.log   # v1.3's net step takes $Q
pending net "network, packets, director" "$R_NET"
#   step net       1 "network, packets, director" $NETREF -- $S/suite_net -1 $Q
pending net-shuf "shuffled child order (seed 5)" "$R_NET"
#   step net-shuf  1 "shuffled child order (seed 5)" tests/p4suites/net-shuf.log -- $S/suite_net 5 quick
pending net-shuf2 "shuffled child order (seed 11)" "$R_NET"
#   step net-shuf2 1 "shuffled child order (seed 11)" tests/p4suites/net-shuf2.log -- $S/suite_net 11 quick
step mem 1 "big SRAM, read and write queues" tests/p4suites/mem.log -- $S/suite_mem -1
step mem-shuf 1 "shuffled child order (seed 3)" tests/p4suites/mem-shuf.log -- $S/suite_mem 3
step mem-shuf2 1 "shuffled child order (seed 8)" tests/p4suites/mem-shuf2.log -- $S/suite_mem 8
pending engine "address generator, write engine, all-to-all (small)" "$R_ENG"
#   step engine 1 "address generator, write engine, all-to-all (small)" tests/p4suites/engine.log -- $S/suite_engine -1
pending eng-shuf "shuffled child order (seed 4)" "$R_ENG"
#   step eng-shuf 1 "shuffled child order (seed 4)" tests/p4suites/eng-shuf.log -- $S/suite_engine 4
pending ctrl "control and the controller (from C)" "$R_CTL"
#   step ctrl      1 "control and the controller (from C)" tests/ctlsuites/ctrl.log -- $S/suite_ctrl -1
pending ctrl-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step ctrl-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/ctrl-shuf.log -- $S/suite_ctrl 4
pending riscv "riscv-tests rv32ui/rv32um on the controller" "$R_CORE"
#   equiv riscv           "riscv-tests rv32ui/rv32um on the controller" "test_golden (every program's retire trace equal to ref's)" -- $S/test_golden
pending rdport "read port, GATHER (with fw/tests/rdport.c and gather.c on the machine)" "$R_ENG"
#   step rdport 1 "read port, GATHER (with fw/tests/rdport.c and gather.c on the machine)" tests/p4suites/rdport.log -- $S/suite_rdport -1
pending accounting "per-phase accounting (from C)" "$R_CTL"
#   step accounting 1 "per-phase accounting (from C)" tests/ctlsuites/accounting.log -- bash -c "$S/suite_accounting -1 && { cmp -s results/phases-a2a.csv tests/ctlsuites/phases-a2a.csv || echo 'FAIL results/phases-a2a.csv differs from v1.3s'; }"
pending searchbench "search steps through the read port (microbenchmark, from C)" "$R_ENG"
#   step searchbench 3 "search steps through the read port (microbenchmark, from C)" tests/p4suites/searchbench.log -- $S/suite_search
pending rdport-shuf "shuffled child order (seed 4)" "$R_ENG"
#   step rdport-shuf 1 "shuffled child order (seed 4)" tests/p4suites/rdport-shuf.log -- $S/suite_rdport 4
pending agtest "address generator read-ahead, figure 12's PTR/HIST ports" "$R_ENG"
#   step agtest 1 "address generator read-ahead, figure 12's PTR/HIST ports" tests/p4suites/agtest.log -- $S/suite_ag -1
pending agtest-shuf "shuffled child order (seed 4)" "$R_ENG"
#   step agtest-shuf 1 "shuffled child order (seed 4)" tests/p4suites/agtest-shuf.log -- $S/suite_ag 4
pending cc-build "building the controllers' firmware (fw/build.sh's cc part: fw/cc/cc1.c, cc23.c)" "$R_FW"
#   step cc-build      2 "building the controllers' firmware (fw/build.sh's cc part: fw/cc/cc1.c, cc23.c)" - -- fw_part cc
pending recipes-build "building the v1.2 recipes (fw/build.sh's recipes part: fw/recipes/, unedited)" "$R_FW"
#   step recipes-build 1 "building the v1.2 recipes (fw/build.sh's recipes part: fw/recipes/, unedited)" - -- fw_part recipes
pending recipes "the v1.2 recipes as suites, and the book's code blocks against their sources" "$R_CTL"
#   step recipes      1 "the v1.2 recipes as suites, and the book's code blocks against their sources" tests/ctlsuites/recipes.log -- $S/suite_recipes -1
pending recipes-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step recipes-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/recipes-shuf.log -- $S/suite_recipes 4
pending cond "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" "$R_CTL"
#   step cond      1 "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" tests/ctlsuites/cond.log -- $S/suite_cond -1
pending cond-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step cond-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/cond-shuf.log -- $S/suite_cond 4
pending regdoc "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" "$R_REGS"
#   step regdoc 1 "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" tests/ctlsuites/regdoc.log -- $S/suite_regdoc
pending hooks "the register map, the send port, the pulses (from Java)" "$R_CTL"
#   step hooks      1 "the register map, the send port, the pulses (from Java)" tests/ctlsuites/hooks.log -- $S/suite_hooks -1
pending hooks-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step hooks-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/hooks-shuf.log -- $S/suite_hooks 4
pending periph "the peripheral framework: the standard window, events, region writes, the DMA" "$R_SLOT"
#   equiv periph          "the peripheral framework: the standard window, events, region writes, the DMA" "test_tileslotconf (TileSlotConf: PeripheralSlotTest's groups on a real tile, against ref)" -- $S/test_tileslotconf
# periph-shuf: test_tileslotconf's shuffled run alone (seed 4, v1.3's), against the same observations of ref; its
# output must be the plain run's (results/periph.out) byte for byte. (Our shuffle seeds don't map to ref's.)
periph_shuf() { TILESLOTCONF_SEED=4 "$S/test_tileslotconf" > results/periph-shuf.raw 2>&1; cat results/periph-shuf.raw; cmp -s results/periph-shuf.raw results/periph.out; }
pending periph-shuf "shuffled child order (seed 4)" "$R_SLOT"
#   equiv periph-shuf     "shuffled child order (seed 4)" "test_tileslotconf shuffled (seed 4), its output the plain run's" -- periph_shuf
pending reqbus "the request port, the request bus and the receive queues in the tree" "$R_REQ"
#   step reqbus 2 "the request port, the request bus and the receive queues in the tree" tests/p4suites/reqbus.log -- $S/suite_reqbus -1
pending reqbus-shuf "shuffled child order (seed 4)" "$R_REQ"
#   step reqbus-shuf 1 "shuffled child order (seed 4)" tests/p4suites/reqbus-shuf.log -- $S/suite_reqbus 4
pending cccores "C&C 1-3 as cores (Config.ccCores): the boot ROM's controller branch, the link, the request tree" "$R_CC"
#   MASK_EXTRA=$D44_MASK step cccores      1 "C&C 1-3 as cores (Config.ccCores): the boot ROM's controller branch, the link, the request tree" tests/ctlsuites/cccores.log -- $S/suite_cccores -1
pending cccores-shuf "shuffled child order (seed 4)" "$R_CC"
#   MASK_EXTRA=$D44_MASK step cccores-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/cccores-shuf.log -- $S/suite_cccores 4
pending replay-build "the replay controller (tests/stageb/replay_build.sh)" "$R_STAGEB"
#   step replay-build 1 "the replay controller (tests/stageb/replay_build.sh)" tests/stageb/replay-build.log -- tests/stageb/replay_build.sh
pending stageb "Stage B: replay_ctl.c on a tile against Stage A, synthetic windows" "$R_STAGEB"
#   step stageb 1 "Stage B: replay_ctl.c on a tile against Stage A, synthetic windows" tests/stageb/stageb.log -- $S/stageb synthetic
if [ -n "${DELIVERY:-}" ]; then   # Stage B on a tracer delivery's guest windows: DELIVERY=<replay-inputs dir> (v1.3's; no such delivery on the runner: unrun, as in v1.3)
  for g in lrd_x0 scd_x0 nop_hint jalr_self jalr_zero; do pending stageb-$g "Stage B on the tile: $g" "$R_STAGEB"; done
  #   for g in lrd_x0 scd_x0 nop_hint jalr_self jalr_zero; do step stageb-$g 1 "Stage B on the tile: $g" - -- $S/stageb "$DELIVERY" state $g; done
fi
step replayer 1 "the replayer model (v1.25): the opcode table, the ALU against vec.tsv at 821e1bd, the unit tests" tests/replayer/replayer.log -- $S/suite_replayer
step memsys 1 "the Tracer's memory system (v1.3, B1): FileStore's trees and statuses, the shared cache's timing" tests/tracersuites/memsys.log -- $S/suite_memsys
step rvc    1 "the Tracer's RVC decompressor against gold's, every 16-bit encoding (v1.3, B1)" tests/tracersuites/rvc.log -- $S/suite_rvc
step tracer 1 "the Tracer (v1.3, B1): execution, memory, the map, errors, cuts, controller-fed expansions, the output path" tests/tracersuites/tracer.log -- $S/suite_tracer
pending sort-p1 "the sort's local sorts (L1, L2)" "$R_SORT"
#   step sort-p1 1 "the sort's local sorts (L1, L2)" tests/sort/ref/sort-p1.log -- $S/sort_test -1 p1
pending sort "the sort, every built-in set at 64 x <= 2,000 (several minutes)" "$R_SORT"
#   step sort      1 "the sort, every built-in set at 64 x <= 2,000 (several minutes)" tests/sort/ref/sort.log -- $S/sort_test -1 sort
pending sort-shuf "shuffled child order (seed 4)" "$R_SORT"
#   step sort-shuf 1 "shuffled child order (seed 4)" tests/sort/ref/sort-shuf.log -- $S/sort_test 4 sort
if [ "$MODE" != quick ]; then
  # the key set prebuilt and checked by hash, made with pipeline/keys only where a JDK is present (tests/sort/keys.sh)
  sort_bt() { local d; d=$(tests/sort/keys.sh 16384 trace10-10) || return 1; $S/sort_test -1 run label=bt16k-$1 keys=$d/trace10-10.keys fmt=$1 ready=8 force=bt; }
  pending sort-bt-kv32 "BT, forced, trace hot 10 / 10 % at 64 x 16,384, 32-bit values (a few minutes)" "$R_SORT"
  #   step sort-bt-kv32 2 "BT, forced, trace hot 10 / 10 % at 64 x 16,384, 32-bit values (a few minutes)" tests/sort/ref/sort-bt-kv32.log -- sort_bt kv32
  pending sort-bt-k128 "the same, 128-bit values" "$R_SORT"
  #   step sort-bt-k128 2 "the same, 128-bit values" tests/sort/ref/sort-bt-k128.log -- sort_bt k128
  pending rdport-full "gather 1 at full size" "$R_ENG"
  #   step rdport-full 1 "gather 1 at full size" tests/p4suites/rdport-full.log -- $S/suite_rdport -1 full
  pending accounting-full "per-phase accounting, full-size all-to-all from C" "$R_CTL"
  #   step accounting-full 1 "per-phase accounting, full-size all-to-all from C" tests/ctlsuites/accounting-full.log -- bash -c "$S/suite_accounting -1 full && { cmp -s results/phases-a2a-full.csv tests/ctlsuites/phases-a2a-full.csv || echo 'FAIL results/phases-a2a-full.csv differs from v1.3s'; }"
  pending agbench "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" "$R_ENG"
  #   step agbench 3 "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" tests/p4suites/agbench.log -- $S/suite_agbench
  pending s1bench "item S1: K's margin over the write path, L = 1-4, and the table-window case (a few minutes)" "$R_CTL"
  #   step s1bench 8 "item S1: K's margin over the write path, L = 1-4, and the table-window case (a few minutes)" tests/ctlsuites/s1bench.log -- $S/suite_s1bench
  pending ccbench "C&C 1-3 as cores: the boot and a phase against the Java model" "$R_CC"
  #   step ccbench 16 "C&C 1-3 as cores: the boot and a phase against the Java model" tests/ctlsuites/ccbench.log -- $S/suite_ccbench
  # the sweep's CSV checked too (with --check): a difference prints a FAIL line into the step's log
  sweep_quick() { $S/sweep quick || return 1; [ "$CHECK" = 0 ] || cmp -s results/sweep-quick.csv tests/sort/ref/sweep-quick.csv || echo "FAIL: results/sweep-quick.csv differs from tests/sort/ref/sweep-quick.csv"; }
  pending sweep-quick "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" "$R_SORT"
  #   step sweep-quick 4 "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" tests/sort/ref/sweep-quick.log -- sweep_quick
  pending ctrl-full "the all-to-all from C on the four data sets" "$R_CTL"
  #   step ctrl-full 1 "the all-to-all from C on the four data sets" tests/ctlsuites/ctrl-full.log -- $S/suite_ctrl -1 full
  pending engine-full "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" "$R_ENG"
  #   step engine-full 1 "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" tests/p4suites/engine-full.log -- $S/suite_engine -1 full
  pending skewtable "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" "$R_NET"
  #   step skewtable 1 "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" tests/p4suites/skewtable.log -- $S/suite_net -1 skewtable 40
fi
# ---- v1.3's summary ------------------------------------------------------------------------------------------------
grep -h "^PENDING" results/*.log | sort | uniq -c
OWN=$(grep -ch "^PENDING" results/rtl-tile.log 2> /dev/null); OWN=${OWN:-0}
echo "== $NPENDING steps PENDING (PENDING-v1.36.md, section 3)$([ "$OWN" = 0 ] || echo "; $OWN PENDING lines of rtl-tile's own (section 1)")"
FAILED=0
grep -h "FAIL" results/*.log | grep -v "(CL4: must fail)" && FAILED=1   # D46: Stage B's must-fail window (v1.3's runner fails on it too)
[ -s results/.differs ] && { cat results/.differs; FAILED=1; }
[ "$FAILED" = 1 ] && { echo "== FAILURES"; exit 1; } || echo "== all passed (the $NPENDING PENDING steps aside); logs in results/"
