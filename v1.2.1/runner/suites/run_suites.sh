#!/usr/bin/env bash
# run_suites.sh -- job 2 for the runner: meshsim v1.1's full-size suites that don't fit the simulator session's turns,
# from the prebuilt classes and tile images in meshsim/ (no RISC-V toolchain needed). Java 21 (the classes' target).
#
#   ./run_suites.sh [-j PARALLEL] [-m HEAP] [-l SUITES] OUTDIR
#     -j  suites at a time (default 4; they are independent)     -m  heap per JVM (default 4g)
#     -l  a comma-separated subset of: ctrl-full,engine-full,skewtable,sweep-full (default all four);
#         smoke (BusTest, seconds) checks the setup
# Writes OUTDIR/<suite>.log and OUTDIR/VERDICT.txt. Exit status 0 only if every selected suite exited 0 and its log's
# last line reads "... N passed, 0 failed" (sweep-full: exits 0 and prints its table; it has no checks).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
P=4; HEAP=4g; SEL=ctrl-full,engine-full,skewtable,sweep-full
while getopts j:m:l: o; do case $o in j) P=$OPTARG;; m) HEAP=$OPTARG;; l) SEL=$OPTARG;; *) sed -n '2,11p' "$0"; exit 2;; esac; done
shift $((OPTIND - 1)); OUT=${1:?usage: run_suites.sh [-j N] [-m HEAP] [-l SUITES] OUTDIR}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
cd "$HERE/meshsim"; mkdir -p results
for f in data/one99.txt data/trace.txt data/zipfraw.txt sw/build/a2a.code.bin; do [ -f "$f" ] || { echo "incomplete package: meshsim/$f missing"; exit 2; }; done
java -version 2>&1 | head -1
cmd() {
  case $1 in
    ctrl-full)   echo "CtrlTest -1 full" ;;      # the all-to-all from C on the four data sets, at full size
    engine-full) echo "EngineTest -1 full" ;;    # full-size all-to-alls (4 data sets) and the radix sort
    skewtable)   echo "NetTest -1 skewtable 40" ;;   # fan-in under the 0/+1 start skew, 40 skew patterns
    sweep-full)  echo "Sweep full" ;;            # the parameter sweep over the hardware suites, full
    smoke)       echo "BusTest" ;;               # a few seconds: checks the setup
    *) echo "" ;;
  esac
}
for s in $(echo "$SEL" | tr ',' ' '); do [ -n "$(cmd "$s")" ] || { echo "unknown suite: $s"; exit 2; }; done
T0=$(date +%s)
for s in $(echo "$SEL" | tr ',' ' '); do
  c=$(cmd "$s"); [ -n "$c" ] || { echo "unknown suite $s"; exit 2; }
  printf '%s\t%s\n' "$s" "$c"
done | xargs -P "$P" -L 1 sh -c 'S=$(date +%s); java -Xmx'"$HEAP"' -cp build "$@" > "'"$OUT"'/$0.log" 2>&1; echo "$? $(( $(date +%s) - S ))" > "'"$OUT"'/$0.rc"; echo "$(date +%H:%M:%S) $0: exit $(cut -d" " -f1 "'"$OUT"'/$0.rc"), $(cut -d" " -f2 "'"$OUT"'/$0.rc") s; $(tail -1 "'"$OUT"'/$0.log" | cut -c1-120)"' 
cp results/sweep-full.csv "$OUT/" 2>/dev/null || true
N=0; BAD=0; : > "$OUT/VERDICT.txt"
for s in $(echo "$SEL" | tr ',' ' '); do
  N=$((N + 1)); rc=$(cut -d' ' -f1 "$OUT/$s.rc" 2>/dev/null || echo missing); last=$(tail -1 "$OUT/$s.log" 2>/dev/null)
  if [ "$rc" != 0 ]; then echo "FAIL $s: exit $rc; $last" >> "$OUT/VERDICT.txt"; BAD=$((BAD + 1))
  elif [ "$s" != sweep-full ] && ! printf '%s' "$last" | grep -qE 'passed, 0 failed'; then echo "FAIL $s: $last" >> "$OUT/VERDICT.txt"; BAD=$((BAD + 1))
  else echo "ok   $s: $last" >> "$OUT/VERDICT.txt"; fi
done
echo "== $N suites, $BAD failed, $(( $(date +%s) - T0 )) s" | tee -a "$OUT/VERDICT.txt"
[ "$BAD" = 0 ] && [ "$N" -gt 0 ]
