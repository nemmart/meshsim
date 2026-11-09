# golden-baseline-body.sh (WORKER13): M3's references and ref's baseline (sim/tests/AUDIT-M3.md section 7, PROMPT 4-5).
# A (not timing; two JVMs in parallel): M3Seg on evm-zone-29 10 and cyclone-29 10 with every unit's fingerprint every
#   100,000 cycles and at the boot's end, C&C 3's exit and the end (fpchip), the counts (9.2's detail, 9.4's write
#   buffer), the framework harness's inputs (the memory snapshot, the big-SRAM landings). Large files go to persistent
#   data ($DATA/w13-golden-m3), with their sha256 here; results keep the chip lines. Checks: the local self-checks; the
#   figures equal Quest 159's; the Tracer's lines equal Quest 160's evm.fp at every common cycle.
# B (timing, alone, sequential): ref (M3Seg, no hooks: = TracerT2, Quest 160) on each run, three runs each, one JVM
#   pinned to one P core (the core and its hyperthread; sim/tools/machine.sh, D21); setup, boot, segment and checks
#   timed apart.
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
[ $OKIN = 1 ] || verdict inputs FAIL
if [ $OKIN = 1 ]; then
  G=$DATA/w13-golden-m3; mkdir -p "$G" || { echo "no $G"; exit 4; }
  cd "$KR"
  sec "self-checks"
  java -Dreplay.tables=$KT -cp $KC M3FpSelf > $W/s1.txt 2>&1; tail -1 $W/s1.txt
  java -Xmx3g -Dreplay.tables=$KT -cp $KC M3ChipFpSelf > $W/s2.txt 2>&1; tail -2 $W/s2.txt
  grep -q 'M3FpSelf: PASS' $W/s1.txt && grep -q 'M3ChipFpSelf: PASS' $W/s2.txt && verdict self-checks PASS || verdict self-checks FAIL
  sec "A: the references (two JVMs)"
  J="java -Xmx${T1_HEAP:-6g} -Dreplay.tables=$KT -cp $KC M3Seg"
  PIDS=""
  for P in evm-zone-29 cyclone-29; do
    S=${P%%-*}
    $J $D9 $P --w26 $D26/$P --segment 10 fpchip=$G/$S.fpchip counts=1 snapshot=$G/$S.files.gz sram=$G/$S.sram.gz csv=$RES/$S.csv \
       "header=run $P segment 10 windows 64 (T2, raw); delivery manifest ${S9[$P]}; w26 ${S26[$P]}; images: 110u's (sim/planning/sessions/WORKER13/tasks/IMAGES)" \
       > $RES/$S.log 2>&1 & PIDS="$PIDS $!:$S"
  done
  for X in $PIDS; do wait ${X%%:*}; R=$?; echo "${X#*:}: exit $R"; cat "$RES/${X#*:}.log"; [ $R = 0 ] && verdict "ref-${X#*:}" PASS || verdict "ref-${X#*:}" FAIL; done
  f() { tail -1 "$RES/$1.csv" | cut -d, -f1-9; }
  [ "$(f evm)" = "10,64,23693532,19531654,4129492,14162,1772179,234206,4194304" ] && verdict evm-equals-159 PASS || verdict evm-equals-159 FAIL
  [ "$(f cyclone)" = "10,64,28105968,23810180,4264156,15716,1974150,200588,4194304" ] && verdict cyclone-equals-159 PASS || verdict cyclone-equals-159 FAIL
  for S in evm cyclone; do
    grep -E '^# |^[0-9]+ chip ' $G/$S.fpchip > $RES/$S.chip
    echo "$S: $(grep -c ' chip ' $RES/$S.chip) points, $(grep -vc '^#' $G/$S.fpchip) unit lines"
    gzip -9f $G/$S.fpchip
  done
  # the Tracer's lines at Quest 160's points (every 100,000 cycles)
  zcat $G/evm.fpchip.gz | grep -E '^[0-9]+ tr(\.[a-z]+)? ' | awk '$1 % 100000 == 0' > $W/a
  grep -E '^[0-9]+ tr(\.[a-z]+)? ' $REPO/results/160-w13-m3seg-cover/evm.fp > $W/b
  join -j1 <(awk '{print $1"/"$2, $3}' $W/a | sort) <(awk '{print $1"/"$2, $3}' $W/b | sort) | awk '$2 != $3 {d++} END {print NR " common, " d+0 " differ"}' | tee $W/j
  grep -q ' 0 differ' $W/j && ! grep -q '^0 common' $W/j && verdict tracer-equals-160 PASS || verdict tracer-equals-160 FAIL
  (cd $G && sha256sum evm.fpchip.gz cyclone.fpchip.gz evm.files.gz cyclone.files.gz evm.sram.gz cyclone.sram.gz | tee $G/HASHES) > $RES/HASHES
  ls -la $G
  sec "B: the baseline (one JVM pinned to one P core; nothing else running)"
  P1=$(take 1 "$P_ONE")
  ONE=$(keep "$(expand "$(cat /sys/devices/system/cpu/cpu$P1/topology/thread_siblings_list 2>/dev/null || echo "$P1")")" "$P_CPUS")
  echo "pinned to CPUs $ONE"
  for S in evm cyclone; do
    P=evm-zone-29; [ $S = cyclone ] && P=cyclone-29
    for k in 1 2 3; do
      taskset -c "$ONE" $J $D9 $P --w26 $D26/$P --segment 10 csv=$RES/base-$S-$k.csv > $RES/base-$S-$k.log 2>&1
      echo "$S run $k: $(grep '^timing' $RES/base-$S-$k.log); $(grep -c ' OK ' $RES/base-$S-$k.log) OK"
    done
  done
  for S in evm cyclone; do
    for k in 1 2 3; do tail -1 $RES/base-$S-$k.csv; done | awk -F, -v s=$S '{seg+=$16; setup+=$11; boot+=$15; n++; if ($13 != "OK") bad++} END {printf "BASELINE %s: segment %.2f s mean (3 runs), setup %.2f s, boot %.2f s, %d not OK\n", s, seg/n, setup/n, boot/n, bad}'
  done | tee $RES/BASELINE.txt
fi
