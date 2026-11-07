#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" NAME=REV [NAME=REV ...] > task.sh (WORKER10): a self-contained Quest task with an
# embedded kit holding sim/ (Makefile, src, tests, bench, tools) at each named revision, unpacked to $W/kit/NAME/sim, so
# a before/after pair is built and run in one task. The body (sourced) sees KITS ("NAME ..."), sec, verdict, run (common.inc)
# and the machine (sim/tools/machine.sh of the last revision, D21). Run from inside the repo; commit first.
set -eu
NAME=$1 BODY=$2 HDR=$3; shift 3
TOP=$(git rev-parse --show-toplevel)
KIT=$(mktemp -d); mkdir -p "$KIT/k"
NAMES=; DESC=
for nv in "$@"; do
  n=${nv%%=*}; r=$(cd "$TOP" && git rev-parse --short "${nv#*=}")
  mkdir -p "$KIT/k/$n"
  ( cd "$TOP" && git archive --format=tar "$r" sim/Makefile sim/src sim/tests sim/bench sim/tools ) | tar -xf - -C "$KIT/k/$n"
  NAMES+="${NAMES:+ }$n"; DESC+="${DESC:+, }$n $r"; LAST=$n
done
( cd "$KIT/k" && tar --sort=name --mtime=@0 --owner=0 --group=0 -cf - . ) | gzip -9n > "$KIT/kit.tgz"
SHA=$(sha256sum "$KIT/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER10 (branch sim-w10; kit: $DESC). TIMING TASK: keep the box quiet while it runs.
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit (sim/ at each revision named above). Writes only results/$NAME/. Scratch in /tmp, removed first
# and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"; rm -f "\$RES/raw.txt"
W=/tmp/w10-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
mkdir -p "\$W/kit" && tar -xzf "\$W/kit.tgz" -C "\$W/kit" || exit 2
KITS="$NAMES"
FAILS=0; FAIL=0
sec() { echo; echo "==== \$*  [\$(date +%T)]"; }
verdict() { echo "VERDICT \$1: \$2"; [ "\$2" = PASS ] || FAIL=1; }
START=\$(date +%s)
{
sec "machine (D21)"; . "\$W/kit/$LAST/sim/tools/machine.sh" "\${QUEST_P_CPUS:-}" "\${QUEST_E_CPUS:-}"; machine_record "\$RES/machine.txt"; uptime
echo "kit: $DESC"
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
