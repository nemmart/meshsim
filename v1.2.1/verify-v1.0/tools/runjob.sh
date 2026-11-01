#!/usr/bin/env bash
# tools/runjob.sh OUT HEAP "impl<TAB>kind<TAB>id<TAB>payload" -- one verification run (called by run.sh from the kit's root).
#   kind kit:   payload = the kit's jobs.txt line (weight, group, id, seed, SortTest arguments), run by that kit's own
#               tools/runjob.sh in IMPL/ with results in OUT/IMPL/kit/
#   kind bt:    the same, at full size (64 x 65,536), results in OUT/IMPL/bt/ (DEV 115)
#   kind split: payload = NN; SplitRun on sets-65536/NN.bin in IMPL/split/, log OUT/IMPL/split/NN.log, whose first line
#               is the run's inputs (SHA-256 of the split jar, image, hops.txt and the set, root-relative names)
# OUT must be relative to the kit's root (native Windows Java does not read Cygwin paths).
OUT=$1; HEAP=$2; LINE=$3
IMPL=$(printf '%s' "$LINE" | cut -f1); KIND=$(printf '%s' "$LINE" | cut -f2); ID=$(printf '%s' "$LINE" | cut -f3); PAY=$(printf '%s' "$LINE" | cut -f4-)
T0=$(date +%s)
if [ "$KIND" = kit ] || [ "$KIND" = bt ]; then
  mkdir -p "$OUT/$IMPL/$KIND"
  (cd "$IMPL" && tools/runjob.sh "../$OUT/$IMPL/$KIND" "$HEAP" "$PAY") > /dev/null
  F=$(printf '%s' "$(printf '%s' "$PAY" | cut -f3)" | tr '/' '_')
  echo "$(date +%H:%M:%S) $IMPL $KIND $ID: $(( $(date +%s) - T0 )) s; $(grep -E 'SortTest.*passed' "$OUT/$IMPL/$KIND/$F.log" 2>/dev/null | tail -1)"
else
  mkdir -p "$OUT/$IMPL/split"
  L="$OUT/$IMPL/split/$PAY.log"
  S=$IMPL/split
  {
    printf 'inputs: '
    java -cp $S/split.jar Inputs hash $S/split.jar $S/sw/build/loader.bin $S/sw/build/split.code.bin $S/sw/build/split.data.bin \
      $S/sw/build/split.sym $S/hops.txt sets-65536/$PAY.bin | awk '{ printf "%s=%s ", $2, $1 } END { print "" }'
    (cd $S && java -Xmx$HEAP -cp split.jar SplitRun ../../sets-65536/$PAY.bin)
  } > "$L.part" 2>&1
  mv "$L.part" "$L"
  echo "$(date +%H:%M:%S) $IMPL $ID: $(( $(date +%s) - T0 )) s; $(grep -E '^SplitRun:' "$L" | tail -1)"
fi
