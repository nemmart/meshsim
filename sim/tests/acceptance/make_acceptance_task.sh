#!/usr/bin/env bash
# make_acceptance_task.sh -- one program's acceptance task (v1.35, WORKER25; v1.3's, meshsim WORKER11): every window of
# the program's delivery on the runner box, Stage A and Stage B (tile 0 alone) at 16 processes, the full chip on every
# 64th.
#   tests/acceptance/make_acceptance_task.sh KIT.tgz NNN PROGRAM MANIFEST_SHA256 [WINDOWS] [DELIVERY] > NNN-w25-accept-PROGRAM.sh
# NNN: claimed in Quest's NUMBERS.md first. MANIFEST_SHA256: of DELIVERY/PROGRAM.manifest.json (the per-program
# manifest). WINDOWS: 8192 (2^29 rows). DELIVERY: a v1.26 delivery (v1.25's W9 deliveries converted first:
# convert_delivery).
set -eu
KIT=$1; NNN=$2; PROG=$3; MS=$4; NW=${5:-8192}; D=${6:-/tmp/w9-deliveries}
HERE=$(cd "$(dirname "$0")" && pwd)
"$HERE/make_task.sh" "$KIT" "$NNN-w25-accept-$PROG" "./run_shard.sh -d $D -p $PROG -M $D/$PROG.manifest.json -f 0 -n $NW -j 16 -x 64 -m $MS \$RES/$PROG"
