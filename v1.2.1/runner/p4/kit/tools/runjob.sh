#!/usr/bin/env bash
# runjob.sh OUTDIR HEAP "weight<TAB>group<TAB>id<TAB>seed<TAB>args" -- one SortTest run (called by run.sh)
OUT=$1; HEAP=$2; LINE=$3
ID=$(printf '%s' "$LINE" | cut -f3); SEED=$(printf '%s' "$LINE" | cut -f4); ARGS=$(printf '%s' "$LINE" | cut -f5)
F=$(printf '%s' "$ID" | tr '/' '_')
T0=$(date +%s)
# shellcheck disable=SC2086
java -Xmx$HEAP -Dsort.build=image -cp meshsim.jar SortTest $SEED run label=$ID $ARGS csv="$OUT/$F.csv.part" phases="$OUT/$F.phases.csv" > "$OUT/$F.log" 2>&1
RC=$?
[ -f "$OUT/$F.csv.part" ] && mv "$OUT/$F.csv.part" "$OUT/$F.csv"
echo "$(date +%H:%M:%S) $ID: exit $RC, $(( $(date +%s) - T0 )) s; $(grep -E 'SortTest.*passed' "$OUT/$F.log" | tail -1)"
