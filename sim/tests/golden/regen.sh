#!/bin/bash
# regen.sh [--check] [PATTERN] -- the golden traces (sim/tests/TRACE-FORMAT.md) from sim/ref, through sim/ref's Observer
# and sim/tests/ref/TraceRun.java, one JVM per run (RUNS; P at a time, default nproc).
#   (no flag)  regenerate: write every trace into sim/tests/golden/
#   (JOPTS: extra JVM options, e.g. -Dmeshsim.skip=false: then the header differs in idle_skip and nothing else may)
#   --check    regenerate into a scratch directory and compare with the committed traces: the header (the '#' lines,
#              D12: the configuration, the tile, the images) first, then the whole trace. Prints SAME, DIFF (header),
#              DIFF (trace) or MISSING per run; exits 1 on any difference.
# First, in both modes, every image is checked against images/MANIFEST.md (build_images.sh): a golden trace is only
# meaningful against those exact bytes. Needs a JDK (javac); not the riscv toolchain.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd); REF=$SIM/ref/meshsim
CHECK=0; [ "${1:-}" = --check ] && { CHECK=1; shift; }
PAT=${1:-.}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# 1. the images against the manifest (its first sha256 block: images/; its second: sim/ref's riscv-tests)
awk '/^```$/{b++; next} b==1 && NF==2' "$HERE/images/MANIFEST.md" > "$W/img.sha"
awk '/^```$/{b++; next} b==3 && NF==2' "$HERE/images/MANIFEST.md" > "$W/rv.sha"
(cd "$HERE/images" && sha256sum -c --quiet "$W/img.sha") || { echo "regen: images/ differ from MANIFEST.md"; exit 2; }
(cd "$REF/test/riscv/bin" && sha256sum -c --quiet "$W/rv.sha") || { echo "regen: sim/ref's riscv-tests images differ from MANIFEST.md"; exit 2; }
echo "images: $(wc -l < "$W/img.sha") + $(wc -l < "$W/rv.sha") files match MANIFEST.md"

# 2. sim/ref's classes (as run_tests.sh builds them) and the trace runner
mkdir -p "$W/ref" "$W/tr"
javac -d "$W/ref" "$REF"/src/*.java || exit 2
grep -v '^#' "$REF/test/PENDING" | awk 'NF{print "'"$REF"'/test/"$1}' > "$W/pending.txt"
javac -d "$W/ref" -cp "$W/ref" $(ls "$REF"/test/*.java | grep -v -x -F -f "$W/pending.txt") || exit 2
javac -d "$W/tr" -cp "$W/ref" "$SIM/tests/ref/TraceRun.java" || exit 2

# 3. the runs (from sim/ref/meshsim: the boot ROM image and hops.txt are read from there)
OUT=$HERE; [ $CHECK = 1 ] && { OUT=$W/out; mkdir -p "$OUT"; }
grep -v '^#' "$HERE/RUNS" | awk 'NF' | grep -E -- "$PAT" > "$W/runs"
export W OUT REF HERE SIM
run() {
  set -- $1
  local n=$1 tile=$2 d=$3 img=$4 tb=${5:-}
  [ "$d" = riscv ] && d=$REF/test/riscv/bin || d=$HERE/images
  (cd "$REF" && java -Xmx1g ${JOPTS:-} -Dmeshsim.observe=on -cp "$W/ref:$W/tr" TraceRun "$OUT/$n.trace" "$tile" "$d" "$img" ${tb:+"$SIM/fw/m1/$tb"}) > "$W/$n.log" 2>&1 \
    || { echo "$n: TraceRun failed"; cat "$W/$n.log"; }
}
export -f run
xargs -d '\n' -P "${P:-$(nproc)}" -I{} bash -c 'run "{}"' < "$W/runs"

# 4. the check
[ $CHECK = 1 ] || { echo "regenerated $(wc -l < "$W/runs") traces into $OUT"; exit 0; }
RC=0
while read -r n _; do
  if [ ! -f "$HERE/$n.trace" ] || [ ! -f "$OUT/$n.trace" ]; then v=MISSING
  elif ! cmp -s <(grep '^#' "$HERE/$n.trace") <(grep '^#' "$OUT/$n.trace"); then v="DIFF (header)"
  elif ! cmp -s "$HERE/$n.trace" "$OUT/$n.trace"; then v="DIFF (trace)"
  else v=SAME; fi
  [ "$v" = SAME ] || { RC=1; echo "$n: $v"; }
done < "$W/runs"
echo "checked $(wc -l < "$W/runs") traces: $([ $RC = 0 ] && echo "all SAME" || echo "DIFFERENCES above")"
exit $RC
