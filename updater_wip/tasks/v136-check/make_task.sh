#!/bin/bash
# make_task.sh -- v1.36's own checks as a Quest task, before anything is built on v1.36 (owner, 2026-11-16).
#   updater_wip/tasks/v136-check/make_task.sh NNN-name suites|check > NNN-name.sh     (commit first: the kit is HEAD's)
# Writes a self-contained task with an embedded kit: v1.36/ at HEAD (git archive), and gold's and jumpmap's classes
# built here (pipeline/inputs/lib/classes.sh), so that the runner needs no javac. The body is body.sh (its header lists
# the items). The suites item also reads Quest task 111's kit from the queue (jolt-asic-planning at the pin).
# Reads no persistent data and writes none; writes only results/NNN-name/. Safe to re-run.
set -eu
export LC_ALL=C
NAME=${1:?usage: make_task.sh NNN-name suites|check}; ITEM=${2:?suites or check}
case $ITEM in suites|check) ;; *) echo "ITEM is suites or check" >&2; exit 1;; esac
TOP=$(git rev-parse --show-toplevel); HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD); BR=$(cd "$TOP" && git rev-parse --abbrev-ref HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- v1.36 | grep -v '^??')" ] || { echo "make_task.sh: v1.36/ has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/kit"
(cd "$TOP" && git archive --format=tar HEAD v1.36) | tar -x -C "$K/kit"
if [ "$ITEM" = suites ]; then bash "$K/kit/v1.36/pipeline/inputs/lib/classes.sh" "$K/kit/v1.36" "$K/kit/classes" >&2; fi
echo "meshsim $R ($BR), v1.36's check kit ($ITEM), built $(date -u +%F)" > "$K/kit/VERSION"
(cd "$K/kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf "$K/kit.tgz" -C "$K" kit
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim v1.36's own checks (v1.36/ at $R, $BR; updater_wip/tasks/v136-check/make_task.sh), item: $ITEM.
# Before the Updater and the cache controller are built on v1.36 (owner, 2026-11-16): suites = the small inputs built
# from the pinned sources, then ./run_tests.sh all --check (every suite; the chip under meshsim's firmware: the sort at
# 64 x 16 and 64 x 65,536, the memory tile, three generated segments and the real ones, EVM Zone's segments 0 and 1 and
# Cyclone's 0, traced and replayed); check = make check from a clean tree (debug and release, gcc and clang, TSan).
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/v136-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2; rm -f "\$W/kit.tgz"
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KIT=\$W/kit; KS=\$KIT/v1.36; REV=$R; ITEM=$ITEM
HEAD
cat "$HERE/body.sh"
echo "__KIT_BEGIN__"
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
