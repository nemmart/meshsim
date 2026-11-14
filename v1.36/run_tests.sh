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
#   Environment: CC_NAME (default clang); MESHSIM_INPUTS (default ~/meshsim-inputs: the real workloads' data, for the
#   steps tracer-block and, in `all`, rtl-segment-real, each pending without it); RTL_SORT_64K_KEYS (the RTL's 64K key file, for the step rtl-sort-64k of
#   `all`, pending without it); the suites build with MODE=suite (release flags, the checker's facts on:
#   src/check.h), into build/$CC_NAME-suite/.
#
# ADDING A STEP (wave 2 and on: each worker adds its own lines, in v1.3's order, and its Makefile include):
#     step LOG TAIL "description" REF -- command args...
#   LOG the log's name (v1.3's), TAIL the lines shown (v1.3's TAIL), REF the reference log ("-" for none), then the
#   command: normally $S/suite_<name> with v1.3's arguments ($S is the suites' build directory; $Q is `quick` or empty).
#   The step's suite program must be in SUITES below (built before the steps run). A step still waiting:
#     pending LOG "description" "who or what it waits for"
# v1.36: the steps whose program is on v1.35's hardware or firmware and is not ported yet are pending this way
# (PENDING-v1.36.md, the platform stage, section 4; the reasons are R_* below); each keeps its v1.3 `step` line under it as a comment, so
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
# suite_accounting suite_s1bench suite_cccores suite_ccbench (P5), sort_test, sweep; the equivalents test_golden
# and test_tileslotconf.
SUITES="suite_memsys suite_rvc suite_tracer tracer_block"
EQUIVS="test_core_programs test_core_tile"           # v1.36: the RTL's core (make-check tests, run as steps)
SUITES="$SUITES suite_replayer"
SUITES="$SUITES test_mem"                            # v1.36: the scratchpad and the inject unit (the mem steps)
# v1.36: the make-check tests that stand for v1.3's unit suites on v1.36's hardware (the steps marked REPLACED)
SUITES="$SUITES test_router test_special_router test_ag test_receive test_gather test_request_tree test_reqbus"
SUITES="$SUITES test_regs test_periph_slot test_ingress_egress test_tile_control"
SUITES="$SUITES test_rtl_tile test_rtl_sort"         # v1.36: acceptances A and B (the rtl-tile and rtl-sort steps)
SUITES="$SUITES test_rtl_segment"                    # v1.36: acceptance C (the rtl-segment steps)

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
NREPLACED=0
# A v1.3 step whose suite cannot run on v1.36's hardware (v1.35's map, commands or firmware), REPLACED: `replaced` runs
# the tests written for v1.36's hardware that stand for it (their expectations are the specification's and the RTL's, in
# the tests; there is no reference log); `same` names the step, already run above, that stands for it.
replaced() {
  local log=$1 what=$2 by=$3; shift 3
  [ "$1" = -- ] && shift
  echo "== $what"; echo "REPLACED ($by)"
  local t0=$SECONDS
  { echo "REPLACED ($by)"; "$@" 2>&1 || echo "FAIL $log: exit status $?"; } > "results/$log.log"
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
  tail -1 "results/$log.log"; NREPLACED=$((NREPLACED + 1))
}
same() { echo "== $2"; echo "REPLACED ($3)" | tee "results/$1.log"; NREPLACED=$((NREPLACED + 1)); }
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

