# Task body (WORKER10): the chip's placement anomalies of Quest 142 (tiles at 8 threads slower than rows at 8, the same
# grouping; load-1 at 6 and 8) measured robustly: bench_chip --reps 9 (nine machines in one process after an untimed
# one), 3 processes each, plain and --time; per-machine min/median/max kept. The sort's boot.
PREPS=${PREPS:-3}; IREPS=${IREPS:-9}
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
sec "1. one thread, then 6 and 8 threads: rows (8), tiles, load, profile; plain and timed; $PREPS processes x $IREPS machines"
for rep in $(seq $PREPS); do for k in $KITS; do
  crun "$k one" "$P1" $k --prog sort --threads 1 --cpus "$P1" --reps $IREPS
  for t in 6 8; do C=$(take $t "$PLACE")
    for p in rows tiles load profile; do
      [ "$p" = rows ] && [ "$t" != 8 ] && continue
      crun "$k t$t-$p" "$P_CPUS" $k --prog sort --threads $t --partition $p --cpus "$C" --reps $IREPS
      crun "$k t$t-$p-timed" "$P_CPUS" $k --prog sort --threads $t --partition $p --cpus "$C" --reps $IREPS --time
    done
  done
done; done
sec "2. digests"
n=$(grep "chip prog=sort" "$RES/raw.txt" | field digest | sort -u | wc -l); [ "$n" = 1 ] && verdict "digests" PASS || verdict "digests: $n distinct" FAIL
sec "3. summary: per configuration, every process's median/min/max (us per cycle) and the timed run's shares x its median"
for k in $KITS; do for tag in one t6-tiles t6-load t6-profile t8-rows t8-tiles t8-load t8-profile; do
  echo "$k $tag: $(grep "^$k $tag " "$RES/raw.txt" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^us_(per_cycle|min|max)=/) printf "%s ", $i; printf "| " }')"
  grep "^$k $tag-timed " "$RES/raw.txt" | while read -r l; do
    u=$(echo "$l" | field us_per_cycle); s="  timed us=$u"
    for f in compute imbalance barrier_wait drain publish clear; do v=$(echo "$l" | field $f | tr -d '"'); s+=" $f=$(awk -v a=$v -v u=$u 'BEGIN { printf "%.3f", a * u }')"; done
    echo "$s"
  done
done; done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
