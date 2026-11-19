#!/bin/bash
# One case: the processes on the wire against the reference.
#
#   tests/case.sh BIN CLIENTS WINDOW BYTES SLOTS SEED CYCLES [REPEATS] [-- stand-in options for both]
#
# BIN is a build directory (build/g++/check). The reference is run once; the processes REPEATS times (1). Each
# run's digests must equal the reference's, at every end. Extra environment: TASKSET="taskset -c 0" pins every
# process to one core; WIRE_PAUSE=ppm:max_us puts pauses inside the library (check builds); LAUNCH_ARGS adds
# arguments to wire-launch; NODE_ONLY adds wire-node options the reference does not take (--pause). Prints one line: OK or DIFF (or FAIL), the case, and the seconds.
set -u
BIN=$1 N=$2 W=$3 B=$4 K=$5 SEED=$6 C=$7
REP=${8:-1}
shift 7; [ $# -gt 0 ] && shift
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
TMP=$(mktemp -d /tmp/wirecase.XXXXXX)
trap 'rm -rf "$TMP"' EXIT
CASE="clients=$N window=$W bytes=$B slots=$K seed=$SEED cycles=$C ${EXTRA[*]} ${NODE_ONLY:-}"
t0=$(date +%s.%N)
"$BIN/wire-ref" --clients "$N" --window "$W" --lane-bytes "$B" --seed "$SEED" --cycles "$C" "${EXTRA[@]}" --out "$TMP/ref.txt" || {
    echo "FAIL $CASE: wire-ref"; exit 1; }
grep -v '^#' "$TMP/ref.txt" | sort > "$TMP/ref.s"
for r in $(seq 1 "$REP"); do
    rm -f "$TMP"/p*.txt
    ${TASKSET:-} "$BIN/wire-launch" --clients "$N" --window "$W" --lane-bytes "$B" --slots "$K" ${LAUNCH_ARGS:-} \
        -- "$BIN/wire-node" --seed "$SEED" --cycles "$C" --out "$TMP/p{p}.txt" "${EXTRA[@]}" ${NODE_ONLY:-} \
        -- "$BIN/wire-node" --seed "$SEED" --cycles "$C" --out "$TMP/p{p}.txt" "${EXTRA[@]}" ${NODE_ONLY:-} 2> "$TMP/launch.err"
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "FAIL $CASE run=$r: wire-launch exit $rc"; sed 's/^/    /' "$TMP/launch.err"; exit 1
    fi
    cat "$TMP"/p*.txt | grep -v '^#' | sort > "$TMP/proc.s"
    if ! cmp -s "$TMP/proc.s" "$TMP/ref.s"; then
        echo "DIFF $CASE run=$r"
        diff "$TMP/ref.s" "$TMP/proc.s" | head -6 | sed 's/^/    /'
        exit 1
    fi
done
t1=$(date +%s.%N)
printf "OK   %s repeats=%s %.1fs\n" "$CASE" "$REP" "$(echo "$t1 - $t0" | bc)"
