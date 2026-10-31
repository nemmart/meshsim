#!/usr/bin/env bash
# run.sh -- the sort's full-size measurements (P4), the shuffled-order check and the whole-sort sweep, on meshsim v1.1
# (spec 2026-10-26n) with the sort program's prebuilt tile images (mesh.h 1.1). One independent JVM per run; longest runs first.
#
#   ./run.sh [-j JOBS] [-m HEAP] [-o OUTDIR] [-n KEYS_PER_TILE] [-g GROUPS] [-f FILTER] [-J JOBFILE] [-s]
#     -j  parallel runs (default 12)                   -m  heap per JVM (default 3g)
#     -o  results directory (default results)          -n  keys per tile (default 65536; 2000 for a smoke test)
#     -g  groups: any of p4,shuffle,sweep (default all three)
#     -f  only the runs whose id contains FILTER (e.g. -f p4/trace)    -J  another job file (default jobs.txt)
# Each run writes OUTDIR/<id>.csv (one row per step and a TOTAL row), <id>.phases.csv (every C&C phase: cycles,
# instructions, peaks) and <id>.log. A run whose CSV exists is skipped, so the script can be stopped and restarted.
# At the end, SortSummary writes OUTDIR/SUMMARY.md and OUTDIR/all.csv (also: ./run.sh -s to summarize only).
# Each log starts with the run's inputs line (SHA-256 of jar, image, hops.txt, keys); SUMMARY.md's Inputs section flags
# runs whose files differ from HASHES or from each other.
set -e
cd "$(dirname "$0")"
J=12; HEAP=3g; OUT=results; N=65536; SEL=p4,shuffle,sweep; FILTER=; SUMMARY_ONLY=; JOBS=jobs.txt
while getopts j:m:o:n:g:f:J:s opt; do
  case $opt in j) J=$OPTARG;; m) HEAP=$OPTARG;; o) OUT=$OPTARG;; n) N=$OPTARG;; g) SEL=$OPTARG;; f) FILTER=$OPTARG;; J) JOBS=$OPTARG;; s) SUMMARY_ONLY=1;;
    *) sed -n '2,12p' "$0"; exit 1;; esac
done
mkdir -p "$OUT"
if [ -z "$SUMMARY_ONLY" ]; then
  java -version 2>&1 | head -1
  KEYS=keys-$N
  # 1. the job list, filtered, longest first (jobs.txt is already in that order), without the finished ones
  grep -v '^#' $JOBS | awk -F'\t' -v g=",$SEL," -v f="$FILTER" 'index(g, "," $2 ",") && (f == "" || index($3, f))' > "$OUT/.queue"
  # 2. the key sets the selected runs need (KeyGen: design v1's data.py ported to Java, bit-exact; 16 MB each), once
  NEED=$(cut -f5 "$OUT/.queue" | grep -o '@KEYS@/[a-z0-9-]*\.keys' | sed 's|@KEYS@/||; s|\.keys||' | sort -u | while read s; do [ -f $KEYS/$s.keys ] || echo $s; done | paste -sd, -)
  if [ -n "$NEED" ]; then echo "== generating key sets into $KEYS: $NEED"; java -Xmx2g -cp meshsim.jar KeyGen $KEYS --n $N --only "$NEED"; fi
  # 3. the kit's files and the key sets present against HASHES, the reference set: a warning, not a stop
  java -cp meshsim.jar Inputs check HASHES || echo "== continuing; SUMMARY.md will flag the runs (Inputs)"
  TODO=$(awk -F'\t' -v o="$OUT" '{ id = $3; gsub("/", "_", id); if (system("test -f " o "/" id ".csv") != 0) print }' "$OUT/.queue")
  NT=$(printf '%s\n' "$TODO" | grep -c . || true)
  echo "== $NT runs to do ($(grep -c . "$OUT/.queue") selected), $J at a time, heap $HEAP each; logs in $OUT/"
  if [ "$NT" -gt 0 ]; then
    printf '%s\n' "$TODO" | sed "s|@KEYS@|$KEYS|g; s|@NH@|$((N / 2))|g; s|@N@|$N|g" | \
      tr '\n' '\0' | xargs -0 -P "$J" -I{} tools/runjob.sh "$OUT" "$HEAP" "{}"
  fi
fi
java -cp meshsim.jar SortSummary "$OUT"
