#!/bin/bash
# make_profile_task.sh MESHSIM_WIP TASKNAME > task.sh: WORKER28's step 1 (the profile) as a Quest task, from a meshsim
# checkout's wip/ on branch sim-idle-profile (measurement only: Probe.java, test/JfrFold.java). Builds the kit (meshsim's
# and gold's classes for Java 21, the runtime files T2 and Stage B read), embeds it after an explicit exit, SHA-256 checked.
# (The kit's layout and the input checks are WORKER22's make_t1_task.sh, which made 110e.)
set -e
WIP=$(cd "$1" && pwd); NAME=$2; K=/tmp/w28kit-$$; rm -rf $K; mkdir -p $K/k/classes $K/k/gold $K/k/tables
javac --release 21 -nowarn -d $K/k/gold $(find $WIP/gold/src -name '*.java') 2>&1 | grep -v '^Note:' || true
grep -v '^#' $WIP/meshsim/test/PENDING | awk 'NF{print "'$WIP'/meshsim/test/"$1}' > $K/pending.txt
javac --release 21 -nowarn -d $K/k/classes $WIP/meshsim/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/k/classes -cp $K/k/classes:$K/k/gold $(ls $WIP/meshsim/test/*.java | grep -v -x -F -f $K/pending.txt) 2>&1 | grep -v '^Note:' || true
for c in TracerT2 ReplayShard ConvertDelivery JfrFold Probe; do [ -f $K/k/classes/$c.class ] || { echo "no $c.class" >&2; exit 1; }; done
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
T2WIN=${T2WIN:-32}; SBN=${SBN:-16}; LIM=${LIM:-40m}
# SET=time (113b): instead, the time probe (-Dmeshsim.timeprobe: ns by chip section every cycle, by tile class and child 1
# cycle in 16, C&C 1-3 per child every cycle), on the same T2 windows and Stage B windows, four JVMs
# SET=sb (113c): Stage B and F only, under JFR, the activity probe and the time probe, six JVMs (113 and 113b lacked the
# tables' manifest.json, so their Stage B runs failed at the start)
SET=${SET:-all}
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER28, step 1 of the exact idle-skipping work (meshsim WORKER28/PROMPT.md): the profile, on
# branch sim-idle-profile (v1.3-tracer 515c11a plus a measurement-only probe), to see where a full-chip run's time goes and
# how much of it is units with nothing to do. Eight JVMs at once:
#   T2 (TracerT2, WORKER22) on segment 0 of evm-zone-29 and cyclone-29, raw:
#     jfr-*    all 64 windows under Java Flight Recorder (wall time by unit: JfrFold), unperturbed by the probe;
#     probe-*  the first $T2WIN windows with -Dmeshsim.probe (per unit and cycle: idle, router only, engines, core;
#              the core's states; idle runs; per-2^20-cycle bins), about twice as slow, so half the segment;
#   Stage B (ReplayShard, the acceptance kit's path) on windows 0-$((SBN - 1)) of both programs, converted to v1.26.1 in
#     scratch as 105 did: stage B (tile 0 alone, the 63 others parked) and F (the full chip) on every window, under JFR
#     and under the probe, one JVM each.
# Inputs only read (\$DATA/w9-deliveries, tasks 103/104; \$DATA/w26-jumpmap, task 109), checked first. Fail-fast: T2's own
# (no progress for 3 M cycles; a tile not exited 1 M cycles after C&C 3's exit), Stage B's no-row limit (50,000 cycles),
# and every JVM under timeout $LIM (the probe's report and the recording are still written on the TERM). Writes only
# results/$NAME/. Reboot-safe: scratch in /tmp, removed first. Expected ~25 min (110e: 1,228 s per T2 segment, four JVMs).
# THIS TASK'S SET: $SET$( [ $SET = time ] && echo " -- the time probe only, four JVMs: T2 on the first $T2WIN windows of segment 0 of both
# workloads and Stage B and F on windows 0-$((SBN - 1)), with -Dmeshsim.timeprobe (ns per cycle by chip section; by tile class and child, 1
# cycle in 16; C&C 1-3 per child every cycle, the Tracer's slot split from its DMA; the cores by state: the interpreter's share).
# Expected ~13 min (about 5% slower than an untimed run)." )$( [ $SET = sb ] && echo " -- Stage B only: B (tile 0 alone) and F (the full chip)
# on windows 0-$((SBN - 1)) of both programs, under JFR, the activity probe and the time probe, six JVMs. Expected ~10 min." )
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
DATA=\${W28_DATA:-\$HOME/data}
[ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
echo "persistent data: \$DATA"
W=/tmp/w28-profile; rm -rf "\$W"; mkdir -p "\$W"
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
if [ $SET = sb ]; then for PS in \$PROGS; do
  P=\${PS%%:*}
  SB="-Xmx1500m -Dstageb.norow=50000 -Dreplayer.logq=256 -Dreplay.tables=\$W/k/tables/replayer -Dreplay.build=\$W/k/sw/replay/build"
  SA="--manifest \$W/v1261/\$P.manifest.json --stages B --first 0 --count $SBN --stride 1 --phase 0 --full-every 1"
  run jfr-sb-\$P \$SB \$JFR=\$W/jfr-sb-\$P.jfr -- ReplayShard "\$W/v1261" "\$P" "\$RES/jfr-sb-\$P.csv" \$SA
  run probe-sb-\$P \$SB -Dmeshsim.probe=\$RES/probe-sb-\$P.txt -- ReplayShard "\$W/v1261" "\$P" "\$RES/probe-sb-\$P.csv" \$SA
  run time-sb-\$P \$SB -Dmeshsim.timeprobe=\$RES/time-sb-\$P.txt -- ReplayShard "\$W/v1261" "\$P" "\$RES/time-sb-\$P.csv" \$SA
done; elif [ $SET = time ]; then for PS in \$PROGS; do
  P=\${PS%%:*}
  run time-t2-\$P -Xmx5g -Dreplay.tables=\$W/k/tables/replayer -Dmeshsim.timeprobe=\$RES/time-t2-\$P.txt -- TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --windows $T2WIN --quiet
  SB="-Xmx1500m -Dstageb.norow=50000 -Dreplayer.logq=256 -Dreplay.tables=\$W/k/tables/replayer -Dreplay.build=\$W/k/sw/replay/build"
  run time-sb-\$P \$SB -Dmeshsim.timeprobe=\$RES/time-sb-\$P.txt -- ReplayShard "\$W/v1261" "\$P" "\$RES/time-sb-\$P.csv" --manifest \$W/v1261/\$P.manifest.json --stages B --first 0 --count $SBN --stride 1 --phase 0 --full-every 1
done; else
for PS in \$PROGS; do
  P=\${PS%%:*}
  run jfr-t2-\$P -Xmx5g -Dreplay.tables=\$W/k/tables/replayer \$JFR=\$W/jfr-t2-\$P.jfr -- TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --quiet
  run probe-t2-\$P -Xmx5g -Dreplay.tables=\$W/k/tables/replayer -Dmeshsim.probe=\$RES/probe-t2-\$P.txt -- TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --windows $T2WIN --quiet
  SB="-Xmx1500m -Dstageb.norow=50000 -Dreplayer.logq=256 -Dreplay.tables=\$W/k/tables/replayer -Dreplay.build=\$W/k/sw/replay/build"
  SA="--manifest \$W/v1261/\$P.manifest.json --stages B --first 0 --count $SBN --stride 1 --phase 0 --full-every 1"
  run jfr-sb-\$P \$SB \$JFR=\$W/jfr-sb-\$P.jfr -- ReplayShard "\$W/v1261" "\$P" "\$RES/jfr-sb-\$P.csv" \$SA
  run probe-sb-\$P \$SB -Dmeshsim.probe=\$RES/probe-sb-\$P.txt -- ReplayShard "\$W/v1261" "\$P" "\$RES/probe-sb-\$P.csv" \$SA
done
fi
RC=0
for X in \$PIDS; do wait \${X%%:*}; R=\$?; echo "\${X#*:}: exit \$R; \$(grep -h -E 'OK;|FAIL|STOPPED|T2 on' "\$RES/\${X#*:}.log" | tail -2 | tr '\n' ' ')"; [ \$R = 0 ] || RC=1; done
for F in "\$W"/*.jfr; do
  [ -f "\$F" ] || continue; B=\$(basename "\$F" .jfr)
  java -cp \$CP JfrFold "\$F" 40 > "\$RES/\$B-fold.txt" 2>&1 || echo "\$B: fold failed"
  [ \$(stat -c %s "\$F") -lt 30000000 ] && gzip -c "\$F" > "\$RES/\$B.jfr.gz"
  sed -n '1p;/== unit/,/== self/p' "\$RES/\$B-fold.txt" | head -24
done
for F in "\$RES"/probe-*.txt; do [ -f "\$F" ] && { echo "== \$(basename "\$F")"; grep '^AGG' "\$F"; }; done
for F in "\$RES"/time-*.txt; do [ -f "\$F" ] && { echo "== \$(basename "\$F")"; grep -E 'section|cc3 |idle |router |engines |core ' "\$F" | head -60; }; done
grep -h -E "Exception|STOPPED" "\$RES"/*.log | head -20
echo "verdict: \$([ \$RC = 0 ] && echo PASS || echo FAIL); exit \$RC at \$(date -u)"
rm -rf "\$W"
exit \$RC
__KIT_BEGIN__
EOF
base64 -w 76 $K/kit.tgz
echo "__KIT_END__"
rm -rf $K
