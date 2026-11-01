#!/usr/bin/env bash
# run.sh -- meshsim v4.0 M12's verification against M11 (worker 13, 9 October 2026; worker 12's M11 kit adapted): the two
# full-size BT runs, SplitRun on the split's 25 sets and the sort kit's 92 runs at 64 x 2,000, each on M11 and on M12,
# one independent JVM per run, then the comparisons (the kit and BT phase by phase: VerifySummary, PhaseNeutral).
#
#   ./run.sh [-j JOBS] [-m HEAP] [-o OUTDIR] [-g GROUPS] [-i IMPLS] [-f FILTER] [-n KEYS_PER_TILE] [-s]
#     -j  parallel runs (default 12)                 -m  heap per JVM (default 3g)
#     -o  results directory, relative to this one (default results)
#     -g  groups: any of split,kit,bt (default all)  -i  versions: any of m11,m12 (default both)
#         bt: the full-size BT runs (64 x 65,536, trace hot=10 frac=0.10, 32-bit and 128-bit values; DEV 115)
#     -f  only the runs whose id contains FILTER (e.g. -f split/0 or -f p4/trace)
#     -n  keys per tile for the kit runs (default 2000)
#     -s  the summary only
# Restartable: a finished run (a split log, or a kit CSV) is skipped. At the end: each kit's SortSummary, SortCompare
# (OUT/COMPARE-kit.md, COMPARE-bt.md) and VerifySummary (OUT/VERIFY.md, PHASES-kit.md, PHASES-bt.md). Upload the whole results directory.
set -e
cd "$(dirname "$0")"
J=12; HEAP=3g; OUT=results; SEL=split,kit,bt; IMPLS=m11,m12; FILTER=; N=2000; SUMMARY_ONLY=
while getopts j:m:o:g:i:f:n:s opt; do
  case $opt in j) J=$OPTARG;; m) HEAP=$OPTARG;; o) OUT=$OPTARG;; g) SEL=$OPTARG;; i) IMPLS=$OPTARG;; f) FILTER=$OPTARG;; n) N=$OPTARG;;
    s) SUMMARY_ONLY=1;; *) sed -n '2,15p' "$0"; exit 1;; esac
