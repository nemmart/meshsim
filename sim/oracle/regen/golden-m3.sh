#!/bin/bash
# regen.sh KIT_RUN KIT_CP TABLES DATA OUT (WORKER13; sim/tests/AUDIT-M3.md section 7): regenerate M3's references and
# check them against this directory. KIT_RUN is a directory holding hops.txt and sw/ (the images, IMAGES' hashes),
# KIT_CP the classpath of sim/ref's classes, gold's and sim/oracle/ref's drivers (compiled for Java 21), TABLES the
# universal replayer tables, DATA the persistent data (w9-deliveries, w26-jumpmap, w13-golden-m3), OUT a scratch dir.
# As a Quest task: sim/planning/sessions/WORKER13-tasks/mk_task.sh with regen-body.sh. Checks, per run (evm-zone-29 10,
# cyclone-29 10): RUNS' figures; every chip line (*.chip); every unit line against DATA/w13-golden-m3/*.fpchip.gz; the
# inputs' dumps against HASHES. Exit 0 when all hold.
set -u
RUN=$1 CP=$2 TB=$3 DATA=$4 OUT=$5; HERE=$(cd "$(dirname "$0")/../../tests/golden-m3" && pwd); G=$DATA/w13-golden-m3
mkdir -p "$OUT"; cd "$RUN"; FAIL=0
for S in evm cyclone; do
  P=evm-zone-29; [ $S = cyclone ] && P=cyclone-29
  java -Xmx6g -Dreplay.tables=$TB -cp $CP M3Seg $DATA/w9-deliveries $P --w26 $DATA/w26-jumpmap/$P --segment 10 \
     fpchip=$OUT/$S.fpchip snapshot=$OUT/$S.files.gz sram=$OUT/$S.sram.gz csv=$OUT/$S.csv > $OUT/$S.log 2>&1 || { echo "$S: M3Seg failed"; FAIL=1; }
  [ "$(tail -1 $OUT/$S.csv | cut -d, -f1-10)" = "$(grep "^$S " $HERE/RUNS | cut -d' ' -f2)" ] && echo "$S: figures equal" || { echo "$S: figures differ"; FAIL=1; }
  diff <(grep -E '^[0-9]+ chip ' $HERE/$S.chip) <(grep -E '^[0-9]+ chip ' $OUT/$S.fpchip) > /dev/null && echo "$S: chip lines equal" || { echo "$S: chip lines differ"; FAIL=1; }
  if [ -r $G/$S.fpchip.gz ]; then cmp -s <(zcat $G/$S.fpchip.gz | grep -v '^#') <(grep -v '^#' $OUT/$S.fpchip) && echo "$S: unit lines equal" || { echo "$S: unit lines differ"; FAIL=1; }; fi
  for f in files sram; do
    [ "$(sha256sum < $OUT/$S.$f.gz | cut -d' ' -f1)" = "$(grep " $S.$f.gz\$" $HERE/HASHES | cut -d' ' -f1)" ] && echo "$S: $f dump equal" || { echo "$S: $f dump differs"; FAIL=1; }
  done
done
exit $FAIL
