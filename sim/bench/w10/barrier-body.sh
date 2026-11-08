# Task body (WORKER10): what the barrier costs, alone and with the engine's per-interval work, against the chip's useful
# work (0.156 us a cycle a thread at 6 threads: 0.47 us an interval of 3). (1) bench_threads --form 3 --barrier-only:
# T threads doing nothing but the barrier (us per round), spin (W4's default 2000 pauses then yield) and pure spin.
# (2) --form 3: T domains, a unit each stepping every cycle, a ring of Links of 3 (rate 0: no words; 0.3: some), with
# --time: the bare interval (publish, barrier, drain, clear, the engine's entry). REPS (default 5).
REPS=${REPS:-5}
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
NPC=$(count "$P_CPUS")
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 3 4 6 8 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    run "$k bo-t$t-yield" "$TSET" $B --form 3 --barrier-only --threads $t --cycles 2000000 --cpus "$C"
    run "$k bo-t$t-spin" "$TSET" $B --form 3 --barrier-only --threads $t --cycles 2000000 --barrier spin --cpus "$C"
    for r in 0 0.3; do
      run "$k bare-t$t-r$r" "$TSET" $B --form 3 --threads $t --cycles 600000 --warmup 3000 --rate $r --cpus "$C"
      run "$k bare-t$t-r$r-timed" "$TSET" $B --form 3 --threads $t --cycles 600000 --warmup 3000 --rate $r --cpus "$C" --time
    done
  done
done; done
sec "summary (medians): us per barrier round alone; us per bare interval, and its timed breakdown (us per interval)"
for k in $KITS; do for t in $TS; do
  line="$k t$t barrier_only: yield=$(med "$k bo-t$t-yield" us_per_round) spin=$(med "$k bo-t$t-spin" us_per_round);"
  for r in 0 0.3; do
    u=$(med "$k bare-t$t-r$r" us_per_interval); line+=" bare(r$r)=$u ["
    for f in compute imbalance barrier_wait drain publish clear; do
      sh=$(med "$k bare-t$t-r$r-timed" $f); ut=$(med "$k bare-t$t-r$r-timed" us_per_interval)
      line+="$f=$(awk -v s=$sh -v u=$ut 'BEGIN { printf "%.3f", s * u }') "
    done
    line+="]"
  done
  echo "$line"
done; done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
