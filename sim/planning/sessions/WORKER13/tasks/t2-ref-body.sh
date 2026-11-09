# t2-ref-body.sh (WORKER13): sim/ref's TracerT2 on 110u's two runs (segment 10 of evm-zone-29 and cyclone-29, 64 windows,
# raw, C&C 1's CC_SEGMENT), in parallel as 110u, against 110u's figures; and an inventory of the persistent data.
sec "the persistent data"
ls -la "$DATA"; du -sh "$DATA"/* 2>/dev/null; ls "$D9" | head -50; ls "$D26"
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
[ $OKIN = 1 ] || { verdict inputs FAIL; }
if [ $OKIN = 1 ]; then
  sec "T2 on segment 10 of both workloads (sim/ref, idle-skip on: its default)"
  cd "$KR"
  J="java -Xmx${T1_HEAP:-5g} -Dreplay.tables=$KT -cp $KC TracerT2"
  PIDS=""
  for P in evm-zone-29 cyclone-29; do
    $J "$D9" $P --w26 "$D26/$P" --segments 10 --quiet --csv "$RES/$P-t.csv" > "$RES/$P-t.log" 2>&1 & PIDS="$PIDS $!:$P"
  done
  for X in $PIDS; do wait ${X%%:*}; echo "${X#*:}: exit $?"; cat "$RES/${X#*:}-t.log"; done
  # 110u: C&C 3 cycles, trace rows, drain, transfers, DMA packets, replay max, rows
  declare -A WANT=([evm-zone-29]="10,64,23693506,19531654,4129492,14162,1772179,234206,4194304" [cyclone-29]="10,64,28105942,23810180,4264156,15716,1974150,200588,4194304")
  for P in evm-zone-29 cyclone-29; do
    GOTL=$(tail -1 "$RES/$P-t.csv" 2>/dev/null | cut -d, -f1-9); OKL=$(tail -1 "$RES/$P-t.csv" 2>/dev/null | cut -d, -f11)
    echo "$P: got $GOTL ($OKL), 110u ${WANT[$P]}"
    [ "$GOTL" = "${WANT[$P]}" ] && [ "$OKL" = OK ] && verdict "$P-seg10-as-110u" PASS || verdict "$P-seg10-as-110u" FAIL
  done
fi
