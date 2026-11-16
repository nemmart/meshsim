#!/usr/bin/env bash
# run_shard.sh -- the acceptance kit (v1.35, v1.3's acceptance/run_shard.sh
# on the C++ meshsim): one shard of a program's windows, read from the program's delivery
# where the writer task left it on the runner box, replayed in Stage A by JOBS processes (v1.3: JVMs). The
# delivery is only read: results go to OUTDIR. Needs the kit built (./build.sh: clang, make); no Java, no Python.
#
#   ./run_shard.sh -d DELIVERY -p PROGRAM -f FIRST -n COUNT [-j JOBS] [-m MANIFEST_SHA256] OUTDIR
#     -d  the delivery's directory: PROGRAM/window_NNNNN.*, and the manifest as manifest.json or PROGRAM.manifest.json
#         (the delivery_writer: /tmp/w9-deliveries with PROGRAM.manifest.json beside PROGRAM/); tables/ optional
#     -f, -n  window indices FIRST..FIRST+COUNT-1, or -n all: every window the manifest lists for PROGRAM (a sparse
#             set, e.g. /tmp/w9-deliveries-selected)     -j  processes in parallel (default nproc)
#     -m  the manifest's SHA-256, as the writer task reported it: checked before anything is read
#     -M  the manifest file (default DELIVERY/manifest.json, else DELIVERY/PROGRAM.manifest.json). The per-program one is
#         stable while a later writer task rewrites the combined manifest.json
#     -q  the log queues' entries: 256 only in v1.36 (REPLAY_QUEUE_ENTRIES; the 512-entry control was v1.35's model's,
#         and the replayer refuses another size)
# The stage is A, the replayer model under the ideal controller, its cycles against the RTL's where the kit holds the
# program's reference (sim/tests/acceptance/rtl). v1.3's stages B and F (-s, -x, -g: v1.35's replay controller on
# v1.35's tile) are retired: the segment on the chip is tests/test_segment.cpp.
# Every file a window's replay reads is checked against the manifest's SHA-256 first (replay_shard). Writes
# OUTDIR/results.csv (one line per window), OUTDIR/jvm-*.log (v1.3's names: one per process), SUMMARY.txt
# and VERDICT.txt (shard_summary: v1.3's inline Python in C++). Exit 0 only if every window of the range is in the
# manifest and matched.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/sim/build/clang-suite
D=; P=; F=; N=; J=$(nproc); MS=; MF=; Q=256
while getopts d:p:f:n:j:m:M:q: o; do case $o in d) D=$OPTARG;; p) P=$OPTARG;; f) F=$OPTARG;; n) N=$OPTARG;; j) J=$OPTARG;;
  m) MS=$OPTARG;; M) MF=$OPTARG;; q) Q=$OPTARG;; *) sed -n '7,23p' "$0"; exit 2;; esac; done
shift $((OPTIND - 1)); OUT=${1:-}; [ "$N" = all ] && F=${F:-0}
[ -n "$D" ] && [ -n "$P" ] && [ -n "$F" ] && [ -n "$N" ] && [ -n "$OUT" ] || { sed -n '7,16p' "$0"; exit 2; }
[ -x "$BIN/replay_shard" ] && [ -x "$BIN/shard_summary" ] || { echo "the kit isn't built: run $HERE/build.sh"; exit 2; }
D=$(cd "$D" && pwd) || { echo "no delivery at $D"; exit 2; }
[ -n "$MF" ] || { MF="$D/manifest.json"; [ -f "$MF" ] || MF="$D/$P.manifest.json"; }
[ -f "$MF" ] || { echo "no manifest.json or $P.manifest.json in $D"; exit 2; }
[ -d "$D/$P" ] || { echo "no $D/$P"; exit 2; }
if [ -n "$MS" ]; then
  GOT=$(sha256sum "$MF" | cut -d' ' -f1)
  [ "$GOT" = "$MS" ] || { echo "manifest.json sha256 $GOT, expected $MS"; exit 2; }
fi
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd); : > "$OUT/results.csv"
SHARD="$BIN/replay_shard"; OPTS=(--logq "$Q" --tables "$HERE/sim/data/tables/replayer")
if [ "$N" = all ]; then F=0; N=all; RANGE=""; else RANGE="--first $F --count $N"; fi
T0=$(date +%s)
echo "shard: $P windows ${RANGE:-all} of $D, stage A, $J processes, log queues $Q entries, start $(date -u)"
for ((k = 0; k < J; k++)); do
  (cd "$HERE/sim" && "$SHARD" "$D" "$P" "$OUT/part-$k.csv" --manifest "$MF" $RANGE --stride "$J" --phase "$k" "${OPTS[@]}" > "$OUT/jvm-$k.log" 2>&1) &
done
wait
cat "$OUT"/part-*.csv >> "$OUT/results.csv" 2>/dev/null; rm -f "$OUT"/part-*.csv
gzip -f "$OUT"/jvm-*.log 2>/dev/null                    # a line per window: keep the committed results small
"$BIN/shard_summary" "$OUT" "$P" "$F" "$N" A "$(( $(date +%s) - T0 ))" "$J" 0 "$MF"
RC=$?
cat "$OUT/SUMMARY.txt" "$OUT/VERDICT.txt"
exit $RC
