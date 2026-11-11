#!/bin/bash
# prove_observer.sh MESHSIM OUT -- the proof that sim/ref's observer changes nothing (sim/ref/README.md; DECISIONS D5).
# MESHSIM is a sim/ref/meshsim tree with its firmware built (run_tests.sh's builds: sw/build.sh, the sort, build_cc.sh,
# build_recipes.sh, sw/replay/build.sh), build-base/ (main's pristine sim/ref, src and tests, compiled), build/ (this
# branch's, compiled) and build/gold. Each Java step of run_tests.sh quick (taken from run_tests.sh as tools/run_quick.sh
# takes them) runs three ways from MESHSIM: base (pristine), off (the observer present, not switched on), on
# (-Dmeshsim.observe=OUT/<step>.trace,0). Each log is masked of wall times as WORKER29's prove_same.sh masks them and
# compared with base's, and every chip's fingerprint (-Dmeshsim.digest) byte for byte. P (default 12) runs at a time.
# Writes OUT/VERDICTS.txt (per step: SAME or what differs, the exit codes, the times); exits 0 only if every step is SAME.
set -u
T=$(cd "$1" && pwd); OUT=$(mkdir -p "$2" && cd "$2" && pwd); P=${P:-12}
cd "$T"
awk '/^if \[ "\$MODE" != quick \]/{full=1} /^fi$/{full=0} !full && /^LOG=/ && /\$J /' run_tests.sh \
  | sed 's/^LOG=\([^ ]*\).*\$J /\1|/; s/\$Q/quick/' > "$OUT/steps.txt"
export T OUT
one() {   # one MODE|STEP|COMMAND
  local m=${1%%|*} r=${1#*|}; local s=${r%%|*} c=${r#*|} cp=build x=
  [ "$m" = base ] && cp=build-base
  [ "$m" = on ] && x="-Dmeshsim.observe=$OUT/$s.trace,0"
  rm -f "$OUT/$s.$m.digest"
  local a=$(date +%s.%N)
  timeout ${STEP_LIMIT:-1500} java -Xmx${HEAP:-3g} $x -Dmeshsim.digest="$OUT/$s.$m.digest" -cp $cp $c > "$OUT/$s.$m.log" 2>&1
  echo "$? $(echo "$(date +%s.%N) - $a" | bc)" > "$OUT/$s.$m.rc"
}
export -f one
while IFS= read -r l; do for m in base off on; do echo "$m|$l"; done; done < "$OUT/steps.txt" \
  | xargs -d '\n' -P "$P" -I{} bash -c 'one "{}"'
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms|µs|us)\b/#s/g; s/[0-9]+(\.[0-9]+)? us\/cycle/# us\/cycle/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g' "$1"; }
RC=0; : > "$OUT/VERDICTS.txt"
while IFS= read -r l; do
  s=${l%%|*}; v=""
  read -r rb tb < "$OUT/$s.base.rc"
  for m in off on; do
    read -r r t < "$OUT/$s.$m.rc"
    [ "$r" = "$rb" ] || v="$v $m: DIFF (exit);"
    cmp -s <(mask "$OUT/$s.base.log") <(mask "$OUT/$s.$m.log") || v="$v $m: DIFF (log);"
    if [ -e "$OUT/$s.base.digest" ] || [ -e "$OUT/$s.$m.digest" ]; then cmp -s "$OUT/$s.base.digest" "$OUT/$s.$m.digest" || v="$v $m: DIFF (fingerprint);"; fi
    eval "R_$m=$r; T_$m=$t"
  done
  [ -n "$v" ] && RC=1 || v=SAME
  printf "%-14s %s  exit %s/%s/%s  base %.1f s  off %.1f s  on %.1f s  chips %s  trace %s lines\n" "$s" "$v" "$rb" "$R_off" "$R_on" "$tb" "$T_off" "$T_on" \
    "$(cat "$OUT/$s.base.digest" 2>/dev/null | wc -l)" "$(cat "$OUT/$s.trace" 2>/dev/null | wc -l)" >> "$OUT/VERDICTS.txt"
done < "$OUT/steps.txt"
echo "$(grep -c ' SAME ' "$OUT/VERDICTS.txt") of $(wc -l < "$OUT/steps.txt") steps the same, off and on" >> "$OUT/VERDICTS.txt"
exit $RC
