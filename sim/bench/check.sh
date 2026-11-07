#!/bin/bash
# sim/bench check (make check): run in the debug build (ASan, UBSan) at small sizes.
#   every mesh variant gives the same digest (both mixes, two rates); 1, 2 and 4 threads give the same digest;
#   the fold agrees with the slow reference and with Python; the toy unit's forms and holds agree; and the allocation
#   hook fires inside the measured loop (so "no allocation" is proven, not assumed).
set -u
B=${1:?build dir}
C=$("$(dirname "$0")/../tools/machine.sh" cpus all 1)   # one usable CPU, every thread on it (D21: detected, not CPU 0)
FAIL=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; FAIL=1; }
dig() { grep -o 'digest=[0-9a-f]*' | cut -d= -f2 | grep . || echo "none"; }

for mix in 128 mixed; do for rate in 0.1 0.35; do
  ref=""
  for lw in a b; do for dl in e1 e2; do for w in rt fx; do for rd in bm poll tr; do
    d=$($B/mesh --lw $lw --dl $dl --width $w --ready $rd --mix $mix --rate $rate --cycles 3000 --warmup 300 | dig)
    [ -z "$ref" ] && ref=$d
    [ "$d" = "$ref" ] && [ "$d" != none ] || bad "mesh lw=$lw dl=$dl width=$w ready=$rd mix=$mix rate=$rate: digest $d != $ref"
  done; done; done; done
  [ -n "$ref" ] && ok "mesh: 24 variants, mix=$mix rate=$rate, one digest $ref"
done; done

ref=""
for t in 1 3 4; do
  d=$($B/mesh --lw a --meshes 4 --threads $t --cpus $C,$C,$C,$C --mix mixed --cycles 2000 --warmup 200 | dig)
  [ -z "$ref" ] && ref=$d
  [ "$d" = "$ref" ] && [ "$d" != none ] || bad "mesh 4 meshes on $t threads: digest $d != $ref"
done
ok "mesh: 4 meshes on 1, 3 (uneven), 4 threads, one digest $ref"

$B/fp128 --check 20000 && ok "fp128: forms against the reference" || bad "fp128 --check"
$B/fp128 --dump 2000 | python3 -c "
import sys
p = 2**128 - 159; n = bad = 0
for l in sys.stdin:
    a, b, r = (int(x, 16) for x in l.split()); n += 1
    bad += (a * b) % p != r
print(n, bad); sys.exit(1 if bad or not n else 0)" && ok "fp128: forms against Python" || bad "fp128 against Python"

ref=""
for f in u128 acc limbs; do for h in pool inline; do for t in 1 3; do
  d=$($B/fp128 --unit --form $f --hold $h --units 64 --threads $t --cpus $C,$C,$C,$C --cycles 2000 --warmup 100 | dig)
  [ -z "$ref" ] && ref=$d
  [ "$d" = "$ref" ] && [ "$d" != none ] || bad "fp128 unit form=$f hold=$h threads=$t: digest $d != $ref"
done; done; done
ok "fp128 unit: 3 forms x 2 holds x 1, 3 threads, one digest $ref"

out=$($B/mesh --alloc-probe --cycles 100 --warmup 10 2>&1); rc=$?
[ $rc -ne 0 ] && echo "$out" | grep -q "inside a run" && ok "allocation hook fires in the mesh loop" \
  || bad "allocation hook did not fire (rc=$rc)"

[ $FAIL = 0 ] && echo "==== sim/bench check: all passed" || { echo "==== sim/bench check: FAILED"; exit 1; }
