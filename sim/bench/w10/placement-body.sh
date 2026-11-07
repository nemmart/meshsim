# Task body (WORKER10): placement by load against the author's partitions, form 2, over 2 .. every-P-CPU threads, for
# every revision in $KITS (one is enough: placement is a build option). Placements: rows (bands of mesh rows, T <= 8),
# tiles (64 tile domains grouped in order, one engine per thread), load (by_load, 1 per unit), profile (by_load with a
# one-thread calibration run's loads). The mesh at rates 0.35 and 0.05; the synthetic chip (clusters, load, profile).
# REPS (default 5) repeats; CYC (default 60000) cycles.
REPS=${REPS:-5}; CYC=${CYC:-60000}
sec "build (clang release, D18) and test_partition (clang debug), per revision"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=debug build/clang-debug/test_partition >> "$RES/build-$k.log" 2>&1 \
    && ./build/clang-debug/test_partition > /dev/null ) && verdict "test_partition (debug) $k" PASS || verdict "test_partition (debug) $k" FAIL
done
NP=$(count "$P_ONE"); NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
V="--form 2 --cycles $CYC --warmup 2000"
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 3 4 5 $NP 8 10 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
TS=$(echo $TS | tr ' ' '\n' | sort -n | paste -sd' ')

sec "1. one thread, one domain (the base): mesh 0.35, 0.05, synth; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  run "$k one-mesh35" "$P1" $B $V --rate 0.35 --threads 1 --cpus "$P1"
  run "$k one-mesh05" "$P1" $B $V --rate 0.05 --threads 1 --cpus "$P1"
  run "$k one-synth" "$P1" $B $V --chip synth --threads 1 --cpus "$P1"
done; done

sec "2. form 2 over T = $TS threads (placement: $PLACE): mesh 0.35 rows/tiles/load/profile, mesh 0.05 tiles/profile, synth tiles/load/profile; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    [ "$t" -le 8 ] && run "$k t$t-mesh35-rows" "$TSET" $B $V --rate 0.35 --threads $t --cpus "$C"
    for p in tiles load profile; do run "$k t$t-mesh35-$p" "$TSET" $B $V --rate 0.35 --threads $t --partition $p --cpus "$C"; done
    for p in tiles profile; do run "$k t$t-mesh05-$p" "$TSET" $B $V --rate 0.05 --threads $t --partition $p --cpus "$C"; done
    for p in tiles load profile; do [ "$p" = tiles ] && [ "$t" -gt 8 ] && continue
      run "$k t$t-synth-$p" "$TSET" $B $V --chip synth --threads $t --partition $p --cpus "$C"; done
  done
done; done

sec "3. digests: one per chip and rate over every run"
for key in "chip=mesh .*rate_in=0.35" "chip=mesh .*rate_in=0.05" "chip=synth "; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done

sec "4. summary: medians (us per chip-cycle, wall), against one thread; the placement's balance and crossings"
for k in $KITS; do
  m35=$(med "$k one-mesh35" us_per_chip_cycle); m05=$(med "$k one-mesh05" us_per_chip_cycle); sy=$(med "$k one-synth" us_per_chip_cycle)
  printf "%-6s %-22s us=%-8s\n" "$k" one-mesh35 "$m35"; printf "%-6s %-22s us=%-8s\n" "$k" one-mesh05 "$m05"; printf "%-6s %-22s us=%-8s\n" "$k" one-synth "$sy"
  for t in $TS; do for tag in mesh35-rows mesh35-tiles mesh35-load mesh35-profile mesh05-tiles mesh05-profile synth-tiles synth-load synth-profile; do
    u=$(med "$k t$t-$tag" us_per_chip_cycle); [ "$u" = - ] && continue
    case $tag in mesh35*) b=$m35 ;; mesh05*) b=$m05 ;; *) b=$sy ;; esac
    bal=$(grep "^$k t$t-$tag " "$RES/raw.txt" | head -1 | grep -o 'max/mean [0-9.]*' | cut -d' ' -f2)
    cw=$(grep "^$k t$t-$tag " "$RES/raw.txt" | head -1 | grep -o '[0-9]* crossing words' | cut -d' ' -f1)
    bar=$(grep "^$k t$t-$tag " "$RES/raw.txt" | head -1 | grep -o 'barrier every [0-9]*\|no barrier' | head -1)
    printf "%-6s %-22s us=%-8s x_one=%-6s balance=%-6s crossing_words=%-5s %s\n" "$k" "t$t-$tag" "$u" \
      "$(awk -v b="$b" -v u="$u" 'BEGIN { printf "%.2f", b / u }')" "${bal:--}" "${cw:--}" "$bar"
  done; done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
