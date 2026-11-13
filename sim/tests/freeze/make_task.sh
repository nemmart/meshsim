#!/bin/bash
# make_task.sh -- the v1.35 freeze check as a Quest task (WORKER22; PLAN-V135 step 4). Writes a self-contained task with
# an embedded kit: sim/ at HEAD without ref/, oracle/, planning/ and pipeline/'s tools other than keys/ (the stand-alone
# tree: sources, data, firmware images, every fixture), built on the runner with clang; W16's rows reference (t2's
# --rows-ref) in run/; the acceptance kit's scripts (calibrate.sh, run_shard.sh, build.sh) at the kit's top, laid out as
# tests/acceptance/make_kit.sh lays them. The body is tests/freeze/body.sh (inlined; its header lists the items).
#   tests/freeze/make_task.sh NNN-name > NNN-name.sh      (run from anywhere in the repository; commit first)
# Reads on the runner, from its persistent data ($DATA, PIPELINE.md): w7-golden-m2 (the full-size sort's fingerprints),
# w13-golden-m3 (T2's references), w9-deliveries and w26-jumpmap (the chains' inputs). Writes only results/NNN-name/.
set -eu
NAME=${1:?usage: make_task.sh NNN-name}
TOP=$(git rev-parse --show-toplevel); HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD); BR=$(cd "$TOP" && git rev-parse --abbrev-ref HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- sim | grep -v '^??')" ] || { echo "make_task.sh: sim/ has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/kit/run"
(cd "$TOP" && git archive --format=tar HEAD sim) | tar -x -C "$K/kit" --exclude=sim/ref --exclude=sim/oracle --exclude=sim/planning \
    --exclude=sim/pipeline/gold --exclude=sim/pipeline/jumpmap --exclude=sim/pipeline/inputs
(cd "$TOP" && git show HEAD:sim/planning/sessions/WORKER16-tasks/rows.txt) > "$K/kit/run/rows.txt"
cp "$TOP/sim/tests/acceptance/calibrate.sh" "$TOP/sim/tests/acceptance/run_shard.sh" "$TOP/sim/tests/acceptance/build.sh" "$K/kit/"
echo "meshsim $R ($BR), the freeze check kit, built $(date -u +%F)" > "$K/kit/VERSION"
(cd "$K/kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf "$K/kit.tgz" -C "$K" kit
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim v1.35's freeze check (sim/ $R, $BR; tests/freeze/make_task.sh, WORKER22). PLAN-V135 step 4:
# 1. ./run_tests.sh all --check (every step, the full-size ones included); 2. the full-size sort (p4 uniform-k128,
# every fingerprint point against golden-m2, TOTAL 1,229,687, one thread; bench_sort's digest, one thread against 6 by tiles); 3. T2 on evm-zone-29 and
# cyclone-29 segment 10, every point against golden-m3, one domain and 6; 4. the two 128-segment chains, every segment's
# checks; 5. the replay acceptance kit's calibration (128 synthetic windows, seed 11); 6. the machine (D21), the firmware
# toolchain's versions and the image builds' locale (LC_ALL=C). VERDICTS:
# one line per item (and per run within an item). Not timing (the items overlap; each one's time recorded to plan).
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end; inputs only read.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/freeze-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2; rm -f "\$W/kit.tgz"
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KIT=\$W/kit; KS=\$KIT/sim
DATA=/home/questrunner/data; [ -d "\$DATA/w13-golden-m3" ] || DATA=\$REPO/data
HEAD
cat "$HERE/body.sh"
echo "exit \$FREEZE_RC"
echo "__KIT_BEGIN__"
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
