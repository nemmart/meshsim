# Task body: form 1, independent chips on independent threads (WORKER4). Sourced by the task (mk_task.sh).
cd "$S" || exit 2
sec "build (clang release, D18) and the thread tests (clang debug: ASan, UBSan)"
make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
make -j8 CC_NAME=clang MODE=debug build/clang-debug/test_parallel >> "$RES/build.log" 2>&1 && ./build/clang-debug/test_parallel \
  && verdict "test_parallel (debug)" PASS || verdict "test_parallel (debug)" FAIL
B=build/clang-release/bench_threads

sec "footprint per chip (D15), from Sizes"
for c in mesh synth; do $B --sizes --chip $c | tee -a "$RES/sizes.txt"; done

sec "1. one chip per thread (as W6's N1): P-cores one per core 1..6, then 12 with hyperthreads; E-cores 1 and 4; 3 repeats"
for rep in 1 2 3; do for c in mesh synth; do
  V="--chip $c --cycles 40000 --warmup 4000"
  for n in 1 2 3 4 5 6; do
    cpus=$(seq -s, 0 2 $((2 * n - 2)))
    run "N1-P$n" 0-11 $B $V --chips $n --threads $n --cpus $cpus
  done
  run "N1-P12" 0-11 $B $V --chips 12 --threads 12 --cpus 0-11
  run "N1-E1" 12-15 $B $V --chips 1 --threads 1 --cpus 12
  run "N1-E4" 12-15 $B $V --chips 4 --threads 4 --cpus 12-15
done; done

sec "2. sixteen chips per thread (past L2, as W6's N16): P1, P6, P12; E4; 3 repeats"
for rep in 1 2 3; do for c in mesh synth; do
  V="--chip $c --cycles 4000 --warmup 400"
  run "N16-P1" 0-11 $B $V --chips 16 --threads 1 --cpus 0
  run "N16-P6" 0-11 $B $V --chips 96 --threads 6 --cpus 0,2,4,6,8,10
  run "N16-P12" 0-11 $B $V --chips 192 --threads 12 --cpus 0-11
  run "N16-E4" 12-15 $B $V --chips 64 --threads 4 --cpus 12-15
done; done

sec "3. the same 12 chips on 1, 6 and 12 threads: identical digests (and IPC, perf stat)"
for c in mesh synth; do
  V="--chip $c --chips 12 --cycles 10000 --warmup 1000"
  PERF=1 run "D-P1" 0-11 $B $V --threads 1 --cpus 0
  PERF=1 run "D-P6" 0-11 $B $V --threads 6 --cpus 0,2,4,6,8,10
  PERF=1 run "D-P12" 0-11 $B $V --threads 12 --cpus 0-11
  n=$(grep "^D-P" "$RES/raw.txt" | grep "chip=$c " | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests of 12 $c chips on 1, 6, 12 threads" PASS || verdict "digests of 12 $c chips ($n distinct)" FAIL
done
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
