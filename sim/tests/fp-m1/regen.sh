#!/bin/bash
# regen.sh [--check] [PATTERN] -- the core's canonical fingerprint on M1's goldens (WORKER8): for every run of
# sim/tests/golden/RUNS, sim/tests/ref/CoreFp.java boots it exactly as TraceRun does and writes, per cycle, the tile's
# core stream (StateFp.core) and its memories (StateFp's cmem without the port-B bus) into NAME.corefp here.
#   --check  regenerate into a scratch directory and compare with the committed files (SAME / DIFF / MISSING per run).
# Needs a JDK. The images are those of sim/tests/golden (checked there by its own regen.sh against MANIFEST.md).
set -u
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd); REF=$SIM/ref/meshsim; G=$SIM/tests/golden
CHECK=0; [ "${1:-}" = --check ] && { CHECK=1; shift; }
PAT=${1:-.}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/ref" "$W/tr"
javac -nowarn -d "$W/ref" "$REF"/src/*.java 2>&1 | grep -v '^Note' || true
grep -v '^#' "$REF/test/PENDING" | awk 'NF{print "'"$REF"'/test/"$1}' > "$W/pending.txt"
javac -nowarn -d "$W/ref" -cp "$W/ref" $(ls "$REF"/test/*.java | grep -v -x -F -f "$W/pending.txt") 2>&1 | grep -v '^Note' || true
javac -nowarn -d "$W/tr" -cp "$W/ref" "$SIM/tests/ref/StateFp.java" "$SIM/tests/ref/CoreFp.java" 2>&1 | grep -v '^Note' || true
[ -f "$W/tr/CoreFp.class" ] || { echo "regen: CoreFp did not compile"; exit 2; }
OUT=$HERE; [ $CHECK = 1 ] && { OUT=$W/out; mkdir -p "$OUT"; }
grep -v '^#' "$G/RUNS" | awk 'NF' | grep -E -- "$PAT" > "$W/runs"
export W OUT REF G SIM
run() {
  set -- $1
  local n=$1 tile=$2 d=$3 img=$4 tb=${5:-}
  [ "$d" = riscv ] && d=$REF/test/riscv/bin || d=$G/images
  (cd "$REF" && java -Xmx1g -cp "$W/ref:$W/tr" CoreFp "$OUT/$n.corefp" "$tile" "$d" "$img" ${tb:+"$SIM/fw/$tb"}) > "$W/$n.log" 2>&1 \
    || { echo "$n: CoreFp failed"; cat "$W/$n.log"; }
}
export -f run
xargs -d '\n' -P "${P:-$(nproc)}" -I{} bash -c 'run "{}"' < "$W/runs"
[ $CHECK = 1 ] || { echo "regenerated $(wc -l < "$W/runs") files into $OUT"; exit 0; }
RC=0
while read -r n _; do
  if [ ! -f "$HERE/$n.corefp" ] || [ ! -f "$OUT/$n.corefp" ]; then v=MISSING
  elif ! cmp -s "$HERE/$n.corefp" "$OUT/$n.corefp"; then v=DIFF; else v=SAME; fi
  [ "$v" = SAME ] || { RC=1; echo "$n: $v"; }
done < "$W/runs"
echo "checked $(wc -l < "$W/runs") files: $([ $RC = 0 ] && echo "all SAME" || echo "DIFFERENCES above")"
exit $RC
