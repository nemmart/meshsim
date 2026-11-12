#!/usr/bin/env bash
# make_task.sh -- writes a Quest task (v1.35, WORKER25; v1.3's acceptance/make_task.sh, meshsim WORKER11) with the
# acceptance kit embedded as a base64 tgz, SHA-256 checked (the runner can't reach private repositories; PIPELINE.md).
# The task unpacks the kit, checks HASHES, builds it (./build.sh, clang) and records the machine (sim/tools/machine.sh,
# D21) before running COMMAND.
#   tests/acceptance/make_task.sh KIT.tgz TASKNAME 'COMMAND' [SETUP.sh] > NNN-TASKNAME.sh
# COMMAND runs in the unpacked kit with $RES (the task's results directory, committed) and $W (scratch) set, e.g.
#   './calibrate.sh $RES/calib'  or  './run_shard.sh -d /path -p cyclone-29 -f 0 -n 700 $RES/cyclone-0'
# SETUP.sh, if given, is inlined before COMMAND.
set -eu
KIT=$1; NAME=$2; CMD=$3; SETUP=${4:-}
SHA=$(sha256sum "$KIT" | cut -d' ' -f1)
cat <<HDR
#!/bin/bash
# Task $NAME -- meshsim v1.35 acceptance kit (sim/ WORKER25; v1.3's: WORKER11, WORKER20, WORKER25). The kit (sim/'s
# sources for replay_shard, shard_summary and synthetic_delivery, built here with clang; the replay controller's images;
# the universal replayer tables; run_shard.sh, calibrate.sh) is embedded below as a base64 tgz, SHA-256 checked.
# Runs: $CMD
# Commits only results (CSV, summaries, logs); deliveries are only read, or generated into scratch and deleted.
set -u
cd "\$(dirname "\$0")/.."
RES=\$PWD/results/$NAME; mkdir -p "\$RES"
WANT=$SHA
W=/tmp/w25-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "\$WANT" ] || { echo "kit sha256 mismatch (want \$WANT)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cd "\$W/w25-kit" || exit 2
sha256sum -c --quiet HASHES || { echo "kit files differ from HASHES"; exit 2; }
echo "start \$(date -u) on \$(hostname), \$(nproc) threads; kit \$(cat VERSION)"; free -g | head -2
bash sim/tools/machine.sh > "\$RES/machine.txt" 2>&1 || true
./build.sh > "\$RES/build.log" 2>&1 || { cat "\$RES/build.log"; echo "the kit did not build"; exit 2; }
tail -1 "\$RES/build.log"
HDR
[ -n "$SETUP" ] && cat "$SETUP"
cat <<CMDS
$CMD; RC=\$?
echo "exit \$RC at \$(date -u)"
rm -rf "\$W"
exit \$RC
__KIT_BEGIN__
CMDS
base64 -w 76 "$KIT"
echo "__KIT_END__"
