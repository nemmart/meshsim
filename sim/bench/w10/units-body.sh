# Task body (WORKER10): per-thread speed on what runs now (W9's chip with W8's and W11's units): the sort's boot, wfi and
# regs (both to their stop) on one thread. (1) plain, bench_chip --reps 9, 3 processes; (2) --units: each unit kind's
# time per cycle, steps per cycle and ns a step (a calibrating run, the clock's own cost taken off), 3 processes;
# (3) perf record on a plain run (top symbols), where perf may record.
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
P1=$(take 1 "$P_ONE")
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
for rep in 1 2 3; do for k in $KITS; do for p in sort wfi regs; do
  crun "$k plain-$p" "$P1" $k --prog $p --threads 1 --cpus "$P1" --reps 9
  ( cd "$W/kit/$k/sim" && taskset -c "$P1" ./build/clang-release/bench_chip --prog $p --units > "$RES/units-$k-$p-$rep.txt" 2>&1 ) || echo "RUNFAIL units $p"
done; done; done
sec "perf record (top symbols, self time), one plain run each"
for k in $KITS; do for p in sort wfi regs; do
  ( cd "$W/kit/$k/sim" && taskset -c "$P1" perf record -q -o "$W/perf-$p.data" -- ./build/clang-release/bench_chip --prog $p --reps 9 > /dev/null 2>&1 \
    && perf report -i "$W/perf-$p.data" --stdio --no-children --percent-limit 0.7 --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' | head -45 \
       > "$RES/perf-$k-$p.txt" ) || echo "perf record not available for $p" > "$RES/perf-$k-$p.txt"
  echo "== perf $p"; head -45 "$RES/perf-$k-$p.txt"
done; done
sec "digests"
for key in "chip prog=sort" "chip prog=wfi" "chip prog=regs"; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l); [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done
sec "summary: one thread us per cycle (medians of the processes' medians); the units by kind (3 runs each)"
for k in $KITS; do for p in sort wfi regs; do
  echo "$k $p: one thread us_per_cycle $(grep "^$k plain-$p " "$RES/raw.txt" | field us_per_cycle | paste -sd' ')"
  for rep in 1 2 3; do head -1 "$RES/units-$k-$p-$rep.txt"; done
  echo "  by kind (run 1; runs 2, 3 in units-*.txt):"; grep '^unit ' "$RES/units-$k-$p-1.txt" | head -16 | sed 's/^/  /'
done; done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
