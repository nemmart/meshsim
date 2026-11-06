#!/bin/bash
# make_profile_task.sh MESHSIM_WIP TASKNAME > task.sh: WORKER29's profile on the runner (P2 step 1), from a meshsim checkout's
# wip/ on branch sim-step-cost. Builds the kit (meshsim's and gold's classes for Java 21, WORKER29's JFR tools, the runtime
# files the sort, ReplayBench and T2 read, the sort image), embeds it after an explicit exit, SHA-256 checked.
# (The kit's layout and the input checks are WORKER28's make_skip_task.sh.)
set -e
WIP=$(cd "$1" && pwd); NAME=$2; HERE=$(cd "$(dirname "$0")" && pwd); K=/tmp/w29prof-$$; rm -rf $K; mkdir -p $K/k/classes $K/k/gold $K/k/tables $K/k/tools
javac --release 21 -nowarn -d $K/k/gold $(find $WIP/gold/src -name '*.java') 2>&1 | grep -v '^Note:' || true
grep -v '^#' $WIP/meshsim/test/PENDING | awk 'NF{print "'$WIP'/meshsim/test/"$1}' > $K/pending.txt
javac --release 21 -nowarn -d $K/k/classes $WIP/meshsim/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/k/classes -cp $K/k/classes:$K/k/gold $(ls $WIP/meshsim/test/*.java | grep -v -x -F -f $K/pending.txt) 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/k/tools $HERE/../tools/*.java
for c in TracerT2 ReplayBench ConvertDelivery SortTest KeyGen; do [ -f $K/k/classes/$c.class ] || { echo "no $c.class" >&2; exit 1; }; done
cp -r $WIP/tables/replayer $K/k/tables/; cp $WIP/tables/manifest.json $K/k/tables/
mkdir -p $K/k/sw/build $K/k/sw/replay/build $K/k/sw/tracer/build $K/k/image
cp $WIP/meshsim/hops.txt $K/k/; cp $WIP/meshsim/sw/bootrom.hex $K/k/sw/
cp $WIP/meshsim/sw/build/cc1.* $WIP/meshsim/sw/build/cc23.* $K/k/sw/build/
cp $WIP/meshsim/sw/replay/build/replay_ctl.code.bin $WIP/meshsim/sw/replay/build/replay_ctl.data.bin $WIP/meshsim/sw/replay/build/replay_ctl.sym $K/k/sw/replay/build/
cp $WIP/meshsim/sw/tracer/build/tracer_ctl.code.bin $WIP/meshsim/sw/tracer/build/tracer_ctl.data.bin $WIP/meshsim/sw/tracer/build/tracer_ctl.sym $K/k/sw/tracer/build/
cp $WIP/meshsim/sw/ext/sort/build/sort.code.bin $WIP/meshsim/sw/ext/sort/build/sort.data.bin $WIP/meshsim/sw/ext/sort/build/sort.sym $K/k/image/
(cd $WIP && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), gold $(git log -1 --format=%h -- gold), built $(date -u +%F)") > $K/k/VERSION
(cd $K/k && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar -czf $K/kit.tgz -C $K k
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER29 (P2, a faster engine for busy workloads), step 1: the profile of a busy chip, on branch
# sim-step-cost (sim-idle-skip with stage 1 on, plus WORKER29's benchmark test/ReplayBench.java; the engine unchanged).
# Eight JVMs at once, each once under JFR (settings=profile) and once without, for clean wall times:
#   sort    the full-size sort p4/uniform-k128 (64 x 65,536 keys, ready=8), expected TOTAL 1,229,008 cycles; its phases CSV;
#   rb-*    ReplayBench: all 64 tiles replaying at once, tile i window i of segment 0 (windows 0-63) of evm-zone-29 and of
#           cyclone-29 (converted to v1.26.1 in scratch, as 105), each tile's rows against rows.bin, 5 repeats (fresh chips);
#   rbsyn   ReplayBench synthetic (64 x 300 rows on every tile, 5 repeats): the same run as locally, for the box's speed;
#   t2      TracerT2 on segment 0 of evm-zone-29, raw, all 64 windows, stage 1 on (as 114's on runs).
# Reports: each JFR recording folded (JfrFold: by unit, top frame, inclusive) and the callers of the hot frames (Callers),
# allocation by site; the recordings themselves are not kept. Inputs only read, checked first. Every JVM under timeout 40m.
# Writes only results/$NAME/. Scratch in /tmp, removed first and at the end. Expected ~15 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
DATA=\${W28_DATA:-\$HOME/data}
[ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
echo "persistent data: \$DATA"
W=/tmp/w29-prof; rm -rf "\$W"; mkdir -p "\$W"
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
  echo "\$P: delivery checked"
done
P=evm-zone-29; S26=57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d
[ "\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)" = "\$S26" ] && [ "\$S26" = "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" ] && (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's inputs aren't complete or differ (task 109)"; exit 2; }
echo "\$P: WORKER26's inputs checked"
CP=\$W/k/classes:\$W/k/gold
cd "\$W/k"                                     # hops.txt, sw/ and image/ are read from here
for PS in \$PROGS; do
  P=\${PS%%:*}
  java -Xmx2g -cp \$CP ConvertDelivery "\$D9" "\$W/v1261" "\$P" > "\$RES/convert-\$P.log" 2>&1 || { echo "conversion of \$P failed"; tail -3 "\$RES/convert-\$P.log"; exit 1; }
  tail -1 "\$RES/convert-\$P.log"
done
java -Xmx2g -cp \$CP KeyGen "\$W/keys" --n 65536 --only uniform > "\$RES/keygen.log" 2>&1 || { echo "KeyGen failed"; cat "\$RES/keygen.log"; exit 1; }
ls -l "\$W/keys" | tail -n +2
PIDS=""
run() {   # run NAME JVM-OPTIONS -- MAIN ARGS...; NAME-jfr under JFR, NAME without
  N=\$1; shift; O=""; while [ "\$1" != -- ]; do O="\$O \$1"; shift; done; shift
  for J in jfr plain; do
    X=""; [ \$J = jfr ] && X="-XX:StartFlightRecording=settings=profile,dumponexit=true,filename=\$W/\$N.jfr"
    ( a=\$(date +%s); timeout -s TERM 40m java \$O \$X -cp \$CP "\$@" > "\$RES/\$N-\$J.log" 2>&1; r=\$?; echo "\$N-\$J: exit \$r, \$((\$(date +%s) - a)) s" >> "\$RES/\$N-\$J.log"; exit \$r ) &
    PIDS="\$PIDS \$!:\$N-\$J"
  done
}
RB="-Xmx6g -Dreplay.tables=\$W/k/tables/replayer -Dreplay.build=\$W/k/sw/replay/build"
run sort -Xmx6g -Dsort.build=\$W/k/image -- SortTest -1 run label=p4/uniform-k128 keys=\$W/keys/uniform.keys fmt=k128 ready=8 phases=\$RES/sort-phases.csv
run rb-evm-zone-29 \$RB -- ReplayBench "\$W/v1261" evm-zone-29 --first 0 --repeats 5
run rb-cyclone-29 \$RB -- ReplayBench "\$W/v1261" cyclone-29 --first 0 --repeats 5
run rbsyn \$RB -- ReplayBench synthetic 300 64 5
RC=0
for X in \$PIDS; do wait \${X%%:*}; R=\$?; echo "\${X#*:}: exit \$R"; [ \$R = 0 ] || RC=1; done
# T2 after the others (its JVM holds the memory image; as 114's on runs)
P=evm-zone-29
for J in jfr plain; do
  X=""; [ \$J = jfr ] && X="-XX:StartFlightRecording=settings=profile,dumponexit=true,filename=\$W/t2.jfr"
  a=\$(date +%s); timeout -s TERM 40m java -Xmx5g \$X -Dreplay.tables=\$W/k/tables/replayer -cp \$CP TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --quiet --csv "\$RES/t2-\$J.csv" > "\$RES/t2-\$J.log" 2>&1; r=\$?
  echo "t2-\$J: exit \$r, \$((\$(date +%s) - a)) s" | tee -a "\$RES/t2-\$J.log"; [ \$r = 0 ] || RC=1
done
# ---- the reports
for F in \$W/*.jfr; do
  N=\$(basename \$F .jfr); O=\$RES/\$N-profile.txt
  java -cp \$W/k/tools JfrFold \$F 40 > \$O 2>&1
  for T in BusBase.enterSlow java.util.Arrays.fill java.util.HashMap java.lang.Integer.valueOf java.lang.Long.valueOf TileControl.computeStatus TileControl.endCycle SRAMReadQueue.runCycle Router.runCycle Controller.step Replayer.runCycle; do
    echo; java -cp \$W/k/tools Callers \$F jdk.ExecutionSample \$T 4 12
  done >> \$O 2>&1
  echo >> \$O; java -cp \$W/k/tools Callers \$F jdk.ObjectAllocationSample "" 3 25 >> \$O 2>&1
  echo >> \$O; jfr summary \$F | grep -E "GarbageCollection |ExecutionSample" >> \$O 2>&1
done
echo "== results"
grep -h -E "TOTAL|SortTest:" "\$RES"/sort-*.log | cut -c1-120
grep -h -E "^repeat|ReplayBench:" "\$RES"/rb*-*.log | cut -c1-400
grep -h -E "exit [0-9]+, [0-9]+ s" "\$RES"/*.log
grep -h -E "T2 on|OK;|FAIL" "\$RES"/t2-*.log | tail -4 | cut -c1-300
grep -q "TOTAL 1,229,008 cycles" "\$RES/sort-plain.log" && echo "sort: 1,229,008 cycles as expected" || { echo "sort: NOT the expected 1,229,008 cycles"; RC=1; }
echo "end \$(date -u), exit \$RC"
rm -rf "\$W"
exit \$RC
__KIT_BEGIN__
EOF
base64 -w 0 $K/kit.tgz | fold -w 76
echo
echo __KIT_END__
rm -rf $K
