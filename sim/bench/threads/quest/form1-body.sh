# Task body: form 1, independent chips on independent threads (WORKER4). Sourced by the task (mk_task.sh).
cd "$S" || exit 2
sec "build (clang release, D18) and the thread tests (clang debug: ASan, UBSan)"
make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
make -j8 CC_NAME=clang MODE=debug build/clang-debug/test_parallel >> "$RES/build.log" 2>&1 && ./build/clang-debug/test_parallel \
  && verdict "test_parallel (debug)" PASS || verdict "test_parallel (debug)" FAIL
B=build/clang-release/bench_threads

sec "footprint per chip (D15), from Sizes"
for c in mesh synth; do $B --sizes --chip $c | tee -a "$RES/sizes.txt"; done

# the machine (D21, sim/tools/machine.sh, sourced by the task): one CPU per P core in placement order, every P CPU, the E
# CPUs; NP P cores, NPT P threads, NE E CPUs. Nothing about the runner is written here.
NP=$(count "$P_ONE"); NPT=$(count "$P_CPUS"); NE=$(count "$E_CPUS"); P1=$(take 1 "$P_ONE")

sec "1. one chip per thread (as W6's N1): one per P core, 1..$NP; then all $NPT P threads; the E cores, 1 and $NE; 3 repeats"
for rep in 1 2 3; do for c in mesh synth; do
  V="--chip $c --cycles 40000 --warmup 4000"
  for n in $(seq 1 "$NP"); do run "N1-P$n" "$P_CPUS" $B $V --chips $n --threads $n --cpus "$(take $n "$P_ONE")"; done
  run "N1-P$NPT" "$P_CPUS" $B $V --chips $NPT --threads $NPT --cpus "$P_CPUS"
  if [ "$NE" -gt 0 ]; then
    run "N1-E1" "$E_CPUS" $B $V --chips 1 --threads 1 --cpus "$(take 1 "$E_CPUS")"
    run "N1-E$NE" "$E_CPUS" $B $V --chips $NE --threads $NE --cpus "$E_CPUS"
  fi
done; done

sec "2. sixteen chips per thread (past L2, as W6's N16): P1, P$NP, P$NPT; E$NE; 3 repeats"
for rep in 1 2 3; do for c in mesh synth; do
  V="--chip $c --cycles 4000 --warmup 400"
  run "N16-P1" "$P_CPUS" $B $V --chips 16 --threads 1 --cpus "$P1"
  run "N16-P$NP" "$P_CPUS" $B $V --chips $((16 * NP)) --threads $NP --cpus "$P_ONE"
  run "N16-P$NPT" "$P_CPUS" $B $V --chips $((16 * NPT)) --threads $NPT --cpus "$P_CPUS"
  if [ "$NE" -gt 0 ]; then run "N16-E$NE" "$E_CPUS" $B $V --chips $((16 * NE)) --threads $NE --cpus "$E_CPUS"; fi
done; done

sec "3. the same 12 chips on 1, $NP and $NPT threads: identical digests (and IPC, perf stat)"
for c in mesh synth; do
  V="--chip $c --chips 12 --cycles 10000 --warmup 1000"
  PERF=1 run "D-P1" "$P_CPUS" $B $V --threads 1 --cpus "$P1"
  PERF=1 run "D-P$NP" "$P_CPUS" $B $V --threads $(( NP < 12 ? NP : 12 )) --cpus "$P_ONE"
  PERF=1 run "D-P$NPT" "$P_CPUS" $B $V --threads $(( NPT < 12 ? NPT : 12 )) --cpus "$P_CPUS"
  n=$(grep "^D-P" "$RES/raw.txt" | grep "chip=$c " | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests of 12 $c chips on 1, $NP, $NPT threads" PASS || verdict "digests of 12 $c chips ($n distinct)" FAIL
done
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
