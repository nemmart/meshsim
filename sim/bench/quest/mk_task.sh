#!/bin/bash
# mk_task.sh NNN-name BODY "header lines" > task.sh: a self-contained Quest task with an embedded kit (sim/src/base and
# sim/bench at HEAD), the body sourced after the kit is checked and unpacked. Run from sim/bench. The machine comes from
# sim/tools/machine.sh (D21): P_CPUS, P_ONE, E_CPUS, PLACE and NP, NPT, NE, P1, E1 for the bodies; nothing hardcoded.
set -eu
NAME=$1 BODY=$2 HDR=$3
make -s kit >&2
SHA=$(sha256sum build/kit.tgz | cut -d' ' -f1)
REV=$(git rev-parse --short HEAD)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER6 (branch sim-w6 $REV). TIMING TASK: keep the box quiet while it runs.
$(echo "$HDR" | sed 's/^/# /')
# From an embedded kit (sim/src/base and sim/bench at $REV). Writes only results/$NAME/. Scratch in /tmp, removed first
# and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"; rm -f "\$RES/raw.txt"
W=/tmp/w6-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
mkdir -p "\$W/kit" && tar -xzf "\$W/kit.tgz" -C "\$W/kit" || exit 2
K=\$W/kit/sim/bench
FAILS=0; FAIL=0
sec() { echo; echo "==== \$*  [\$(date +%T)]"; }
verdict() { echo "VERDICT \$1: \$2"; [ "\$2" = PASS ] || FAIL=1; }
START=\$(date +%s)
{
sec "machine (D21)"; . "\$K/../tools/machine.sh" "\${QUEST_P_CPUS:-}" "\${QUEST_E_CPUS:-}"; machine_record "\$RES/machine.txt"; uptime
NP=\$(count "\$P_ONE"); NPT=\$(count "\$P_CPUS"); NE=\$(count "\$E_CPUS"); P1=\$(take 1 "\$P_ONE"); E1=\$(take 1 "\$E_CPUS")
$(cat quest/common.inc)
$(cat "$BODY")
sec "done in \$((\$(date +%s) - START)) s"
} 2>&1 | tee "\$RES/run.log"
grep '^VERDICT' "\$RES/run.log" > "\$RES/VERDICTS.txt"
rm -rf "\$W"
grep -q ': FAIL\$' "\$RES/VERDICTS.txt" && exit 1
exit 0
__KIT_BEGIN__
HEAD
base64 -w 76 build/kit.tgz
echo "__KIT_END__"
