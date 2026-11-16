#!/bin/bash
# build.sh SET...: the host's side of v1.37's proof steps (run_tests.sh: proof-matrix, proof-real, proof-29), from the
# pinned sources: for each run, gold's frame file (`gold frames`), the link's packet file (`gold packets`), and gold's
# reference for each segment the steps run (`gold frames-images`: every image's SHA-256). Gold is the frames' gold,
# ../../../updater_wip/pipeline/gold (not ../gold, v1.36's). Seconds a run: gold is an emulator.
#
#   SET     matrix  the small inputs (EVM Zone trace-23, Cyclone input/22), N 64, cut into segments of 8 windows
#                   (--segment-rows=524288), each four ways: its update-word frames raw or LRU-coded, a full new jump
#                   table or one added to a static base table. References for s = 0, 1 and the middle segment
#           real    the small inputs as the chip runs them today: N 128, segments of 64 windows, a static base table,
#                   each frame in its smaller form. References for EVM Zone's s = 0 and 1 and Cyclone's 0, the three
#                   segments whose windows are recorded (pipeline/inputs)
#           29      EVM Zone trace-29 and Cyclone input/29, likewise. References for s = 0, 1, the middle segment and
#                   the last two that have rows
# Environment:
#   OUT=DIR           the inputs' directory (default ~/meshsim-inputs, pipeline/inputs/build.sh's): the files go to
#                     OUT/v137/SET/
#   JAP_DIR=DIR       a jolt-asic-planning checkout at pipeline/inputs/pins.sh's pin (default OUT/src/jolt-asic-planning)
#   GOLD_CLASSES=DIR  gold's classes, for a machine without javac (default: compiled here into OUT/build/gold-frames)
#   PACKETIZE=FILE    the tests' packet writer (build/clang-suite/packetize): if it is there, each packet file must
#                     also equal its output, byte for byte
#   JAVA_HEAP=3g
# Each set's folder gets RUNS, a line a run: its name, N, the windows a segment has, and the segments to run; and
# SHA256SUMS. A set is rebuilt whole (seconds). Exit 1 on any failure.
set -eu
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/../inputs/pins.sh"
[ $# -ge 1 ] || { sed -n '2,14p' "$0"; exit 1; }
OUT=${OUT:-$HOME/meshsim-inputs}; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
T=${JAP_DIR:-$OUT/src/jolt-asic-planning}/tracer
[ -d "$T" ] || { echo "frames: no tracer folder at $T (JAP_DIR)"; exit 1; }
GOLD=$(cd "$HERE/../../../updater_wip/pipeline/gold" 2>/dev/null && pwd) || true
CLASSES=${GOLD_CLASSES:-}
if [ -z "$CLASSES" ]; then
  [ -n "$GOLD" ] || { echo "frames: no ../updater_wip/pipeline/gold, and GOLD_CLASSES names no classes"; exit 1; }
  command -v javac > /dev/null || { echo "frames: no javac (a JDK, Java 21), and GOLD_CLASSES names no classes"; exit 1; }
  CLASSES=$OUT/build/gold-frames; rm -rf "$CLASSES"; mkdir -p "$CLASSES"
  javac -d "$CLASSES" $(find "$GOLD/src" -name '*.java') 2> "$OUT/build/gold-frames.log" || { cat "$OUT/build/gold-frames.log"; exit 1; }
fi
J="java -Xmx${JAVA_HEAP:-3g} -cp $CLASSES gold.Main"
PACKETIZE=${PACKETIZE:-$HERE/../../build/clang-suite/packetize}

# one NAME RUN N WINDOWS BASE COMPRESSION PICK: a frame file, its packet file, and the references PICK names:
# "middle" 0, 1 and the middle segment; "ends" those and the last two with rows; else a list
one() {
  local name=$1 run=$2 n=$3 windows=$4 base=$5 comp=$6 pick=$7 f rows segs list s
  run_info "$run"
  f=$D/$name.frames
  $J frames "$T/$PD" "$T/$IN" "$f" --segment-rows=$((windows * 65536)) --asics="$n" --base="$base" --compression="$comp" --verify > "$D/$name.log" 2>&1 \
    && grep -q VERIFIED "$D/$name.log" || { tail -3 "$D/$name.log"; echo "frames: gold did not make and verify $name"; exit 1; }
  rows=$(sed -n 's/.*"rows": *\([0-9]*\).*/\1/p' "$f.json" | head -1)
  segs=$(( (rows + windows * 65536 - 1) / (windows * 65536) ))     # the segments that have rows
  case $pick in
    middle) list="0 1 $((segs / 2))" ;;
    ends)   list="0 1 $((segs / 2)) $((segs - 2)) $((segs - 1))" ;;
    *)      list=$pick ;;
  esac
  list=$(for s in $list; do [ "$s" -ge 0 ] && [ "$s" -lt "$segs" ] && echo "$s"; done | sort -un | tr '\n' ' ')
  $J packets "$f" "$D/$name.packets" >> "$D/$name.log" 2>&1 || { tail -3 "$D/$name.log"; echo "frames: gold did not packetize $name"; exit 1; }
  $J packets-read "$D/$name.packets" --compare="$f" >> "$D/$name.log" 2>&1 || { tail -3 "$D/$name.log"; echo "frames: $name.packets does not read back as its frames"; exit 1; }
  if [ -x "$PACKETIZE" ]; then
    "$PACKETIZE" "$f" "$D/$name.packets.tests" > /dev/null && cmp -s "$D/$name.packets" "$D/$name.packets.tests" \
      || { echo "frames: $name: the tests' packet writer and gold's packetizer differ"; exit 1; }
    rm -f "$D/$name.packets.tests"; CROSS=$((CROSS + 1))
  fi
  for s in $list; do
    $J frames-images "$f" "$s" 2>> "$D/$name.log" | grep -v '^ok$' > "$D/$name-s$s.images.txt"
    [ "$(wc -l < "$D/$name-s$s.images.txt")" -ge 6 ] || { echo "frames: gold's reference for $name, s $s"; exit 1; }
  done
  echo "$name $n $windows $list" >> "$D/RUNS"
  echo "$name: $rows rows, $segs segments of $windows windows, N $n; frames $(stat -c %s "$f") B, packets $(stat -c %s "$D/$name.packets") B; s: $list"
}

for SET in "$@"; do
  D=$OUT/v137/$SET; rm -rf "$D"; mkdir -p "$D"; CROSS=0
  case $SET in
    matrix)
      for w in evm-zone-23 cyclone-22; do
        one "$w-raw-full" "$w" 64 8 none none middle
        one "$w-raw-base" "$w" 64 8 static none middle
        one "$w-lru-full" "$w" 64 8 none lru middle
        one "$w-lru-base" "$w" 64 8 static lru middle
      done ;;
    real)
      one evm-zone-23 evm-zone-23 128 64 static auto "0 1"
      one cyclone-22 cyclone-22 128 64 static auto "0" ;;
    29)
      one evm-zone-29 evm-zone-29 128 64 static auto ends
      one cyclone-29 cyclone-29 128 64 static auto ends ;;
    *) echo "frames: SET is matrix, real or 29 (got '$SET')"; exit 1 ;;
  esac
  rm -f "$D"/*.log
  (cd "$D" && sha256sum $(ls | grep -v SHA256SUMS) > SHA256SUMS)
  echo "frames: $SET: $(wc -l < "$D/RUNS") runs in $D$([ $CROSS -gt 0 ] && echo "; $CROSS packet files equal to the tests' writer's")"
done
