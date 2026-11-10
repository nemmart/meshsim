# t2-body.sh (WORKER16): T2 on the framework's chip on the runner (AUDIT-M3 0, 5, 7).
# 1. Checked runs, both programs (evm-zone-29, cyclone-29 segment 10): one domain and T2's 6-domain split (mode 3),
#    every point against the references' unit lines (fpchip.gz), the rows against rows.bin (Quest 169's counts and sink
#    hashes, in the kit), every tile's exit, tr_results, lag_cycles 0. Not timing (the four in parallel).
# 2. Timing, alone and sequential: per program, one thread (one P core) and the split (6 threads on 6 P cores), REPS
#    in-process repetitions each, setup and boot apart (W13's baseline: ref's segment 46.23 s and 50.90 s).
# 3. Where the time goes: Options::calibrate (each unit's steps: by kind, C&C 3's domain alone, the framework's share),
#    one repetition per program on one thread; Options::time_threads on the split; perf on one thread (clang release -g).
REPS=${REPS:-3}
sec "machine (D21)"; . "$KS/tools/machine.sh" "${QUEST_P_CPUS:-}" "${QUEST_E_CPUS:-}"; machine_record "$RES/machine.txt"; uptime
sec "build (clang release; clang release -g for perf)"
( cd "$KS" && make -j8 CC_NAME=clang MODE=release build/clang-release/t2 > "$RES/build.log" 2>&1 \
  && make -j8 CC_NAME=clang MODE=release B=build/prof RELEASE="-O2 -DNDEBUG -g" build/prof/t2 >> "$RES/build.log" 2>&1 ) \
  && verdict build PASS || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
T2=$KS/build/clang-release/t2; T2P=$KS/build/prof/t2
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
(cd "$G" && sha256sum -c --quiet "$W/h") && verdict inputs PASS || { verdict inputs FAIL; exit 1; }
declare -A PROG=([evm]=evm-zone-29 [cyclone]=cyclone-29) END=([evm]=23940706 [cyclone]=28319575)
args() { echo "--sram $G/$1.sram.gz --files $G/$1.files.gz"; }
sec "1. checked runs: one domain and the 6-domain split, every point, the rows (parallel)"
PIDS=""
for S in evm cyclone; do for N in 1 6; do
  ( cd "$KS" && "$T2" $(args $S) --segment --parts $N --fp-points "$G/$S.fpchip.gz" --rows-ref "$W/kit/run/rows.txt" --program "${PROG[$S]}" ) \
    > "$RES/check-$S-$N.log" 2>&1 & PIDS="$PIDS $!:$S-$N"
done; done
for X in $PIDS; do wait ${X%%:*}; R=$?; L=$RES/check-${X#*:}.log
  grep -E "^boot-end|^end:|^points:|^tr_results|^exits" "$L" | sed "s/^/${X#*:}: /"
  [ $R = 0 ] && grep -q "points: [0-9]* checked, 0 with a difference.*lag_cycles 0;" "$L" && grep -q "rows: 64 of 64" "$L" && grep -q "exits: 64 of 64" "$L" \
    && verdict "check-${X#*:}" PASS || { grep -v "^\.\.\.\| 0 differ" "$L" | head -20; verdict "check-${X#*:}" FAIL; }
done
P1=$(take 1 "$P_ONE"); P6=$(take 6 "$P_ONE")
sec "2. timing: one thread on CPU $P1, the split on CPUs $P6; $REPS repetitions each; nothing else running"
for S in evm cyclone; do
  ( cd "$KS" && taskset -c "$P1" "$T2" $(args $S) --time $REPS --end ${END[$S]} --parts 1 ) > "$RES/time-$S-1.log" 2>&1; cat "$RES/time-$S-1.log"
  ( cd "$KS" && taskset -c "$P6" "$T2" $(args $S) --time $REPS --end ${END[$S]} --parts 6 ) > "$RES/time-$S-6.log" 2>&1; cat "$RES/time-$S-6.log"
  grep -q "NOT OK" "$RES/time-$S-1.log" "$RES/time-$S-6.log" && verdict "time-$S" FAIL || verdict "time-$S" PASS
done
sec "3. where the time goes: units by kind (one thread, calibrate), the split's phases, perf"
for S in evm cyclone; do
  ( cd "$KS" && taskset -c "$P1" "$T2" $(args $S) --time 1 --end ${END[$S]} --parts 1 --units ) > "$RES/units-$S.log" 2>&1; cat "$RES/units-$S.log"
  ( cd "$KS" && taskset -c "$P6" "$T2" $(args $S) --time 1 --end ${END[$S]} --parts 6 --thread-times ) > "$RES/threads-$S.log" 2>&1; cat "$RES/threads-$S.log"
done
if command -v perf > /dev/null; then
  ( cd "$KS" && taskset -c "$P1" perf record -F 999 -g -o "$W/perf.data" "$T2P" $(args evm) --time 1 --end ${END[evm]} --parts 1 ) > "$RES/perf-run.log" 2>&1
  perf report -i "$W/perf.data" --no-children --sort symbol --stdio 2>/dev/null | grep -v "^#" | grep "%" | head -60 > "$RES/perf-evm.txt"
  cat "$RES/perf-evm.txt" | head -40
else echo "perf: not installed"; fi
