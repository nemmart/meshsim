# Task body (WORKER10): the 16K sort's timing, first measurement (bench_sort: M2Sort's counted span, L1's GO to L2's
# all-READY, 496,619 cycles; setup excluded as W7's baseline; in-process repeats; the machine recorded, D21).
# (1) one thread; (2) split over 2, 4, 6, 8, every P CPU: tiles, rows (2, 4, 8), load, profile (calibrated over the
# whole sort), plain and --time; (3) phases (one thread and 6 by tiles); (4) the units by kind (one thread); (5) the
# cost by steps a cycle (windows of 1000). The keys: KeyGen's uniform 64 x 16384 in the kit (sha256 against HASHES).
REPS=${REPS:-3}
KEYS=$W/kit/extra/uniform.keys
sec "the keys"
h=$(sha256sum < "$KEYS" | cut -c1-64); want=$(awk '$2 == "keys-16384/uniform.keys" {print $1}' "$W/kit/after/sim/tests/golden-m2/HASHES")
[ "$h" = "$want" ] && verdict "keys sha256 as HASHES" PASS || { verdict "keys sha256 as HASHES ($h)" FAIL; exit 1; }
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_sort > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
NPC=$(count "$P_CPUS"); P1=$(take 1 "$P_ONE")
cpus_for() { local t=$1; if [ "$t" -le "$NPC" ]; then echo "$P_CPUS"; else echo "$ALL_CPUS"; fi; }
TS=; for t in 2 4 6 8 $NPC; do [ "$t" -le "$NPC" ] && [[ " $TS " != *" $t "* ]] && TS+="${TS:+ }$t"; done
SPAN=""   # found by the first run (its polling), then passed to the rest
srun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_sort --keys "$KEYS" $SPAN "$@" ); }
# run() greps "^(threads|chip) ": this body's lines start "sort "
run() {
  local tag=$1 cpus=$2; shift 2
  taskset -c "$cpus" "$@" > "$W/out.txt" 2> "$W/err.txt"; local rc=$?
  local line; line=$(grep -E '^sort ' "$W/out.txt" | tail -1)
  if [ $rc -ne 0 ] || [ -z "$line" ]; then echo "RUNFAIL $tag rc=$rc: $*"; head -5 "$W/err.txt"; FAILS=$((FAILS + 1)); return 1; fi
  echo "$tag $line" | tee -a "$RES/raw.txt"
  grep -E '^sort_' "$W/out.txt" | sed "s/^/$tag /" >> "$RES/detail.txt"
}
for k in $KITS; do
  sec "1. one thread ($k)"
  srun "$k one" "$P1" $k --threads 1 --cpus "$P1" --reps $REPS
  SPAN="--span $(grep -o 'span [0-9]* -> [0-9]*' "$W/err.txt" | awk '{print $2, $4}')"; echo "span: $SPAN"
  sec "2. split ($k): T = $TS"
  for t in $TS; do C=$(take $t "$PLACE"); TSET=$(cpus_for $t)
    for p in tiles rows load profile; do
      [ $p = rows ] && case $t in 2|4|8) ;; *) continue ;; esac
      srun "$k t$t-$p" "$TSET" $k --threads $t --partition $p --cpus "$C" --reps $REPS
      srun "$k t$t-$p-timed" "$TSET" $k --threads $t --partition $p --cpus "$C" --reps 1 --time
    done
  done
  sec "3. phases ($k): one thread, 6 by tiles"
  srun "$k phases-one" "$P1" $k --threads 1 --cpus "$P1" --reps $REPS --phases
  srun "$k phases-t6" "$P_CPUS" $k --threads 6 --partition tiles --cpus "$(take 6 "$PLACE")" --reps $REPS --phases
  sec "4. units ($k)"
  ( cd "$W/kit/$k/sim" && taskset -c "$P1" ./build/clang-release/bench_sort --keys "$KEYS" --units > "$RES/units-$k.txt" 2>&1 ); cat "$RES/units-$k.txt"
  sec "5. steps a cycle ($k), windows of 1000"
  ( cd "$W/kit/$k/sim" && taskset -c "$P1" ./build/clang-release/bench_sort --keys "$KEYS" --windows 1000 > "$RES/windows-$k.txt" 2>&1 ); cat "$RES/windows-$k.txt"
done
sec "digests (the machine at L2's all-READY, every configuration)"
n=$(grep -E "^[^ ]+ [^ ]+ sort " "$RES/raw.txt" | grep -o 'digest=[0-9a-f]*' | sort -u | wc -l)
[ "$n" = 1 ] && verdict "digests" PASS || verdict "digests: $n distinct" FAIL
sec "summary: us per cycle (median of the in-process machines), x one thread, seconds over the span; timed breakdown per cycle"
for k in $KITS; do
  b=$(grep "^$k one " "$RES/raw.txt" | grep -o 'us_per_cycle=[0-9.]*' | cut -d= -f2)
  echo "$k one thread: $b us/cycle, $(grep "^$k one " "$RES/raw.txt" | grep -o 'secs=[0-9.]*') s, $(grep "^$k one " "$RES/raw.txt" | grep -o 'rss_mb=[0-9.]* sram_resident_mb=[0-9.]*')"
  for t in $TS; do for p in tiles rows load profile; do
    l=$(grep "^$k t$t-$p " "$RES/raw.txt"); [ -z "$l" ] && continue
    u=$(echo "$l" | grep -o 'us_per_cycle=[0-9.]*' | cut -d= -f2)
    lt=$(grep "^$k t$t-$p-timed " "$RES/raw.txt"); ut=$(echo "$lt" | grep -o 'us_per_cycle=[0-9.]*' | cut -d= -f2)
    br=""; for f in compute imbalance barrier_wait drain publish clear; do v=$(echo "$lt" | grep -o "$f=[0-9.-]*" | cut -d= -f2); br+="$f=$(awk -v a=$v -v u=$ut 'BEGIN { printf "%.3f", a * u }') "; done
    printf "%s t%-2s %-8s us=%-7s x%-5s secs=%-6s [%s] %s\n" "$k" "$t" "$p" "$u" "$(awk -v a=$b -v c=$u 'BEGIN { printf "%.2f", a / c }')" \
      "$(echo "$l" | grep -o 'secs=[0-9.]*' | cut -d= -f2)" "$br" "$(echo "$l" | grep -o 'load max/mean [0-9.]*')"
  done; done
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