# ---- why a step is pending (v1.36; PENDING-v1.36.md, the platform stage, section 4) ----------------------------------------------------
R_FW="meshsim's own firmware is not ported: v1.35's map and commands, and no riscv64-unknown-elf-gcc on the runner; the images the chip runs are the RTL's (the step rtl-images); a firmware stage (fw/README.md)"
R_NET="v1.36's chip has no start skew to tabulate (the release is one flip-flop, in every tile in the same cycle: 18); deletion with the old suites"
R_ENG="its suite runs v1.35's C programs (fw/tests) or measures v1.35's settings; the units are tested without firmware (the steps engine, mem); a firmware stage"
R_CTL="its suite runs v1.35 firmware under C&C's commands (GO, DONE, the command block), which v1.36 does not have (7.3); a firmware stage, or deletion"
R_SORT="v1.35's sort firmware under C&C's commands; the sort end to end on v1.36's chip is the step rtl-sort (the RTL's four runs); sort_test's other key sets and its measurements wait (PENDING-v1.36.md)"
# ---- v1.3's steps, in v1.3's order -------------------------------------------------------------------------------
echo "== the layout (v1.3: tools/check_layout.sh; v1.35: tools/check_copies.sh)"; sh tools/check_copies.sh > results/layout.log 2>&1 && tail -1 results/layout.log || { cat results/layout.log; echo "FAIL the layout"; }
pending sw-build "building the tile programs (fw/build.sh: the boot ROM, fw/tests' programs, the M1, M3, replay and Tracer programs)" "$R_FW"
#   step sw-build   2 "building the tile programs (fw/build.sh: the boot ROM, fw/tests' programs, the M1, M3, replay and Tracer programs)" - -- fw_part boot tests m1 m3 replay tracer
pending sort-build "building the sort program (fw/build.sh's sort part: one image, the split, the planner)" "$R_FW"
#   step sort-build 2 "building the sort program (fw/build.sh's sort part: one image, the split, the planner)" - -- fw_part sort
step bus 1 "bus primitives (on the framework's port kinds; the debug build, its throws checks being debug assertions)" tests/p2suites/bus.log -- build/$CC_NAME-debug/suite_bus
check rtl-images "the RTL's vendored firmware against its hash list (fw/check_images.sh rtl; v1.36's, no v1.3 step)" -- bash fw/check_images.sh rtl
check rtl-tile "acceptance A: the RTL's tile programs on one compute tile (tests/test_rtl_tile.cpp; v1.36's, no v1.3 step)" -- $S/test_rtl_tile
# Acceptance B: the RTL's sort images end to end on the chip, the master, memory and tracer tiles booted as the RTL's
# chip test boots them (tests/test_rtl_sort.cpp). The 16-record runs and the run split over threads here; the 64K runs
# in `all`, with the RTL's 64K key file (16 MiB, not vendored: fw/rtl/README.md), which RTL_SORT_64K_KEYS names.
rtl_sort() { $S/test_rtl_sort sort_oracle && $S/test_rtl_sort sort_16; }
check rtl-sort "acceptance B: the RTL's sort images on the chip, 64 x 16 records: kv32, k128, and kv32 split over threads (tests/test_rtl_sort.cpp)" -- rtl_sort
if [ "$MODE" != quick ]; then
  if [ -f "${RTL_SORT_64K_KEYS:-}" ]; then
    check rtl-sort-64k "acceptance B at 64 x 65,536 records: kv32 and k128 (about a minute)" -- $S/test_rtl_sort sort_64k
  else
    pending rtl-sort-64k "acceptance B at 64 x 65,536 records: kv32 and k128" "skipped: RTL_SORT_64K_KEYS does not name the RTL's 64K key file (fw/rtl/README.md)"
  fi
