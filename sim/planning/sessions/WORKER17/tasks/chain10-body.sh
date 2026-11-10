# chain10-body.sh (WORKER17): the chain on segments 0-10 of evm-zone-29 and cyclone-29 (not timing; the two in
# parallel), then segment 10 from the chain's own inputs against M3's references (the owner's Q2: the fingerprint here).
# 1. chain 0-10, one domain: every segment's start memory against ref's (tests/golden-chain, Quest 172), the landings
#    of 0, 1, 10 against ref's, everything TracerT2.chip checks, T1's end checks; segment 10's start and landings
#    dumped in W13's forms.
# 2. those dumps byte for byte against W13's ($DATA/w13-golden-m3: evm.files.gz, evm.sram.gz, ...).
# 3. W16's t2 tool on the chain's dumps, unchanged: every point against golden-m3's unit lines (fpchip.gz), one domain
#    and the 6-domain split, the rows (Quest 169), the exits, tr_results, lag_cycles 0; C&C 3's exit and the end
#    exactly at W16's (--hint-exit, --hint-end).
# 4. The exit and end cycles the chain reports (the register blocks' sim_exit_at) beside t2's labels, for the mapping.
sec "machine (D21)"; . "$KS/tools/machine.sh" "${QUEST_P_CPUS:-}" "${QUEST_E_CPUS:-}"; machine_record "$RES/machine.txt"; uptime
sec "build (clang release)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/chain build/clang-release/t2 build/clang-release/test_chain > "$RES/build.log" 2>&1 ) \
  && ( cd "$KS" && ./build/clang-release/test_chain ) && verdict build PASS || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
CH=$KS/build/clang-release/chain; T2=$KS/build/clang-release/t2
G=$DATA/w13-golden-m3
sec "inputs"
cat > "$W/h" <<'H'
9ffec285e4f9c612157b7e51bc72b4dd82733716c058ebf9c74f2ccb7b999166  evm.fpchip.gz
bf53765324ee4388b64296f241c53508b540083374674b2d86e726cd322ec231  cyclone.fpchip.gz
dcf71fc01b5596c0aea3eb8366942376243eadabfb52ff3d5177639143490d24  evm.files.gz
8317810b72e8b07c25abc62c7f4bbbd4be54e64683e40a72eb9a91abaa40c9db  cyclone.files.gz
64838bcdf9bdfaa29067ccd2bc5a8610e9ba569d0a3415abfa34d466dd28f406  evm.sram.gz
a228f9c394514c92365e2e2bd3c1b1b363f510d9b04b86ca77786bc7b30d6f61  cyclone.sram.gz
H
OKIN=1; for P in evm-zone-29 cyclone-29; do checkin $P || OKIN=0; done
(cd "$G" && sha256sum -c --quiet "$W/h") || OKIN=0
[ $OKIN = 1 ] && verdict inputs PASS || { verdict inputs FAIL; exit 1; }
declare -A PROG=([evm]=evm-zone-29 [cyclone]=cyclone-29) END=([evm]=23940706 [cyclone]=28319575) EXIT=([evm]=23694305 [cyclone]=28106741)
mkdir -p "$W/d"
sec "1. the chain, segments 0-10 (both workloads in parallel)"
PIDS=""
for S in evm cyclone; do
  ( cd "$KS" && "$CH" --w26 "$D26/${PROG[$S]}" --delivery "$D9" --program "${PROG[$S]}" --segments 0-10 --oracle tests/golden-chain/$S.starts \
      --dump 10 --dump-dir "$W/d" ) > "$RES/chain-$S.log" 2>&1 & PIDS="$PIDS $!:$S"
done
for X in $PIDS; do wait ${X%%:*}; R=$?; S=${X#*:}; cat "$RES/chain-$S.log"
  N=$(grep -c "^${PROG[$S]} segment [0-9]*: OK  start equal" "$RES/chain-$S.log")
  [ $R = 0 ] && [ "$N" = 11 ] && verdict "chain-$S" PASS || verdict "chain-$S" FAIL
  for k in 0 1 10; do grep -q "^${PROG[$S]} segment $k: OK  start equal, landings equal" "$RES/chain-$S.log" && verdict "$S-landings-$k" PASS || verdict "$S-landings-$k" FAIL; done
done
sec "2. segment 10's start and landings against W13's dumps"
for S in evm cyclone; do
  for F in files sram; do
    A=$(zcat "$W/d/${PROG[$S]}.010.$F.gz" | sha256sum | cut -d' ' -f1); B=$(zcat "$G/$S.$F.gz" | sha256sum | cut -d' ' -f1)
    echo "$S $F: chain $A, W13 $B"; [ -n "$A" ] && [ "$A" = "$B" ] && verdict "$S-$F-equals-w13" PASS || verdict "$S-$F-equals-w13" FAIL
  done
done
sec "3. segment 10 from the chain's inputs: every point (W16's t2), one domain and the split (parallel)"
PIDS=""
for S in evm cyclone; do for N in 1 6; do
  ( cd "$KS" && "$T2" --sram "$W/d/${PROG[$S]}.010.sram.gz" --files "$W/d/${PROG[$S]}.010.files.gz" --segment --parts $N --fp-points "$G/$S.fpchip.gz" \
      --rows-ref "$W/kit/run/rows.txt" --program "${PROG[$S]}" --hint-exit ${EXIT[$S]} --hint-end ${END[$S]} ) > "$RES/t2-$S-$N.log" 2>&1 & PIDS="$PIDS $!:$S-$N"
done; done
for X in $PIDS; do wait ${X%%:*}; R=$?; L=$RES/t2-${X#*:}.log
  grep -E "^boot-end|^C&C 3 exited|^end:|^points:|^tr_results|^exits" "$L" | sed "s/^/${X#*:}: /"
  [ $R = 0 ] && grep -q "points: [0-9]* checked, 0 with a difference.*lag_cycles 0;" "$L" && grep -q "rows: 64 of 64" "$L" && grep -q "exits: 64 of 64" "$L" \
    && verdict "t2-${X#*:}" PASS || { grep -v "^\.\.\.\| 0 differ" "$L" | head -20; verdict "t2-${X#*:}" FAIL; }
done
sec "4. the chain's exit and end (sim_exit_at) beside t2's labels"
for S in evm cyclone; do
  grep "^${PROG[$S]} segment 10:" "$RES/chain-$S.log" | grep -o "C&C 3 exit [0-9]*, end [0-9]*"
  echo "  t2's labels: exit ${EXIT[$S]}, end ${END[$S]}"
done