done
case "$OUT" in /*) echo "-o must be relative to $(pwd) (native Windows Java does not read Cygwin paths)"; exit 1;; esac
has() { case ",$1," in *",$2,"*) return 0;; esac; return 1; }
mkdir -p "$OUT"
if [ -z "$SUMMARY_ONLY" ]; then
  java -version 2>&1 | head -1
  # 1. data: the split's 25 sets (SplitSets: export_sets.py ported to Java, byte-identical; 346 MB) and each kit's key sets
  if has "$SEL" split; then
    NEED=$(for i in $(seq 0 24); do f=$(printf '%02d' $i); [ -f sets-65536/$f.bin ] || echo $f; done | paste -sd, -)
    if [ -n "$NEED" ]; then echo "== generating split sets into sets-65536: $NEED"; java -Xmx3g -cp m12/split/split.jar SplitSets sets-65536 --only "$NEED"; fi
    java -cp m12/split/split.jar Inputs check HASHES-SPLIT || echo "== continuing; VERIFY.md will flag the split runs"
  fi
  BT_KEYS=trace10-10
  if has "$SEL" bt; then
    for im in $(echo "$IMPLS" | tr ',' ' '); do
      [ -f $im/keys-65536/$BT_KEYS.keys ] || { echo "== generating the BT runs' key set into $im/keys-65536"; (cd $im && java -Xmx2g -cp meshsim.jar KeyGen keys-65536 --n 65536 --only $BT_KEYS > /dev/null); }
    done
  fi
  if has "$SEL" kit; then
    for im in $(echo "$IMPLS" | tr ',' ' '); do
      if [ "$(ls $im/keys-$N/*.keys 2>/dev/null | wc -l)" -lt 20 ]; then echo "== generating key sets into $im/keys-$N"; (cd $im && java -Xmx2g -cp meshsim.jar KeyGen keys-$N --n $N > /dev/null); fi
      (cd $im && java -cp meshsim.jar Inputs check HASHES) || echo "== continuing; $im's SUMMARY.md will flag the runs"
    done
  fi
  # 2. the queue: the BT runs, then the split runs, M11 and M12 interleaved; then the kit runs in jobs.txt's order (longest first)
  Q="$OUT/.queue"; : > "$Q"
  # the BT runs first (lead, 9 October: the laptop's first runs; full size, each about as long as a split run or more), M11 and M12 interleaved
  if has "$SEL" bt; then
    for id in p4/trace10-10-k128 p4/trace10-10-kv32; do for im in $(echo "$IMPLS" | tr ',' ' '); do
      f=$(printf '%s' "$id" | tr '/' '_'); [ -f "$OUT/$im/bt/$f.csv" ] && continue
      line=$(awk -F'\t' -v id=$id '$3 == id' $im/jobs.txt | sed "s|@KEYS@|keys-65536|g; s|@NH@|32768|g; s|@N@|65536|g")
      [ -n "$line" ] && printf '%s\tbt\t%s\t%s\n' "$im" "$id" "$line" >> "$Q"
    done; done
  fi
  # then the split runs (the long ones)
  if has "$SEL" split; then
    for i in $(seq 0 24); do f=$(printf '%02d' $i); for im in $(echo "$IMPLS" | tr ',' ' '); do
      [ -f "$OUT/$im/split/$f.log" ] && continue
      printf '%s\tsplit\tsplit/%s\t%s\n' "$im" "$f" "$f" >> "$Q"
    done; done
  fi
  if has "$SEL" kit; then
    for im in $(echo "$IMPLS" | tr ',' ' '); do
      grep -v '^#' $im/jobs.txt | sed "s|@KEYS@|keys-$N|g; s|@NH@|$((N / 2))|g; s|@N@|$N|g" | while IFS= read -r line; do
        id=$(printf '%s' "$line" | cut -f3); f=$(printf '%s' "$id" | tr '/' '_')
        [ -f "$OUT/$im/kit/$f.csv" ] && continue
        printf '%s\tkit\t%s\t%s\n' "$im" "$id" "$line"
      done
    done > "$Q.kit"
    # interleave the two versions' kit runs so both progress together
    if [ -s "$Q.kit" ]; then awk -F'\t' '{ n[$1]++; print n[$1] "\t" $0 }' "$Q.kit" | sort -s -n -k1,1 | cut -f2- >> "$Q"; fi
    rm -f "$Q.kit"
  fi
  [ -n "$FILTER" ] && { awk -F'\t' -v f="$FILTER" 'index($3, f)' "$Q" > "$Q.f"; mv "$Q.f" "$Q"; }
  NT=$(grep -c . "$Q" || true)
  echo "== $NT runs to do, $J at a time, heap $HEAP each; results in $OUT/"
  if [ "$NT" -gt 0 ]; then tr '\n' '\0' < "$Q" | xargs -0 -P "$J" -I{} tools/runjob.sh "$OUT" "$HEAP" "{}"; fi
fi
# 3. the summaries
for im in m11 m12; do
  if [ -d "$OUT/$im/kit" ]; then (cd $im && java -cp meshsim.jar SortSummary "../$OUT/$im/kit" > /dev/null) && echo "== $OUT/$im/kit/SUMMARY.md written"; fi
done
if [ -d "$OUT/m11/bt" ] && [ -d "$OUT/m12/bt" ]; then java -cp m12/meshsim.jar SortCompare "$OUT/m12/bt" "$OUT/m11/bt" "$OUT/COMPARE-bt.md" > /dev/null && echo "== $OUT/COMPARE-bt.md written"; fi
if [ -d "$OUT/m11/kit" ] && [ -d "$OUT/m12/kit" ]; then java -cp m12/meshsim.jar SortCompare "$OUT/m12/kit" "$OUT/m11/kit" "$OUT/COMPARE-kit.md" > /dev/null && echo "== $OUT/COMPARE-kit.md written"; fi
java -cp m12/split/split.jar VerifySummary "$OUT" HASHES-SPLIT
