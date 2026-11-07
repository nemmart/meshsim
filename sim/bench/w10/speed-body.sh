# Task body (WORKER10): the speed matrix, for every kit revision in $KITS, interleaved run by run. Used for the baseline
# (one revision) and for each change's before/after (two). The machine from machine.sh (D21): PLACE, P_ONE, P_CPUS, take,
# count. REPS (default 5) repeats; CYC (default 60000) cycles per run.
REPS=${REPS:-5}; CYC=${CYC:-60000}
sec "build (clang release, D18) and the thread tests (clang debug: ASan, UBSan), per revision"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=debug build/clang-debug/test_threads build/clang-debug/test_matrix >> "$RES/build-$k.log" 2>&1 \
    && ./build/clang-debug/test_threads > /dev/null && ./build/clang-debug/test_matrix > /dev/null ) \
    && verdict "test_threads, test_matrix (debug) $k" PASS || verdict "test_threads, test_matrix (debug) $k" FAIL
done
NP=$(count "$P_ONE"); NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
V="--form 2 --cycles $CYC --warmup 2000"
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }   # the taskset: P CPUs while they suffice
TS=; for t in 2 4 $NP 8 $NPC; do [[ " $TS " == *" $t "* ]] || TS+="${TS:+ }$t"; done                 # thread counts, P cores first then hyperthreads

sec "1. one thread, one domain: mesh at rate 0.35 and 0.05, the synthetic chip; perf stat; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  PERF=1 run "$k one-mesh35" "$P1" $B $V --rate 0.35 --threads 1 --cpus "$P1"
  PERF=1 run "$k one-mesh05" "$P1" $B $V --rate 0.05 --threads 1 --cpus "$P1"
  PERF=1 run "$k one-synth" "$P1" $B $V --chip synth --threads 1 --cpus "$P1"
done; done

sec "2. the split's own cost on one thread: mesh by rows (2, 4, 6, 8 domains) and by tiles (64); synth by clusters (8); $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for d in 2 4 6 8; do run "$k split1-rows$d" "$P1" $B $V --threads 1 --domains $d --cpus "$P1"; done
  PERF=1 run "$k split1-tiles64" "$P1" $B $V --threads 1 --partition tiles --cpus "$P1"
  run "$k split1-synth8" "$P1" $B $V --chip synth --threads 1 --partition tiles --cpus "$P1"
done; done

sec "3. form 2: mesh by rows (T domains, T <= 8) and by tiles (64 domains), synth by clusters (8 domains), over T = $TS threads (placement: $PLACE); $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    [ "$t" -le 8 ] && run "$k t$t-rows" "$TSET" $B $V --threads $t --cpus "$C"
    run "$k t$t-tiles64" "$TSET" $B $V --threads $t --partition tiles --cpus "$C"
    [ "$t" -le 8 ] && run "$k t$t-synth8" "$TSET" $B $V --chip synth --threads $t --partition tiles --cpus "$C"
  done
done; done

sec "4. digests: one per chip and rate over every run and revision"
for key in "chip=mesh .*rate_in=0.35" "chip=mesh .*rate_in=0.05" "chip=synth "; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done

sec "5. summary: medians, us per chip-cycle (wall), words (Sizes), and against one thread"
for k in $KITS; do
  base=$(med "$k one-mesh35" us_per_chip_cycle); sb=$(med "$k one-synth" us_per_chip_cycle)
  for tag in one-mesh35 one-mesh05 one-synth split1-rows2 split1-rows4 split1-rows6 split1-rows8 split1-tiles64 split1-synth8 \
             $(for t in $TS; do echo t$t-rows t$t-tiles64 t$t-synth8; done); do
    u=$(med "$k $tag" us_per_chip_cycle); [ "$u" = - ] && continue
    w=$(med "$k $tag" words); case $tag in *synth*) b=$sb ;; one-mesh05) b=$u ;; *) b=$base ;; esac
    printf "%-6s %-16s us=%-8s x_one=%-6s words=%s\n" "$k" "$tag" "$u" "$(awk -v b="$b" -v u="$u" 'BEGIN { printf "%.2f", b / u }')" "$w"
  done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
