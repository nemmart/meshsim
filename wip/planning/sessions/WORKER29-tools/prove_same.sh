#!/bin/bash
# prove_same.sh BASE_CP NEW_CP OUTDIR [EXTRA_JVM_OPTS] < steps -- WORKER29's exactness check of an engine change: each step
# ("name|Class args", run from wip/meshsim) with the base build's classes and with the new build's, the logs compared after
# masking wall times (as WORKER28's prove_skip.sh) and every chip's fingerprint (-Dmeshsim.digest) compared byte for byte.
# Prints per step: SAME or DIFF, both exit codes and wall times. Exit 0 only if every step is SAME with equal exit codes.
B=$1; N=$2; OUT=$3; X=${4:-}; mkdir -p "$OUT"; RC=0
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms|µs|us)\b/#s/g; s/[0-9]+(\.[0-9]+)? us\/cycle/# us\/cycle/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g' "$1"; }
while IFS='|' read -r S C; do
  [ -n "$S" ] || continue; case "$S" in \#*) continue;; esac
  for M in base new; do
    [ $M = base ] && CP=$B || CP=$N
    rm -f "$OUT/$S.$M.digest"
    a=$(date +%s.%N); timeout ${STEP_LIMIT:-1800} java -Xmx${HEAP:-3g} -Dmeshsim.digest="$OUT/$S.$M.digest" $X -cp $CP $C > "$OUT/$S.$M.log" 2>&1; r=$?
    b=$(date +%s.%N); eval "R_$M=$r; T_$M=$(echo "$b - $a" | bc)"
  done
  v=SAME
  [ "$R_base" = "$R_new" ] || v="DIFF (exit)"
  cmp -s <(mask "$OUT/$S.base.log") <(mask "$OUT/$S.new.log") || v="$v DIFF (log)"
  cmp -s "$OUT/$S.base.digest" "$OUT/$S.new.digest" 2>/dev/null || { [ ! -e "$OUT/$S.base.digest" ] && [ ! -e "$OUT/$S.new.digest" ]; } || v="$v DIFF (fingerprint)"
  v=${v#SAME }; [ "$v" = SAME ] || RC=1
  printf "%-14s %s  exit %s/%s  base %.1f s  new %.1f s  chips %s\n" "$S" "$v" $R_base $R_new $T_base $T_new "$(cat "$OUT/$S.new.digest" 2>/dev/null | wc -l)"
done
exit $RC
