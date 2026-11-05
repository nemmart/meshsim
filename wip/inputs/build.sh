#!/bin/bash
# build.sh WORKLOAD SIZE [STEP...]: rebuilds the simulation's inputs from the pinned sources (README.md).
#   WORKLOAD  evm-zone | cyclone          SIZE  small (EVM Zone trace-23, Cyclone input/22) | 29 (2^29)
#   STEP      tools deliveries convert gold jumpmap   (default: all, in that order; each needs the ones before it)
# Environment:
#   OUT=DIR          where everything goes (default ~/meshsim-inputs): src/, build/, logs/ and the outputs, laid out as the
#                    runner's persistent data: w9-deliveries/, w9-deliveries-selected/, v1261/, gold/, w26-jumpmap/
#   JAP_DIR=DIR      a jolt-asic-planning checkout at the pin (default: OUT/src/jolt-asic-planning, fetched by fetch.sh)
#   CLASSES=DIR      prebuilt classes (gold/, jumpmap/, meshsim/), for a machine without javac (a Quest task's kit)
#   GOLD_COMMIT=H    recorded in proof.json (default: the last commit of wip/gold in this checkout)
#   GOLD_FORMAT=v1.2 the segment file's format (v1.3: WORKER18's frames 6 and 7, once they land in gold)
#   LIBELF_PREFIX=D  a libelf installed under D/usr (no libelf-dev on the machine)
#   JOBS=N           compile jobs (default nproc);  JAVA_HEAP=4g
# Every step writes into a staging directory beside its target and moves it into place only when complete and checked,
# with a COMPLETE marker; a re-run skips a complete step (FORCE=1 redoes it). Safe to re-run after an interruption.
set -u
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/pins.sh"
MESHSIM=$(cd "$HERE/../.." && pwd)
[ $# -ge 2 ] || { sed -n '2,4p' "$0"; exit 1; }
RUN=$(run_name "$1" "$2") || exit 1; shift 2
STEPS=${*:-tools deliveries convert gold jumpmap}
run_info "$RUN" || exit 1
OUT=${OUT:-$HOME/meshsim-inputs}; mkdir -p "$OUT/logs"; OUT=$(cd "$OUT" && pwd)
JAP=${JAP_DIR:-$OUT/src/jolt-asic-planning}; T=$JAP/tracer
TB=$OUT/build/tracer; DW=$OUT/build/delivery_writer
PREBUILT=${CLASSES:+1}; CLASSES=${CLASSES:-$OUT/build/classes}
TAB=$MESHSIM/$TABLES_REL
GOLD_FORMAT=${GOLD_FORMAT:-v1.2}
GOLD_COMMIT=${GOLD_COMMIT:-$(git -C "$MESHSIM" log -1 --format=%h -- wip/gold 2>/dev/null)}
JOBS=${JOBS:-$(nproc)}; HEAP=${JAVA_HEAP:-4g}
PY="python3 $HERE/lib/inputs.py"
if [ -n "${LIBELF_PREFIX:-}" ]; then
  export CPATH=$LIBELF_PREFIX/usr/include${CPATH:+:$CPATH}
  export LIBRARY_PATH=$LIBELF_PREFIX/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}
  export LD_LIBRARY_PATH=$LIBELF_PREFIX/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
fi
say() { echo "[$RUN $(date -u +%H:%M:%S)] $*"; }
die() { say "FAILED: $*"; exit 1; }
complete() { [ -z "${FORCE:-}" ] && [ -f "$1.COMPLETE" ] && [ -e "$2" ] && [ "$(cat "$1.COMPLETE")" = "$(sha256sum "$2" | cut -d' ' -f1)" ]; }
mark() { sha256sum "$2" | cut -d' ' -f1 > "$1.COMPLETE"; }

