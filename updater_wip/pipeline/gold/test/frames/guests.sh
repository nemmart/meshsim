#!/bin/sh
# The host's frames on the repository's guests (WORKER1): every gold guest that runs to its end (test/guests/expected.txt),
# jumpmap's jumps guest (v1.36/pipeline/jumpmap/test/make_jump_guest.py), and keccak256 again with all three input files
# (a mem_conf of its own, so that frame 6 has three runs). Each guest is spread over many of the 128 segments, written
# twice (--base=none --lru=none; --base=static --lru=all) with --verify, then read back with frames-read --check and
# --seg against the segments command's file at the same cuts. Then FramesTest: the page counts two ways, and the checks
# caught on broken files.
#   test/frames/guests.sh     from gold's directory, after build.sh
set -e
cd "$(dirname "$0")/../.."
CP=build
W=/tmp/gold-frames-guests; rm -rf "$W"; mkdir -p "$W/empty"
JUMPMAP=../../../v1.36/pipeline/jumpmap
python3 "$JUMPMAP/test/make_jump_guest.py" "$W" > /dev/null
# keccak256 with input, trusted and untrusted advice (sizes not multiples of 8)
mkdir -p "$W/kin" "$W/kin-input"
cp test/guests/keccak256/program.elf "$W/kin/"
cat > "$W/kin/program.json" <<'J'
{"mem_conf": {"max_untrusted_advice_size": 8192, "max_trusted_advice_size": 1024, "max_input_size": 300,
 "max_output_size": 8, "stack_size": 1048576, "memory_size": 1048576}}
J
python3 -c "
import random; r=random.Random(7)
open('$W/kin-input/input.bin','wb').write(bytes(r.randrange(256) for _ in range(299)))
open('$W/kin-input/trusted_advice.bin','wb').write(bytes(r.randrange(256) for _ in range(1001)))
open('$W/kin-input/untrusted_advice.bin','wb').write(bytes([0]*4096 + [r.randrange(256) for _ in range(4000)]))"
GUESTS=$(grep "rows, run hash" test/guests/expected.txt | cut -d: -f1)
fail=0; n=0
for g in $GUESTS jumps kin; do
  case $g in jumps) d=$W/jumps; i=$W/empty;; kin) d=$W/kin; i=$W/kin-input;; *) d=test/guests/$g; i=$W/empty;; esac
  rows=$(java -cp $CP gold.Main run "$d" "$i" | sed 's/ rows.*//')
  R=$(( (rows + 99) / 100 )); [ $R -lt 1 ] && R=1               # about 100 segments with rows
  java -cp $CP gold.Main segments "$d" "$i" "$W/$g.seg" --segment-rows=$R > /dev/null
  for v in "none none" "static all"; do
    set -- $v
    out=$(java -cp $CP gold.Main frames "$d" "$i" "$W/$g.frames" --segment-rows=$R --base=$1 --lru=$2 --verify) || true
    chk=$(java -cp $CP gold.Main frames-read "$W/$g.frames" --check --seg="$W/$g.seg" --brief | tail -2 | tr '\n' ' ') || true
    n=$((n + 1))
    if echo "$out" | grep -q VERIFIED && echo "$chk" | grep -q "check: ok" && echo "$chk" | grep -q "segment file ([0-9]* segments): ok"; then
      echo "$g ($rows rows, $R a segment, base $1, lru $2): ok"
    else
      echo "$g (base $1, lru $2): FAILED"; echo "$out"; echo "$chk"; fail=$((fail + 1))
    fi
  done
done
java -cp $CP gold.FramesTest test/guests/keccak256 "$W/empty" 32 | grep -v "^verify:\|^check:\|^CHECK\|^COUNT\|^IMAGE\|^$" || fail=$((fail + 1))
rm -rf "$W"
[ $fail = 0 ] && echo "frames on the guests: $n runs, all ok" || { echo "frames on the guests: $fail FAILED"; exit 1; }
