#!/bin/bash
# The processes against the reference over the brief's matrix: clients 1, 2, 4, 16; windows 16, 64, 256, 1024;
# 1, 2, 4 and 8 bytes a cycle; 2 and 4 slots. Every case under each condition asked for.
#
#   tests/matrix.sh BIN CYCLES REPEATS MODE [CLIENTS...]
#
# MODE: plain   as it comes
#       loaded  with a busy loop a core running beside it
#       pinned  every process on one core (taskset -c 0)
#       paused  random real-time pauses between cycles and at the library's own steps
#       all     the four in turn
# Each case's seed differs, and its processes finish at different cycles. Prints a line a case, then a total.
set -u
BIN=$1 CYC=$2 REP=$3 MODE=$4
shift 4
CLIENTS=${*:-1 2 4 16}
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$MODE" = all ] && MODES="plain loaded pinned paused" || MODES=$MODE
LOADPIDS=()
stop_load() { for p in "${LOADPIDS[@]}"; do kill "$p" 2>/dev/null; done; LOADPIDS=(); }
trap stop_load EXIT
T0=$(date +%s)
ok=0 bad=0
for mode in $MODES; do
    export TASKSET="" WIRE_PAUSE="" NODE_ONLY=""
    unset WIRE_PAUSE
    case $mode in
    plain) ;;
    loaded)
        for i in $(seq 1 "$(nproc)"); do (while :; do :; done) & LOADPIDS+=($!); done ;;
    pinned) export TASKSET="taskset -c 0" ;;
    paused) export WIRE_PAUSE=3000:200 NODE_ONLY="--pause 300:300" ;;
    *) echo "unknown mode $mode"; exit 2 ;;
    esac
    m0=$(date +%s)
    seed=1
    for n in $CLIENTS; do for w in 16 64 256 1024; do for b in 1 2 4 8; do for k in 2 4; do
        seed=$((seed + 1))
        line=$("$HERE/case.sh" "$BIN" "$n" "$w" "$b" "$k" "$seed" "$CYC" "$REP")
        echo "$mode $line"
        case $line in OK*) ok=$((ok + 1)) ;; *) bad=$((bad + 1)) ;; esac
    done; done; done; done
    stop_load
    echo "mode $mode: $(( $(date +%s) - m0 ))s"
done
echo "matrix: $ok passed, $bad failed, $(( $(date +%s) - T0 ))s on $(nproc) cores (cycles $CYC, repeats $REP, modes $MODES)"
[ $bad = 0 ]
