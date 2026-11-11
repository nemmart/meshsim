#!/bin/bash
# oracle/quest/inputs_make_task.sh (v1.3's inputs/quest/make_task.sh, moved by W19: Quest task infrastructure, not the
# pipeline's function; it packs the repository's wip/ layout as v1.3 did and is kept as v1.3 left it, unchecked)
# quest/make_task.sh NNN WORKLOAD JAP_DIR LIBELF_FROM > NNN-w27-inputs-RUN.sh: the 2^29 build of one workload as a Quest task.
#   JAP_DIR      a jolt-asic-planning checkout at the pin (fetch.sh): its tracer/ is packed by git archive
#   LIBELF_FROM  Quest's tasks/103-w20-regen-evm-zone-29.sh: its tarball's libelf (Ubuntu 24.04 debs), for a runner
#                without libelf-dev
# The kit (embedded, SHA-256 checked): this checkout's wip/inputs/ and v1.26.1/tables/replayer, gold, jumpmap and
# ConvertDelivery compiled here for Java 21 (the runner has no javac), the tracer at the pin, the libelf fallback.
# The task builds every step into $DATA/w27-inputs (persistent data, never /tmp), compares it with the runner's copies
# (compare_runner.sh), records the manifest into results/, and deletes $DATA/w27-inputs when the comparison passes.
set -eu
N=$1; WL=$2; JAP=$(cd "$3" && pwd); L103=$4
HERE=$(cd "$(dirname "$0")/../../pipeline/inputs" && pwd); . "$HERE/pins.sh"   # W19: moved from inputs/quest/
MESHSIM=$(cd "$HERE/../../.." && pwd)
RUN=$(run_name "$WL" 29); NAME=$N-w27-inputs-$RUN
[ "$(git -C "$JAP" rev-parse HEAD)" = "$JAP_COMMIT" ] || { echo "$JAP is not at $JAP_COMMIT" >&2; exit 1; }
[ -z "$(git -C "$MESHSIM" status --porcelain -- wip/inputs wip/gold wip/jumpmap v1.26.1)" ] || { echo "commit wip/inputs first: the kit records the commit" >&2; exit 1; }
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/k/meshsim/wip" "$K/k/meshsim/$(dirname "$TABLES_REL")" "$K/k/jap"
cp -r "$HERE" "$K/k/meshsim/wip/inputs"
cp -r "$MESHSIM/$TABLES_REL" "$K/k/meshsim/$TABLES_REL"
bash "$HERE/lib/classes.sh" "$MESHSIM" "$K/k/classes" >&2
git -C "$JAP" archive "$JAP_COMMIT" tracer | tar -x -C "$K/k/jap"; echo "$JAP_COMMIT" > "$K/k/jap/COMMIT"
sed -n '/^__TGZ_BEGIN__$/,/^__TGZ_END__$/p' "$L103" | sed '1d;$d' | base64 -d | tar -xz -C "$K" w9/libelf; mv "$K/w9/libelf" "$K/k/libelf"; rmdir "$K/w9"
MC=$(git -C "$MESHSIM" rev-parse --short HEAD); GC=$(git -C "$MESHSIM" log -1 --format=%h -- wip/gold)
echo "meshsim $MC (inputs-pipeline), gold $GC, jolt-asic-planning $JAP_COMMIT, built $(date -u +%F)" > "$K/k/VERSION"
(cd "$K/k" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf - -C "$K" k | gzip -n > "$K/kit.tgz"
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER27: the input pipeline (meshsim inputs-pipeline, wip/inputs/, at $MC) rebuilds every input of
# $RUN from the pinned sources, as on a fresh machine, and proves it identical to the runner's copies in persistent data:
#   deliveries (WORKER9's delivery_writer at jolt-asic-planning ${JAP_COMMIT:0:7}, jolt-cpp 88dca85) and the selected windows,
#     against tasks 103/104 (written at 8d546e3, jolt-cpp 821e1bd); the v1.26.1 conversion against 105-107's logged manifests;
#   gold's segment file and jumpmap's outputs against task 109.
# Writes \$DATA/w27-inputs (persistent data; about 47 GB), deleted when the comparison passes (owner, 2026-11-05); the runner's
# originals are only read. results/$NAME/: the logs, compare.txt, $RUN.sha256 (the manifest for wip/inputs/manifest/).
# Reboot-safe: scratch (/tmp) and \$DATA/w27-inputs are removed at the start, every step stages and marks COMPLETE. ~20 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
EOF
cat "$MESHSIM/wip/planning/sessions/WORKER20-tools/tasks/data_dir.inc"
cat <<EOF
echo "persistent data: \$DATA" | tee "\$RES/data_path.txt"
W=/tmp/w27-$N; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" && (cd "\$W/k" && sha256sum -c --quiet HASHES) || { echo "kit files differ from HASHES"; exit 2; }
cat "\$W/k/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1
I=\$W/k/meshsim/wip/inputs; O=\$DATA/w27-inputs
[ -e /usr/include/libelf.h ] || [ -e /usr/include/libelf/libelf.h ] || { export LIBELF_PREFIX=\$W/k/libelf; echo "no system libelf.h: the kit's"; }
OUT=\$O bash "\$I/deps.sh" --runtime | tee "\$RES/deps.txt"
for p in w9-deliveries/$RUN.COMPLETE w26-jumpmap/$RUN.COMPLETE; do [ -f "\$DATA/\$p" ] || { echo "the runner's \$p is missing (tasks 103/104, 109)"; rm -rf "\$W"; exit 2; }; done
rm -rf "\$O"; mkdir -p "\$O"
AV=\$(df -BG --output=avail "\$O" | tail -1 | tr -dc 0-9); echo "free at \$O: \${AV} GB"
[ "\$AV" -ge 60 ] || { echo "need about 47 GB plus margin at \$O"; rm -rf "\$W"; exit 4; }
T0=\$(date +%s)
OUT=\$O JAP_DIR=\$W/k/jap CLASSES=\$W/k/classes GOLD_COMMIT=$GC MESHSIM_COMMIT=$MC bash "\$I/build.sh" $WL 29 2>&1 | tee "\$RES/build.log"
BRC=\${PIPESTATUS[0]}; echo "build exit \$BRC after \$(( \$(date +%s) - T0 )) s"
cp "\$O"/logs/*.log "\$RES/" 2>/dev/null; rm -f "\$RES"/tracer-build.log
RC=1
if [ \$BRC = 0 ]; then
  OUT=\$O bash "\$I/compare_runner.sh" $RUN "\$DATA" 2>&1 | tee "\$RES/compare.txt"; RC=\${PIPESTATUS[0]}
  OUT=\$O MESHSIM_COMMIT=$MC bash "\$I/record.sh" $RUN "Quest $N, the build identical to the runner's copies (compare.txt)" && cp "\$I/manifest/$RUN.sha256" "\$RES/"
  OUT=\$O bash "\$I/check.sh" $RUN | tail -1 | tee -a "\$RES/compare.txt"
fi
du -sh "\$O" 2>/dev/null | tee "\$RES/size.txt"
if [ \$RC = 0 ]; then rm -rf "\$O"; echo "PASS: \$O deleted (the runner's originals stay)"; else echo "FAIL: \$O kept for inspection"; fi
rm -rf "\$W"
echo "end \$(date -u), exit \$RC"
exit \$RC
__KIT_BEGIN__
EOF
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
