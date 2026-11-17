#!/bin/bash
# make_task.sh -- v1.37_wip's checks as a Quest task (WORKER3; v136-check's generator, for v1.37_wip).
#   updater_wip/tasks/v137-check/make_task.sh NNN-name suites|suites-cache|suites-ctl|suites-ctl-varied|check|faster > NNN-name.sh     (commit first: the kit is HEAD's)
# (suites-cache, WORKER5's: suites with CACHE_CTL=1, the proof steps on the cache's controller; suites-ctl and
# suites-ctl-varied, WORKER6's: suites with the Updater's controller in the chip, its timing fixed and varied; body.sh's header)
# Writes a self-contained task with an embedded kit: v1.37_wip/ and updater_wip/pipeline/gold at HEAD (git archive),
# gold's and jumpmap's classes built here (pipeline/inputs/lib/classes.sh) and the frames' gold's (updater_wip/pipeline/
# gold: classes-frames), so that the runner needs no javac, and, for suites, the link's test vectors:
# updater_wip/pipeline/gold/test/packets, HEAD's if it has them, else those of $VECTORS_REV (default origin/updater-w2).
# The body is body.sh (its header lists the items). The suites item also reads Quest task 111's kit from the queue
# (jolt-asic-planning at the pin, with the workloads' inputs, the 2^29 ones too).
# Reads no persistent data and writes none; writes only results/NNN-name/. Safe to re-run.
set -eu
export LC_ALL=C
NAME=${1:?usage: make_task.sh NNN-name suites|suites-cache|suites-ctl|suites-ctl-varied|check|faster}; ITEM=${2:?suites, suites-cache, suites-ctl, suites-ctl-varied, check or faster}
case $ITEM in suites|suites-cache|suites-ctl|suites-ctl-varied|check|faster) ;; *) echo "ITEM is suites, suites-cache, suites-ctl, suites-ctl-varied, check or faster" >&2; exit 1;; esac
TOP=$(git rev-parse --show-toplevel); HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$TOP" && git rev-parse --short HEAD); BR=$(cd "$TOP" && git rev-parse --abbrev-ref HEAD)
[ -z "$(cd "$TOP" && git status --porcelain -- v1.37_wip updater_wip/pipeline/gold | grep -v '^??')" ] || { echo "make_task.sh: v1.37_wip/ or gold has uncommitted changes" >&2; exit 1; }
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/kit"
(cd "$TOP" && git archive --format=tar HEAD v1.37_wip updater_wip/pipeline/gold) | tar -x -C "$K/kit"
VEC="HEAD's"
if [ "$ITEM" != check ]; then
  bash "$K/kit/v1.37_wip/pipeline/inputs/lib/classes.sh" "$K/kit/v1.37_wip" "$K/kit/classes" >&2
  mkdir -p "$K/kit/classes-frames"
  javac -d "$K/kit/classes-frames" $(find "$K/kit/updater_wip/pipeline/gold/src" -name '*.java') >&2
  P=updater_wip/pipeline/gold/test/packets
  if [ ! -f "$K/kit/$P/VECTORS.txt" ]; then
    VR=${VECTORS_REV:-origin/updater-w2}; VEC="$(cd "$TOP" && git rev-parse --short "$VR")'s ($VR)"
    (cd "$TOP" && git archive --format=tar "$VR" "$P") | tar -x -C "$K/kit"
  fi
  [ -f "$K/kit/$P/VECTORS.txt" ] || { echo "make_task.sh: no VECTORS.txt" >&2; exit 1; }
fi
echo "meshsim $R ($BR), v1.37_wip's check kit ($ITEM; the link's vectors: $VEC), built $(date -u +%F)" > "$K/kit/VERSION"
(cd "$K/kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 -czf "$K/kit.tgz" -C "$K" kit
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim v1.37_wip's checks (v1.37_wip/ at $R, $BR; updater_wip/tasks/v137-check/make_task.sh), item: $ITEM.
# v1.37 is v1.36 with Ingress's host link, the Updater and a file store that places pages, behind a switch that is off
# by default, and the master tile's program that runs the whole chip from a packet file (updater_wip/planning/sessions/
# WORKER3). suites = the small inputs built from the pinned sources; the host's side of the proof steps (gold's frames,
# packets and reference images: the 24-run matrix, the three recorded segments, both workloads at 2^29); then
# ./run_tests.sh all --check: every v1.36 step against its v1.36 log, and v1.37's own steps, the proof steps with them;
# the proof runs' figures. check = make check from a clean tree (debug and release, gcc and clang, TSan). faster = the
# same host's side, then the 2^29 runs and the matrix with the link at 2, 4 and 8 bytes a cycle, and the matrix with the
# link as fast as taken (WORKER7; WORKER4's thresholds before), with their figures.
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/v137-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2; rm -f "\$W/kit.tgz"
(cd "\$W/kit" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
KIT=\$W/kit; KS=\$KIT/v1.37_wip; REV=$R; ITEM=$ITEM
HEAD
cat "$HERE/body.sh"
echo "__KIT_BEGIN__"
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
