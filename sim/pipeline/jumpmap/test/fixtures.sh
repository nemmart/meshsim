#!/bin/sh
# The jump map's test fixtures (wip/jumpmap/out/): gold's hand-assembled guests that run to their end, and the jumps
# guest (make_jump_guest.py), each cut into 4 segments of 8 rows (keccak256: 1,024) so the per-segment page lists and the image check across
# segments are exercised. Writes each guest's outputs (proof.json, map.lines, frame6.bin, initial.pages, seg/, report)
# into out/<guest>/.
#   test/fixtures.sh            regenerate out/ and fail if anything differs from the committed files (git diff)
#   test/fixtures.sh --write    regenerate out/ only
set -e
cd "$(dirname "$0")/.."
./build.sh > /dev/null
CP=../gold/build:build
W=/tmp/jumpmap-fixtures; rm -rf "$W"; mkdir -p "$W/empty"
python3 test/make_jump_guest.py "$W" > /dev/null
GUESTS="assert_eq_pass d12_c_slli_x0 d1_decodable_gap d1_rodata_in_text ebreak fence keccak256 rd0_add stack_canary_store unimp_unreached"
rm -rf out; mkdir -p out
for gname in $GUESTS jumps; do
  if [ $gname = jumps ]; then d=$W/jumps; else d=../gold/test/guests/$gname; fi
  R=8; [ $gname = keccak256 ] && R=1024                     # keccak256: 3,091 rows
  java -cp $CP gold.Main segments "$d" "$W/empty" "$W/$gname.seg" --segment-rows=$R --segments=4 --format=v1.2 > /dev/null
  java -cp $CP jumpmap.Main "$d" "$W/empty" "$W/$gname.seg" "out/$gname" --segment-rows=$R --name=$gname --gold-commit=fixture \
    > "out/$gname.log" 2>&1 || { cat "out/$gname.log"; echo "$gname: FAILED"; exit 1; }
  sed -i 's/; [0-9.]* s$//' "out/$gname.log"
  sed -i '/"seconds"/d' "out/$gname/report.json"
  sed -i 's#"input": "[^"]*"#"input": "empty"#' "out/$gname/proof.json"
  echo "$gname: $(grep -c . "out/$gname/segments.csv") lines in segments.csv, $(grep acceptance "out/$gname.log" | cut -d: -f2 | cut -d';' -f1)"
done
rm -rf "$W"
if [ "$1" != "--write" ]; then
  git diff --exit-code --stat -- out && echo "fixtures: unchanged"
fi
