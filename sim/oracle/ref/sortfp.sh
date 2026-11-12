#!/bin/bash
# sortfp.sh -- the sort's fingerprint references (WORKER24, P6): every run in tests/sort/fp/RUNS through SortFp (this
# folder) in ref, a state fingerprint at every GO and all-READY. Needs a JDK. sim/ref is only read: as sortlogs.sh, a
# scratch copy with the C++ tree's committed images. One JVM per run, P at a time (default 1).
#
#   sortfp.sh --fixtures [PATTERN]   (re)write tests/sort/fp/<name>.out and <name>.fp.gz, and HASHES
#   sortfp.sh --check [PATTERN]      regenerate and compare every file's sha256 with HASHES (the .fp uncompressed)
set -u
export LC_ALL=C
SIM=$(cd "$(dirname "$0")/../.." && pwd); REF=$SIM/ref/meshsim; FIX=$SIM/tests/sort/fp
MODE=${1:-}; PAT=${2:-.}
case "$MODE" in --fixtures|--check) ;; *) echo "usage: sortfp.sh --fixtures|--check [PATTERN]"; exit 2;; esac
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT

mkdir -p "$W/m"
(cd "$REF" && tar cf - --exclude=./build --exclude=./sw/build --exclude=./sw/ext/sort/build --exclude=./results .) | (cd "$W/m" && tar xf -)
mkdir -p "$W/m/sw/build" "$W/m/sw/ext/sort/build" "$W/m/build"
for f in "$SIM"/tests/golden-m2/images/sort.*; do cp "$f" "$W/m/sw/ext/sort/build/"; done
(cd "$W/m" && javac -nowarn -d build src/*.java) || exit 2
grep -v '^#' "$W/m/test/PENDING" | awk 'NF{print "test/"$1}' > "$W/pending.txt"
(cd "$W/m" && javac -nowarn -d build -cp build $(ls test/*.java | grep -v -x -F -f "$W/pending.txt")) 2>&1 | grep -v '^Note:'
javac -nowarn -d "$W/m/build" -cp "$W/m/build" "$SIM"/oracle/ref/{StateFp,FpHooks,SortFp}.java 2>&1 | grep -v '^Note:'
[ -f "$W/m/build/SortFp.class" ] || { echo "sortfp: the driver did not compile"; exit 2; }
javac -nowarn -d "$W/kg" "$SIM/pipeline/keys/KeyGen.java" || exit 2

# the key sets the runs name (keys-N/SET.keys), from KeyGen, checked against tests/sort/ref/KEYS.sha256 where listed
grep -v '^#' "$FIX/RUNS" | awk 'NF' | grep -E -- "$PAT" > "$W/runs"
for ks in $(grep -o 'keys=keys-[0-9]*/[a-z0-9-]*\.keys' "$W/runs" | sort -u | sed 's/^keys=//'); do
  d=${ks%/*}; s=${ks##*/}; s=${s%.keys}
  (cd "$W/m" && java -Xmx2g -cp "$W/kg" KeyGen "$d" --n "${d#keys-}" --only "$s" > /dev/null) || exit 2
  h=$(sha256sum < "$W/m/$ks" | cut -c1-64); want=$(awk -v f="$ks" '$2 == f {print $1}' "$SIM/tests/sort/ref/KEYS.sha256")
  [ -z "$want" ] || [ "$h" = "$want" ] || { echo "sortfp: $ks differs from KEYS.sha256"; exit 2; }
done

OUT=$W/out; mkdir -p "$OUT"
export W OUT
one() {
  set -- $1; local name=$1; shift
  (cd "$W/m" && java ${JS:--Xmx3g} -cp build SortFp "$@" fp="$OUT/$name.fp" > "$OUT/$name.out" 2>&1)
  echo "$name: $(tail -1 "$OUT/$name.out")"
}
export -f one
tr '\n' '\0' < "$W/runs" | xargs -0 -P "${P:-1}" -I{} bash -c 'one "$@"' _ {}

(cd "$OUT" && sha256sum *.out *.fp) > "$W/hashes"
if [ "$MODE" = --fixtures ]; then
  for f in "$OUT"/*.out; do cp "$f" "$FIX/"; done
  for f in "$OUT"/*.fp; do gzip -9n < "$f" > "$FIX/$(basename "$f").gz"; done
  if [ -f "$FIX/HASHES" ]; then awk 'NR==FNR{n[$2]=1; next} !($2 in n)' "$W/hashes" "$FIX/HASHES" > "$W/keep"; else : > "$W/keep"; fi
  sort -k2 "$W/keep" "$W/hashes" > "$FIX/HASHES"; echo "wrote $(wc -l < "$W/hashes") files into $FIX"
else
  bad=0
  while read -r h f; do
    want=$(awk -v f="$f" '$2 == f {print $1}' "$FIX/HASHES"); [ "$h" = "$want" ] && echo "SAME $f" || { echo "DIFF $f"; bad=1; }
  done < "$W/hashes"
  exit $bad
fi
