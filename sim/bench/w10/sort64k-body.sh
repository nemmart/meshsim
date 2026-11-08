# Task body (WORKER10): the full-size sort's timing, the number M2 is judged by (the owner's ~4 s aim). bench_sort over
# M2Sort's counted span (L1's GO 14,838 to L2's all-READY 1,244,525: 1,229,687 cycles; setup excluded as W7's
# baseline), p4 uniform 64 x 65536 keys (KeyGen, built here; sha256 against HASHES). One thread, and split by tiles
# over 4, 6 and 12 threads; in-process repeats (an untimed machine, then REPS timed) in PROCS processes each; one
# --time run at 6 threads for the breakdown. Against W7's ref baseline (Quest 134): 95.80-96.01 s.
REPS=${REPS:-2}; PROCS=${PROCS:-3}
SPAN="--span 14838 1244525"
sec "the keys (KeyGen, 64 x 65536)"
mkdir -p "$W/cls" "$W/keys"
javac -nowarn -d "$W/cls" "$W/kit/after/sim/ref/meshsim/test/KeyGen.java" 2>&1 | grep -v '^Note:'
( cd "$W/keys" && java -Xmx2g -cp "$W/cls" KeyGen "$W/keys/k64" --n 65536 --only uniform > /dev/null ) || { verdict "KeyGen" FAIL; exit 1; }
KEYS=$W/keys/k64/uniform.keys
h=$(sha256sum < "$KEYS" | cut -c1-64); want=$(awk '$2 == "keys-65536/uniform.keys" {print $1}' "$W/kit/after/sim/tests/golden-m2/HASHES")
[ "$h" = "$want" ] && verdict "keys sha256 as HASHES" PASS || { verdict "keys sha256 as HASHES ($h)" FAIL; exit 1; }
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_sort > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
P1=$(take 1 "$P_ONE")
run() {
  local tag=$1 cpus=$2; shift 2
  taskset -c "$cpus" "$@" > "$W/out.txt" 2> "$W/err.txt"; local rc=$?
  local line; line=$(grep -E '^sort ' "$W/out.txt" | tail -1)
  if [ $rc -ne 0 ] || [ -z "$line" ]; then echo "RUNFAIL $tag rc=$rc: $*"; head -5 "$W/err.txt"; FAILS=$((FAILS + 1)); return 1; fi
  echo "$tag $line" | tee -a "$RES/raw.txt"
}
srun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_sort --keys "$KEYS" $SPAN "$@" ); }
for p in $(seq $PROCS); do for k in $KITS; do
  sec "process $p ($k)"
  srun "$k one" "$P1" $k --threads 1 --cpus "$P1" --reps $REPS
  for t in 4 6 12; do C=$(take $t "$PLACE"); TSET=$P_CPUS; [ $t -gt $(count "$P_CPUS") ] && TSET=$ALL_CPUS
    srun "$k t$t" "$TSET" $k --threads $t --partition tiles --cpus "$C" --reps $((REPS + 1))
  done
done; done
for k in $KITS; do srun "$k t6-timed" "$P_CPUS" $k --threads 6 --partition tiles --cpus "$(take 6 "$PLACE")" --reps 1 --time; done
sec "digests (the machine at L2's all-READY, every configuration)"
n=$(grep -o 'digest=[0-9a-f]*' "$RES/raw.txt" | sort -u | wc -l)
[ "$n" = 1 ] && verdict "digests" PASS || verdict "digests: $n distinct" FAIL
sec "summary: seconds over the counted span (the median machine of each process; min-max over all), us per cycle, x one thread, x W7's ref (95.9 s)"
for k in $KITS; do
  b=$(grep "^$k one " "$RES/raw.txt" | grep -o 'secs=[0-9.]*' | cut -d= -f2 | sort -g | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
  for tag in one t4 t6 t12; do
    s=$(grep "^$k $tag " "$RES/raw.txt" | grep -o 'secs=[0-9.]*' | cut -d= -f2 | sort -g | paste -sd' ')
    m=$(echo $s | tr ' ' '\n' | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    u=$(grep "^$k $tag " "$RES/raw.txt" | grep -o 'us_per_cycle=[0-9.]*' | cut -d= -f2 | sort -g | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    printf "%s %-4s secs=%-7s (processes: %s) us_per_cycle=%-7s x_one=%-5s x_ref=%s\n" "$k" "$tag" "$m" "$s" "$u" \
      "$(awk -v a=$b -v c=$m 'BEGIN { printf "%.2f", a / c }')" "$(awk -v c=$m 'BEGIN { printf "%.1f", 95.9 / c }')"
  done
  lt=$(grep "^$k t6-timed " "$RES/raw.txt"); ut=$(echo "$lt" | grep -o 'us_per_cycle=[0-9.]*' | cut -d= -f2)
  br=""; for f in compute imbalance barrier_wait drain publish clear; do v=$(echo "$lt" | grep -o "$f=[0-9.-]*" | cut -d= -f2); br+="$f=$(awk -v a=$v -v u=$ut 'BEGIN { printf "%.3f", a * u }') "; done
  echo "$k t6 timed: us_per_cycle=$ut [$br]"
  echo "$k memory: $(grep "^$k one " "$RES/raw.txt" | head -1 | grep -o 'rss_mb=[0-9.]* sram_resident_mb=[0-9.]*')"
done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
