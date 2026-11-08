# Task body (WORKER10): the flags barrier (Options::barrier_flags: per-thread epochs) against W4's counter barrier.
# (1) the barrier alone and the bare interval (bench_threads --form 3); (2) the chip's boot by tiles (bench_chip, in-
# process repeats: 9 machines after an untimed one), plain and --time; (3) the mesh at 0.35 by tiles. 2, 4, 6, 8, every
# P CPU. Barriers: counter (yield after 2000 pauses), flags (the same policy), flags-spin. REPS (default 5) processes.
REPS=${REPS:-5}
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads build/clang-release/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 4 6 8 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
tb() { case $1 in counter) echo "--barrier yield" ;; flags) echo "--barrier flags" ;; flags-spin) echo "--barrier flags-spin" ;; esac; }
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  [ $rep = 1 ] && crun "$k one" "$P1" $k --prog sort --threads 1 --cpus "$P1" --reps 9
  [ $rep = 1 ] && run "$k mesh-one" "$P1" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads 1 --cpus "$P1"
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    for b in counter flags flags-spin; do
      run "$k bo-t$t-$b" "$TSET" $B --form 3 --barrier-only --threads $t --cycles 2000000 $(tb $b) --cpus "$C"
      run "$k bare-t$t-$b" "$TSET" $B --form 3 --threads $t --cycles 600000 --warmup 3000 --rate 0.3 $(tb $b) --cpus "$C"
      crun "$k chip-t$t-$b" "$TSET" $k --prog sort --threads $t --partition tiles --barrier $b --cpus "$C" --reps 9
      crun "$k chip-t$t-$b-timed" "$TSET" $k --prog sort --threads $t --partition tiles --barrier $b --cpus "$C" --reps 9 --time
      run "$k mesh-t$t-$b" "$TSET" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads $t --partition tiles $(tb $b) --cpus "$C"
    done
  done
done; done
sec "digests"
for key in "chip prog=sort" "threads form=2 chip=mesh" "threads form=3 threads=2 "; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done
sec "summary (medians): barrier alone (us per round), bare interval (us), the chip's boot (us per cycle, x one thread, and the timed breakdown per cycle), the mesh (us per cycle)"
for k in $KITS; do
  c1=$(med "$k one" us_per_cycle); m1=$(med "$k mesh-one" us_per_chip_cycle); echo "$k one: chip $c1, mesh $m1"
  for t in $TS; do for b in counter flags flags-spin; do
    cu=$(med "$k chip-t$t-$b" us_per_cycle); mu=$(med "$k mesh-t$t-$b" us_per_chip_cycle)
    line="$k t$t $b: barrier_only=$(med "$k bo-t$t-$b" us_per_round) bare=$(med "$k bare-t$t-$b" us_per_interval) chip=$cu x$(awk -v a=$c1 -v b=$cu 'BEGIN { printf "%.2f", a / b }') ["
    ct=$(med "$k chip-t$t-$b-timed" us_per_cycle)
    for f in compute imbalance barrier_wait drain publish clear; do sh=$(med "$k chip-t$t-$b-timed" $f); line+="$f=$(awk -v s=$sh -v u=$ct 'BEGIN { printf "%.3f", s * u }') "; done
    line+="] mesh=$mu x$(awk -v a=$m1 -v b=$mu 'BEGIN { printf "%.2f", a / b }')"
    echo "$line"
  done; done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
