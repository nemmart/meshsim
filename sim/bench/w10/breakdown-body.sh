# Task body (WORKER10): where a split run's time goes, at the flattening point and around it. For each placement and
# thread count, runs with --time (Options::time_threads: per-thread phase shares) interleaved with plain runs (the
# timer's own cost), REPS each. The shares times the plain run's wall time give us per chip-cycle per phase:
# compute (mean thread), imbalance (slowest less mean, per interval), barrier_wait (mean wait less imbalance), drain,
# publish + clear. Also compute inflation: threads x compute against the one-thread run.
REPS=${REPS:-5}; CYC=${CYC:-60000}
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
NP=$(count "$P_ONE"); NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
V="--form 2 --cycles $CYC --warmup 2000"
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 4 $NP 8 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
TS=$(echo $TS | tr ' ' '\n' | sort -n | paste -sd' ')
CASES="mesh35-tiles mesh35-profile mesh05-tiles synth-tiles"
args_of() { case $1 in mesh35-tiles) echo "--rate 0.35 --partition tiles" ;; mesh35-profile) echo "--rate 0.35 --partition profile" ;;
                       mesh05-tiles) echo "--rate 0.05 --partition tiles" ;; synth-tiles) echo "--chip synth --partition tiles" ;; esac; }

sec "1. one thread (the base): mesh 0.35, 0.05, synth; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  run "$k one-mesh35" "$P1" $B $V --rate 0.35 --threads 1 --cpus "$P1"
  run "$k one-mesh05" "$P1" $B $V --rate 0.05 --threads 1 --cpus "$P1"
  run "$k one-synth" "$P1" $B $V --chip synth --threads 1 --cpus "$P1"
done; done

sec "2. split, plain and timed, over T = $TS threads (placement $PLACE): $CASES; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    for c in $CASES; do
      [ "$c" = synth-tiles ] && [ "$t" -gt 8 ] && continue
      run "$k t$t-$c-plain" "$TSET" $B $V $(args_of $c) --threads $t --cpus "$C"
      run "$k t$t-$c-timed" "$TSET" $B $V $(args_of $c) --threads $t --cpus "$C" --time
    done
  done
done; done

sec "3. digests: one per chip and rate"
for key in "chip=mesh .*rate_in=0.35" "chip=mesh .*rate_in=0.05" "chip=synth "; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done

sec "4. summary: us per chip-cycle: wall (plain), the timer's cost, and the breakdown (shares of the timed runs, medians, times the plain wall)"
for k in $KITS; do
  for c in $CASES; do
    case $c in mesh35*) base=$(med "$k one-mesh35" us_per_chip_cycle) ;; mesh05*) base=$(med "$k one-mesh05" us_per_chip_cycle) ;; *) base=$(med "$k one-synth" us_per_chip_cycle) ;; esac
    for t in $TS; do
      u=$(med "$k t$t-$c-plain" us_per_chip_cycle); [ "$u" = - ] && continue
      ut=$(med "$k t$t-$c-timed" us_per_chip_cycle)
      line="$k t$t-$c one=$base wall=$u timed=$ut ideal=$(awk -v b=$base -v t=$t 'BEGIN { printf "%.3f", b / t }')"
      for f in compute imbalance barrier_wait drain publish clear end other; do
        sh=$(med "$k t$t-$c-timed" $f)
        line+=" $f=$(awk -v s=$sh -v u=$u 'BEGIN { printf "%.3f", s * u }')"
      done
      cs=$(med "$k t$t-$c-timed" compute)
      line+=" inflation=$(awk -v s=$cs -v u=$u -v t=$t -v b=$base 'BEGIN { printf "%.2f", s * u * t / b }')"
      echo "$line"
    done
  done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
