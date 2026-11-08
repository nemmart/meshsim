# Task body (WORKER10): the chip's footprint on the runner (D21: the machine's caches recorded by machine.sh). bench_chip
# on the boot, wfi and regs, one thread and 6 threads by tiles (9 machines a process): the process's resident set after
# build and after the run, the big SRAM's allocated against resident (mincore), and perf stat's cache misses (L1d and
# LLC load misses), against the run's cycles. 3 processes.
sec "build (clang release, D18)"
for k in $KITS; do
  ( cd "$W/kit/$k/sim" && make -j8 CC_NAME=clang MODE=release build/clang-release/bench_chip > "$RES/build-$k.log" 2>&1 ) \
    || { tail -20 "$RES/build-$k.log"; verdict "build $k" FAIL; exit 1; }
done
grep -i "cache" "$RES/machine.txt"
P1=$(take 1 "$P_ONE"); C6=$(take 6 "$PLACE")
crun() { local tag=$1 cpus=$2 k=$3; shift 3; ( cd "$W/kit/$k/sim" && run "$tag" "$cpus" ./build/clang-release/bench_chip "$@" ); }
for rep in 1 2 3; do for k in $KITS; do for p in sort wfi regs; do
  PERF=1 crun "$k one-$p" "$P1" $k --prog $p --threads 1 --cpus "$P1" --reps 9
  PERF=1 crun "$k t6-$p" "$P_CPUS" $k --prog $p --threads 6 --partition tiles --cpus "$C6" --reps 9
done; done; done
sec "summary (medians): us per cycle; RSS MB after build and run; big SRAM MB allocated, resident; L1d and LLC load misses (perf, the whole process: 10 machines built and run)"
for k in $KITS; do for p in sort wfi regs; do for t in one t6; do
  tag="$k $t-$p"
  printf "%s %-4s %-5s us=%-6s rss=%s/%s sram=%s resident=%s/%s l1d_miss=%s llc_miss=%s instr=%s\n" "$k" "$p" "$t" "$(med "$tag" us_per_cycle)" \
    "$(med "$tag" rss_mb_built)" "$(med "$tag" rss_mb_ran)" "$(med "$tag" sram_mb)" "$(med "$tag" sram_resident_mb_built)" "$(med "$tag" sram_resident_mb_ran)" \
    "$(med "$tag" core_L1-dcache-load-misses)" "$(med "$tag" core_LLC-load-misses)" "$(med "$tag" core_instructions)"
done; done; done | tee "$RES/summary.txt"
[ "$FAILS" = 0 ] && verdict "every run completed" PASS || verdict "every run completed ($FAILS failed)" FAIL
