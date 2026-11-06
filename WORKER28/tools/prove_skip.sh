#!/bin/bash
# prove_skip.sh OUTDIR [EXTRA_JVM_OPTS] < steps -- stage 1's exactness check (WORKER28): each step ("name|Class args",
# run from wip/meshsim with build/ compiled) twice, -Dmeshsim.skip=false and =true; the logs compared after masking wall
# times ("12.3 s", "450 ms", "rows per JVM-second"). Prints per step: SAME or DIFF, exit codes, both wall times.
# Exit 0 only if every step is SAME with equal exit codes.
OUT=$1; X=${2:-}; mkdir -p "$OUT"; RC=0
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms|µs|us)\b/#s/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g' "$1"; }
while IFS='|' read -r N C; do
  [ -n "$N" ] || continue; case "$N" in \#*) continue;; esac
  for M in false true; do
    a=$(date +%s.%N); timeout ${STEP_LIMIT:-1800} java -Xmx3g -Dmeshsim.skip=$M $X -cp build $C > "$OUT/$N.$M.log" 2>&1; r=$?
    b=$(date +%s.%N); eval "R_$M=$r; T_$M=$(echo "$b - $a" | bc)"
  done
  if [ "$R_false" = "$R_true" ] && cmp -s <(mask "$OUT/$N.false.log") <(mask "$OUT/$N.true.log"); then v=SAME; else v=DIFF; RC=1; fi
  printf "%-14s %s  exit %s/%s  off %.1f s  on %.1f s\n" "$N" $v $R_false $R_true $T_false $T_true
done
exit $RC
