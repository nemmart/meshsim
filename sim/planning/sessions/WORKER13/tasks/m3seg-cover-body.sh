# m3seg-cover-body.sh (WORKER13): M3Seg (sim/tests/ref) on segment 10, five JVMs in parallel (not timing):
#   cover   evm-zone-29: coverage (ref's counters by unit group and phase, the new units', pc histograms), the counts of
#           findings 9.2 and 9.3, the Tracer's fingerprints every 100,000 cycles with their cost per call
#   cyc     cyclone-29: coverage and counts (whether it reaches paths evm-zone doesn't)
#   old     evm-zone-29 with 110u's latencies (go 1, ready 1, stage_delay 32, error = ready): finding 9.1, want 23,693,506
#   plain   evm-zone-29, no hooks: M3Seg equals TracerT2 (Quest 159: 23,693,532 ...)
#   fp      evm-zone-29, fingerprints only: the same fingerprints as cover's (deterministic; the coverage hooks neutral)
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
[ $OKIN = 1 ] || verdict inputs FAIL
if [ $OKIN = 1 ]; then
  sec "M3FpSelf (local check, on the runner too)"
  (cd "$KR" && java -Dreplay.tables=$KT -cp $KC M3FpSelf) > $W/self.txt 2>&1; tail -2 $W/self.txt
  grep -q 'M3FpSelf: PASS' $W/self.txt && verdict m3fpself PASS || verdict m3fpself FAIL
  sec "M3Seg, five runs in parallel"
  cd "$KR"
  J="java -Xmx${T1_HEAP:-5g} -Dreplay.tables=$KT -cp $KC M3Seg"
  E="$D9 evm-zone-29 --w26 $D26/evm-zone-29 --segment 10"
  C="$D9 cyclone-29 --w26 $D26/cyclone-29 --segment 10"
  PIDS=""
  $J $E cover=$RES/evm.cover.csv counts=1 fp=$RES/evm.fp fpevery=100000 csv=$RES/evm-cover.csv > $RES/evm-cover.log 2>&1 & PIDS="$PIDS $!:evm-cover"
  $J $C cover=$RES/cyc.cover.csv counts=1 csv=$RES/cyc-cover.csv > $RES/cyc-cover.log 2>&1 & PIDS="$PIDS $!:cyc-cover"
  $J $E go=1 ready=1 stage=32 error=-1 csv=$RES/evm-old.csv > $RES/evm-old.log 2>&1 & PIDS="$PIDS $!:evm-old"
  $J $E csv=$RES/evm-plain.csv > $RES/evm-plain.log 2>&1 & PIDS="$PIDS $!:evm-plain"
  $J $E fp=$RES/evm-fponly.fp fpevery=100000 csv=$RES/evm-fponly.csv > $RES/evm-fponly.log 2>&1 & PIDS="$PIDS $!:evm-fponly"
  for X in $PIDS; do wait ${X%%:*}; R=$?; echo "${X#*:}: exit $R"; cat "$RES/${X#*:}.log"; [ $R = 0 ] && verdict "${X#*:}" PASS || verdict "${X#*:}" FAIL; done
  f() { tail -1 "$RES/$1.csv" | cut -d, -f1-10; }
  echo "evm plain   $(f evm-plain)"; echo "evm cover   $(f evm-cover)"; echo "evm fponly  $(f evm-fponly)"; echo "evm old     $(f evm-old)"; echo "cyc cover   $(f cyc-cover)"
  # Quest 159's figures (seg, windows, cc3, rows, drain, transfers, packets, replay max, rows checked)
  W159="10,64,23693532,19531654,4129492,14162,1772179,234206,4194304"; W110U="10,64,23693506,19531654,4129492,14162,1772179,234206,4194304"
  [ "$(f evm-plain | cut -d, -f1-9)" = "$W159" ] && verdict plain-equals-159 PASS || verdict plain-equals-159 FAIL
  [ "$(f evm-plain)" = "$(f evm-cover)" ] && [ "$(f evm-plain)" = "$(f evm-fponly)" ] && verdict hooks-neutral PASS || verdict hooks-neutral FAIL
  [ "$(f evm-old | cut -d, -f1-9)" = "$W110U" ] && echo "9.1: 110u's latencies give 110u's figures" || echo "9.1: 110u's latencies give $(f evm-old), 110u $W110U"
  grep -v '^#' $RES/evm.fp > $W/a; grep -v '^#' $RES/evm-fponly.fp > $W/b
  cmp -s $W/a $W/b && verdict fp-deterministic PASS || verdict fp-deterministic FAIL
  echo "fingerprint points: $(grep -c ' tracer ' $W/a)"; grep '^# cost' $RES/evm.fp
  gzip -9 $RES/*.pcs 2>/dev/null; ls -la $RES
fi
