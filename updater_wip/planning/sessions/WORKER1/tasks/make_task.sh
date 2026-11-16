#!/bin/sh
# Writes the Quest task for WORKER1's frames: make_task.sh <number> <meshsim commit> <out task file>
# The kit is a git archive of the commit (gold with the frames, and the frozen files its tests read), embedded in the
# task as base64 with its SHA-256; the inputs come from Quest task 111's own kit (jolt-asic-planning fa59d9a), checked.
set -eu
N=$1; C=$2; OUT=$3
cd "$(git rev-parse --show-toplevel)"
K=$(mktemp -d)
git archive --format=tar.gz -o "$K/kit.tgz" "$C" updater_wip/pipeline/gold v1.36/pipeline/gold/src v1.36/pipeline/gold/test/guests v1.36/pipeline/jumpmap/test \
  v1.3/planning/sessions/WORKER18-data/v13/segment-files-v1.3.sha256
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
NAME=$N-w1-frames
cat > "$OUT" <<TASK
#!/bin/bash
# Task $NAME -- meshsim updater_wip WORKER1: the host's frames for v1.4 (meshsim updater-w1 at ${C}).
#   gold's build.sh: its tests, the frames on every guest (--verify, frames-read --check --seg, FramesTest), the segments
#     command byte-identical to v1.36 gold's on every guest and the guests as expected;
#   the small inputs (EVM Zone trace-23, Cyclone input/22, the hard cuts) and both workloads at 2^29 (test/frames/workloads.sh):
#     the frames three times (a full table, no compression; the static base, the smaller form (the real configuration); the static base, LRU, N 256), each with --verify and frames-read --check --seg;
#     at 2^29 the segments command's files against their published v1.3 SHA-256s; small: against v1.36 gold's, byte for byte.
# The inputs are Quest task 111's kit (tasks/111-w27-inputs-evm-zone-29.sh, jolt-asic-planning fa59d9a), checked by SHA-256.
# Reads no persistent data and writes none; scratch in /tmp, removed at the start and the end. Safe to re-run. ~10 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w1-$N; rm -rf "\$W" /tmp/gold-frames-guests /tmp/gold-old-unchanged /tmp/gold-v136-classes; mkdir -p "\$W"
echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; python3 --version
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
[ "\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1)" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
mkdir -p "\$W/m" && tar -xzf "\$W/kit.tgz" -C "\$W/m" || exit 2
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' tasks/111-w27-inputs-evm-zone-29.sh | sed '1d;\$d' | base64 -d > "\$W/kit111.tgz" || exit 2
[ "\$(sha256sum "\$W/kit111.tgz" | cut -d' ' -f1)" = "3ddbcebf08ff8cf3aaf75c1b88fd5eceb93a2a62ce2c942b42ef55f14c0a00b3" ] || { echo "task 111's kit differs"; exit 2; }
mkdir -p "\$W/i" && tar -xzf "\$W/kit111.tgz" -C "\$W/i" k/jap || exit 2
T=\$W/i/k/jap/tracer
cd "\$W/m/updater_wip/pipeline/gold"
V=\$RES/VERDICTS; : > "\$V"
step() { name=\$1; shift; t0=\$(date +%s); "\$@" > "\$RES/\$name.txt" 2>&1; rc=\$?
  echo "\$name: \$([ \$rc = 0 ] && echo PASS || echo FAIL) (\$(( \$(date +%s) - t0 )) s)" | tee -a "\$V"; tail -3 "\$RES/\$name.txt"; }
step build ./build.sh
cp /tmp/gold-frames-guests.log "\$RES/guests.txt" 2>/dev/null
step small sh test/frames/workloads.sh "\$T" "\$RES/small" small
step small-old-unchanged sh test/frames/old_unchanged.sh \$T/evm-zone-4e3451c1d8c5 \$T/evm-zone-4e3451c1d8c5/inputs/trace-23-812ded164e49 test/cuts/evm-zone-23-hardcuts.cuts \$T/cyclone \$T/cyclone/input/22/input-default test/cuts/cyclone-22-hardcuts.cuts
step 29 sh test/frames/workloads.sh "\$T" "\$RES/29" 29
cd "\$REPO"; rm -rf "\$W" /tmp/gold-frames-guests /tmp/gold-old-unchanged /tmp/gold-v136-classes
cat "\$V"; echo "end \$(date -u)"
! grep -q FAIL "\$V"
exit \$?
__KIT_BEGIN__
TASK
base64 -w 76 "$K/kit.tgz" >> "$OUT"
echo "__KIT_END__" >> "$OUT"
rm -rf "$K"
echo "wrote $OUT ($(wc -c < "$OUT") bytes), kit $SHA"
