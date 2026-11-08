#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" [REV] > task.sh (WORKER12): a self-contained Quest task with an embedded kit,
# sim/ at REV (default HEAD: Makefile, src, tests, tools, ref/meshsim's src, test, sw and hops.txt), unpacked to
# $W/kit/sim. The body (sourced) sees KS (the kit's sim/), RES, W, sec, verdict and the machine (sim/tools/machine.sh, D21).
# Not a timing task: the queue runs one task at a time, so it never overlaps W10's measurements. Run from inside the repo;
# commit first.
set -eu
NAME=$1 BODY=$2 HDR=$3 REV=${4:-HEAD}
TOP=$(git rev-parse --show-toplevel)
R=$(cd "$TOP" && git rev-parse --short "$REV")
KIT=$(mktemp -d)
( cd "$TOP" && git archive --format=tar "$R" sim/Makefile sim/src sim/tests sim/tools sim/fw sim/bench \
    sim/ref/meshsim/src sim/ref/meshsim/test sim/ref/meshsim/sw sim/ref/meshsim/hops.txt ) | tar -xf - -C "$KIT"
for need in sim/tests/golden-m2/images sim/tests/golden-m2/sort64k.out sim/tests/golden-m2/regen.sh sim/ref/meshsim/test/KeyGen.java; do
  [ -e "$KIT/$need" ] || { echo "mk_task.sh: the kit lacks $need" >&2; exit 1; }
done
( cd "$KIT" && tar --sort=name --mtime=@0 --owner=0 --group=0 -cf - sim ) | gzip -9n > "$KIT/kit.tgz"
SHA=$(sha256sum "$KIT/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER12 (branch sim-w12 $R). Not a timing task.
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit (sim/ at $R). Writes only results/$NAME/. Scratch in /tmp, removed first and at the end.
# Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w12-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
mkdir -p "\$W/kit" && tar -xzf "\$W/kit.tgz" -C "\$W/kit" || exit 2
KS=\$W/kit/sim
FAIL=0
sec() { echo; echo "==== \$*  [\$(date +%T)]"; }
verdict() { echo "VERDICT \$1: \$2"; [ "\$2" = PASS ] || FAIL=1; }
START=\$(date +%s)
{
sec "machine (D21)"; . "\$KS/tools/machine.sh" "\${QUEST_P_CPUS:-}" "\${QUEST_E_CPUS:-}"; machine_record "\$RES/machine.txt"; uptime
echo "kit: sim-w12 $R"
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
