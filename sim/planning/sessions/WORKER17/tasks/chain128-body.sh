# chain128-body.sh (WORKER17): all 128 segments of evm-zone-29 and of cyclone-29, back to back, the memory carried
# (the owner's step for v1.35). Timing: the two workloads as two independent chains side by side, each on one thread
# pinned to its own physical P core (sim/tools/machine.sh's P_ONE: one CPU per physical core; D21, D26), nothing else
# running. Per segment: the start memory against ref's (golden-chain), every TracerT2 check, T1's end checks, C&C 3's
# exit and the end (W16's labels), the segment's cycles, setup / boot / segment / checks / carry times; then the totals.
# Segment 10 also against Quest 173's figures (the chain there equal to golden-m3 at every point).
sec "machine (D21)"; . "$KS/tools/machine.sh" "${QUEST_P_CPUS:-}" "${QUEST_E_CPUS:-}"; machine_record "$RES/machine.txt"; uptime
sec "build (clang release)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/chain build/clang-release/test_chain > "$RES/build.log" 2>&1 ) \
  && ( cd "$KS" && ./build/clang-release/test_chain ) && verdict build PASS || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
CH=$KS/build/clang-release/chain
sec "inputs"
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
[ $OKIN = 1 ] && verdict inputs PASS || { verdict inputs FAIL; exit 1; }
declare -A PROG=([evm]=evm-zone-29 [cyclone]=cyclone-29)
P2=$(take 2 "$P_ONE"); declare -A CPU=([evm]=${P2%%,*} [cyclone]=${P2##*,})
echo "P_ONE (one CPU per physical P core): $P_ONE; evm on CPU ${CPU[evm]}, cyclone on CPU ${CPU[cyclone]}"
[ "${CPU[evm]}" != "${CPU[cyclone]}" ] && verdict separate-cores PASS || verdict separate-cores FAIL
sec "the chains: segments 0-127, both workloads side by side"
PIDS=""
for S in evm cyclone; do
  ( cd "$KS" && taskset -c "${CPU[$S]}" "$CH" --w26 "$D26/${PROG[$S]}" --delivery "$D9" --program "${PROG[$S]}" --segments 0-127 \
      --oracle tests/golden-chain/$S.starts ) > "$RES/chain-$S.log" 2>&1 & PIDS="$PIDS $!:$S"
done
for X in $PIDS; do wait ${X%%:*}; R=$?; S=${X#*:}; L=$RES/chain-$S.log
  head -1 "$L"; tail -1 "$L"; grep -A3 "segment [0-9]*: FAIL" "$L" | head -40
  N=$(grep -c "^${PROG[$S]} segment [0-9]*: OK  start equal" "$L")
  echo "$S: $N of 128 segments OK with the start memory equal to ref's"
  [ $R = 0 ] && [ "$N" = 128 ] && verdict "chain-$S" PASS || verdict "chain-$S" FAIL
done
sec "segment 10 against Quest 173 (the run equal to golden-m3)"
f() { grep "^$1 segment 10:" "$2" | grep -o "C&C 3 exit [0-9]*, end [0-9]*; segment [0-9]* cycles\|transfers [0-9]*; DMA packets [0-9]*; trace rows [0-9]*, drain [0-9]*\|replay max [0-9]*" | tr '\n' ' '; }
[ "$(f evm-zone-29 $RES/chain-evm.log)" = "C&C 3 exit 23694305, end 23940706; segment 23933484 cycles transfers 14162; DMA packets 1772179; trace rows 19531654, drain 4129492 replay max 234206 " ] \
  && verdict evm-segment10-as-173 PASS || { f evm-zone-29 $RES/chain-evm.log; echo; verdict evm-segment10-as-173 FAIL; }
[ "$(f cyclone-29 $RES/chain-cyclone.log)" = "C&C 3 exit 28106741, end 28319575; segment 28312353 cycles transfers 15716; DMA packets 1974150; trace rows 23810180, drain 4264156 replay max 200588 " ] \
  && verdict cyclone-segment10-as-173 PASS || { f cyclone-29 $RES/chain-cyclone.log; echo; verdict cyclone-segment10-as-173 FAIL; }
sec "the table (segment, ok, exit, end, cycles, setup s, boot s, segment s, us/cycle)"
for S in evm cyclone; do
  grep "^${PROG[$S]} segment [0-9]*:" "$RES/chain-$S.log" | sed -E 's/^[^ ]+ segment ([0-9]+): ([A-Z]+) .*C&C 3 exit ([0-9]+), end ([0-9]+); segment ([0-9]+) cycles.*setup ([0-9.]+) s, boot ([0-9.]+) s, segment ([0-9.]+) s \(([0-9.]+) us\/cycle\).*/\1,\2,\3,\4,\5,\6,\7,\8,\9/' \
    | awk 'BEGIN{print "segment,ok,cc3_exit,end,cycles,setup_s,boot_s,segment_s,us_per_cycle"}{print}' > "$RES/$S.csv"
  echo "$S: $(($(wc -l < "$RES/$S.csv") - 1)) rows in $S.csv"
done
