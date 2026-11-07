# Task body: form 2, one mesh over several threads (WORKER4). Sourced by the task (mk_task.sh); the machine from
# sim/tools/machine.sh (D21): P_ONE, P_CPUS, take, count.
cd "$S" || exit 2
sec "build (clang release, D18) and the thread tests (clang debug: ASan, UBSan)"
make -j8 CC_NAME=clang MODE=release build/clang-release/bench_threads > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; verdict build FAIL; exit 1; }
make -j8 CC_NAME=clang MODE=debug build/clang-debug/test_threads build/clang-debug/test_matrix >> "$RES/build.log" 2>&1 \
  && ./build/clang-debug/test_threads && ./build/clang-debug/test_matrix && verdict "test_threads, test_matrix (debug)" PASS \
  || verdict "test_threads, test_matrix (debug)" FAIL
B=build/clang-release/bench_threads
NP=$(count "$P_ONE"); P1=$(take 1 "$P_ONE")
MAXD=$(( NP < 8 ? NP : 8 ))                       # the mesh has 8 rows: at most 8 domains
V="--form 2 --cycles 60000 --warmup 2000"

sec "1. the due mask's cost: one domain, due on against off (as in a split machine's domains); rate 0.35 and 0.05; 3 repeats"
for rep in 1 2 3; do for r in 0.35 0.05; do
  run "due-on" "$P1" $B $V --rate $r --threads 1 --cpus "$P1"
  run "due-off" "$P1" $B $V --rate $r --threads 1 --cpus "$P1" --due-off
done; done

sec "2. the split's own cost on one thread: 2, 4 and $MAXD domains on CPU $P1; 3 repeats"
for rep in 1 2 3; do for d in 2 4 $MAXD; do
  run "split-1t-d$d" "$P1" $B $V --threads 1 --domains $d --cpus "$P1"
done; done

sec "3. one mesh over 2, 4 and $NP threads (one per P core) against one; the barrier: spin, yield after 200/2000/20000, block; 3 repeats"
for rep in 1 2 3; do
  run "one" "$P1" $B $V --threads 1 --cpus "$P1"
  for t in 2 4 $NP; do
    [ "$t" -gt "$MAXD" ] && continue
    C=$(take $t "$P_ONE")
    run "t$t-spin" "$P_CPUS" $B $V --threads $t --cpus "$C" --barrier spin
    for n in 200 2000 20000; do run "t$t-yield$n" "$P_CPUS" $B $V --threads $t --cpus "$C" --barrier yield --spins $n; done
    run "t$t-block" "$P_CPUS" $B $V --threads $t --cpus "$C" --barrier block
  done
done

sec "4. the barrier's own cost: rate 0 (routers step, no words move), 1 thread against $NP, spin and yield 2000; 3 repeats"
for rep in 1 2 3; do
  run "idle-one" "$P1" $B $V --rate 0 --threads 1 --cpus "$P1"
  run "idle-t$NP-spin" "$P_CPUS" $B $V --rate 0 --threads $NP --cpus "$(take $NP "$P_ONE")" --barrier spin
  run "idle-t$NP-yield" "$P_CPUS" $B $V --rate 0 --threads $NP --cpus "$(take $NP "$P_ONE")"
done

sec "5. digests: every run at a rate gives one digest (threads, domains, barrier, due mask)"
for r in 0.35 0.05 0.00; do
  n=$(grep "rate_in=$r " "$RES/raw.txt" | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests at rate $r" PASS || verdict "digests at rate $r ($n distinct)" FAIL
done
sec "6. footprint: words, one domain against the splits"
grep -E "^(one|split-1t)" "$RES/raw.txt" | sed 's/.*domains=\([0-9]*\).*words=\([0-9]*\).*/domains \1 words \2/' | sort -u
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
