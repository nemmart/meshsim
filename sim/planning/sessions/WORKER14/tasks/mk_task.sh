#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" > task.sh (WORKER14): a self-contained Quest task with an embedded kit:
#   sim/ (src, Makefile, bench, tools, the tests' sources: built on the runner with clang, as WORKER12's tasks), and
#   ref's classes precompiled here with TracerConf's (the runner may have no javac; W13's kits do the same): meshsim's
#   src, the test classes TracerConf needs, the copies tracerconf.sh makes (TracerConfRig, TracerConfCases,
#   TracerConfT1), StateFp and StateFpM3; the universal tables.
# Unpacked to $W/kit: $KS sim/ (make there), $KC the classpath, $KT the tables, $KR the run directory (ref/meshsim's
# layout, for the relative paths). The body (sourced) sees RES, W, sec, verdict, DATA, D9, D26.
set -eu
NAME=$1 BODY=$2 HDR=$3
TOP=$(git rev-parse --show-toplevel); HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- sim)" ] || { echo "mk_task.sh: sim/ has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); mkdir -p $K/kit/run $K/kit/tables
(cd "$TOP" && git archive --format=tar HEAD sim/src sim/Makefile sim/bench sim/tools sim/fw) | tar -x -C $K/kit
mkdir -p $K/kit/sim/tests && (cd "$TOP/sim/tests" && cp *.cpp *.h $K/kit/sim/tests/)
# ref's classes and the drivers, as tracerconf.sh builds them
bash -c "source <(sed -n '/^build()/,/^}/p' $TOP/sim/tests/ref/tracerconf.sh); SIM=$TOP/sim; REF=$TOP/sim/ref/meshsim; \
  FIELDS='$(grep ^FIELDS $TOP/sim/tests/ref/tracerconf.sh | cut -d"'" -f2)'; build $K/kit/classes" >&2
rm -rf $K/kit/classes/src
cp -r $TOP/sim/ref/tables/replayer $K/kit/tables/
cp $TOP/sim/ref/meshsim/hops.txt $K/kit/run/ 2>/dev/null || true
echo "meshsim $R (sim-w14), built $(date -u +%F)" > $K/kit/VERSION
(cd $K/kit && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf $K/kit.tgz -C $K kit
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER14 (branch sim-w14 $R).
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end; inputs only read.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w14-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KS=\$W/kit/sim; KC=\$W/kit/classes; KT=\$W/kit/tables/replayer; KR=\$W/kit/run
DATA=/home/questrunner/data; [ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
D9=\$DATA/w9-deliveries; D26=\$DATA/w26-jumpmap
FAIL=0
sec() { echo; echo "==== \$* (\$(date -u +%T))"; }
verdict() { echo "VERDICT \$1: \$2"; echo "\$1 \$2" >> "\$RES/VERDICTS"; [ "\$2" = PASS ] || FAIL=1; }
echo "kit: \$(cat \$W/kit/VERSION); host \$(hostname); \$(nproc) CPUs; \$(java -version 2>&1 | head -1); \$(clang++ --version | head -1)"
HEAD
cat "$BODY"
cat <<TAIL
sec "verdict"
cat "\$RES/VERDICTS"
rm -rf "\$W"
[ \$FAIL = 0 ] && { echo "ALL PASS"; exit 0; } || { echo "SOME FAILED"; exit 1; }
__KIT_BEGIN__
$(base64 -w 0 $K/kit.tgz)
__KIT_END__
TAIL
rm -rf $K
