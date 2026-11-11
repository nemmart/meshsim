#!/bin/bash
# oracle/synthetic_crosscheck.sh SYNTHETIC_DELIVERY [--calibration] (WORKER19, P1, D42): v1.35's synthetic_delivery
# against v1.3's SyntheticDelivery (sim/ref). Needs sim/ref and a JDK.
#   (1) Against a scratch copy of v1.3's patched in exactly D42's two places (the inlines' advice from
#       SplittableRandom(seed ^ 0xD42) instead of an unseeded one; the manifest's Map.of maps in their source order):
#       every file of every window and the manifest byte for byte, for every SyntheticDelivery run ref's tests make
#       (replayconf's syn1-syn4; with --calibration also the acceptance kit's calibration, seed 11, 128 windows of
#       65,536 rows, ~2.6 GB of scratch), plus WORKER9's layout and a shard (--first).
#   (2) Against v1.3 itself: with no inlines (--inline-weight 0) every file equal and the manifest equal as JSON (Map.of's
#       order aside); with inlines, the first difference (window, then byte of rows.bin) is exactly where two patched
#       runs with different K first differ: where the unseeded advice enters, and nowhere earlier.
set -eu
SIM=$(cd "$(dirname "$0")/.." && pwd)
SD=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); CAL=${2:-}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
M=$SIM/ref/meshsim
patch_k() {   # patch_k K DIR: v1.3's SyntheticDelivery.java with D42's two changes into DIR
  mkdir -p "$2"; local f=$2/SyntheticDelivery.java
  cp "$M/test/SyntheticDelivery.java" "$f"
  sed -i 's/^    final SplittableRandom rnd = new SplittableRandom();$/    SplittableRandom rnd;   \/\/ D42: seeded in run()/' "$f"
  sed -i "s/^        gen = new SplittableRandom(seed);\$/        gen = new SplittableRandom(seed); rnd = new SplittableRandom(seed ^ $1L);   \/\/ D42/" "$f"
  sed -i 's/Map\.of(/lm(/g' "$f"
  sed -i '$d' "$f"; cat >> "$f" <<'JAVA'
    /** D42: the pairs in their source order (v1.3: an unordered map). */
    static Map<String, Object> lm(Object... kv) { Map<String, Object> m = new LinkedHashMap<>(); for (int i = 0; i < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]); return m; }
}
JAVA
  [ "$(grep -c 'lm(' "$f")" = 4 ] && [ "$(grep -c '// D42' "$f")" = 2 ] && ! grep -q 'Map\.of\|new SplittableRandom()' "$f" \
    || { echo "synthetic_crosscheck: the patch didn't apply as expected"; exit 1; }
}
build() {   # build DIR [SRC]: the classes, with SRC's SyntheticDelivery.java if given
  javac -nowarn -d "$1" "$M"/src/*.java "$M"/test/{ReplayDelivery,ReplayDriver,ReplayTables,ReplayWindow}.java "${2:-$M/test}/SyntheticDelivery.java" 2>&1 | grep -v '^Note' || true
}
patch_k 0xD42 "$T/pk"; build "$T/ck" "$T/pk"
patch_k 0xD43 "$T/pk2"; build "$T/ck2" "$T/pk2"
build "$T/c13"
TABLES=$SIM/data/tables/replayer
java_sd() { local c=$1; shift; (cd "$M" && java -Xmx3g -Dreplay.tables="$TABLES" -cp "$c" SyntheticDelivery "$@") > /dev/null; }
cpp_sd() { "$SD" "$@" --tables "$TABLES" > /dev/null; }
bad=0
canon() { perl -pe 's/\{([^{}\[\]]*)\}/"{" . join(", ", sort split(m!, (?=")!, $1)) . "}"/ge' "$1"; }

# (1) the patched v1.3, byte for byte
RUNS=("3 2048 7" "4 1024 11 --inline-weight 6" "3 1024 3" "2 8192 5" "3 2048 7 --w9-layout --program evm-zone-29" "2 2048 9 --first 3")
[ "$CAL" = --calibration ] && RUNS+=("128 65536 11 --program synthetic")
for r in "${RUNS[@]}"; do
  set -- $r; w=$1 rows=$2 seed=$3; shift 3
  rm -rf "$T/j" "$T/c"
  java_sd "$T/ck" "$T/j" --windows "$w" --rows "$rows" --seed "$seed" "$@"
  cpp_sd "$T/c" --windows "$w" --rows "$rows" --seed "$seed" "$@"
  if diff -r "$T/j" "$T/c" > "$T/d"; then echo "patched v1.3 = C++: $r ($(find "$T/c" -type f | wc -l) files)"
  else echo "DIFFERS from the patched v1.3: $r"; head -5 "$T/d"; bad=1; fi
done

# (2) v1.3 itself
first_diff() {   # first_diff A B: "window W byte N" of the first differing rows.bin (windows in order), or "none"
  local f n
  for f in $(cd "$1" && find . -name 'window_*.rows.bin' | sort); do
    n=$(cmp "$1/$f" "$2/$f" 2>/dev/null | awk '{print $5}' | tr -d ,) || true
    if ! cmp -s "$1/$f" "$2/$f"; then echo "$f byte ${n:-?}"; return; fi
  done
  echo none
}
for r in "3 2048 7" "4 1024 11" "3 1024 3" "2 8192 5"; do
  set -- $r; w=$1 rows=$2 seed=$3
  rm -rf "$T/j" "$T/c"
  java_sd "$T/c13" "$T/j" --windows "$w" --rows "$rows" --seed "$seed" --inline-weight 0
  cpp_sd "$T/c" --windows "$w" --rows "$rows" --seed "$seed" --inline-weight 0
  if diff -r -x manifest.json "$T/j" "$T/c" > /dev/null && diff <(canon "$T/j/manifest.json") <(canon "$T/c/manifest.json") > /dev/null; then
    echo "v1.3 = C++ with no inlines: $r (the manifest as JSON)"
  else echo "DIFFERS from v1.3 with no inlines: $r"; bad=1; fi
  for x in j c k2; do rm -rf "$T/$x"; done
  java_sd "$T/c13" "$T/j" --windows "$w" --rows "$rows" --seed "$seed"
  cpp_sd "$T/c" --windows "$w" --rows "$rows" --seed "$seed"
  java_sd "$T/ck2" "$T/k2" --windows "$w" --rows "$rows" --seed "$seed"
  a=$(first_diff "$T/j" "$T/c"); b=$(first_diff "$T/k2" "$T/c")
  if [ "$a" = "$b" ]; then echo "v1.3 vs C++ with inlines: $r: equal up to $a (where the advice's K matters)"
  else echo "DIFFERS: $r: v1.3 vs C++ first at $a, K vs K' at $b"; bad=1; fi
done
[ $bad = 0 ] || { echo "synthetic_crosscheck: FAILED"; exit 1; }
echo "synthetic_crosscheck: synthetic_delivery equal to v1.3's patched by D42, and to v1.3 up to the unseeded advice"
