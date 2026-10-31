#!/usr/bin/env bash
# run_p4.sh -- job 1 for the runner: the sort's 43 full-size P4 runs (64 x 65,536 keys, N = 4M) on meshsim v1.2 (the sort port, WORKER8)
# (spec v1.2), with the prebuilt tile image in kit/. Java 17+ only.
#
#   ./run_p4.sh [-j PARALLEL] [-m HEAP] [-l RUNLIST] OUTDIR
#     -j  runs at a time (default 8)       -m  heap per JVM (default 3g)
#     -l  a file of run ids, one per line (default: all 43 p4 runs of kit/jobs.txt; e.g. p4/uniform-k128)
#     -n  keys per tile (default 65536; -n 2000 for a smoke test of a few minutes)
# Writes OUTDIR/<id>.log, <id>.csv, <id>.phases.csv per run, OUTDIR/SUMMARY.md and all.csv (SortSummary), and
# OUTDIR/VERDICT.txt. Exit status 0 only if every selected run finished, passed all its checks (the sorted output,
# every value with its key, counters, invariants) and read inputs matching kit/HASHES.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
J=8; HEAP=3g; LIST=; N=65536
while getopts j:m:l:n: o; do case $o in j) J=$OPTARG;; m) HEAP=$OPTARG;; l) LIST=$OPTARG;; n) N=$OPTARG;; *) sed -n '2,11p' "$0"; exit 2;; esac; done
shift $((OPTIND - 1)); OUT=${1:?usage: run_p4.sh [-j N] [-m HEAP] [-l RUNLIST] OUTDIR}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
if [ -n "$LIST" ]; then [ -f "$LIST" ] || { echo "no run list $LIST"; exit 2; }; LIST=$(cd "$(dirname "$LIST")" && pwd)/$(basename "$LIST"); fi
cd "$HERE/kit"
JOBS=jobs.txt
if [ -n "$LIST" ]; then                      # restrict jobs.txt to the listed ids (keeping its longest-first order)
  awk -F'\t' 'NR == FNR { if ($1 != "" && $1 !~ /^#/) want[$1] = 1; next } /^#/ || ($3 in want)' "$LIST" jobs.txt > "$OUT/.jobs"
  JOBS="$OUT/.jobs"
  grep -q . "$JOBS" || { echo "no p4 run in jobs.txt matches $LIST"; exit 2; }
fi
T0=$(date +%s)
./run.sh -j "$J" -m "$HEAP" -n "$N" -o "$OUT" -g p4 -J "$JOBS"
# the verdict: every selected run has its CSV, its log says "0 failed", and its inputs match HASHES
SEL=$(grep -v '^#' "$JOBS" | awk -F'\t' '$2 == "p4" { print $3 }')
N=0; BAD=0; : > "$OUT/VERDICT.txt"
for id in $SEL; do
  N=$((N + 1)); f=$(printf '%s' "$id" | tr '/' '_')
  if [ ! -f "$OUT/$f.csv" ]; then echo "FAIL $id: no CSV (did not finish)" >> "$OUT/VERDICT.txt"; BAD=$((BAD + 1)); continue; fi
  if ! grep -qE 'SortTest.*passed, 0 failed' "$OUT/$f.log"; then echo "FAIL $id: $(grep -E 'SortTest.*passed' "$OUT/$f.log" | tail -1)" >> "$OUT/VERDICT.txt"; BAD=$((BAD + 1)); continue; fi
  echo "ok   $id: $(grep -E 'TOTAL' "$OUT/$f.log" | head -1 | sed 's/^ *//' | cut -c1-60)" >> "$OUT/VERDICT.txt"
done
if grep -qE 'differ from the reference set|different kits' "$OUT/SUMMARY.md" 2>/dev/null; then
  echo "FAIL inputs: SUMMARY.md's Inputs section flags runs whose files differ from HASHES" >> "$OUT/VERDICT.txt"; BAD=$((BAD + 1))
fi
echo "== $N runs, $BAD failed, $(( $(date +%s) - T0 )) s" | tee -a "$OUT/VERDICT.txt"
[ "$BAD" = 0 ] && [ "$N" -gt 0 ]
