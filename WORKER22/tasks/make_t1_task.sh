#!/bin/bash
# make_t1_task.sh MESHSIM_WIP TASKNAME > task.sh: B1's T1 at 2^29 as a Quest task (WORKER22). Builds the kit from a meshsim
# checkout's wip/ (meshsim's classes and gold's, compiled for Java 21: the runner has no javac; the universal tables; the
# summary script), embeds it after an explicit exit, SHA-256 checked.
set -e
WIP=$(cd "$1" && pwd); NAME=$2; K=/tmp/t1kit-$$; rm -rf $K; mkdir -p $K/t1/classes $K/t1/gold $K/t1/tables
javac --release 21 -nowarn -d $K/t1/gold $(find $WIP/gold/src -name '*.java') 2>&1 | grep -v '^Note:' || true
grep -v '^#' $WIP/meshsim/test/PENDING | awk 'NF{print "'$WIP'/meshsim/test/"$1}' > $K/pending.txt
javac --release 21 -nowarn -d $K/t1/classes $WIP/meshsim/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/t1/classes -cp $K/t1/classes:$K/t1/gold $(ls $WIP/meshsim/test/*.java | grep -v -x -F -f $K/pending.txt) 2>&1 | grep -v '^Note:' || true
cp -r $WIP/tables/replayer $K/t1/tables/
cp $(dirname "$0")/t1_summary.py $K/t1/
(cd $WIP && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), gold $(git log -1 --format=%h -- gold), built $(date -u +%F)") > $K/t1/VERSION
(cd $K/t1 && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar -czf $K/kit.tgz -C $K t1
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER22, B1's T1 at 2^29 (meshsim WORKER22/PLAN.md section 5; DECISIONS-v1.3 after the work list, 9cf1b52):
# the Tracer model, its memory system (the shared cache, no TLB) and an ideal controller on every segment of evm-zone-29
# and cyclone-29, in order on one memory: WORKER26's initial image, map and per-segment pages written (Quest 109,
# \$DATA/w26-jumpmap), gold's segment file for frame 2, frame 3 and the update lists. Per window: the start state, the
# streams and the bitmap against WORKER9's deliveries (\$DATA/w9-deliveries, tasks 103/104); per segment boundary: the state
# against the next frame 2, the memory against the update list. Three runs per workload, in parallel:
#   raw  every raw stream value for value against the delivery (the timing run: the shared cache, the map in it);
#   lru  the streams LRU-coded (sizes and picks against the delivery's encoder), then Stage A: the replayer model on the
#        Tracer's own record and streams against rows.bin (jolt-cpp's rows), every stream consumed exactly;
#   sep  the control: the map's probes on a shared cache of their own (the map lines' effect on the data), raw checks.
# Writes only results/$NAME/: per run a log and a per-segment CSV, and SUMMARY.txt (the core's cycles per row against 4,
# the all-in cycles per segment against 100 ms, stalls, hit rates by line class, the walk's share, the map's effect).
# Reboot-safe: scratch in /tmp, removed first; inputs only read, checked against their COMPLETE markers. Expected ~25 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
DATA=\${T1_DATA:-\$HOME/data}
[ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
echo "persistent data: \$DATA"
W=/tmp/w22-t1; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
(cd "\$W/t1" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
cat "\$W/t1/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; free -g | head -2
# program : w9 delivery manifest SHA-256 : w26 manifest.sha256's SHA-256 (its COMPLETE marker)
PROGS=\${T1_PROGS:-"evm-zone-29:7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f:57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d cyclone-29:f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982:ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba"}
D9=\$DATA/w9-deliveries; D26=\$DATA/w26-jumpmap
for PS in \$PROGS; do
  IFS=: read P S9 S26 <<< "\$PS"
  [ "\$(sha256sum "\$D9/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)" = "\$S9" ] && [ "\$(cat "\$D9/\$P.COMPLETE" 2>/dev/null)" = "\$S9" ] || { echo "\$P: the delivery isn't complete or differs (tasks 103/104)"; exit 2; }
  [ "\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)" = "\$S26" ] && [ "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" = "\$S26" ] || { echo "\$P: WORKER26's inputs aren't complete or differ (task 109)"; exit 2; }
  (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's files differ from their manifest"; exit 2; }
  echo "\$P: inputs checked (delivery \$S9, w26 \$S26)"
done
J="java -Xmx\${T1_HEAP:-5g} -Dreplay.tables=\$W/t1/tables/replayer -cp \$W/t1/classes:\$W/t1/gold TracerT1"; X=\${T1_EXTRA:-}
PIDS=""
for PS in \$PROGS; do
  IFS=: read P S9 S26 <<< "\$PS"
  \$J "\$D9" "\$P" --w26 "\$D26/\$P" --quiet --no-stage-a --csv "\$RES/\$P-raw.csv" \$X > "\$RES/\$P-raw.log" 2>&1 & PIDS="\$PIDS \$!:\$P-raw"
  \$J "\$D9" "\$P" --w26 "\$D26/\$P" --quiet --compress --csv "\$RES/\$P-lru.csv" \$X > "\$RES/\$P-lru.log" 2>&1 & PIDS="\$PIDS \$!:\$P-lru"
  \$J "\$D9" "\$P" --w26 "\$D26/\$P" --quiet --no-stage-a --map-cache-separate --csv "\$RES/\$P-sep.csv" \$X > "\$RES/\$P-sep.log" 2>&1 & PIDS="\$PIDS \$!:\$P-sep"
done
RC=0
for X in \$PIDS; do wait \${X%%:*}; R=\$?; echo "\${X#*:}: exit \$R, \$(tail -1 "\$RES/\${X#*:}.log")"; [ \$R = 0 ] || RC=1; done
grep -h "FAIL\|STOPPED\|Exception" "\$RES"/*.log | head -20
python3 "\$W/t1/t1_summary.py" "\$RES" \$(for PS in \$PROGS; do echo \${PS%%:*}; done)
echo "verdict: \$([ \$RC = 0 ] && echo PASS || echo FAIL); exit \$RC at \$(date -u)"
rm -rf "\$W"
exit \$RC
__KIT_BEGIN__
EOF
base64 -w 76 $K/kit.tgz
echo "__KIT_END__"
rm -rf $K
