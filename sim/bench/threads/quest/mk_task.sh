#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" > task.sh: a self-contained Quest task (WORKER4) with an embedded kit (sim/'s
# Makefile, src/ and the tests and bench files the threads need, at HEAD: commit first), the body run after the kit is
# checked and unpacked. Run from anywhere inside the repo.
set -eu
NAME=$1 BODY=$2 HDR=$3
TOP=$(git rev-parse --show-toplevel)
REV=$(git rev-parse --short HEAD)
KIT=$(mktemp -d)
( cd "$TOP" && git archive --format=tar HEAD sim/Makefile sim/src sim/bench/threads \
    sim/tests/harness.h sim/tests/harness.cpp sim/tests/synth.h sim/tests/test_parallel.cpp sim/tests/test_threads.cpp 2>/dev/null \
  || git archive --format=tar HEAD sim/Makefile sim/src sim/bench/threads sim/tests/harness.h sim/tests/harness.cpp \
    sim/tests/synth.h sim/tests/test_parallel.cpp ) | gzip -9n > "$KIT/kit.tgz"
SHA=$(sha256sum "$KIT/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER4 (branch sim-w4 $REV). TIMING TASK: keep the box quiet while it runs.
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit (sim/ at $REV: Makefile, src, bench/threads, the thread tests). Writes only results/$NAME/.
# Scratch in /tmp, removed first and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"; rm -f "\$RES/raw.txt"
W=/tmp/w4-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
mkdir -p "\$W/kit" && tar -xzf "\$W/kit.tgz" -C "\$W/kit" || exit 2
S=\$W/kit/sim
FAILS=0; FAIL=0
sec() { echo; echo "==== \$*  [\$(date +%T)]"; }
verdict() { echo "VERDICT \$1: \$2"; [ "\$2" = PASS ] || FAIL=1; }
START=\$(date +%s)
{
sec "machine"; uname -r; nproc; lscpu | grep -E 'Model name|^CPU\\(s\\)'; uptime; clang++ --version | head -1
$(cat "$(dirname "$0")/common.inc")
$(cat "$BODY")
sec "done in \$((\$(date +%s) - START)) s"
} 2>&1 | tee "\$RES/run.log"
grep '^VERDICT' "\$RES/run.log" > "\$RES/VERDICTS.txt"
rm -rf "\$W"
grep -q ': FAIL\$' "\$RES/VERDICTS.txt" && exit 1
exit 0
__KIT_BEGIN__
HEAD
base64 -w 76 "$KIT/kit.tgz"
echo "__KIT_END__"
rm -rf "$KIT"
