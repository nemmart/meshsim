#!/usr/bin/env bash
# run_shard.sh -- the acceptance kit (meshsim WORKER11; v1.26, WORKER20: 68-word records, 4 KB queues): one shard of a program's windows, read from the program's
# delivery where WORKER9's writer task left it on the runner box (DECISIONS-v1.25 s7, revised 2026-11-01), replayed in
# Stage A and Stage B by JOBS JVMs. The delivery is only read: results go to OUTDIR. Java 17+; nothing else.
#
#   ./run_shard.sh -d DELIVERY -p PROGRAM -f FIRST -n COUNT [-j JOBS] [-s AB] [-m MANIFEST_SHA256] OUTDIR
#     -d  the delivery's directory: PROGRAM/window_NNNNN.*, and the manifest as manifest.json or PROGRAM.manifest.json
#         (WORKER9's delivery_writer: /tmp/w9-deliveries with PROGRAM.manifest.json beside PROGRAM/); tables/ optional
#     -f, -n  window indices FIRST..FIRST+COUNT-1, or -n all: every window the manifest lists for PROGRAM (a sparse
#             set, e.g. /tmp/w9-deliveries-selected)     -j  JVMs in parallel (default nproc)
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
# Every file a window's replay reads is checked against the manifest's SHA-256 first (ReplayShard). Writes
# OUTDIR/results.csv (one line per window and stage), OUTDIR/jvm-*.log, SUMMARY.txt and VERDICT.txt. Exit 0 only if
# every window of the range is in the manifest and matched in every stage.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
D=; P=; F=; N=; J=$(nproc); S=AB; MS=; X=0; MF=; G=50000; Q=256
while getopts d:p:f:n:j:s:m:x:M:g:q: o; do case $o in d) D=$OPTARG;; p) P=$OPTARG;; f) F=$OPTARG;; n) N=$OPTARG;; j) J=$OPTARG;;
  s) S=$OPTARG;; m) MS=$OPTARG;; x) X=$OPTARG;; M) MF=$OPTARG;; g) G=$OPTARG;; q) Q=$OPTARG;; *) sed -n '2,22p' "$0"; exit 2;; esac; done
RB=replay; [ "$Q" = 256 ] || RB=replay-8k
shift $((OPTIND - 1)); OUT=${1:-}; [ "$N" = all ] && F=${F:-0}
[ -n "$D" ] && [ -n "$P" ] && [ -n "$F" ] && [ -n "$N" ] && [ -n "$OUT" ] || { sed -n '2,14p' "$0"; exit 2; }
D=$(cd "$D" && pwd) || { echo "no delivery at $D"; exit 2; }
[ -n "$MF" ] || { MF="$D/manifest.json"; [ -f "$MF" ] || MF="$D/$P.manifest.json"; }
[ -f "$MF" ] || { echo "no manifest.json or $P.manifest.json in $D"; exit 2; }
[ -d "$D/$P" ] || { echo "no $D/$P"; exit 2; }
if [ -n "$MS" ]; then
  GOT=$(sha256sum "$MF" | cut -d' ' -f1)
  [ "$GOT" = "$MS" ] || { echo "manifest.json sha256 $GOT, expected $MS"; exit 2; }
fi
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd); : > "$OUT/results.csv"
JAVA="java -Xmx1500m -Dstageb.norow=$G -Dreplayer.logq=$Q -Dreplay.tables=$HERE/tables/replayer -Dreplay.build=$HERE/$RB -cp $HERE/meshsim.jar"
if [ "$N" = all ]; then F=0; N=all; RANGE=""; else RANGE="--first $F --count $N"; fi
T0=$(date +%s)
echo "shard: $P windows ${RANGE:-all} of $D, stages $S, $J JVMs, no-row limit $G cycles, log queues $Q entries ($RB), start $(date -u)"
for ((k = 0; k < J; k++)); do
  (cd "$HERE" && $JAVA ReplayShard "$D" "$P" "$OUT/part-$k.csv" --manifest "$MF" --stages "$S" $RANGE --stride "$J" --phase "$k" --full-every "$X" > "$OUT/jvm-$k.log" 2>&1) &
