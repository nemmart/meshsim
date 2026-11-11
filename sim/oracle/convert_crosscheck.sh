#!/bin/bash
# oracle/convert_crosscheck.sh CONVERT_DELIVERY [SRC PROGRAM ...] (WORKER19, P1, D42): v1.35's convert_delivery against
# v1.3's ConvertDelivery (sim/ref) on the same v1.25 deliveries: every converted record, every link (name and target)
# and the manifest, byte for byte. With no SRC: three v1.25 deliveries made here by v1.25's own SyntheticDelivery
# (v1.25/meshsim; seeds 7, 11 (WORKER9's layout), 5). Needs sim/ref, v1.25/ and a JDK.
set -eu
SIM=$(cd "$(dirname "$0")/.." && pwd); TOP=$(cd "$SIM/.." && pwd)
CD=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); shift
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
javac -nowarn -d "$T/v13" "$SIM"/ref/meshsim/src/*.java "$SIM"/ref/meshsim/test/{ReplayDelivery,ReplayDriver,ReplayTables,ReplayWindow,ConvertDelivery}.java 2>&1 | grep -v '^Note' || true
if [ $# = 0 ]; then
  javac -nowarn -d "$T/v125" "$TOP"/v1.25/meshsim/src/*.java "$TOP"/v1.25/meshsim/test/{ReplayDelivery,ReplayDriver,ReplayTables,ReplayWindow,SyntheticDelivery}.java 2>&1 | grep -v '^Note' || true
  for c in "a 3 2048 7" "b 4 1024 11 --w9-layout" "c 2 8192 5"; do
    set -- $c; n=$1; shift
    (cd "$TOP/v1.25/meshsim" && java -cp "$T/v125" SyntheticDelivery "$T/src-$n" --windows $1 --rows $2 --seed $3 ${4:-}) > /dev/null
  done
  set -- "$T/src-a" synthetic "$T/src-b" synthetic "$T/src-c" synthetic
fi
bad=0; k=0
while [ $# -ge 2 ]; do
  S=$1 P=$2; shift 2; k=$((k + 1))
  java -cp "$T/v13" ConvertDelivery "$S" "$T/j$k" "$P" > "$T/j$k.log" || { cat "$T/j$k.log"; bad=1; }
  "$CD" "$S" "$T/c$k" "$P" > "$T/c$k.log" || { cat "$T/c$k.log"; bad=1; }
  (cd "$T/j$k" && find . | sort) > "$T/lj"; (cd "$T/c$k" && find . | sort) > "$T/lc"
  diff "$T/lj" "$T/lc" > /dev/null || { echo "DIFFERS: $S: the converted trees' names"; bad=1; }
  n=0
  while read -r f; do
    if [ -L "$T/j$k/$f" ]; then [ "$(readlink "$T/j$k/$f")" = "$(readlink "$T/c$k/$f")" ] || { echo "DIFFERS: $S: link $f"; bad=1; }
    elif [ -f "$T/j$k/$f" ]; then cmp -s "$T/j$k/$f" "$T/c$k/$f" || { echo "DIFFERS: $S: $f"; bad=1; }; n=$((n + 1)); fi
  done < "$T/lj"
  echo "$S $P: $n files and $(find "$T/j$k" -type l | wc -l) links compared; $(tail -1 "$T/j$k.log" | sed 's/.*sha256/manifest sha256/')"
done
[ $bad = 0 ] || { echo "convert_crosscheck: FAILED"; exit 1; }
echo "convert_crosscheck: convert_delivery equal to v1.3's ConvertDelivery on every record, link and manifest"