fi
# Acceptance C: the segment on the chip, run by the RTL's own firmware (the master's trace run, the tracer tile's image,
# the replay image), as the RTL's chip_trace_test and chip_replay_test run it (tests/test_rtl_segment.cpp). The RTL test's
# three generated segments here, each against the RTL's models' expectations and, cycle for cycle, the RTL chip's own
# figures (tests/segment/rtl/); the real workloads' windows in `all`, with their data, outside the repository.
check rtl-segment "acceptance C: the segment on the chip under the RTL's firmware: 4 windows, 64 windows and a window of 65,536 rows, traced, delivered and replayed, in the RTL chip's cycles (tests/test_rtl_segment.cpp)" -- $S/test_rtl_segment segment_
if [ "$MODE" != quick ]; then
  SEG_INPUTS=${MESHSIM_INPUTS:-$HOME/meshsim-inputs}
  if [ -f "$SEG_INPUTS/w26-jumpmap/evm-zone-23/proof.json" ] && [ -f "$SEG_INPUTS/v1261/cyclone-22/window_00000.rows.bin" ]; then
    check rtl-segment-real "acceptance C on the real workloads' windows: EVM Zone's 0-63 and 64-127 and Cyclone's 0-63, a segment of 64 windows each, every row and delivery against the recorded files, the cycles against the RTL chip's (about a minute)" -- env MESHSIM_INPUTS="$SEG_INPUTS" $S/test_rtl_segment real_
  else
    pending rtl-segment-real "acceptance C on the real workloads' windows (EVM Zone, Cyclone), against the recorded files and the RTL chip's figures" "skipped: the workloads' data is not at $SEG_INPUTS (MESHSIM_INPUTS)"
  fi
