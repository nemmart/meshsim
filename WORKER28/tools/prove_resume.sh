#!/bin/bash
# prove_resume.sh OUTDIR STEPS BUDGET_S [JVM_OPTS] -- prove_skip.sh one step at a time, resuming: steps with a verdict in
# OUTDIR/verdicts.txt are skipped; stops starting new steps after BUDGET_S seconds (for sessions with a time limit per call).
OUT=$1; S=$2; B=$3; X=${4:-}; mkdir -p "$OUT"; touch "$OUT/verdicts.txt"; T0=$(date +%s)
H=$(dirname "$0")
while IFS= read -r L; do
  N=${L%%|*}; [ -n "$N" ] || continue
  grep -q "^$N " "$OUT/verdicts.txt" && continue
  [ $(( $(date +%s) - T0 )) -ge $B ] && { echo "budget spent"; break; }
  echo "$L" | bash "$H/prove_skip.sh" "$OUT" "$X" >> "$OUT/verdicts.txt"
  tail -1 "$OUT/verdicts.txt"
done < "$S"
