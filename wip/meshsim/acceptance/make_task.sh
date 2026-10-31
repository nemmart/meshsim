#!/usr/bin/env bash
# make_task.sh -- writes a Quest task (meshsim WORKER11) with the acceptance kit embedded as a base64 tgz, SHA-256
# checked (the runner can't reach private repositories; PIPELINE.md, task 074's pattern).
#   ./acceptance/make_task.sh KIT.tgz TASKNAME 'COMMAND' [SETUP.sh] > NNN-TASKNAME.sh
# COMMAND runs in the unpacked kit with $RES (the task's results directory, committed) and $W (scratch) set, e.g.
#   './calibrate.sh $RES/calib'  or  './run_shard.sh -p cyclone -f 0 -n 700 -w "$WRITER" $RES/cyclone-0'
# SETUP.sh, if given, is inlined before COMMAND (e.g. unpacking a hand-copied writer tarball and setting $WRITER).
set -eu
KIT=$1; NAME=$2; CMD=$3; SETUP=${4:-}
SHA=$(sha256sum "$KIT" | cut -d' ' -f1)
cat <<HDR
#!/bin/bash
# Task $NAME -- meshsim v1.25 acceptance kit (WORKER11, branch v1.25-meshsim). The kit (meshsim.jar, Java 17 bytecode;
# the prebuilt replay controller; the universal replayer tables; run_shard.sh, calibrate.sh) is embedded below as a base64
# tgz, SHA-256 checked. Runs: $CMD
# Commits only results (CSV, summaries, logs); deliveries are generated into scratch and deleted (DECISIONS-v1.25 s7).
set -u
cd "\$(dirname "\$0")/.."
RES=\$PWD/results/$NAME; mkdir -p "\$RES"
WANT=$SHA
W=/tmp/w11-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "\$WANT" ] || { echo "kit sha256 mismatch (want \$WANT)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cd "\$W/w11-kit" || exit 2
sha256sum -c --quiet HASHES || { echo "kit files differ from HASHES"; exit 2; }
echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; free -g | head -2
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
