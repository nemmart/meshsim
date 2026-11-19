#!/bin/bash
# make_task.sh -- the wire's checks (WORKER10) as a Quest task.
#   wire/tests/make_task.sh NNN-name [STEP...] > NNN-name.sh      (commit first: the kit is HEAD's)
# STEP: build matrix long stopping plants sanitize bench (all by default; see tests/run_checks.sh).
# Env passed on to the task: REPEATS (5; each matrix case's repeats in each mode), ROUNDS (5; the stopping rounds).
# Writes a self-contained task with an embedded kit: the wire's folder, the one this script's tests/ is in, at HEAD
# (git archive). The runner needs g++,
# clang++ with its sanitizer runtimes, make, bc, taskset, pgrep and gdb (the step under a debugger is skipped without it).
# Works on any number of cores, only slower with fewer than the processes (17 for a chain of 16); the results say how
# many there were and how long each step took. Writes only results/NNN-name/. Scratch in /tmp. Safe to re-run.
set -eu
export LC_ALL=C
NAME=${1:?usage: make_task.sh NNN-name [STEP...]}; shift
STEPS=${*:-build matrix long stopping plants sanitize bench}
for s in $STEPS; do case $s in build|matrix|long|stopping|plants|sanitize|bench) ;; *) echo "unknown step $s" >&2; exit 1;; esac; done
REPEATS=${REPEATS:-5}; ROUNDS=${ROUNDS:-5}
TOP=$(git rev-parse --show-toplevel)
WIRE=$(cd "$(dirname "$0")/.." && git rev-parse --show-prefix); WIRE=${WIRE%/}   # the wire's folder, from the repository's top
R=$(cd "$TOP" && git rev-parse --short HEAD); BR=$(cd "$TOP" && git rev-parse --abbrev-ref HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- "$WIRE" | grep -v '^??')" ] || { echo "make_task.sh: $WIRE has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/kit"
(cd "$TOP" && git archive --format=tar HEAD "$WIRE") | tar -x -C "$K/kit"
echo "meshsim $R ($BR), $WIRE, built $(date -u +%F)" > "$K/kit/VERSION"
(cd "$K/kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf "$K/kit.tgz" -C "$K" kit
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim's wire between processes ($WIRE/ at $R, $BR; $WIRE/tests/make_task.sh).
# Steps: $STEPS (REPEATS=$REPEATS, ROUNDS=$ROUNDS). The wire joins the chain's processes (driver, ASICs) by shared memory
# with a sync every window; these runs check that the processes give exactly what a one-process reference gives, every
# byte at every end in every cycle, under load, pinned to one core and with random pauses; that stopping never hangs
# and leaves nothing behind; that planted bugs are caught; the sanitizers; and the cost (WORKER10's report, in the
# version's planning/updater/sessions/WORKER10). From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/wire-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2; rm -f "\$W/kit.tgz"
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
{ echo "kit: \$(cat "\$W/kit/VERSION")"; echo "host \$(hostname); \$(nproc) CPUs; \$(uname -sr)"
  g++ --version 2>&1 | head -1; clang++ --version 2>&1 | head -1; gdb --version 2>&1 | head -1; free -g | sed -n 2p
} | tee "\$RES/machine.txt"
T0=\$(date +%s)
cd "\$W/kit/$WIRE"
REPEATS=$REPEATS ROUNDS=$ROUNDS bash tests/run_checks.sh "\$RES/logs" $STEPS; rc=\$?
cp "\$RES/logs/summary.txt" "\$RES/SUMMARY.txt"
echo "total \$(( \$(date +%s) - T0 )) s on \$(nproc) CPUs" | tee -a "\$RES/SUMMARY.txt"
if [ \$rc = 0 ]; then echo "VERDICT wire-check: PASS" | tee "\$RES/VERDICTS"; else echo "VERDICT wire-check: FAIL" | tee "\$RES/VERDICTS"; fi
cd "\$REPO"; rm -rf "\$W"
exit \$rc
EOF
echo "__KIT_BEGIN__"
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
