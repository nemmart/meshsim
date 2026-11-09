#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" > task.sh (WORKER13): a self-contained Quest task with an embedded kit of sim/ref,
# precompiled (the runner has no javac and no riscv toolchain): meshsim's classes, gold's, the test classes and W13's
# drivers (sim/tests/ref/*.java, ref's default package) for Java 21; the universal tables; hops.txt; the boot ROM; C&C 1-2's,
# C&C 3's (tracer_ctl) and the tiles' (replay_ctl) images, built here and checked against IMAGES (110u's kit); sim/tools.
# Unpacked to $W/kit: $KR is the run directory (hops.txt, sw/), $KC the classpath, $KT the tables, $KS the kit's sim/tools.
# The body (sourced) also sees RES, W, sec, verdict, DATA (persistent data), D9 / D26 (the deliveries, WORKER26's inputs)
# and checkin PROGRAM (checks a workload's inputs against their COMPLETE markers, as 110u). Run from inside the repo,
# committed, with sim/ref built (javac, sw/build.sh, sw/build_cc.sh, sw/replay/build.sh, sw/tracer/build.sh).
set -eu
NAME=$1 BODY=$2 HDR=$3
TOP=$(git rev-parse --show-toplevel); M=$TOP/sim/ref/meshsim; HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- sim)" ] || { echo "mk_task.sh: sim/ has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); mkdir -p $K/kit/run/sw/build $K/kit/run/sw/replay/build $K/kit/run/sw/tracer/build $K/kit/classes $K/kit/gold $K/kit/tables
javac --release 21 -nowarn -d $K/kit/gold $(find $TOP/sim/ref/gold/src -name '*.java') 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d $K/kit/classes $M/src/*.java 2>&1 | grep -v '^Note:' || true
grep -v '^#' $M/test/PENDING | awk 'NF{print "'$M'/test/"$1}' > $K/pending.txt
javac --release 21 -nowarn -d $K/kit/classes -cp $K/kit/classes:$K/kit/gold $(ls $M/test/*.java | grep -v -x -F -f $K/pending.txt) \
    $TOP/sim/tests/ref/*.java 2>&1 | grep -v '^Note:' || true
cp -r $TOP/sim/ref/tables/replayer $K/kit/tables/
cp $M/hops.txt $K/kit/run/; cp $M/sw/bootrom.hex $K/kit/run/sw/
cp $M/sw/build/cc1.code.bin $M/sw/build/cc1.data.bin $M/sw/build/cc1.sym $M/sw/build/cc23.code.bin $M/sw/build/cc23.data.bin $M/sw/build/cc23.sym $K/kit/run/sw/build/
cp $M/sw/replay/build/replay_ctl.code.bin $M/sw/replay/build/replay_ctl.data.bin $M/sw/replay/build/replay_ctl.sym $K/kit/run/sw/replay/build/
cp $M/sw/tracer/build/tracer_ctl.code.bin $M/sw/tracer/build/tracer_ctl.data.bin $M/sw/tracer/build/tracer_ctl.sym $K/kit/run/sw/tracer/build/
(cd $K/kit/run && sha256sum -c --quiet "$HERE/IMAGES") || { echo "mk_task.sh: the images differ from IMAGES (110u's)" >&2; exit 1; }
mkdir -p $K/kit/sim && cp -r $TOP/sim/tools $K/kit/sim/
mkdir -p $K/kit/golden-m3 && cp $TOP/sim/tests/golden-m3/regen.sh $TOP/sim/tests/golden-m3/RUNS $TOP/sim/tests/golden-m3/HASHES $TOP/sim/tests/golden-m3/*.chip $K/kit/golden-m3/
echo "meshsim $R (sim-w13), sim/ref, built $(date -u +%F)" > $K/kit/VERSION
(cd $K/kit && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf $K/kit.tgz -C $K kit
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER13 (branch sim-w13 $R; sim/ref, precompiled).
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end; inputs only read.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w13-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KR=\$W/kit/run; KC=\$W/kit/classes:\$W/kit/gold; KT=\$W/kit/tables/replayer; KS=\$W/kit/sim
DATA=\${T1_DATA:-\$HOME/data}; [ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
D9=\$DATA/w9-deliveries; D26=\$DATA/w26-jumpmap
FAIL=0
sec() { echo; echo "==== \$*  [\$(date +%T)]"; }
verdict() { echo "VERDICT \$1: \$2"; [ "\$2" = PASS ] || FAIL=1; }
# program : the delivery manifest's SHA-256 : WORKER26's manifest.sha256's SHA-256 (as 110u)
declare -A S9=([evm-zone-29]=7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f [cyclone-29]=f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982)
declare -A S26=([evm-zone-29]=57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d [cyclone-29]=ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba)
checkin() {
  local P=\$1 A B
  A=\$(sha256sum "\$D9/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1); B=\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)
  [ "\$A" = "\${S9[\$P]}" ] && [ "\$(cat "\$D9/\$P.COMPLETE" 2>/dev/null)" = "\$A" ] || { echo "\$P: the delivery isn't complete or differs"; return 1; }
  [ "\$B" = "\${S26[\$P]}" ] && [ "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" = "\$B" ] || { echo "\$P: WORKER26's inputs aren't complete or differ"; return 1; }
  (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's files differ from their manifest"; return 1; }
  echo "\$P: inputs checked (delivery \$A, w26 \$B)"
}
START=\$(date +%s)
{
sec "machine (D21)"; . "\$KS/tools/machine.sh" "\${QUEST_P_CPUS:-}" "\${QUEST_E_CPUS:-}"; machine_record "\$RES/machine.txt"; uptime
cat "\$W/kit/VERSION"; java -version 2>&1 | head -1; echo "persistent data: \$DATA"
$(cat "$BODY")
sec "done in \$((\$(date +%s) - START)) s"
} 2>&1 | tee "\$RES/run.log"
grep '^VERDICT' "\$RES/run.log" > "\$RES/VERDICTS.txt"
rm -rf "\$W"
grep -q ': FAIL\$' "\$RES/VERDICTS.txt" && exit 1
exit 0
__KIT_BEGIN__
HEAD
base64 -w 76 $K/kit.tgz
echo "__KIT_END__"
rm -rf $K
