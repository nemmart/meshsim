#!/bin/bash
# run_tests.sh: v1.36's suite runner. Every step runs a test or a suite of v1.36's hardware and writes
# results/<step>.log; the summary lists every FAIL line and every log that differs from its reference.
#
#   ./run_tests.sh [all|quick] [--check] [--no-build]
#     all | quick   quick leaves out the long steps (the block under `if [ "$MODE" != quick ]`)
#     --check       also compare each log that has a reference log (the step's REF) with it, timing masked (mask()
#                   below); a difference fails the run as a FAIL line would
#     --no-build    use the binaries as they are
#   Environment: CC_NAME (default clang); JOBS (the build's parallel jobs, default 4); STEP_TIMES=1 (each step's wall
#   time in results/.step-times). Two things are outside the repository, and a step that needs one is SKIPPED, and
#   printed and counted as such, when it is not on this machine:
#     MESHSIM_INPUTS      (default ~/meshsim-inputs) the real workloads' data: the steps tracer-block and, in `all`,
#                         segment-real
#     the RISC-V toolchain (fw/build.sh's header; RISCV_PREFIX names it): the step fw-build. With it, RISCV_TESTS and
#                         COREMARK name the checkouts that step also rebuilds riscv-tests' and CoreMark's images from
#   The 64K runs' key file (16 MiB) is built here (the step sort-64k of `all`, into build/sort-64k.keys).
#   LPDDR_SEED=S (v1.37, the INTEGRATOR): every chip's LPDDR model in its jitter mode: test_master with
#   MESHSIM_LPDDR_SEED=S, each proof run with --lpddr-seed S + its number. Without it the model's timing is fixed. (WORKER5's
#   CACHE_CTL and WORKER6's MESHSIM_UPDATER_CTL are gone: the cache's and the Updater's controllers are the machine.)
#   The suites build with MODE=suite (release flags, the checker's facts on: src/check.h), into build/$CC_NAME-suite/.
#
# ADDING A STEP:
#     step LOG TAIL "description" REF -- command args...
#   a suite whose log is compared with a reference: LOG the log's name, TAIL the lines shown, REF the reference log,
#   then the command ($S is the suites' build directory);
#     check LOG "description" -- command args...
#   a test with no reference log: its own FAIL lines or a non-zero exit fail the run. Its expectations are in the test,
#   from the specification or the RTL.
#   The step's program must be in SUITES below (built before the steps run).
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
mkdir -p results
rm -f results/*.log
: > results/.differs
[ -z "${STEP_TIMES:-}" ] || : > results/.step-times

# The programs the steps run (targets of the Makefile).
SUITES="test_core_programs test_core_tile test_rv32_rtl test_regs test_tile_control"                      # the core, the map, the tile control unit
SUITES="$SUITES test_router test_special_router test_request_tree test_reqbus"              # the router, the request tree
SUITES="$SUITES test_mem test_ag test_receive test_gather test_mach test_checker"                     # the tile's units, and the tile
SUITES="$SUITES test_periph_slot test_ingress_egress test_rw_engine"                        # the peripherals
SUITES="$SUITES test_rtl_tile test_rtl_stream test_sort test_memory test_segment"           # the firmware on the tile and the chip
SUITES="$SUITES suite_replayer suite_memsys suite_rvc suite_tracer tracer_block"            # the replayer and the Tracer
SUITES="$SUITES fwimage"                                                                   # the runs' keys (tools/fwimage)
SUITES="$SUITES test_file_store test_updater test_ingress_link test_ingress_chain test_packet_vectors updater_frames packetize"       # v1.37: the file store's pages by address, the Updater black box, Ingress's host link
SUITES="$SUITES test_master proof_run"                                                     # v1.37: the master tile's program, the whole chip from a packet file
SUITES="$SUITES test_updater_ctl"                                                         # v1.37: the Updater's controller (WORKER6)
SUITES="$SUITES test_lpddr_model test_axi_arbiter test_cache test_memory_system"                               # updater_wip, WORKER5: the LPDDR model, the AXI arbiter, the cache's controller

if [ "$BUILD" = 1 ]; then
  echo "== building the suites (MODE=suite CC_NAME=$CC_NAME)"
  t_build=$SECONDS
  make -s -j"${JOBS:-4}" MODE=suite CC_NAME="$CC_NAME" $(for x in $SUITES; do echo "$S/$x"; done) > results/.build.log 2>&1 \
    || { cat results/.build.log; echo "== the build failed"; exit 1; }
  # suite_bus's throws checks are the framework's debug assertions, so its step runs suite_bus's debug build (tiny)
  make -s -j"${JOBS:-4}" MODE=debug CC_NAME="$CC_NAME" "build/$CC_NAME-debug/suite_bus" > results/.build-bus.log 2>&1 \
    || { cat results/.build-bus.log; echo "== the build failed (suite_bus, MODE=debug)"; exit 1; }
  [ -z "${STEP_TIMES:-}" ] || echo "(build) $((SECONDS - t_build)) s" >> results/.step-times
fi

# Timing masked for --check: wall-clock figures (seconds, ms, rates) vary by machine; cycles never do.
# An `inputs:` line's first field names the simulator: metadata, masked.
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(ms|s|sec|seconds|us|µs|ns)\b/<time>/g; s/[0-9.]+ ?[kKMG]?(cycles|instructions|packets|steps)\/s/<rate>/g; s/^(inputs: )[^ ]+/\1<simulator>/'; }

step() {
  local log=$1 tail=$2 what=$3 ref=$4; shift 4
  [ "$1" = -- ] && shift
  echo "== $what"
  local t0=$SECONDS
  "$@" > "results/$log.log" 2>&1
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
  tail -"$tail" "results/$log.log"
  if [ "$CHECK" = 1 ]; then
    if [ ! -f "$ref" ]; then echo "LOG DIFFERS ($log): no reference log $ref" | tee -a results/.differs
    elif ! diff -q <(mask < "$ref") <(mask < "results/$log.log") > /dev/null; then
      echo "LOG DIFFERS ($log): results/$log.log against $ref" | tee -a results/.differs
    fi
  fi
}
check() {
  local log=$1 what=$2; shift 2
  [ "$1" = -- ] && shift
  echo "== $what"
  local t0=$SECONDS
  "$@" > "results/$log.log" 2>&1 || echo "FAIL $log: exit status $?" >> "results/$log.log"
  [ -z "${STEP_TIMES:-}" ] || echo "$log $((SECONDS - t0)) s" >> results/.step-times
  tail -1 "results/$log.log"
}
# A step that needs what is outside the repository and not on this machine: not run, and said so.
NSKIPPED=0
skipped() { echo "== $2"; echo "SKIPPED ($3)" | tee "results/$1.log"; NSKIPPED=$((NSKIPPED + 1)); }
all_of() { local t; for t in "$@"; do "$S/$t" || return 1; done; }   # several tests as one step

# ---- the framework, the copies and the firmware ----
echo "== the files copied from v1.3 (tools/check_copies.sh)"; sh tools/check_copies.sh > results/layout.log 2>&1 && tail -1 results/layout.log || { cat results/layout.log; echo "FAIL the layout" | tee -a results/layout.log; }
step bus 1 "the framework's port kinds (the debug build: its throws checks are debug assertions)" tests/p2suites/bus.log -- build/$CC_NAME-debug/suite_bus
check fw-images "the committed firmware images against their hash lists, and the runs' keys made here against the committed keys (fw/check_images.sh)" -- env FWIMAGE=$S/fwimage bash fw/check_images.sh
# Every C and assembly image rebuilt from fw/'s sources and compared, byte for byte, with its committed copy: only with
# the toolchain the committed images were built with.
if why=$(bash fw/build.sh --toolchain); then
  check fw-build "the firmware rebuilt from its sources: every image byte-equal to its committed copy (fw/build.sh --check)" -- bash fw/build.sh --check
else
  skipped fw-build "the firmware rebuilt from its sources, against the committed images" "$why"
fi

# ---- the core and the register map ----
check core-programs "the RTL's core programs on the core: each pass, cycle, retirement and trap the RTL's (tests/core-programs/)" -- $S/test_core_programs
check core-tile "the RTL's core on the tile's environment: region 0xFF's accesses in register_bus's cycles" -- $S/test_core_tile
check core-contract "the RTL's core against its contract: the core specification's §1 on a bus, and machine-spec 5 and 6.4 through register_bus.h's decode (tests/test_rv32_rtl.cpp)" -- $S/test_rv32_rtl
check regs "REGISTERS.md and mesh.h against their generator, the simulator's decode, the specification and the RTL firmware's definitions (tests/test_regs.cpp)" -- $S/test_regs
check tile-control "the tile control unit and the register bus's decode: events, errors, the barrier (machine-spec 5, 7)" -- $S/test_tile_control

# ---- the NoC ----
check router "the router against the RTL's own and from machine-spec 16 (tests/test_router.cpp, test_special_router.cpp)" -- all_of test_router test_special_router
check requests "the request unit and the request tree (machine-spec 11; tests/test_request_tree.cpp, test_reqbus.cpp)" -- all_of test_request_tree test_reqbus

# ---- the tile's units ----
check mem "the scratchpad and the inject unit (machine-spec 14, 15; tests/test_mem.cpp)" -- $S/test_mem
mem_shuf() { MEM_SHUFFLE=$1 $S/test_mem; }
check mem-shuf "the same, the units of a level stepped in a shuffled order (seed 3)" -- mem_shuf 3
check mem-shuf2 "the same (seed 8)" -- mem_shuf 8
check units "the DMA unit (8.2-8.5: test_ag), the write engine with PTR and HIST (12.4, 12.5: test_receive), the gather unit (13: test_gather)" -- all_of test_ag test_receive test_gather
units_shuf() { SIM_SHUFFLE=4 $S/test_ag && SIM_SHUFFLE=4 $S/test_receive; }
check units-shuf "test_ag and test_receive, the units of a level stepped in a shuffled order (seed 4)" -- units_shuf
check mach "the tile and the machine harness: the pull boot on all 64 tiles, the chip's build, packets, reads, an all-to-all, the gather unit on the tile (tests/test_mach.cpp)" -- $S/test_mach
check checker "the checker's report: packets that break a rule, each named once at its sender and at its receiver; ERRORS bits by machine-spec 7.4's names (tests/test_checker.cpp)" -- $S/test_checker

# ---- the peripherals ----
check periph "the peripheral slot: the control block and the DMA (machine-spec 17.1-17.4, the RTL's peripheral_control and peripheral_dma; tests/test_periph_slot.cpp)" -- $S/test_periph_slot
check ingress-egress "Ingress/Egress: the nets, RELEASE, the watchdog, the error lines, the toast pin, the UART, Egress, its words and its UB (17.6; tests/test_ingress_egress.cpp)" -- $S/test_ingress_egress

# ---- the RTL's tile programs, unmodified, on the tile; meshsim's firmware on the chip ----
check rtl-tile "the RTL's tile programs on one compute tile, with the boot ROM's cycles (tests/test_rtl_tile.cpp)" -- $S/test_rtl_tile
# The sort end to end on the chip under meshsim's firmware (C&C 1's program, cc1.c), the master and memory tiles
# placed, the compute tiles booted by LOAD. The 16-record runs and the run split over threads here; the 64K runs in `all`, with their key file built here.
sort_16() { $S/test_sort sort_oracle && $S/test_sort sort_16; }
check sort "the sort on the chip, 64 x 16 records: kv32, k128, and kv32 split over threads, against the oracle, every tile's error provoked (tests/test_sort.cpp)" -- sort_16
check rtl-stream "the master's host interface and UART on the chip: the RTL's chip_stream_test, and the UART through the core (tests/test_rtl_stream.cpp)" -- $S/test_rtl_stream
memory_run() { $S/test_rw_engine && $S/test_memory; }
check memory "the memory tile: the RW engine against the RTL's unit, cycle for cycle, and the memory run on the chip (tests/test_rw_engine.cpp, tests/test_memory.cpp)" -- memory_run
# The segment on the chip, run by meshsim's firmware (cc1.c, tracer_ctl.c, replay_ctl.c): the RTL test's three
# generated segments here; the real workloads' windows in `all`, with their data.
check segment "the segment on the chip: 4 windows, 64 windows and a window of 65,536 rows, traced, delivered and replayed, every row and delivery against the segment's (tests/test_segment.cpp)" -- $S/test_segment segment_

# ---- the replayer and the Tracer at the block level ----
step replayer 1 "the replayer: the opcode table, the ALU against vec.tsv at 821e1bd, the unit tests" tests/replayer/replayer.log -- $S/suite_replayer
step memsys 1 "the Tracer's memory system: FileStore's trees and statuses, the shared cache's timing" tests/tracersuites/memsys.log -- $S/suite_memsys
step rvc    1 "the Tracer's RVC decompressor against gold's, every 16-bit encoding" tests/tracersuites/rvc.log -- $S/suite_rvc
step tracer 1 "the Tracer: execution, memory, the map, errors, cuts, core-fed expansions, the output path" tests/tracersuites/tracer.log -- $S/suite_tracer
# The Tracer at the block level, on the RTL acceptance's bench (tests/tracer_block_main.cpp). A run that stops without
# its verdict (a FATAL) is a FAIL line too.
tracer_block_synth() { $S/tracer_block --synthetic || echo "FAIL tracer_block --synthetic: exit $?"; }
step tracer-block-synth 1 "the Tracer alone on a synthetic RV64IMAC guest, against an interpreter of it: the output, the exact counters, the cycles adding up and by hand" tests/tracerblock/tracer-block-synth.log -- tracer_block_synth
INPUTS=${MESHSIM_INPUTS:-$HOME/meshsim-inputs}
tracer_block_real() {   # every segment at both compressions and both latencies against gold's files and the RTL's figures; then on the simulator's memory system, against gold's files
  local ps p s c f
  for ps in evm-zone-23:0 evm-zone-23:1 cyclone-22:0; do
    p=${ps%:*} s=${ps#*:}
    for c in 0 7; do for f in 1 55,35; do
      $S/tracer_block --inputs "$INPUTS" --program $p --segment $s --compression $c --fill $f --quiet \
        --ref tests/tracerblock/rtl/windows-$p-s$s-c$c-l${f/,/-}.txt || echo "FAIL tracer_block $p segment $s, compression $c, fill $f: exit $?"
    done; done
    $S/tracer_block --inputs "$INPUTS" --program $p --segment $s --fill model --quiet || echo "FAIL tracer_block $p segment $s, fill model: exit $?"
  done
}
if [ -d "$INPUTS/v1261/evm-zone-23" ] && [ -d "$INPUTS/v1261/cyclone-22" ] && [ -d "$INPUTS/gold-v1.3" ] && [ -d "$INPUTS/w26-jumpmap" ]; then
  step tracer-block 1 "the Tracer alone on the real segments (EVM Zone 0 and 1, Cyclone 0): every run against gold's files and the RTL's figures, cycle for cycle" tests/tracerblock/tracer-block.log -- tracer_block_real
else
  skipped tracer-block "the Tracer alone on the real segments, against gold's files and the RTL's figures" "the workloads' data is not at $INPUTS: MESHSIM_INPUTS"
fi
# updater_wip, WORKER5: the same segments on the cache's controller, the chip's memory system (every run against gold's
# files; its cycles are the cache's, so not against the RTL's figures)
tracer_block_cache() {
  local ps p s c
  for ps in evm-zone-23:0 evm-zone-23:1 cyclone-22:0; do
    p=${ps%:*} s=${ps#*:}
    for c in 0 7; do
      $S/tracer_block --inputs "$INPUTS" --program $p --segment $s --compression $c --fill cache --quiet || echo "FAIL tracer_block $p segment $s, compression $c, fill cache: exit $?"
    done
  done
}
if [ -d "$INPUTS/v1261/evm-zone-23" ] && [ -d "$INPUTS/v1261/cyclone-22" ] && [ -d "$INPUTS/gold-v1.3" ] && [ -d "$INPUTS/w26-jumpmap" ]; then
  check tracer-block-cache "WORKER5: the Tracer alone on the real segments on the cache's controller (EVM Zone 0 and 1, Cyclone 0; compression 0 and 7): every run against gold's files" -- tracer_block_cache
fi

# ---- updater_wip, WORKER5: the path to LPDDR, each unit alone, and the Tracer on the cache's controller ----
cache_units() { for t in test_lpddr_model test_axi_arbiter test_cache; do $S/$t || echo "FAIL $t: exit $?"; done; }
check cache "WORKER5: the LPDDR model, the AXI arbiter and the cache's controller alone; the cache against the stand-in with no cache on random traffic (tests/test_lpddr_model.cpp, test_axi_arbiter.cpp, test_cache.cpp)" -- cache_units
cache_synth() { for seed in 1 2 3 4 5 6 7 8; do TRACER_CACHE=1 $S/tracer_block --synthetic $seed --quiet || echo "FAIL tracer_block --synthetic $seed on the cache: exit $?"; done; }
check cache-synth "WORKER5: the Tracer alone on the cache's controller, eight synthetic guests against their interpreter (tracer_block --synthetic, with the cache)" -- cache_synth

# ---- v1.37: the units of the Updater's work, each alone (../updater_wip; CHANGES-v1.37.md) ----
check file-store "v1.37: the file store's pages by address: placed where told, whole pages, beside the pages it hands out (tests/test_file_store.cpp)" -- $S/test_file_store
check updater "v1.37: the Updater black box alone: registers, queues and credits, update words raw and LRU, copy-on-write, the Tracer's copy, errors; gold's fixtures, each image against gold's (tests/test_updater.cpp)" -- $S/test_updater
check ingress-link "v1.37: Ingress's host side on the real link (WORKER8's Node, the host's End): the link's start, the rule for a service flag, the landing, reading out by the DMA, the forward count, both queues, the stop bit, the rates, noise, link_error, the reset (tests/test_ingress_link.cpp)" -- $S/test_ingress_link
check ingress-chain "v1.37: the chain with no chips: a host's End and two or three Ingress/Egress units back to back; every word to every ASIC, the replies by hops, the chain before an ASIC's own, the stop bits, a reset in the middle (28.12; tests/test_ingress_chain.cpp)" -- $S/test_ingress_chain
VECTORS=${PACKET_VECTORS:-../updater_wip/pipeline/gold/test/packets}
if [ -f "$VECTORS/VECTORS.txt" ]; then
  check packet-vectors "v1.37: Ingress's host link against gold's packetizer: every vector, packet file and damaged file of $VECTORS, by the reader and through the black box (tests/test_packet_vectors.cpp)" -- env PACKET_VECTORS="$VECTORS" $S/test_packet_vectors
else
  skipped packet-vectors "v1.37: Ingress's host link against gold's packetizer's vectors and packet files" "no $VECTORS/VECTORS.txt: gold's packetizer is not on this branch yet (PACKET_VECTORS names another folder)"
fi
if command -v javac > /dev/null && [ -d ../updater_wip/pipeline/gold/src ]; then
  check updater-fixtures "v1.37: the fixtures made again by gold (../updater_wip/pipeline/gold), equal to the committed ones (tests/updater/make_fixtures.sh --check)" -- bash tests/updater/make_fixtures.sh --check
else
  skipped updater-fixtures "v1.37: the fixtures made again by gold, against the committed ones" "no javac, or no ../updater_wip/pipeline/gold"
fi
master_run() { if [ -n "${LPDDR_SEED:-}" ]; then MESHSIM_LPDDR_SEED=$LPDDR_SEED $S/test_master; else $S/test_master; fi; }
check master "v1.37: the master tile's program on the whole chip, from a packet file: gold's frames of the guests in the link's packets, the Updater's images against gold's, the hand-over, the segment traced and replayed; the error path; the checks' teeth (tests/test_master.cpp)" -- master_run
check updater-ctl "v1.37: the Updater's controller with the test LPDDR and the stand-in cache, and joined to the memory system (the real cache, arbiter and LPDDR model): gold's fixtures alone and against the black box, random streams, varied timing, a second client at once, the errors (tests/test_updater_ctl.cpp)" -- $S/test_updater_ctl
check memory-system "v1.37: the memory system, the memory tile's peripheral 1, in the chip: its window, its error on the error line, the reset, the RW engine on client 2 (tests/test_memory_system.cpp)" -- $S/test_memory_system


# ---- the long steps ----
if [ "$MODE" != quick ]; then
  # The 64K runs' keys: SortTest's uniform set, built here (16 MiB, not committed) and checked against its committed sha256.
  sort_64k() {
    $S/fwimage keys 65536 build/sort-64k.keys && (cd build && sha256sum -c ../fw/images/sort/sort-64k.keys.sha256) &&
      SORT_64K_KEYS=build/sort-64k.keys $S/test_sort sort_64k
  }
  check sort-64k "the sort on the chip at 64 x 65,536 records: kv32 and k128, against the oracle (about a minute)" -- sort_64k
  if [ -f "$INPUTS/w26-jumpmap/evm-zone-23/proof.json" ] && [ -f "$INPUTS/v1261/cyclone-22/window_00000.rows.bin" ]; then
    check segment-real "the segment on the real workloads' windows: EVM Zone's 0-63 and 64-127 and Cyclone's 0-63, every row and delivery against the recorded files (about a minute)" -- env MESHSIM_INPUTS="$INPUTS" $S/test_segment real_
  else
    skipped segment-real "the segment on the real workloads' windows (EVM Zone, Cyclone), against the recorded files" "the workloads' data is not at $INPUTS: MESHSIM_INPUTS"
  fi
  # v1.37: the whole chip from a packet file, on the real workloads (tests/proof_run_main.cpp). The host's side, gold's
  # frame files, the link's packet files and gold's reference for each segment, is made by pipeline/frames/build.sh
  # into $INPUTS/v137/SET/; its RUNS lists each run's name, N, a segment's windows and the segments to run.
  proof_set() {   # proof_set SET [--recorded]: every run of the set, every segment listed
    local d=$INPUTS/v137/$1 name n windows list s rec seed k=0
    while read -r name n windows list; do
      for s in $list; do
        rec=""; [ "${2:-}" = --recorded ] && rec="--recorded $name"
        seed=""; k=$((k + 1)); [ -n "${LPDDR_SEED:-}" ] && seed="--lpddr-seed $((LPDDR_SEED + k))"
        MESHSIM_INPUTS="$INPUTS" $S/proof_run "$d/$name.frames" --packets "$d/$name.packets" --asics "$n" --segment "$s" --windows "$windows" \
          --gold "$d/$name-s$s.images.txt" $rec $seed --quiet || echo "FAIL proof_run $name, s $s: exit $?"
      done
    done < "$d/RUNS"
  }
  V137="the host's side is not at $INPUTS/v137 (pipeline/frames/build.sh makes it: MESHSIM_INPUTS)"
  if [ -f "$INPUTS/v137/real/RUNS" ] && [ -f "$INPUTS/w26-jumpmap/evm-zone-23/proof.json" ] && [ -f "$INPUTS/v1261/cyclone-22/window_00000.rows.bin" ]; then
    check proof-real "v1.37: segment-real's three segments from packet files: the master builds the images with the Updater and hands the tracer tile its segment; every row and delivery against the recorded files, every image against gold's" -- proof_set real --recorded
  else
    skipped proof-real "v1.37: segment-real's three segments from packet files, against the recorded files and gold's images" "the recorded windows or $V137"
  fi
  if [ -f "$INPUTS/v137/matrix/RUNS" ]; then
    check proof-matrix "v1.37: the small inputs from packet files, segments of 8 windows: EVM Zone and Cyclone, raw and LRU-coded, with and without a base table, s = 0, 1 and the middle; every image against gold's, the Tracer's last record against record s + 1 (24 runs)" -- proof_set matrix
  else
    skipped proof-matrix "v1.37: the small inputs from packet files, raw and LRU-coded, with and without a base table" "$V137"
  fi
  if [ -f "$INPUTS/v137/29/RUNS" ]; then
    check proof-29 "v1.37: both workloads at 2^29 from packet files, N 128: five segments of each (the first two, the middle, the last two), every image against gold's, the Tracer's last record against record s + 1 (10 runs)" -- proof_set 29
  else
    skipped proof-29 "v1.37: both workloads at 2^29 from packet files" "$V137"
  fi
fi

# ---- the summary ----
FAILED=0
grep -h "FAIL" results/*.log && FAILED=1
[ -s results/.differs ] && { cat results/.differs; FAILED=1; }
[ "$NSKIPPED" = 0 ] || grep -H "^SKIPPED" results/*.log | sed 's|^results/\(.*\)\.log:|  \1: |'
if [ "$FAILED" = 1 ]; then echo "== FAILURES"; exit 1; fi
if [ "$NSKIPPED" = 0 ]; then echo "== all passed; logs in results/"
else echo "== all passed, $NSKIPPED steps SKIPPED for what is not on this machine (data, a toolchain); logs in results/"; fi
