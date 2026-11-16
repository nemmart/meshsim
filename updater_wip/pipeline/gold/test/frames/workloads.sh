#!/bin/sh
# The host's frames on the two workloads (WORKER1): EVM Zone and Cyclone, small (trace-23 and input/22, with the hard-cut
# lists) or at 2^29 (128 segments of 2^22 rows). Per workload: the segments command's v1.3 file (at 2^29 against its
# published SHA-256, v1.3/planning/sessions/WORKER18-data/v13/segment-files-v1.3.sha256); the frames twice with --verify
# (--base=none --lru=none, --base=static --lru=all); frames-read --check --seg on each. Logs and the side files (sizes)
# into <out dir>; the frame files are deleted after their checks, their SHA-256 kept in the side files.
#   test/frames/workloads.sh <jolt-asic-planning tracer dir> <out dir> small|29
set -u
cd "$(dirname "$0")/../.."
T=$1; OUT=$2; SIZE=$3
mkdir -p "$OUT"
EZ=$T/evm-zone-4e3451c1d8c5; CY=$T/cyclone
if [ "$SIZE" = small ]; then
  RUNS="evm-zone-23 $EZ $EZ/inputs/trace-23-812ded164e49 test/cuts/evm-zone-23-hardcuts.cuts
cyclone-22 $CY $CY/input/22/input-default test/cuts/cyclone-22-hardcuts.cuts"
else
  RUNS="evm-zone-29 $EZ $EZ/inputs/trace-29-4599adc50712 -
cyclone-29 $CY $CY/input/29/input-15961266 -"
fi
PUBLISHED=../../../v1.3/planning/sessions/WORKER18-data/v13/segment-files-v1.3.sha256
J="java -Xmx5g -cp build"
fail=0
echo "$RUNS" | while read name prog in cuts; do
  C=""; [ "$cuts" != - ] && C="--cuts-file=$cuts"
  $J gold.Main segments $prog $in "$OUT/$name.seg" $C > "$OUT/$name.segments.log" 2>&1
  sha=$(sha256sum "$OUT/$name.seg" | cut -d' ' -f1)
  if [ "$SIZE" = 29 ]; then
    want=$(grep " $name.seg" "$PUBLISHED" | cut -d' ' -f1)
    [ "$sha" = "$want" ] && echo "$name: segments file $sha, the published v1.3 file: identical" \
      || { echo "$name: segments file $sha, published $want: DIFFERS"; echo FAIL > "$OUT/FAILED"; }
  else
    echo "$name: segments file $sha"
  fi
  for v in "full none none" "base static all"; do
    set -- $v
    f="$OUT/$name-$1"
    $J gold.Main frames $prog $in "$f.frames" $C --base=$2 --lru=$3 --verify > "$f.log" 2>&1
    $J gold.Main frames-read "$f.frames" --check --seg="$OUT/$name.seg" > "$f.read.txt" 2>&1
    if grep -q VERIFIED "$f.log" && grep -q "^check: ok" "$f.read.txt" && grep -q "segment file (.*): ok" "$f.read.txt"; then
      echo "$name, $1 table, lru $3: verified, checked, against the segment file ok"
    else
      echo "$name, $1 table, lru $3: FAILED"; echo FAIL > "$OUT/FAILED"
    fi
    grep -v "Picked up" "$f.log" | sed 's/^/    /'
    rm -f "$f.frames"
  done
  rm -f "$OUT/$name.seg" "$OUT/$name.seg.cuts"
done
[ ! -f "$OUT/FAILED" ]
