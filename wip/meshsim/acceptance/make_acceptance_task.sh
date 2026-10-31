#!/usr/bin/env bash
# make_acceptance_task.sh -- one program's acceptance task (meshsim WORKER11; REPORT.md section 4): every window of the
# program's delivery on the runner box, Stage A and Stage B (tile 0 alone) at 16 JVMs, the full chip on every 64th.
#   ./acceptance/make_acceptance_task.sh KIT.tgz NNN PROGRAM MANIFEST_SHA256 [WINDOWS] [DELIVERY] > NNN-w11-accept-PROGRAM.sh
# NNN: claimed in Quest's NUMBERS.md first. MANIFEST_SHA256: WORKER9's, of DELIVERY/manifest.json. WINDOWS: 8192 (2^29
# rows). DELIVERY: /tmp/w9-deliveries.
set -eu
KIT=$1; NNN=$2; PROG=$3; MS=$4; NW=${5:-8192}; D=${6:-/tmp/w9-deliveries}
HERE=$(cd "$(dirname "$0")" && pwd)
"$HERE/make_task.sh" "$KIT" "$NNN-w11-accept-$PROG" "./run_shard.sh -d $D -p $PROG -f 0 -n $NW -j 16 -x 64 -m $MS \$RES/$PROG"
