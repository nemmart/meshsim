#!/usr/bin/env bash
# run_shard.sh -- the acceptance kit (v1.35, WORKER25: v1.3's acceptance/run_shard.sh (meshsim WORKER11; v1.26,
# WORKER20; v1.26.1, WORKER25) on the C++ meshsim): one shard of a program's windows, read from the program's delivery
# where the writer task left it on the runner box, replayed in Stage A and Stage B by JOBS processes (v1.3: JVMs). The
# delivery is only read: results go to OUTDIR. Needs the kit built (./build.sh: clang, make); no Java, no Python.
#
#   ./run_shard.sh -d DELIVERY -p PROGRAM -f FIRST -n COUNT [-j JOBS] [-s AB] [-m MANIFEST_SHA256] OUTDIR
#     -d  the delivery's directory: PROGRAM/window_NNNNN.*, and the manifest as manifest.json or PROGRAM.manifest.json
#         (WORKER9's delivery_writer: /tmp/w9-deliveries with PROGRAM.manifest.json beside PROGRAM/); tables/ optional
#     -f, -n  window indices FIRST..FIRST+COUNT-1, or -n all: every window the manifest lists for PROGRAM (a sparse
#             set, e.g. /tmp/w9-deliveries-selected)     -j  processes in parallel (default nproc)
#     -s  stages (default AB): A the model, B the controller on tile 0 alone (the 63 idle tiles parked), F the same on
#         the full chip; with B and F both, each window's cycles must agree
#     -m  the manifest's SHA-256, as WORKER9's writer task reported it: checked before anything is read
#     -M  the manifest file (default DELIVERY/manifest.json, else DELIVERY/PROGRAM.manifest.json). The per-program one is
#         stable while a later writer task rewrites the combined manifest.json
#     -x  K: also stage F (the full chip) on every window whose index is a multiple of K, its cycles checked against B
#     -q  512: the 8 KB-log control (v1.25's read-log and write-log queues, replay-8k's controller), for the stall
#         comparison (V3-12); default 256, v1.26's 4 KB
#     -g  N: Stage B fails a window, as starved, when a running pass delivers no row for N cycles (default 50000; CL4).
#         The largest normal gap measured is ~1,100 cycles (synthetic windows); each window's is in results.csv
# Every file a window's replay reads is checked against the manifest's SHA-256 first (replay_shard). Writes
# OUTDIR/results.csv (one line per window and stage), OUTDIR/jvm-*.log (v1.3's names: one per process), SUMMARY.txt
# and VERDICT.txt (shard_summary: v1.3's inline Python in C++). Exit 0 only if every window of the range is in the
# manifest and matched in every stage.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/sim/build/clang-release
D=; P=; F=; N=; J=$(nproc); S=AB; MS=; X=0; MF=; G=50000; Q=256
while getopts d:p:f:n:j:s:m:x:M:g:q: o; do case $o in d) D=$OPTARG;; p) P=$OPTARG;; f) F=$OPTARG;; n) N=$OPTARG;; j) J=$OPTARG;;
  s) S=$OPTARG;; m) MS=$OPTARG;; x) X=$OPTARG;; M) MF=$OPTARG;; g) G=$OPTARG;; q) Q=$OPTARG;; *) sed -n '7,27p' "$0"; exit 2;; esac; done
RB=$HERE/sim/tests/m3-images; [ "$Q" = 256 ] || RB=$HERE/sim/fw/images/log8k
shift $((OPTIND - 1)); OUT=${1:-}; [ "$N" = all ] && F=${F:-0}
[ -n "$D" ] && [ -n "$P" ] && [ -n "$F" ] && [ -n "$N" ] && [ -n "$OUT" ] || { sed -n '7,19p' "$0"; exit 2; }
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
SHARD="$BIN/replay_shard"; OPTS=(--norow "$G" --logq "$Q" --tables "$HERE/sim/data/tables/replayer" --replay-build "$RB")
if [ "$N" = all ]; then F=0; N=all; RANGE=""; else RANGE="--first $F --count $N"; fi
T0=$(date +%s)
echo "shard: $P windows ${RANGE:-all} of $D, stages $S, $J processes, no-row limit $G cycles, log queues $Q entries ($(basename "$RB")), start $(date -u)"
for ((k = 0; k < J; k++)); do
  (cd "$HERE/sim" && "$SHARD" "$D" "$P" "$OUT/part-$k.csv" --manifest "$MF" --stages "$S" $RANGE --stride "$J" --phase "$k" --full-every "$X" "${OPTS[@]}" > "$OUT/jvm-$k.log" 2>&1) &
done
wait
cat "$OUT"/part-*.csv >> "$OUT/results.csv" 2>/dev/null; rm -f "$OUT"/part-*.csv
gzip -f "$OUT"/jvm-*.log 2>/dev/null                    # a line per window and stage: keep the committed results small
"$BIN/shard_summary" "$OUT" "$P" "$F" "$N" "$S" "$(( $(date +%s) - T0 ))" "$J" "$X" "$MF"
RC=$?
cat "$OUT/SUMMARY.txt" "$OUT/VERDICT.txt"
exit $RC
