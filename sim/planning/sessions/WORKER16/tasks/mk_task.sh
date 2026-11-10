#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" > task.sh (WORKER16; WORKER14's mk_task.sh pared down): a self-contained
# Quest task with an embedded kit: sim/ (src, Makefile, bench, tools, fw, the tests' sources, tests/m3-images, ref's
# hops.txt and boot ROM where build_machine reads them: built on the runner with clang) and run/rows.txt (Quest 169's
# rows reference). No Java. Unpacked to $W/kit: $KS sim/ (make and run there). The body (sourced) sees RES, W, sec,
# verdict, DATA.
set -eu
NAME=$1 BODY=$2 HDR=$3
TOP=$(git rev-parse --show-toplevel); HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- sim)" ] || { echo "mk_task.sh: sim/ has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); mkdir -p $K/kit/run
(cd "$TOP" && git archive --format=tar HEAD sim/src sim/Makefile sim/bench sim/tools sim/fw sim/tests/m3-images sim/ref/meshsim/hops.txt sim/ref/meshsim/sw/bootrom.hex) | tar -x -C $K/kit
mkdir -p $K/kit/sim/tests && (cd "$TOP/sim/tests" && cp *.cpp *.h $K/kit/sim/tests/)
cp "$HERE/rows.txt" $K/kit/run/
echo "meshsim $R (sim-w16), built $(date -u +%F)" > $K/kit/VERSION
(cd $K/kit && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf $K/kit.tgz -C $K kit
SHA=$(sha256sum $K/kit.tgz | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER16 (branch sim-w16 $R).
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end; inputs only read.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w16-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KS=\$W/kit/sim
DATA=/home/questrunner/data; [ -d "\$DATA/w13-golden-m3" ] || DATA=\$REPO/data
FAIL=0
sec() { echo; echo "==== \$* (\$(date -u +%T))"; }
verdict() { echo "VERDICT \$1: \$2"; echo "\$1 \$2" >> "\$RES/VERDICTS"; [ "\$2" = PASS ] || FAIL=1; }
echo "kit: \$(cat \$W/kit/VERSION); host \$(hostname); \$(nproc) CPUs; \$(clang++ --version | head -1)"
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
