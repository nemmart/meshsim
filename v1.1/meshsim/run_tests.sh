#!/bin/bash
# meshsim v1.1 (spec 2026-10-26n): build and run every suite (logs in results/). Usage: ./run_tests.sh [all|quick]
# Everything runs: the hardware suites, the recipes (RecipeTest), the sort (sw/ext/sort/build.sh, SortTest) and, in full
# mode, the forced-BT runs at 64 x 16,384.
set -e
cd $(dirname $0)
MODE=${1:-all}
mkdir -p build results
echo "== the layout: meshsim's copies of sw/ and sort/ (tools/check_layout.sh)"; sh tools/check_layout.sh
echo "== compiling"; javac -d build src/*.java test/*.java
echo "== building the tile programs (sw/build.sh; regenerates sw/mesh.h)"; sw/build.sh > results/sw-build.log 2>&1 || { cat results/sw-build.log; exit 1; }; grep PENDING results/sw-build.log || true
echo "== building the sort program (sw/ext/sort/build.sh: overlays, the split, the planner)"; sh sw/ext/sort/build.sh > results/sort-build.log 2>&1 || { cat results/sort-build.log; exit 1; }; tail -1 results/sort-build.log
[ -d test/riscv/bin ] || { echo "test/riscv/bin missing: test/riscv/build.sh <riscv-tests dir> test/riscv/bin"; exit 1; }
JX=${JX:--Xmx4g}                  # the big runs' heap (a 3 GB box: JX=-Xmx2500m)
J="java ${JS:--Xmx3g} -cp build"
run() { echo "== $1"; shift; "$@" 2>&1 | tee results/$LOG.log | tail -${TAIL:-1}; }
Q=; [ "$MODE" = quick ] && Q=quick
LOG=bus       TAIL=1 run "bus primitives"                         $J BusTest
LOG=net       TAIL=1 run "network, packets, director"         $J NetTest -1 $Q
LOG=net-shuf  TAIL=1 run "shuffled child order (seed 5)"      $J NetTest 5 quick
LOG=net-shuf2 TAIL=1 run "shuffled child order (seed 11)"     $J NetTest 11 quick
LOG=mem       TAIL=1 run "big SRAM, read and write queues"    $J MemTest -1
LOG=mem-shuf  TAIL=1 run "shuffled child order (seed 3)"      $J MemTest 3
LOG=mem-shuf2 TAIL=1 run "shuffled child order (seed 8)"      $J MemTest 8
LOG=engine    TAIL=1 run "address generator, write engine, all-to-all (small)" $J EngineTest -1
LOG=eng-shuf  TAIL=1 run "shuffled child order (seed 4)"      $J EngineTest 4
LOG=ctrl      TAIL=1 run "control and the controller"          $J CtrlTest -1
LOG=ctrl-shuf TAIL=1 run "shuffled child order (seed 4)"      $J CtrlTest 4
LOG=riscv     TAIL=1 run "riscv-tests rv32ui/rv32um on the controller" $J RiscvTest
LOG=rdport    TAIL=1 run "read port, GATHER"                $J ReadPortGatherTest -1
LOG=accounting TAIL=1 run "per-phase accounting"                $J AccountingTest -1
LOG=searchbench TAIL=1 run "search steps through the read port (microbenchmark)" $J SearchBench
LOG=rdport-shuf TAIL=1 run "shuffled child order (seed 4)" $J ReadPortGatherTest 4
LOG=agtest    TAIL=1 run "address generator read-ahead, figure 12's PTR/HIST ports" $J AgTest -1
LOG=agtest-shuf TAIL=1 run "shuffled child order (seed 4)"   $J AgTest 4
LOG=recipes   TAIL=1 run "the recipe book (RECIPES.md, sw/tests/rcp_*)" $J RecipeTest -1
LOG=recipes-shuf TAIL=1 run "shuffled child order (seed 4)"  $J RecipeTest 4
LOG=cond      TAIL=1 run "conditions, the done gate, the error line, C&C's receive FIFO, REPORT" $J CondTest -1
LOG=cond-shuf TAIL=1 run "shuffled child order (seed 4)"     $J CondTest 4
LOG=regdoc    TAIL=1 run "REGISTERS.md against the generator, mesh.h, the split's split_regs.h and the simulator" $J RegDocTest
LOG=hooks     TAIL=1 run "the 26n register map, the send port, TILE_PULSE, the empty slot and the unwired UART, from Java" $J HooksTest -1
LOG=hooks-shuf TAIL=1 run "shuffled child order (seed 4)"   $J HooksTest 4
LOG=periph    TAIL=1 run "the peripheral slot: claims, landing and counting, HALT, the replayer in the slot" $J PeripheralSlotTest -1
LOG=periph-shuf TAIL=1 run "shuffled child order (seed 4)"  $J PeripheralSlotTest 4
LOG=sender    TAIL=1 run "the sender: packet split, addr, busy and sender idle, ignored stores, HALT (a test peripheral)" $J SenderTest -1
LOG=sender-shuf TAIL=1 run "shuffled child order (seed 4)"  $J SenderTest 4
LOG=reqbus    TAIL=2 run "the request port and the request bus (targets strapped for the test), from Java and from C" $J RequestBusTest -1
LOG=reqbus-shuf TAIL=1 run "shuffled child order (seed 4)"  $J RequestBusTest 4
if [ -n "$DELIVERY" ]; then   # Stage B (sw/replay) on a tracer delivery's guest windows: DELIVERY=<replay-inputs dir>
  sh sw/replay/build.sh > results/replay-build.log 2>&1 || { cat results/replay-build.log; exit 1; }
  for g in lrd_x0 scd_x0 nop_hint jalr_self jalr_zero; do LOG=stageb-$g TAIL=1 run "Stage B on the tile: $g" $J StageB $DELIVERY state $g; done
fi
LOG=replayer  TAIL=1 run "the replayer model (cp11, its owner's suite): the ALU against vec.tsv, the unit tests" $J ReplayerTest ../tools/replayer-opcodes/vec.tsv
LOG=sort-p1   TAIL=1 run "the sort's local sorts (L1, L2)"                     $J SortTest -1 p1
LOG=sort      TAIL=1 run "the sort, every built-in set at 64 x <= 2,000 (several minutes)" $J SortTest -1 sort
LOG=sort-shuf TAIL=1 run "shuffled child order (seed 4)"                      $J SortTest 4 sort
if [ "$MODE" != quick ]; then
  [ -f build/keys-16384/trace10-10.keys ] || $J KeyGen build/keys-16384 --n 16384 --only trace10-10 > /dev/null
  LOG=sort-bt-kv32 TAIL=2 run "BT, forced, trace hot 10 / 10 % at 64 x 16,384, 32-bit values (a few minutes)" $J SortTest -1 run label=bt16k-kv32 keys=build/keys-16384/trace10-10.keys fmt=kv32 ready=8 force=bt
  LOG=sort-bt-k128 TAIL=2 run "the same, 128-bit values" $J SortTest -1 run label=bt16k-k128 keys=build/keys-16384/trace10-10.keys fmt=k128 ready=8 force=bt
fi
if [ "$MODE" != quick ]; then LOG=rdport-full TAIL=1 run "gather 1 at full size, Java and from C (a few minutes)" java $JX -cp build ReadPortGatherTest -1 full; fi
if [ "$MODE" != quick ]; then LOG=accounting-full TAIL=1 run "per-phase accounting, full-size all-to-all from C" java $JX -cp build AccountingTest -1 full; fi
if [ "$MODE" != quick ]; then LOG=agbench TAIL=3 run "read-ahead measurements: ag_lookahead, zero-length groups, start to first request (a few minutes)" $J AgBench; fi
if [ "$MODE" != quick ]; then LOG=sweep-quick TAIL=4 run "parameter sweep, quick (about 1 minute; 'java Sweep full' takes about 25)" $J Sweep quick; fi
if [ "$MODE" != quick ]; then LOG=ctrl-full TAIL=1 run "the all-to-all from C on the four data sets (several minutes)" java $JX -cp build CtrlTest -1 full; fi
if [ "$MODE" != quick ]; then LOG=engine-full TAIL=1 run "full-size all-to-alls (4 data sets) and radix sort (about 1.5 minutes)" java $JX -cp build EngineTest -1 full; fi
if [ "$MODE" != quick ]; then LOG=skewtable TAIL=1 run "fan-in under 0/+1 skew, 40 skew patterns (several minutes)" $J NetTest -1 skewtable 40; fi
grep -h "FAIL" results/*.log && { echo "== FAILURES"; exit 1; } || echo "== all passed; logs in results/"
