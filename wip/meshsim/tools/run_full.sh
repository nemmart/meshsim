#!/bin/bash
# run_tests.sh's full-mode extras (meshsim v1.2, WORKER4), up to P at a time: each writes OUT/<name>.log.
OUT=$1; P=${P:-4}; mkdir -p "$OUT"
cat > "$OUT/fsteps.txt" <<'S'
ctrl-full|CtrlTest -1 full
accounting-full|AccountingTest -1 full
rdport-full|ReadPortGatherTest -1 full
engine-full|EngineTest -1 full
sweep-quick|Sweep quick
agbench|AgBench
skewtable|NetTest -1 skewtable 40
s1bench|S1Bench
S
run() { log=${1%%|*}; cmd=${1#*|}; java -Xmx4g -cp build $cmd > "$OUT/$log.log" 2>&1; echo "$log rc=$?" >> "$OUT/frc.txt"; }
export -f run; export OUT
cat "$OUT/fsteps.txt" | xargs -P "$P" -I{} bash -c 'run "{}"'
cp results/*.csv "$OUT/" 2>/dev/null
F=$(grep -c 'rc=[^0]' "$OUT/frc.txt"); N=$(wc -l < "$OUT/fsteps.txt")
{ sort "$OUT/frc.txt"; echo "$N full steps; nonzero exits $F"; } > "$OUT/FULL-VERDICT.txt"
[ "$F" = 0 ]
