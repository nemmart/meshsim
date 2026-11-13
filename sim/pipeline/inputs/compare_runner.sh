#!/bin/bash
# compare_runner.sh RUN DATA: a 2^29 build in OUT against the runner's copies in DATA (~questrunner/data), file by file:
#   the delivery and its selected windows: the per-program manifests (which hash every file) equal 103/104's and the
#     runner's, every rebuilt window file equals its manifest, and the tables beside the windows equal the runner's;
#   the v1.26.1 conversion: its manifest equals 105/106/107's logged one in one of ConvertDelivery's two key orders;
#   gold and jumpmap: every file of DATA/w26-jumpmap/RUN (task 109) equal, but report.json; proof.json and
#     segments.seg.json without their path fields.
# Prints one line per check and PASS or FAIL.
set -u
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/pins.sh"
RUN=$1; DATA=$2; run_info "$RUN" || exit 2
OUT=${OUT:-$HOME/meshsim-inputs}; PY="python3 $HERE/lib/inputs.py"
ref() { awk -v r="$RUN" -v w="$1" '$1==r && $2==w {print $3}' "$HERE/manifest/runner-refs.txt"; }
bad=0; ok() { echo "ok    $*"; }; no() { echo "FAIL  $*"; bad=1; }
for k in delivery:w9-deliveries $( [ -n "$SEL" ] && echo selected:w9-deliveries-selected ); do
  w=${k%%:*} d=${k#*:}
  new=$(sha256sum "$OUT/$d/$RUN.manifest.json" 2>/dev/null | cut -d' ' -f1); old=$(sha256sum "$DATA/$d/$RUN.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ -n "$new" ] && [ "$new" = "$(ref $w)" ] && [ "$new" = "$old" ] && ok "$d/$RUN.manifest.json $new (= 10x's, = the runner's)" || no "$d/$RUN.manifest.json new $new, runner $old, recorded $(ref $w)"
  $PY verify-delivery "$OUT/$d" "$RUN" | sed 's/^/      /' || no "$d: rebuilt files against their manifest"
done
for f in "$DATA/w9-deliveries/$RUN"/*.bin; do
  case $(basename "$f") in window_*) continue ;; esac
  cmp -s "$f" "$OUT/w9-deliveries/$RUN/$(basename "$f")" && ok "tables: $(basename "$f")" || no "tables: $(basename "$f") differs"
done
read -r canon other < <($PY canon-converted "$OUT/v1261/$RUN.manifest.json")
hit=""; for r in $(ref v1261); do [ "$r" = "$canon" ] || [ "$r" = "$other" ] && hit="$hit $(grep "$r" "$HERE/manifest/runner-refs.txt" | awk '{print $4}')"; done
[ -n "$hit" ] && ok "v1261/$RUN.manifest.json $canon (canonical), equal to the logged manifests of task$hit (one in each key order)" || no "v1261/$RUN.manifest.json $canon / $other: neither order equals $(ref v1261 | tr '\n' ' ')"
$PY verify-delivery "$OUT/v1261" "$RUN" | sed 's/^/      /' || no "v1261: converted files against their manifest"
M=$DATA/w26-jumpmap/$RUN/manifest.sha256; N=$OUT/w26-jumpmap/$RUN
[ -f "$M" ] || no "no $M"
cmp -s <(awk '{print $2}' "$M" | sort) <(cd "$N" && find . -type f ! -name manifest.sha256 | sort) && ok "w26-jumpmap/$RUN: the same $(wc -l < "$M") files" || no "w26-jumpmap/$RUN: the file lists differ"
n=0; while read -r sha f; do
  case $f in
    ./report.json) continue ;;
    ./proof.json) $PY jsoneq "$DATA/w26-jumpmap/$RUN/proof.json" "$N/proof.json" input > /dev/null && ok "proof.json equal (without input, a path)" || no "proof.json differs" ;;
    ./segments.seg.json) $PY jsoneq "$DATA/w26-jumpmap/$RUN/segments.seg.json" "$N/segments.seg.json" program input > /dev/null && ok "segments.seg.json equal (without program, input: paths)" || no "segments.seg.json differs" ;;
    *) [ "$(sha256sum "$N/$f" 2>/dev/null | cut -d' ' -f1)" = "$sha" ] && n=$((n+1)) || no "w26-jumpmap/$RUN/${f#./} differs" ;;
  esac
done < "$M"
ok "w26-jumpmap/$RUN: $n files byte for byte (segments.seg, map.lines, frame6.bin, initial.pages, seg/*, segments.csv, run_only.csv, .cuts)"
cmp -s "$OUT/gold/$RUN/segments.seg" "$N/segments.seg" && ok "gold/$RUN/segments.seg = the jumpmap directory's" || no "gold/$RUN/segments.seg differs from w26-jumpmap's"
if [ -f "$OUT/gold-v1.3/$RUN.COMPLETE" ]; then   # GOLD_FORMAT=v1.3: WORKER18's accepted v1.3 file
  v=$(sha256sum "$OUT/gold-v1.3/$RUN/segments.seg" | cut -d' ' -f1)
  [ "$v" = "$(ref gold-v13)" ] && ok "gold-v1.3/$RUN/segments.seg $v (= WORKER18's and WORKER16's)" || no "gold-v1.3/$RUN/segments.seg $v, WORKER18's $(ref gold-v13)"
fi
[ $bad = 0 ] && echo "$RUN against the runner's copies: PASS" || { echo "$RUN against the runner's copies: FAIL"; exit 1; }
