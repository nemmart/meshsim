# Task body (WORKER10): W9's chip first (bench_chip: the sort's boot, 14,791 cycles; wfi to its stop; regs to 824),
# then the mesh. Placements over T threads: rows (row_bands, T in 2, 4, 8), tiles (64 domains grouped in order), load
# (by_load, 1 per unit), profile at crossing_ns 0 (balance only), 20 (Quest 141's price) and 60; one thread the base.
# Then the breakdown (--time) for tiles and profile-20 at the flattening point. REPS (default 7) repeats.
REPS=${REPS:-7}
sec "build (clang release, D18) and test_partition (clang release), per revision"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_chip build/clang-release/bench_threads \
      build/clang-release/test_partition > "$RES/build-$k.log" 2>&1 ) || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
  ( cd "$W/kit/$k/sim" && ./build/clang-release/test_partition > /dev/null ) && verdict "test_partition $k" PASS || verdict "test_partition $k" FAIL
done
NP=$(count "$P_ONE"); NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 4 $NP 8 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
TS=$(echo $TS | tr ' ' '\n' | sort -n | paste -sd' ')
# chip runs: from the kit's sim/ (it reads tests/golden-m2/images and ref/meshsim/hops.txt)
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
PARTS="tiles load profile0 profile20 profile60"
pargs() { case $1 in profile0) echo "--partition profile --crossing-ns 0" ;; profile20) echo "--partition profile --crossing-ns 20" ;;
                     profile60) echo "--partition profile --crossing-ns 60" ;; *) echo "--partition $1" ;; esac; }

sec "1. the chip on one thread: sort's boot, wfi, regs; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do for p in sort wfi regs; do
  crun "$k one-$p" "$P1" $k --prog $p --threads 1 --cpus "$P1"
done; done; done

sec "2. the chip split over T = $TS threads (placement $PLACE): rows (T = 2, 4, 8), $PARTS; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    for p in sort wfi regs; do
      case $t in 2|4|8) crun "$k t$t-$p-rows" "$TSET" $k --prog $p --threads $t --partition rows --cpus "$C" ;; esac
      for pp in $PARTS; do crun "$k t$t-$p-$pp" "$TSET" $k --prog $p --threads $t $(pargs $pp) --cpus "$C"; done
    done
  done
done; done

sec "3. the breakdown on the sort's boot (--time): tiles and profile20 over T = $TS; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    for pp in tiles profile20; do crun "$k t$t-sort-$pp-timed" "$TSET" $k --prog sort --threads $t $(pargs $pp) --cpus "$C" --time; done
  done
done; done

sec "4. the mesh second (bench_threads form 2, rate 0.35, 60,000 cycles): tiles, profile at 0 and 20 ns over T = $TS; $REPS repeats"
for rep in $(seq $REPS); do for k in $KITS; do B=$W/kit/$k/sim/build/clang-release/bench_threads
  run "$k mesh-one" "$P1" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads 1 --cpus "$P1"
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    run "$k mesh-t$t-tiles" "$TSET" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads $t --partition tiles --cpus "$C"
    for ns in 0 20; do run "$k mesh-t$t-profile$ns" "$TSET" $B --form 2 --cycles 60000 --warmup 2000 --rate 0.35 --threads $t --partition profile --crossing-ns $ns --cpus "$C"; done
  done
done; done

sec "5. digests: one per program (chip) and for the mesh"
for key in "chip prog=sort" "chip prog=wfi" "chip prog=regs" "threads form=2 chip=mesh"; do
  n=$(grep -E "$key" "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests ($key)" PASS || verdict "digests ($key): $n distinct" FAIL
done

sec "6. summary: medians; us per cycle (wall), against one thread; the placement's line"
for k in $KITS; do
  for p in sort wfi regs; do
    b=$(med "$k one-$p" us_per_cycle); echo "$k chip $p one us=$b"
    for t in $TS; do for pp in rows $PARTS; do
      u=$(med "$k t$t-$p-$pp" us_per_cycle); [ "$u" = - ] && continue
      pl=$(grep "^$k t$t-$p-$pp " "$RES/raw.txt" | head -1 | grep -o 'load max/mean [0-9.]*\|% of the load\|cost max/mean [0-9.]*\|[0-9.]*% of' | paste -sd' ')
      printf "%s chip %s t%s %-10s us=%-7s x_one=%-5s %s\n" "$k" "$p" "$t" "$pp" "$u" "$(awk -v b=$b -v u=$u 'BEGIN { printf "%.2f", b / u }')" "$pl"
    done; done
  done
  for t in $TS; do for pp in tiles profile20; do
    u=$(med "$k t$t-sort-$pp-timed" us_per_cycle); [ "$u" = - ] && continue
    line="$k chip sort t$t $pp timed us=$u"
    for f in compute imbalance barrier_wait drain publish clear; do
      sh=$(med "$k t$t-sort-$pp-timed" $f); line+=" $f=$(awk -v s=$sh -v u=$u 'BEGIN { printf "%.3f", s * u }')"
    done
    echo "$line"
  done; done
  mb=$(med "$k mesh-one" us_per_chip_cycle); echo "$k mesh one us=$mb"
  for t in $TS; do for pp in tiles profile0 profile20; do
    u=$(med "$k mesh-t$t-$pp" us_per_chip_cycle); [ "$u" = - ] && continue
    printf "%s mesh t%s %-10s us=%-7s x_one=%s\n" "$k" "$t" "$pp" "$u" "$(awk -v b=$mb -v u=$u 'BEGIN { printf "%.2f", b / u }')"
  done; done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
