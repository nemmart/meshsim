# Task body (WORKER10): the framework's cost per step, before/after interleaved: W9's chip on one thread (the boot,
# wfi, regs; bench_chip --reps 9, in-process repeats) and at 6 threads by tiles (the boot), W4's mesh and the synthetic
# chip on one thread (bench_threads form 2, 60,000 cycles), perf stat on the chip's boot. REPS (default 5) processes.
REPS=${REPS:-5}
sec "build (clang release, D18) and the goldens (test_golden, test_chip: clang release)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_chip build/clang-release/bench_threads \
      build/clang-release/test_golden build/clang-release/test_chip > "$RES/build-$k.log" 2>&1 ) || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
  ( cd "$W/kit/$k/sim" && ./build/clang-release/test_golden > "$RES/golden-$k.log" 2>&1 && ./build/clang-release/test_chip >> "$RES/golden-$k.log" 2>&1 ) \
    && verdict "test_golden, test_chip $k" PASS || verdict "test_golden, test_chip $k" FAIL
done
P1=$(take 1 "$P_ONE"); C6=$(take 6 "$PLACE")
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for p in sort wfi regs; do crun "$k one-$p" "$P1" $k --prog $p --threads 1 --cpus "$P1" --reps 9; done
  [ $rep = 1 ] && PERF=1 crun "$k perf-sort" "$P1" $k --prog sort --threads 1 --cpus "$P1" --reps 9
  crun "$k t6-sort" "$P_CPUS" $k --prog sort --threads 6 --partition tiles --cpus "$C6" --reps 9
  run "$k mesh-one" "$P1" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads 1 --cpus "$P1"
  run "$k synth-one" "$P1" $B --form 2 --chip synth --cycles 60000 --warmup 2000 --threads 1 --cpus "$P1"
done; done
sec "digests"
for key in "chip prog=sort threads=1" "chip prog=wfi" "chip prog=regs" "chip prog=sort threads=6" "chip=mesh" "chip=synth"; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l); [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done
sec "summary: medians (us per cycle) per revision"
for t in one-sort one-wfi one-regs t6-sort; do echo "$t: $(for k in $KITS; do printf '%s %s  ' $k $(med "$k $t" us_per_cycle); done)"; done | tee "$RES/summary.txt"
for t in mesh-one synth-one; do echo "$t: $(for k in $KITS; do printf '%s %s  ' $k $(med "$k $t" us_per_chip_cycle); done)"; done | tee -a "$RES/summary.txt"
echo "perf (boot, one process, 10 machines): $(for k in $KITS; do printf '%s instr=%s cycles=%s  ' $k $(med "$k perf-sort" core_instructions) $(med "$k perf-sort" core_cycles); done)" | tee -a "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
