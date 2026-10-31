#!/bin/bash
# meshsim v1.2 (WORKER4's port, in progress): build and run every suite (logs in results/). Usage: ./run_tests.sh [all|quick]
# Suites waiting for a later phase of the port print "PENDING (<phase>)" and write that line to their log; their test
# sources are left out of the build through test/PENDING. The sort's steps are PENDING (sort port): the sort is ported
# onto this simulator after WORKER4 (WORKER4/QUESTIONS.md, W4-01).
set -e
cd $(dirname $0)
MODE=${1:-all}
mkdir -p build results
echo "== compiling (test sources in test/PENDING are left out)"
javac -d build src/*.java
grep -v '^#' test/PENDING | awk 'NF{print "test/"$1}' > build/pending.txt
javac -d build -cp build $(ls test/*.java | grep -v -x -F -f build/pending.txt)
JX=${JX:--Xmx4g}                  # the big runs' heap (a 3 GB box: JX=-Xmx2500m)
J="java ${JS:--Xmx3g} -cp build"
run() { echo "== $1"; shift; "$@" 2>&1 | tee results/$LOG.log | tail -${TAIL:-1}; }
pending() { echo "== $2"; echo "PENDING ($3)" | tee results/$1.log; }
Q=; [ "$MODE" = quick ] && Q=quick
pending layout     "the layout: meshsim's copies of sw/ and sort/ (tools/check_layout.sh)" "sort port"
LOG=sw-build TAIL=3 run "building the tile programs (sw/build.sh; regenerates sw/mesh.h)" sh sw/build.sh
pending sort-build "building the sort program (sw/ext/sort/build.sh)" "sort port"
LOG=bus       TAIL=1 run "bus primitives"                         $J BusTest
LOG=net       TAIL=1 run "network, packets, director"         $J NetTest -1 $Q
LOG=net-shuf  TAIL=1 run "shuffled child order (seed 5)"      $J NetTest 5 quick
LOG=net-shuf2 TAIL=1 run "shuffled child order (seed 11)"     $J NetTest 11 quick
LOG=mem       TAIL=1 run "big SRAM, read and write queues"    $J MemTest -1
LOG=mem-shuf  TAIL=1 run "shuffled child order (seed 3)"      $J MemTest 3
LOG=mem-shuf2 TAIL=1 run "shuffled child order (seed 8)"      $J MemTest 8
LOG=engine    TAIL=1 run "address generator, write engine, all-to-all (small)" $J EngineTest -1
LOG=eng-shuf  TAIL=1 run "shuffled child order (seed 4)"      $J EngineTest 4
LOG=ctrl      TAIL=1 run "control and the controller (from C)"  $J CtrlTest -1
LOG=ctrl-shuf TAIL=1 run "shuffled child order (seed 4)"      $J CtrlTest 4
LOG=riscv     TAIL=1 run "riscv-tests rv32ui/rv32um on the controller" $J RiscvTest
LOG=rdport    TAIL=1 run "read port, GATHER (Java; the C parts PENDING (phase 2))" $J ReadPortGatherTest -1
LOG=accounting TAIL=1 run "per-phase accounting (from C)"    $J AccountingTest -1
LOG=searchbench TAIL=3 run "search steps through the read port (microbenchmark, from C)" $J SearchBench
LOG=rdport-shuf TAIL=1 run "shuffled child order (seed 4)" $J ReadPortGatherTest 4
LOG=agtest    TAIL=1 run "address generator read-ahead, figure 12's PTR/HIST ports" $J AgTest -1
LOG=agtest-shuf TAIL=1 run "shuffled child order (seed 4)"   $J AgTest 4
pending recipes      "the recipe book (v1.1's RecipeTest; replaced by the v1.2 recipes)" "phase 6"
pending recipes-shuf "shuffled child order (seed 4)" "phase 6"
LOG=cond      TAIL=2 run "the errors end to end: ERRORS, the handler's reports, the error lines, the toast pin, the reset" $J CondTest -1
LOG=cond-shuf TAIL=1 run "shuffled child order (seed 4)"      $J CondTest 4
LOG=regdoc    TAIL=1 run "REGISTERS.md and mesh.h against their generators, the draft header and the simulator" $J RegDocTest
LOG=hooks     TAIL=1 run "the register map, the send port, the pulses (from Java)" $J HooksTest -1
LOG=hooks-shuf TAIL=1 run "shuffled child order (seed 4)"     $J HooksTest 4
LOG=periph    TAIL=1 run "the peripheral framework: the standard window, events, region writes, the DMA" $J PeripheralSlotTest -1
LOG=periph-shuf TAIL=1 run "shuffled child order (seed 4)"   $J PeripheralSlotTest 4
LOG=reqbus    TAIL=2 run "the request port, the request bus and the receive queues in the tree" $J RequestBusTest -1
LOG=reqbus-shuf TAIL=1 run "shuffled child order (seed 4)"    $J RequestBusTest 4
LOG=replay-build TAIL=1 run "the replay controller (sw/replay/build.sh)" sh sw/replay/build.sh
LOG=stageb    TAIL=1 run "Stage B: replay_ctl.c on a tile against Stage A, synthetic windows" $J StageB synthetic
if [ -n "$DELIVERY" ]; then   # Stage B on a tracer delivery's guest windows: DELIVERY=<replay-inputs dir>
  for g in lrd_x0 scd_x0 nop_hint jalr_self jalr_zero; do LOG=stageb-$g TAIL=1 run "Stage B on the tile: $g" $J StageB $DELIVERY state $g; done
fi
LOG=replayer  TAIL=1 run "the replayer model (v1.25): the opcode table, the ALU against vec.tsv at 821e1bd, the unit tests" $J ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt
pending sort-p1   "the sort's local sorts (L1, L2)" "sort port"
pending sort      "the sort, every built-in set at 64 x <= 2,000" "sort port"
pending sort-shuf "shuffled child order (seed 4)" "sort port"
if [ "$MODE" != quick ]; then
  pending sort-bt-kv32 "BT, forced, trace hot 10 / 10 % at 64 x 16,384, 32-bit values" "sort port"
  pending sort-bt-k128 "the same, 128-bit values" "sort port"
  LOG=rdport-full TAIL=1 run "gather 1 at full size (Java; the C parts PENDING (phase 2))" java $JX -cp build ReadPortGatherTest -1 full
  LOG=accounting-full TAIL=1 run "per-phase accounting, full-size all-to-all from C" java $JX -cp build AccountingTest -1 full
  LOG=agbench TAIL=3 run "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" $J AgBench
  LOG=sweep-quick TAIL=4 run "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" $J Sweep quick
  LOG=ctrl-full TAIL=1 run "the all-to-all from C on the four data sets" java $JX -cp build CtrlTest -1 full
  LOG=engine-full TAIL=1 run "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" java $JX -cp build EngineTest -1 full
  LOG=skewtable TAIL=1 run "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" $J NetTest -1 skewtable 40
fi
grep -h "PENDING" results/*.log | sort | uniq -c
grep -h "FAIL" results/*.log && { echo "== FAILURES"; exit 1; } || echo "== all passed (PENDING suites aside); logs in results/"