fi
NETREF=tests/p4suites/net.log; [ "$MODE" = quick ] && NETREF=tests/p4suites/net-quick.log   # v1.3's net step takes $Q
net_tests() { $S/test_router && $S/test_special_router; }
replaced net "network, packets, director" "v1.3's NetTest drives v1.35's command conditions and attach tiles 0-2; the router against ref's observations and v1.36's two rules (test_router), the special tile's router, the attach ports and the DMA input from 16.2-16.4 (test_special_router); the packets end to end are test_mach's and rtl-sort's" -- net_tests
#   step net       1 "network, packets, director" $NETREF -- $S/suite_net -1 $Q
same net-shuf "shuffled child order (seed 5)" "in the step net: test_router runs its scripts again with a shuffled and an instance-major order"
#   step net-shuf  1 "shuffled child order (seed 5)" tests/p4suites/net-shuf.log -- $S/suite_net 5 quick
same net-shuf2 "shuffled child order (seed 11)" "in the step net"
#   step net-shuf2 1 "shuffled child order (seed 11)" tests/p4suites/net-shuf2.log -- $S/suite_net 11 quick
check mem "the scratchpad and the inject unit (machine-spec 14, 15; tests/test_mem.cpp; v1.3's MemTest is retired)" -- $S/test_mem
mem_shuf() { MEM_SHUFFLE=$1 $S/test_mem; }
check mem-shuf "the same, the units of a level stepped in a shuffled order (seed 3)" -- mem_shuf 3
check mem-shuf2 "the same (seed 8)" -- mem_shuf 8
engine_tests() { $S/test_ag && $S/test_receive && $S/test_gather; }
replaced engine "address generator, write engine, all-to-all (small)" "v1.3's EngineTest measured v1.35's generator and write engine; the DMA unit from 8.2-8.5 (test_ag), the write engine with PTR and HIST from 12.4, 12.5 (test_receive), the gather unit (test_gather); the all-to-all is test_mach's a2a case and rtl-sort" -- engine_tests
#   step engine 1 "address generator, write engine, all-to-all (small)" tests/p4suites/engine.log -- $S/suite_engine -1
engine_shuf() { SIM_SHUFFLE=4 $S/test_ag && SIM_SHUFFLE=4 $S/test_receive; }
replaced eng-shuf "shuffled child order (seed 4)" "the step engine's tests, the units of a level stepped in a shuffled order" -- engine_shuf
#   step eng-shuf 1 "shuffled child order (seed 4)" tests/p4suites/eng-shuf.log -- $S/suite_engine 4
pending ctrl "control and the controller (from C)" "$R_CTL"
#   step ctrl      1 "control and the controller (from C)" tests/ctlsuites/ctrl.log -- $S/suite_ctrl -1
pending ctrl-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step ctrl-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/ctrl-shuf.log -- $S/suite_ctrl 4
same riscv "riscv-tests rv32ui/rv32um on the controller" "the step core-programs: the RTL's own riscv-tests on the RTL's core, in the RTL's cycles (v1.35's core and its goldens are gone)"
#   equiv riscv           "riscv-tests rv32ui/rv32um on the controller" "test_golden (every program's retire trace equal to ref's)" -- $S/test_golden
# v1.36: the RTL's core (src/units/rv32.h), alone: the RTL's own programs in the RTL's cycles (tests/core-programs/),
# and its join to the tile's register bus, access by access, on a scripted environment
step core-programs 1 "the RTL's core programs on the core: each pass, cycle, retirement and trap the RTL's (tests/core-programs/)" - -- $S/test_core_programs
step core-tile     1 "the RTL's core on the tile's environment: region 0xFF's accesses in register_bus's cycles" - -- $S/test_core_tile
pending rdport "read port, GATHER (with fw/tests/rdport.c and gather.c on the machine)" "$R_ENG"
#   step rdport 1 "read port, GATHER (with fw/tests/rdport.c and gather.c on the machine)" tests/p4suites/rdport.log -- $S/suite_rdport -1
pending accounting "per-phase accounting (from C)" "$R_CTL"
#   step accounting 1 "per-phase accounting (from C)" tests/ctlsuites/accounting.log -- bash -c "$S/suite_accounting -1 && { cmp -s results/phases-a2a.csv tests/ctlsuites/phases-a2a.csv || echo 'FAIL results/phases-a2a.csv differs from v1.3s'; }"
pending searchbench "search steps through the read port (microbenchmark, from C)" "$R_ENG"
#   step searchbench 3 "search steps through the read port (microbenchmark, from C)" tests/p4suites/searchbench.log -- $S/suite_search
pending rdport-shuf "shuffled child order (seed 4)" "$R_ENG"
#   step rdport-shuf 1 "shuffled child order (seed 4)" tests/p4suites/rdport-shuf.log -- $S/suite_rdport 4
same agtest "address generator read-ahead, figure 12's PTR/HIST ports" "in the step engine: the read-ahead and its places (test_ag, 8.5), PTR's and HIST's two ports shared with the core (test_receive, 12.5)"
#   step agtest 1 "address generator read-ahead, figure 12's PTR/HIST ports" tests/p4suites/agtest.log -- $S/suite_ag -1
same agtest-shuf "shuffled child order (seed 4)" "in the step eng-shuf"
#   step agtest-shuf 1 "shuffled child order (seed 4)" tests/p4suites/agtest-shuf.log -- $S/suite_ag 4
pending cc-build "building the controllers' firmware (fw/build.sh's cc part: fw/cc/cc1.c, cc23.c)" "$R_FW"
#   step cc-build      2 "building the controllers' firmware (fw/build.sh's cc part: fw/cc/cc1.c, cc23.c)" - -- fw_part cc
pending recipes-build "building the v1.2 recipes (fw/build.sh's recipes part: fw/recipes/, unedited)" "$R_FW"
#   step recipes-build 1 "building the v1.2 recipes (fw/build.sh's recipes part: fw/recipes/, unedited)" - -- fw_part recipes
pending recipes "the v1.2 recipes as suites, and the book's code blocks against their sources" "$R_CTL"
#   step recipes      1 "the v1.2 recipes as suites, and the book's code blocks against their sources" tests/ctlsuites/recipes.log -- $S/suite_recipes -1
pending recipes-shuf "shuffled child order (seed 4)" "$R_CTL"
#   step recipes-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/recipes-shuf.log -- $S/suite_recipes 4
cond_tests() { $S/test_ingress_egress && $S/test_tile_control; }
replaced cond "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" "v1.3's suite ran v1.35's handler under C&C's commands; rtl-sort provokes every tile's error and checks the NMI, the reports 24 and 25, ERROR_LINES and the toast pin on the chip; ERRORS and the live bits from 7.4 (test_tile_control); the error lines, tile_error and the toast pin from 17.6 (test_ingress_egress)" -- cond_tests
#   step cond      1 "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" tests/ctlsuites/cond.log -- $S/suite_cond -1
same cond-shuf "shuffled child order (seed 4)" "in the steps cond and rtl-sort (rtl-sort's split run steps the chip over 2 and 8 threads)"
#   step cond-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/cond-shuf.log -- $S/suite_cond 4
replaced regdoc "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" "v1.3's RegDocTest read v1.35's tables and draft header; test_regs: REGISTERS.md and mesh.h are the generator's output, the table equals the simulator's decode and the specification's offsets, bits and fields, and the RTL firmware's own definitions; the reset values and the faults on the tile are rtl-tile's registers program" -- $S/test_regs
#   step regdoc 1 "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" tests/ctlsuites/regdoc.log -- $S/suite_regdoc
same hooks "the register map, the send port, the pulses (from Java)" "the steps regdoc (the map), core-tile (the register bus's cycles, the stores that stall), rtl-tile (the RTL's registers and noc programs) and mem (the write unit's send)"
#   step hooks      1 "the register map, the send port, the pulses (from Java)" tests/ctlsuites/hooks.log -- $S/suite_hooks -1
same hooks-shuf "shuffled child order (seed 4)" "in those steps"
#   step hooks-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/hooks-shuf.log -- $S/suite_hooks 4
replaced periph "the peripheral framework: the standard window, events, region writes, the DMA" "v1.3's suite ran boot-ROM programs on v1.35's peripheral window; the slot's control block and DMA from 17.1-17.4 and the RTL's peripheral_control and peripheral_dma (test_periph_slot); rtl-tile's replayer program on the tile" -- $S/test_periph_slot
#   equiv periph          "the peripheral framework: the standard window, events, region writes, the DMA" "test_tileslotconf (TileSlotConf: PeripheralSlotTest's groups on a real tile, against ref)" -- $S/test_tileslotconf
# periph-shuf: test_tileslotconf's shuffled run alone (seed 4, v1.3's), against the same observations of ref; its
# output must be the plain run's (results/periph.out) byte for byte. (Our shuffle seeds don't map to ref's.)
periph_shuf() { TILESLOTCONF_SEED=4 "$S/test_tileslotconf" > results/periph-shuf.raw 2>&1; cat results/periph-shuf.raw; cmp -s results/periph-shuf.raw results/periph.out; }
same periph-shuf "shuffled child order (seed 4)" "in the step periph"
#   equiv periph-shuf     "shuffled child order (seed 4)" "test_tileslotconf shuffled (seed 4), its output the plain run's" -- periph_shuf
req_tests() { $S/test_request_tree && $S/test_reqbus; }
replaced reqbus "the request port, the request bus and the receive queues in the tree" "v1.3's suite drove v1.35's 128-entry request FIFO; the request unit and the tree from chapter 11 (test_request_tree), the tree's units (test_reqbus)" -- req_tests
#   step reqbus 2 "the request port, the request bus and the receive queues in the tree" tests/p4suites/reqbus.log -- $S/suite_reqbus -1
same reqbus-shuf "shuffled child order (seed 4)" "in the step reqbus"
#   step reqbus-shuf 1 "shuffled child order (seed 4)" tests/p4suites/reqbus-shuf.log -- $S/suite_reqbus 4
same cccores "C&C 1-3 as cores (Config.ccCores): the boot ROM's controller branch, the link, the request tree" "the step rtl-sort: the master and memory tiles boot the RTL's images through the RTL's boot ROM (boot_local), the tracer tile waits in it for its LOAD; the attach links and the request tree carry the run"
#   MASK_EXTRA=$D44_MASK step cccores      1 "C&C 1-3 as cores (Config.ccCores): the boot ROM's controller branch, the link, the request tree" tests/ctlsuites/cccores.log -- $S/suite_cccores -1
same cccores-shuf "shuffled child order (seed 4)" "in the step rtl-sort (its split run)"
#   MASK_EXTRA=$D44_MASK step cccores-shuf 1 "shuffled child order (seed 4)" tests/ctlsuites/cccores-shuf.log -- $S/suite_cccores 4
same replay-build "the replay controller's build" "v1.35's replay controller is retired with its machine; v1.36's is the RTL's replay image, vendored and checked against its hash list in the step rtl-images"
same stageb "Stage B: the replay controller on a tile, replaying delivered windows" "v1.35's replay_ctl.c on v1.35's tile is retired; the RTL's replay image replays the Tracer's deliveries on the chip, every row checked and in the RTL chip's cycles, in the steps rtl-segment and rtl-segment-real"
step replayer 1 "the replayer model (v1.25): the opcode table, the ALU against vec.tsv at 821e1bd, the unit tests" tests/replayer/replayer.log -- $S/suite_replayer
step memsys 1 "the Tracer's memory system (v1.3, B1): FileStore's trees and statuses, the shared cache's timing" tests/tracersuites/memsys.log -- $S/suite_memsys
step rvc    1 "the Tracer's RVC decompressor against gold's, every 16-bit encoding (v1.3, B1)" tests/tracersuites/rvc.log -- $S/suite_rvc
step tracer 1 "the Tracer (v1.3, B1): execution, memory, the map, errors, cuts, controller-fed expansions, the output path" tests/tracersuites/tracer.log -- $S/suite_tracer
# The Tracer at the block level, on the RTL acceptance's bench (tests/tracer_block_main.cpp). A run that stops without
# its verdict (a FATAL) is a FAIL line too.
tracer_block_synth() { $S/tracer_block --synthetic || echo "FAIL tracer_block --synthetic: exit $?"; }
step tracer-block-synth 1 "the Tracer alone on a synthetic RV64IMAC guest, against an interpreter of it: the output, the exact counters, the cycles adding up and by hand" tests/tracerblock/tracer-block-synth.log -- tracer_block_synth
# The real segments need the workloads' data, outside the repository: MESHSIM_INPUTS, or ~/meshsim-inputs. Without it
# the step is not run, and says so as a pending step does (no log to compare, with --check or without).
TB_INPUTS=${MESHSIM_INPUTS:-$HOME/meshsim-inputs}
tracer_block_real() {   # every segment at both compressions and both latencies against gold's files and the RTL's figures; then on the simulator's memory system, against gold's files
  local ps p s c f
  for ps in evm-zone-23:0 evm-zone-23:1 cyclone-22:0; do
    p=${ps%:*} s=${ps#*:}
    for c in 0 7; do for f in 1 55,35; do
      $S/tracer_block --inputs "$TB_INPUTS" --program $p --segment $s --compression $c --fill $f --quiet \
        --ref tests/tracerblock/rtl/windows-$p-s$s-c$c-l${f/,/-}.txt || echo "FAIL tracer_block $p segment $s, compression $c, fill $f: exit $?"
    done; done
    $S/tracer_block --inputs "$TB_INPUTS" --program $p --segment $s --fill model --quiet || echo "FAIL tracer_block $p segment $s, fill model: exit $?"
  done
}
if [ -d "$TB_INPUTS/v1261/evm-zone-23" ] && [ -d "$TB_INPUTS/v1261/cyclone-22" ] && [ -d "$TB_INPUTS/gold-v1.3" ] && [ -d "$TB_INPUTS/w26-jumpmap" ]; then
  step tracer-block 1 "the Tracer alone on the real segments (EVM Zone 0 and 1, Cyclone 0): every run against gold's files and the RTL's figures, cycle for cycle" tests/tracerblock/tracer-block.log -- tracer_block_real
else
  pending tracer-block "the Tracer alone on the real segments, against gold's files and the RTL's figures" "skipped: the workloads' data is not at $TB_INPUTS (MESHSIM_INPUTS)"
fi
same sort-p1 "the sort's local sorts (L1, L2)" "the step rtl-sort: L1 and L2 of the RTL's runs, against the oracle and the RTL chip's cycles"
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
  same agbench "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" "the step engine: READ_AHEAD is 2 TABLE_LATENCY + 2, not a setting; the lead-ins, R = 0 groups and the read-ahead's waits are 8.2-8.5's, in test_ag at TABLE_LATENCY 1, 2, 3"
  #   step agbench 3 "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" tests/p4suites/agbench.log -- $S/suite_agbench
  same s1bench "item S1: K's margin over the write path, L = 1-4, and the table-window case (a few minutes)" "the step engine: the write path is a fixed 2 TABLE_LATENCY + 2 cycles, within LAND_LATENCY for TABLE_LATENCY 1-3 and refused at 4; a core store or load meeting the engine at PTR or HIST (test_receive, 12.4, 12.5)"
  #   step s1bench 8 "item S1: K's margin over the write path, L = 1-4, and the table-window case (a few minutes)" tests/ctlsuites/s1bench.log -- $S/suite_s1bench
  same ccbench "C&C 1-3 as cores: the boot and a phase against the Java model" "the step rtl-sort (the load's and each phase's cycles against the RTL chip's)"
  #   step ccbench 16 "C&C 1-3 as cores: the boot and a phase against the Java model" tests/ctlsuites/ccbench.log -- $S/suite_ccbench
  # the sweep's CSV checked too (with --check): a difference prints a FAIL line into the step's log
  sweep_quick() { $S/sweep quick || return 1; [ "$CHECK" = 0 ] || cmp -s results/sweep-quick.csv tests/sort/ref/sweep-quick.csv || echo "FAIL: results/sweep-quick.csv differs from tests/sort/ref/sweep-quick.csv"; }
  pending sweep-quick "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" "$R_SORT"
  #   step sweep-quick 4 "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" tests/sort/ref/sweep-quick.log -- sweep_quick
  pending ctrl-full "the all-to-all from C on the four data sets" "$R_CTL"
  #   step ctrl-full 1 "the all-to-all from C on the four data sets" tests/ctlsuites/ctrl-full.log -- $S/suite_ctrl -1 full
  pending engine-full "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" "the full-size all-to-alls on four data sets are driven through v1.35's harness hooks and compared with ref's cycles; on v1.36 the all-to-all is test_mach's a2a case (2,000 elements a tile) and the sort at full size the step rtl-sort-64k; a port of EngineTest's full-size sets"
  #   step engine-full 1 "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" tests/p4suites/engine-full.log -- $S/suite_engine -1 full
  pending skewtable "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" "$R_NET"
  #   step skewtable 1 "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" tests/p4suites/skewtable.log -- $S/suite_net -1 skewtable 40
fi
# ---- v1.3's summary ------------------------------------------------------------------------------------------------
grep -h "^PENDING" results/*.log | sort | uniq -c
OWN=$(grep -ch "^PENDING" results/rtl-tile.log 2> /dev/null); OWN=${OWN:-0}
echo "== $NREPLACED steps REPLACED by tests of v1.36's hardware; $NPENDING steps PENDING (PENDING-v1.36.md, the platform stage, section 4)$([ "$OWN" = 0 ] || echo "; $OWN PENDING lines of rtl-tile's own (section 1)")"
FAILED=0
grep -h "FAIL" results/*.log && FAILED=1
[ -s results/.differs ] && { cat results/.differs; FAILED=1; }
[ "$FAILED" = 1 ] && { echo "== FAILURES"; exit 1; } || echo "== all passed (the $NPENDING PENDING steps aside; $NREPLACED REPLACED); logs in results/"