# ---- tools: jolt-cpp's tracer and WORKER9's delivery_writer (at the pin), gold, jumpmap, v1.26.1's ConvertDelivery ----
step_tools() {
  [ -d "$JAP/.git" ] || [ -n "${JAP_DIR:-}" ] || OUT=$OUT "$HERE/fetch.sh" "$JAP" || die "fetch"
  [ -f "$T/delivery/delivery_writer.cpp" ] || die "no tracer at $T"
  local have; have=$(git -C "$JAP" rev-parse HEAD 2>/dev/null || cat "$JAP/COMMIT" 2>/dev/null)
  [ "$have" = "$JAP_COMMIT" ] || die "jolt-asic-planning at '$have', the pin is $JAP_COMMIT"
  if [ -n "${FORCE:-}" ] || [ "$(cat "$OUT/build/tracer.stamp" 2>/dev/null)" != "$JAP_COMMIT" ]; then
    say "tracer: cmake (FetchContent: fmt, nlohmann_json, xbyak) and build, $JOBS jobs"
    rm -rf "$TB" "$DW" "$OUT/build/tracer.stamp"
    local X=(); [ -n "${LIBELF_PREFIX:-}" ] && X=(-DCMAKE_PREFIX_PATH="$LIBELF_PREFIX/usr" -DCMAKE_LIBRARY_PATH="$LIBELF_PREFIX/usr/lib/x86_64-linux-gnu")
    cmake -S "$T" -B "$TB" -DCMAKE_BUILD_TYPE=Release "${X[@]}" > "$OUT/logs/tracer-configure.log" 2>&1 || { tail -20 "$OUT/logs/tracer-configure.log"; die "cmake configure"; }
    cmake --build "$TB" -j"$JOBS" --target tracer > "$OUT/logs/tracer-build.log" 2>&1 || { tail -20 "$OUT/logs/tracer-build.log"; die "tracer build"; }
    bash "$T/delivery/build.sh" "$T" "$TB" "$DW" >> "$OUT/logs/tracer-build.log" 2>&1 || { tail -20 "$OUT/logs/tracer-build.log"; die "delivery_writer build"; }
    echo "$JAP_COMMIT" > "$OUT/build/tracer.stamp"
  fi
  say "delivery_writer $DW (jolt-asic-planning ${JAP_COMMIT:0:7})"
  if [ -z "$PREBUILT" ]; then
    local M; M=$(git -C "$MESHSIM" rev-parse HEAD)
    if [ -n "${FORCE:-}" ] || [ "$(cat "$CLASSES.stamp" 2>/dev/null)" != "$M" ]; then
      say "classes: gold, jumpmap (wip/), ConvertDelivery ($MESHSIM_REL), javac --release 21"
      rm -rf "$CLASSES"; mkdir -p "$CLASSES"/{gold,jumpmap,meshsim}
      javac --release 21 -nowarn -d "$CLASSES/gold" $(find "$MESHSIM/wip/gold/src" -name '*.java') || die "javac gold"
      javac --release 21 -nowarn -d "$CLASSES/jumpmap" -cp "$CLASSES/gold" $(find "$MESHSIM/wip/jumpmap/src" -name '*.java') || die "javac jumpmap"
      # as v1.26.1's acceptance/make_kit.sh compiles it (meshsim's src/, then the replay harnesses)
      local S=$MESHSIM/$MESHSIM_REL
      javac --release 21 -nowarn -d "$CLASSES/meshsim" "$S"/src/*.java 2>&1 | grep -v '^Note:'
      javac --release 21 -nowarn -d "$CLASSES/meshsim" -cp "$CLASSES/meshsim" "$S"/test/T.java "$S"/test/Replay*.java \
        "$S"/test/StageB.java "$S"/test/SyntheticDelivery.java "$S"/test/ConvertDelivery.java 2>&1 | grep -v '^Note:'
      [ -f "$CLASSES/meshsim/ConvertDelivery.class" ] || die "javac ConvertDelivery"
      echo "$M" > "$CLASSES.stamp"
    fi
  fi
  for c in gold/gold/Main.class jumpmap/jumpmap/Main.class meshsim/ConvertDelivery.class; do [ -f "$CLASSES/$c" ] || die "no $CLASSES/$c"; done
  say "classes $CLASSES; gold $GOLD_COMMIT"
}

# ---- deliveries: WORKER9's delivery_writer, every window kept and self-proved (tasks 079/080, 103/104) ----
write_delivery() {  # ROOT WINDOWS LOG
  local R=$1 ST=$1/.staging-$RUN T0; T0=$(date +%s); rm -rf "$ST"; mkdir -p "$ST"
  ( cd "$T" && "$DW" "$PD" "$IN" "$TAB" "$ST" "$RUN" --windows="$2" --keep=all ) > "$OUT/logs/$RUN-$3" 2>&1 || { grep -v INFO "$OUT/logs/$RUN-$3" | tail -5; return 1; }
  say "$3: delivery_writer done in $(( $(date +%s) - T0 )) s: $(grep -v INFO "$OUT/logs/$RUN-$3" | tail -1)"
  $PY windows-ok "$ST/$RUN.manifest.json" || return 1
  cp "$TAB"/*.bin "$ST/$RUN/"
  rm -f "$R/$RUN.COMPLETE"; rm -rf "${R:?}/$RUN" "$R/$RUN.manifest.json"
  mv "$ST/$RUN" "$R/$RUN" && mv "$ST/$RUN.manifest.json" "$R/$RUN.manifest.json" && rmdir "$ST" || return 1
  $PY merge "$R" > /dev/null && mark "$R/$RUN" "$R/$RUN.manifest.json"
}
step_deliveries() {
  [ -x "$DW" ] || die "no delivery_writer: run the tools step"
  local D=$OUT/w9-deliveries S=$OUT/w9-deliveries-selected; mkdir -p "$D"
  if complete "$D/$RUN" "$D/$RUN.manifest.json"; then say "deliveries: complete already"; else
    write_delivery "$D" all delivery.log || die "the delivery"; fi
  say "deliveries: $D/$RUN.manifest.json $(cat "$D/$RUN.COMPLETE")"
  [ -n "$SEL" ] || return 0
  mkdir -p "$S"
  if complete "$S/$RUN" "$S/$RUN.manifest.json"; then say "selected windows: complete already"; else
    write_delivery "$S" "$SEL" selected.log || die "the selected windows"; fi
  # packed as 079/080 did (top directory w9-deliveries-selected, every program present), reproducibly (no times, owners)
  tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --exclude '.staging-*' -cf - -C "$OUT" w9-deliveries-selected \
    | gzip -n > "$OUT/w9-selected-windows.tgz.tmp" && mv "$OUT/w9-selected-windows.tgz.tmp" "$OUT/w9-selected-windows.tgz" || die "the tgz"
  say "selected windows: $S/$RUN.manifest.json $(cat "$S/$RUN.COMPLETE"); w9-selected-windows.tgz repacked"
}

# ---- convert: the v1.25 delivery to v1.26.1's records (ConvertDelivery, as tasks 105-107); every other file a link ----
step_convert() {
  local D=$OUT/w9-deliveries V=$OUT/v1261 ST=$OUT/v1261/.staging-$RUN
  complete "$D/$RUN" "$D/$RUN.manifest.json" || die "the delivery isn't complete: run the deliveries step"
  if complete "$V/$RUN" "$V/$RUN.manifest.json"; then say "v1.26.1 conversion: complete already"; else
    rm -rf "$ST"; mkdir -p "$ST"
    java -Xmx2g -cp "$CLASSES/meshsim" ConvertDelivery "$D" "$ST" "$RUN" > "$OUT/logs/$RUN-convert.log" 2>&1 || { tail -5 "$OUT/logs/$RUN-convert.log"; die "ConvertDelivery"; }
    head -2 "$OUT/logs/$RUN-convert.log"
    $PY canon-converted "$ST/$RUN.manifest.json" > /dev/null || die "canonical manifest"
    rm -f "$V/$RUN.COMPLETE"; rm -rf "${V:?}/$RUN" "$V/$RUN.manifest.json"
    mv "$ST/$RUN" "$V/$RUN" && mv "$ST/$RUN.manifest.json" "$V/$RUN.manifest.json" && rmdir "$ST" || die "moving into place"
    mark "$V/$RUN" "$V/$RUN.manifest.json"
  fi
  say "v1.26.1: $V/$RUN.manifest.json $(cat "$V/$RUN.COMPLETE") (canonical key order)"
}

# ---- gold: section 16's segment file (gold.Main segments; v1.2 frames 1-5) ----
step_gold() {
  local G=$OUT/gold ST=$OUT/gold/.staging-$RUN
  case $GOLD_FORMAT in
    v1.2) ;;
    v1.3) die "GOLD_FORMAT=v1.3: gold doesn't write frames 6 and 7 yet (WORKER18); add its command here when it does" ;;
    *) die "GOLD_FORMAT $GOLD_FORMAT (v1.2 or v1.3)" ;;
  esac
  if complete "$G/$RUN" "$G/$RUN/segments.seg"; then say "gold: complete already"; else
    rm -rf "$ST"; mkdir -p "$ST"
    ( cd "$T" && java -Xmx"$HEAP" -cp "$CLASSES/gold" gold.Main segments "$PD" "$IN" "$ST/segments.seg" ) > "$OUT/logs/$RUN-gold.log" 2>&1 || { tail -5 "$OUT/logs/$RUN-gold.log"; die "gold"; }
    tail -1 "$OUT/logs/$RUN-gold.log"
    rm -f "$G/$RUN.COMPLETE"; rm -rf "${G:?}/$RUN"; mv "$ST" "$G/$RUN" || die "moving into place"
    mark "$G/$RUN" "$G/$RUN/segments.seg"
  fi
  say "gold ($GOLD_FORMAT): $G/$RUN/segments.seg $(cat "$G/$RUN.COMPLETE")"
}

# ---- jumpmap: the jump table (frame6.bin, map.lines), initial.pages, seg/NNN.pages, proof.json (as task 109) ----
step_jumpmap() {
  local G=$OUT/gold J=$OUT/w26-jumpmap ST=$OUT/w26-jumpmap/.staging-$RUN
  complete "$G/$RUN" "$G/$RUN/segments.seg" || die "gold's segment file isn't complete: run the gold step"
  mkdir -p "$J"
  if complete "$J/$RUN" "$J/$RUN/manifest.sha256"; then say "jumpmap: complete already"; else
    rm -rf "$ST"; mkdir -p "$ST"; cp "$G/$RUN"/segments.seg* "$ST/"
    ( cd "$T" && java -Xmx"$HEAP" -cp "$CLASSES/gold:$CLASSES/jumpmap" jumpmap.Main "$PD" "$IN" "$ST/segments.seg" "$ST" \
        --name="$RUN" --gold-commit="$GOLD_COMMIT" ) > "$OUT/logs/$RUN-jumpmap.log" 2>&1 || { tail -5 "$OUT/logs/$RUN-jumpmap.log"; die "jumpmap"; }
    grep -v '^  [a-z+]' "$OUT/logs/$RUN-jumpmap.log" | tail -4
    local n ok; n=$(ls "$ST"/seg/*.json | wc -l); ok=$(grep -l '"gold_check": "ok"' "$ST"/seg/*.json | wc -l)
    [ "$n" -gt 0 ] && [ "$ok" = "$n" ] || die "jumpmap: gold_check ok at $ok of $n segments"
    (cd "$ST" && find . -type f ! -name manifest.sha256 | sort | xargs sha256sum > manifest.sha256)
    rm -f "$J/$RUN.COMPLETE"; rm -rf "${J:?}/$RUN"; mv "$ST" "$J/$RUN" || die "moving into place"
    mark "$J/$RUN" "$J/$RUN/manifest.sha256"
    say "jumpmap: gold_check ok at all $n segments"
  fi
  say "jumpmap: $J/$RUN/manifest.sha256 $(cat "$J/$RUN.COMPLETE")"
}

say "build: $STEPS into $OUT (jolt-asic-planning ${JAP_COMMIT:0:7}, meshsim $(git -C "$MESHSIM" rev-parse --short HEAD 2>/dev/null || cat "$MESHSIM/VERSION" 2>/dev/null))"
for s in $STEPS; do
  case $s in tools|deliveries|convert|gold|jumpmap) "step_$s" ;; *) die "unknown step $s" ;; esac
done
say "done: $STEPS"