done
wait
cat "$OUT"/part-*.csv >> "$OUT/results.csv" 2>/dev/null; rm -f "$OUT"/part-*.csv
gzip -f "$OUT"/jvm-*.log 2>/dev/null                    # a line per window and stage: keep the committed results small
python3 - "$OUT" "$P" "$F" "$N" "$S" "$(( $(date +%s) - T0 ))" "$J" "$X" "$MF" <<'PY'
import csv, sys, json
out, prog, f, nn, stages, tot, j, x, mf = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5], int(sys.argv[6]), int(sys.argv[7]), int(sys.argv[8]), sys.argv[9]
if nn == "all":       # every window the manifest lists for the program
    want = {int(w["index"]) for p in json.load(open(mf))["programs"] if p["name"] == prog for w in p["window_entries"] if "files" in w}
    n = len(want); f = min(want) if want else 0
else:
    n = int(nn); want = set(range(f, f + n))
seen = {}; bad = []; per = {}; cyc = {}; waits = {}
for r in csv.reader(open(out + "/results.csv")):
    if len(r) < 9: bad.append(("?", "malformed line " + ",".join(r))); continue
    st = r[3].split(":")[0]
    if r[4] != "OK": bad.append((r[1], r[3] + " " + r[4] + " " + r[8][:300])); continue
    seen.setdefault(st, set()).add(int(r[1])); cyc[(st, int(r[1]))] = r[6]
    import re
    mw = re.search(r"waits image/code/newexp/exp/rlog/wlog/outq ([0-9/]+) logq (\d+)", r[8])
    if mw: waits.setdefault(st, []).append([int(v) for v in mw.group(1).split("/")] + [int(mw.group(2))])
    p = per.setdefault(st, [0, 0, 0]); p[0] += int(r[5]); p[1] += int(r[7]); p[2] += 1
for (st, w), c in list(cyc.items()):
    if st == "B" and ("F", w) in cyc and cyc[("F", w)] != c: bad.append((str(w), f"stage B (tile 0 alone) {c} cycles, stage F (full chip) {cyc[('F', w)]}"))
missing = {st: sorted(want - seen.get(st, set())) for st in stages}
if x > 0 and "F" not in stages:
    stages += "F"; missing["F"] = sorted({w for w in want if w % x == 0} - seen.get("F", set()))
with open(out + "/SUMMARY.txt", "w") as s:
    s.write(f"program {prog}, windows {f}..{f + n - 1} ({n}{'' if nn != 'all' else ', all the manifest lists'}), stages {stages}, {j} JVMs, wall {tot} s\n")
    for st, (rws, ms, k) in sorted(per.items()):
        s.write(f"stage {st}: {k} windows OK, {rws} rows, {ms / 1000:.1f} JVM-s, {rws / max(ms, 1) * 1000:.0f} rows per JVM-second, {ms / max(k, 1) / 1000:.2f} s per window\n")
    for st, ws in sorted(waits.items()):     # v1.26: the stall cycles per window (the queue-size check, V3-12)
        k = len(ws); names = ["image", "code", "newexp", "exp", "rlog", "wlog", "outq"]
        avg = [sum(w[i] for w in ws) / k for i in range(7)]; mx = [max(w[i] for w in ws) for i in range(7)]
        s.write(f"stage {st} stalls, cycles per window over {k} windows (log queues {ws[0][7]} entries): " + ", ".join(f"{names[i]} {avg[i]:.1f} (max {mx[i]})" for i in range(7)) + "\n")
        s.write(f"stage {st} log waits: read log {avg[4]:.1f}, write log {avg[5]:.1f} cycles per window; windows with any: read {sum(1 for w in ws if w[4])}, write {sum(1 for w in ws if w[5])}\n")
    if tot: s.write(f"throughput: {sum(len(seen.get(st, ())) for st in stages) / max(len(stages), 1) / tot * 3600:.0f} windows per hour (all stages)\n")
ok = not bad and all(not m for m in missing.values())
with open(out + "/VERDICT.txt", "w") as v:
    for st in stages:
        tot_st = n if not (st == "F" and x > 0) else len([w for w in want if w % x == 0])
        v.write(f"{'ok  ' if not missing[st] else 'FAIL'} stage {st}: {tot_st - len(missing[st])} of {tot_st} windows matched" + (f"; first missing {missing[st][:20]}" if missing[st] else "") + "\n")
    for w, m in bad[:50]: v.write(f"FAIL window {w}: {m}\n")
    v.write(("PASS" if ok and n > 0 else "FAIL") + f": {prog} windows " + (f"{f}..{f + n - 1}" if nn != "all" else f"(all {n} listed)") + "\n")
    ok = ok and n > 0
sys.exit(0 if ok else 1)
PY
RC=$?
cat "$OUT/SUMMARY.txt" "$OUT/VERDICT.txt"
exit $RC
