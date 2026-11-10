# t1carry-body.sh (WORKER17, the owner's Q1): ref's memory at every segment's start, T1's carry through all 128
# segments of evm-zone-29 and cyclone-29 (T1Carry; two JVMs in parallel; not timing). Per segment: nextFree, pages,
# filesScan and the sha256 of the snapshot's stream (W13's M3FILES1, uncompressed); the landings' sha256 (M3SRAM01,
# uncompressed) of segments 0, 1, 10, 126, 127. Checks: T1's end state against every next frame 2 and seg/NNN.pages
# against the update lists (T1Carry's own); segment 10's start and landings equal W13's snapshot and sram dumps
# ($DATA/w13-golden-m3, checked first against golden-m3/HASHES); 128 starts each.
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
G=$DATA/w13-golden-m3
(cd "$G" && grep -E ' (evm|cyclone)\.(files|sram)\.gz$' "$W/kit/golden-m3/HASHES" | sha256sum -c) || OKIN=0
[ $OKIN = 1 ] && verdict inputs PASS || verdict inputs FAIL
if [ $OKIN = 1 ]; then
  cd "$KR"
  sec "T1's carry, both workloads (two JVMs)"
  PIDS=""
  for P in evm-zone-29 cyclone-29; do
    S=${P%%-*}
    java -Xmx${T1_HEAP:-6g} -Dreplay.tables=$KT -cp $KC T1Carry $P --w26 $D26/$P sram=0,1,10,126,127 > $RES/$S.t1carry.txt 2>&1 & PIDS="$PIDS $!:$S"
  done
  for X in $PIDS; do wait ${X%%:*}; R=$?; S=${X#*:}; echo "$S: exit $R"; grep -v '^start ' "$RES/$S.t1carry.txt"; [ $R = 0 ] && verdict "t1carry-$S" PASS || verdict "t1carry-$S" FAIL; done
  for S in evm cyclone; do
    N=$(grep -c '^start ' $RES/$S.t1carry.txt); echo "$S: $N starts"; [ "$N" = 128 ] && verdict "$S-128-starts" PASS || verdict "$S-128-starts" FAIL
    A=$(awk '$1 == "start" && $2 == 10 {print $10}' $RES/$S.t1carry.txt); B=$(zcat $G/$S.files.gz | sha256sum | cut -d' ' -f1)
    echo "$S segment 10 start: T1Carry $A, W13's snapshot $B"; [ -n "$A" ] && [ "$A" = "$B" ] && verdict "$S-start10-equals-w13" PASS || verdict "$S-start10-equals-w13" FAIL
    A=$(awk '$1 == "sram" && $2 == 10 {print $4}' $RES/$S.t1carry.txt); B=$(zcat $G/$S.sram.gz | sha256sum | cut -d' ' -f1)
    echo "$S segment 10 landings: T1Carry $A, W13's sram $B"; [ -n "$A" ] && [ "$A" = "$B" ] && verdict "$S-sram10-equals-w13" PASS || verdict "$S-sram10-equals-w13" FAIL
    grep -E '^(start|sram) ' $RES/$S.t1carry.txt > $RES/$S.starts
  done
fi
