#!/bin/bash
# make_skip_task.sh MESHSIM_WIP TASKNAME > task.sh: WORKER28's stage 1 proof on the runner, from a meshsim checkout's wip/
# on branch sim-idle-skip: T2 and Stage B with stage 1 off and on, compared by every chip's fingerprint (-Dmeshsim.digest)
# and their logs and CSVs, and stage 1's speed. Builds the kit (meshsim's and gold's classes for Java 21, the runtime
# files T2 and Stage B read), embeds it after an explicit exit, SHA-256 checked. (From make_profile_task.sh.)
# (The kit's layout and the input checks are WORKER22's make_t1_task.sh, which made 110e.)
set -e
WIP=$(cd "$1" && pwd); NAME=$2; K=/tmp/w28skip-$$; rm -rf $K; mkdir -p $K/k/classes $K/k/gold $K/k/tables
javac --release 21 -nowarn -d $K/k/gold $(find $WIP/gold/src -name '*.java') 2>&1 | grep -v '^Note:' || true
grep -v '^#' $WIP/meshsim/test/PENDING | awk 'NF{print "'$WIP'/meshsim/test/"$1}' > $K/pending.txt
javac --release 21 -nowarn -d $K/k/classes $WIP/meshsim/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/k/classes -cp $K/k/classes:$K/k/gold $(ls $WIP/meshsim/test/*.java | grep -v -x -F -f $K/pending.txt) 2>&1 | grep -v '^Note:' || true
for c in TracerT2 ReplayShard ConvertDelivery; do [ -f $K/k/classes/$c.class ] || { echo "no $c.class" >&2; exit 1; }; done
cp -r $WIP/tables/replayer $K/k/tables/
cp $WIP/tables/manifest.json $K/k/tables/            # Stage B reads replay.tables/../manifest.json (113b failed without it)
mkdir -p $K/k/sw/build $K/k/sw/replay/build $K/k/sw/tracer/build
cp $WIP/meshsim/hops.txt $K/k/; cp $WIP/meshsim/sw/bootrom.hex $K/k/sw/
cp $WIP/meshsim/sw/build/cc1.* $WIP/meshsim/sw/build/cc23.* $K/k/sw/build/
cp $WIP/meshsim/sw/replay/build/replay_ctl.code.bin $WIP/meshsim/sw/replay/build/replay_ctl.data.bin $WIP/meshsim/sw/replay/build/replay_ctl.sym $K/k/sw/replay/build/
cp $WIP/meshsim/sw/tracer/build/tracer_ctl.code.bin $WIP/meshsim/sw/tracer/build/tracer_ctl.data.bin $WIP/meshsim/sw/tracer/build/tracer_ctl.sym $K/k/sw/tracer/build/
(cd $WIP && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), gold $(git log -1 --format=%h -- gold), built $(date -u +%F)") > $K/k/VERSION
(cd $K/k && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar -czf $K/kit.tgz -C $K k
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
LIM=${LIM:-42m}; SBN=${SBN:-64}
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER28, stage 1 of the idle-skipping work (meshsim WORKER28/PROMPT.md, step 4 for stage 1):
# branch sim-idle-skip (v1.3-tracer 515c11a plus stage 1: sleeping units are not stepped; -Dmeshsim.skip=false steps every
# unit every cycle, as before). Each run twice, stage 1 off and on, eight JVMs at once:
#   t2-*  TracerT2 on segment 0 of evm-zone-29 and cyclone-29, raw, all 64 windows (the full chip, C&C 3 tracing, every
#         tile replaying its window);
#   sb-*  ReplayShard on windows 0-$((SBN - 1)) of both programs (converted to v1.26.1 in scratch, as 105): stage B (tile 0
#         alone, the others parked) and stage F (the full chip) on every window.
# Exactness: every chip's fingerprint at its end (-Dmeshsim.digest: per unit mcycle, WFI, halted, stalled, instructions,
# pc, halt, profile, router, director, SRAM, control and request counters; the chip's cycle, violations, log) identical
# off and on, and the logs and CSVs identical with wall times masked. Speed: wall seconds per simulated cycle, against
# 113b's 46.9 us (the time probe, 32 windows) and this task's own stage-1-off runs.
# Inputs only read, checked first. Fail-fast: T2's own (no progress for 3 M cycles; a tile not exited 1 M cycles after
# C&C 3's exit), Stage B's no-row limit, and every JVM under timeout $LIM. Writes only results/$NAME/. Reboot-safe:
# scratch in /tmp, removed first. Expected ~25 min (the off runs: 110e's 1,228 s per T2 segment).
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
DATA=\${W28_DATA:-\$HOME/data}
[ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
echo "persistent data: \$DATA"
W=/tmp/w28-skip; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
(cd "\$W/k" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
cat "\$W/k/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; free -g | head -2
PROGS="evm-zone-29:7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f:57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d cyclone-29:f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982:ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba"
D9=\$DATA/w9-deliveries; D26=\$DATA/w26-jumpmap
for PS in \$PROGS; do
  IFS=: read P S9 S26 <<< "\$PS"
  [ "\$(sha256sum "\$D9/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)" = "\$S9" ] && [ "\$(cat "\$D9/\$P.COMPLETE" 2>/dev/null)" = "\$S9" ] || { echo "\$P: the delivery isn't complete or differs (tasks 103/104)"; exit 2; }
  [ "\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)" = "\$S26" ] && [ "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" = "\$S26" ] || { echo "\$P: WORKER26's inputs aren't complete or differ (task 109)"; exit 2; }
  (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's files differ from their manifest"; exit 2; }
  echo "\$P: inputs checked"
done
CP=\$W/k/classes:\$W/k/gold
JFR="-XX:StartFlightRecording=settings=profile,dumponexit=true,filename"
cd "\$W/k"                                     # the full chip reads hops.txt and sw/ from here
# ---- Stage B's windows, converted in scratch (as 105, 110h)
for PS in \$PROGS; do
  P=\${PS%%:*}
  java -Xmx2g -cp \$CP ConvertDelivery "\$D9" "\$W/v1261" "\$P" > "\$RES/convert-\$P.log" 2>&1 || { echo "conversion of \$P failed"; tail -3 "\$RES/convert-\$P.log"; exit 1; }
  tail -1 "\$RES/convert-\$P.log"
done
PIDS=""
run() {   # run NAME JVM-OPTIONS -- MAIN ARGS...
  N=\$1; shift; O=""; while [ "\$1" != -- ]; do O="\$O \$1"; shift; done; shift
  ( a=\$(date +%s); timeout -s TERM $LIM java \$O -cp \$CP "\$@" > "\$RES/\$N.log" 2>&1; r=\$?; echo "\$N: exit \$r, \$((\$(date +%s) - a)) s" >> "\$RES/\$N.log"; exit \$r ) &
  PIDS="\$PIDS \$!:\$N"
}
SB="-Xmx1500m -Dstageb.norow=50000 -Dreplayer.logq=256 -Dreplay.tables=\$W/k/tables/replayer -Dreplay.build=\$W/k/sw/replay/build"
for PS in \$PROGS; do
  P=\${PS%%:*}
  for M in false true; do
    run t2-\$P-\$M -Xmx5g -Dmeshsim.skip=\$M -Dmeshsim.digest=\$RES/t2-\$P-\$M.digest -Dreplay.tables=\$W/k/tables/replayer -- TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --quiet --csv "\$RES/t2-\$P-\$M.csv"
    run sb-\$P-\$M \$SB -Dmeshsim.skip=\$M -Dmeshsim.digest=\$RES/sb-\$P-\$M.digest -- ReplayShard "\$W/v1261" "\$P" "\$RES/sb-\$P-\$M.csv" --manifest \$W/v1261/\$P.manifest.json --stages B --first 0 --count $SBN --stride 1 --phase 0 --full-every 1
  done
done
RC=0
for X in \$PIDS; do wait \${X%%:*}; R=\$?; echo "\${X#*:}: exit \$R; \$(grep -h -E 'OK;|FAIL|STOPPED|T2 on' "\$RES/\${X#*:}.log" | tail -2 | tr '\n' ' ')"; [ \$R = 0 ] || RC=1; done
# ---- the comparison: fingerprints, logs and CSVs (wall times masked), and the speed
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms)\b/#s/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g' "\$1"; }
csvmask() { awk -F, -v c=\$2 'BEGIN{OFS=","} {\$c="#"; print}' "\$1" | sort; }
SAME=0; DIFF=0
for N in t2-evm-zone-29 t2-cyclone-29 sb-evm-zone-29 sb-cyclone-29; do
  V=SAME
  cmp -s "\$RES/\$N-false.digest" "\$RES/\$N-true.digest" || V="DIFF (fingerprints)"
  [ -s "\$RES/\$N-false.digest" ] || V="DIFF (no fingerprints)"
  cmp -s <(mask "\$RES/\$N-false.log" | grep -v ': exit [0-9]*, ') <(mask "\$RES/\$N-true.log" | grep -v ': exit [0-9]*, ') || V="\$V; DIFF (logs)"
  case \$N in t2-*) C=10;; *) C=8;; esac
  cmp -s <(csvmask "\$RES/\$N-false.csv" \$C) <(csvmask "\$RES/\$N-true.csv" \$C) || V="\$V; DIFF (CSV)"
  [ "\$V" = SAME ] && SAME=\$((SAME + 1)) || { DIFF=\$((DIFF + 1)); RC=1; }
  echo "\$N: \$V; chips \$(wc -l < "\$RES/\$N-true.digest" 2>/dev/null)"
done
python3 - "\$RES" <<'PY'
import sys, re, glob, os
res = sys.argv[1]
def wall(log):
    m = re.findall(r": exit \d+, (\d+) s", open(log).read()); return int(m[-1]) if m else None
def cycles(dg):
    return sum(int(re.search(r"cycle (\d+)", l).group(1)) for l in open(dg) if l.startswith("chip"))
with open(res + "/SPEED.txt", "w") as out:
    for n in ["t2-evm-zone-29", "t2-cyclone-29", "sb-evm-zone-29", "sb-cyclone-29"]:
        row = [n]
        for m in ["false", "true"]:
            try:
                w, c = wall(f"{res}/{n}-{m}.log"), cycles(f"{res}/{n}-{m}.digest")
                row.append(f"stage 1 {'on' if m == 'true' else 'off'}: {w} s, {c:,} cycles, {w / c * 1e6:.2f} us/cycle" if w and c else f"{m}: n/a")
            except Exception as e: row.append(f"{m}: n/a ({e})")
        out.write("; ".join(row) + "\n")
    out.write("113b (the time probe, T2 segment 0, 32 windows): 46.9 us/cycle\n")
print(open(res + "/SPEED.txt").read())
PY
echo "fingerprint, log and CSV comparison: \$SAME same, \$DIFF different"
grep -h -E "Exception|STOPPED" "\$RES"/*.log | head -20
echo "verdict: \$([ \$RC = 0 ] && echo PASS || echo FAIL); exit \$RC at \$(date -u)"
rm -rf "\$W"
exit \$RC
__KIT_BEGIN__
EOF
base64 -w 76 $K/kit.tgz
echo "__KIT_END__"
rm -rf $K
