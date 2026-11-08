# Task body (WORKER10): the quiet case (PLAN-M2, the owner's expectation): cost per cycle against the number of active
# units. (1) bench_threads --form 4: N units on one thread, K busy (a little hashing and a put each cycle), the rest
# asleep (a Link in and out each, never used): N = 64, 1024, 16384, 131072; K = 1, 8, 64; perf stat (cycles,
# instructions, L1d and LLC load misses). (2) W9's chip in windows of 64 cycles (bench_chip --windows): the boot and wfi,
# cost per cycle by steps a cycle. REPS (default 5).
REPS=${REPS:-5}
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads build/clang-release/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
P1=$(take 1 "$P_ONE")
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for n in 64 1024 16384 131072; do for a in 1 8 64; do
    [ $a -gt $n ] && continue
    PERF=1 run "$k q-n$n-k$a" "$P1" $B --form 4 --units $n --active $a --cycles 400000 --warmup 5000 --cpus "$P1"
  done; done
done; done
for k in $KITS; do for p in sort wfi; do
  ( cd "$W/kit/$k/sim" && taskset -c "$P1" ./build/clang-release/bench_chip --prog $p --windows 64 --reps 9 > "$RES/windows-$k-$p.txt" 2>&1 ) || echo "RUNFAIL windows $p"
done; done
sec "summary: medians; us per cycle and instructions per cycle (perf: the run's, build and warmup included) against N, K"
for k in $KITS; do for a in 1 8 64; do for n in 64 1024 16384 131072; do
  u=$(med "$k q-n$n-k$a" us_per_cycle); [ "$u" = - ] && continue
  ins=$(med "$k q-n$n-k$a" core_instructions); cyc=$(med "$k q-n$n-k$a" core_cycles); l1=$(med "$k q-n$n-k$a" core_L1-dcache-load-misses); llc=$(med "$k q-n$n-k$a" core_LLC-load-misses)
  printf "%s active=%-3s units=%-7s us_per_cycle=%-8s ns_per_active_step=%-7s words=%-8s perf(all): instr=%s cycles=%s l1_miss=%s llc_miss=%s\n" "$k" "$a" "$n" "$u" \
    "$(med "$k q-n$n-k$a" ns_per_active_step)" "$(med "$k q-n$n-k$a" words)" "$ins" "$cyc" "$l1" "$llc"
done; done; done | tee "$RES/summary.txt"
for k in $KITS; do for p in sort wfi; do echo "== chip $p in windows of 64"; cat "$RES/windows-$k-$p.txt"; done; done | tee -a "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
