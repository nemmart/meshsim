#!/bin/bash
# regen.sh [--check] [PATTERN] -- M2's references (sim/tests/golden-m2/README.md), from sim/ref through WORKER7's drivers
# (sim/tests/ref: M2Sort, M2Prog, FpHooks, StateFp, TraceRun), one JVM per run (RUNS), P at a time (default nproc).
#   (no flag)  regenerate: the "git" runs' files into this directory, every file's sha256 into HASHES
#   --check    regenerate into a scratch directory and compare every file's sha256 with HASHES: SAME, DIFF or MISSING per
#              file; exits 1 on any difference
#   KEEP=DIR   also copy every file the runs made into DIR (the hash-only ones included)
#   ALL=1      also the "runner" runs (the full sort); JX: their heap (default -Xmx6g); JS: the others' (default -Xmx2500m)
# Nothing in sim/ref is written: the runs use a scratch copy of sim/ref/meshsim with this directory's images installed
# (checked against images/MANIFEST.md first). Needs a JDK (javac), not the riscv toolchain.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$HERE/../.." && pwd); REF=$SIM/ref/meshsim
CHECK=0; [ "${1:-}" = --check ] && { CHECK=1; shift; }
PAT=${1:-.}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

# 1. the images against the manifest
awk '/^```$/{b++; next} b==1 && NF==2' "$HERE/images/MANIFEST.md" > "$W/img.sha"
(cd "$HERE/images" && sha256sum -c --quiet "$W/img.sha") || { echo "regen: images/ differ from MANIFEST.md"; exit 2; }
echo "images: $(wc -l < "$W/img.sha") files match MANIFEST.md"

# 2. the scratch copy of sim/ref/meshsim (sources only), the images installed, ref and the drivers compiled
mkdir -p "$W/m"
(cd "$REF" && tar cf - --exclude=./build --exclude=./sw/build --exclude=./sw/ext/sort/build --exclude=./results .) | (cd "$W/m" && tar xf -)
mkdir -p "$W/m/sw/build" "$W/m/sw/ext/sort/build"
for f in "$HERE"/images/*.bin "$HERE"/images/*.sym; do
  case $(basename "$f") in sort.*) cp "$f" "$W/m/sw/ext/sort/build/";; *) cp "$f" "$W/m/sw/build/";; esac
done
mkdir -p "$W/cls"
javac -nowarn -d "$W/cls" "$W/m"/src/*.java || exit 2
grep -v '^#' "$W/m/test/PENDING" | awk 'NF{print "'"$W/m"'/test/"$1}' > "$W/pending.txt"
javac -nowarn -d "$W/cls" -cp "$W/cls" $(ls "$W/m"/test/*.java | grep -v -x -F -f "$W/pending.txt") 2>&1 | grep -v '^Note:'
javac -nowarn -d "$W/cls" -cp "$W/cls" "$SIM"/tests/ref/{TraceRun,StateFp,FpHooks,M2Sort,M2Prog}.java 2>&1 | grep -v '^Note:'
[ -f "$W/cls/M2Sort.class" ] || { echo "regen: the drivers did not compile"; exit 2; }

# 3. the runs to do, and the key sets they need (KeyGen, checked against HASHES)
grep -v '^#' "$HERE/RUNS" | awk 'NF' | grep -E -- "$PAT" | { if [ "${ALL:-0}" = 1 ]; then cat; else awk '$2 == "local"'; fi; } > "$W/runs"
OUT=$W/out; mkdir -p "$OUT"
keys() {   # keys N: KeyGen's uniform set, 64 x N
  local d=$W/keys-$1
  [ -f "$d/uniform.keys" ] && return
  (cd "$W/m" && java -Xmx2g -cp "$W/cls" KeyGen "$d" --n "$1" --only uniform > /dev/null) || { echo "regen: KeyGen $1 failed"; exit 2; }
  local h; h=$(sha256sum < "$d/uniform.keys" | cut -c1-64)
  local want; want=$(awk -v f="keys-$1/uniform.keys" '$2 == f {print $1}' "$HERE/HASHES" 2>/dev/null)
  if [ -n "$want" ] && [ "$h" != "$want" ]; then echo "regen: keys-$1/uniform.keys differs from HASHES"; exit 2; fi
  echo "$h  keys-$1/uniform.keys" >> "$W/keyhashes"
}
grep -q KEYS16K "$W/runs" && keys 16384
grep -q KEYS64K "$W/runs" && keys 65536
export W OUT
run() {
  set -- $1
  local name=$1 where=$2 keep=$3; shift 3
  local jo="${JS:--Xmx2500m}"; [ "$where" = runner ] && jo="${JX:--Xmx6g}"
  local o=""
  while [ "${1:0:1}" = - ]; do
    case $1 in -O) o="$o -Dmeshsim.observe=on";; -D) o="$o -Dmeshsim.digest=$OUT/$name.dig";; esac; shift
  done
  local args=() a
  for a in "$@"; do a=${a//@/$OUT/$name}; a=${a//KEYS16K/$W/keys-16384/uniform.keys}; a=${a//KEYS64K/$W/keys-65536/uniform.keys}; args+=("$a"); done
  (cd "$W/m" && java $jo $o -cp "$W/cls" "${args[@]}") 2>&1 | grep -v '^timing:' > "$OUT/$name.out"
  local rc=${PIPESTATUS[0]}
  echo "$name rc=$rc" >> "$OUT/rc.txt"
}
export -f run
xargs -d '\n' -P "${P:-$(nproc)}" -I{} bash -c 'run "{}"' < "$W/runs"
for f in "$OUT"/*; do [ "$f" = "$OUT/rc.txt" ] || sed -i "s#$W/keys-[0-9]*/##g; s#$OUT/##g; s#$W/m/##g" "$f"; done   # scratch paths out of logs and trace headers
grep -v ' rc=0$' "$OUT/rc.txt" && echo "regen: runs above exited non-zero"

[ -n "${KEEP:-}" ] && { mkdir -p "$KEEP"; for f in "$OUT"/*; do [ "$f" = "$OUT/rc.txt" ] || cp "$f" "$KEEP/"; done; }
# 4. hashes; regenerate or check
(cd "$OUT" && for f in $(ls | grep -v '^rc.txt$' | sort); do sha256sum "$f"; done) > "$W/new.sha"
[ -f "$W/keyhashes" ] && cat "$W/keyhashes" >> "$W/new.sha"
if [ $CHECK = 0 ]; then
  while read -r name where keep _; do
    [ "$keep" = git ] && for f in "$OUT/$name".*; do cp "$f" "$HERE/"; done
  done < "$W/runs"
  touch "$HERE/HASHES"
  awk 'NR == FNR {new[$2] = $0; next} !($2 in new) {print} END {for (k in new) print new[k]}' "$W/new.sha" "$HERE/HASHES" | sort -k2 > "$W/h" && mv "$W/h" "$HERE/HASHES"
  echo "regenerated $(wc -l < "$W/runs") runs; HASHES updated ($(wc -l < "$W/new.sha") files)"
  exit 0
fi
RC=0
while read -r h f; do
  want=$(awk -v f="$f" '$2 == f {print $1}' "$HERE/HASHES")
  if [ -z "$want" ]; then v=MISSING; elif [ "$h" = "$want" ]; then v=SAME; else v=DIFF; fi
  [ $v = SAME ] || { RC=1; echo "$f: $v"; }
done < "$W/new.sha"
echo "checked $(wc -l < "$W/new.sha") files from $(wc -l < "$W/runs") runs: $([ $RC = 0 ] && echo "all SAME" || echo "DIFFERENCES above")"
exit $RC
