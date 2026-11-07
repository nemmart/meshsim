# Task body: W6's addition (D15): throughput against N, footprint per mesh, and the ready marks' cost by load.
cd "$K" || exit 2
EV=cycles,instructions,cache-misses,L1-dcache-load-misses,LLC-loads,LLC-load-misses
sec "build (gcc -O2)"
make -j8 > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; exit 2; }
B=build/gcc-O2/mesh
L="--dl e1 --width fx --rate 0.35"     # task 124's best layout and width
ORDER=(0 2 4 6 8 10 1 3 5 7 9 11)      # one hyperthread per P-core up to 6, then the siblings
cpus() { local n=$1 s=""; for ((i = 0; i < n; i++)); do s="$s${s:+,}${ORDER[$i]}"; done; echo "$s"; }

sec "1. footprint per mesh (bytes the sizing allocates), (a) and (b)"
for lw in a b; do run "footprint" 0 $B --lw $lw $L --cycles 1000 --warmup 100 --footprint --cpus 0; done

sec "2. throughput against N: N independent meshes, one per thread, N = 1..12 (CPUs ${ORDER[*]}), 3 repeats"
for rep in 1 2 3; do for lw in a b; do for mix in 128 mixed; do for n in 1 2 3 4 5 6 7 8 9 10 11 12; do
  run "ncurve-1" 0-11 $B --lw $lw --mix $mix $L --meshes $n --threads $n --cpus $(cpus $n) --cycles 300000 --warmup 30000
done; done; done; done

sec "3. throughput against N with 16 meshes per thread (~2.3 MB each, past L2: shared L3 and memory), 128-bit, 3 repeats"
for rep in 1 2 3; do for lw in a b; do for n in 1 2 4 6 8 10 12; do
  run "ncurve-16" 0-11 $B --lw $lw --mix 128 $L --meshes $((16 * n)) --threads $n --cpus $(cpus $n) --cycles 20000 --warmup 2000
done; done; done

sec "4. ready marks by load: per delivery (bm), none/polling (poll), on empty -> non-empty (tr); one mesh, CPU 0, 3 repeats"
for rep in 1 2 3; do for lw in a b; do for rate in 0.01 0.1 0.35; do for rd in bm poll tr; do
  cyc=300000; [ $rate != 0.35 ] && cyc=1000000
  run "ready" 0 $B --lw $lw --dl e1 --width fx --mix 128 --rate $rate --ready $rd --cycles $cyc --warmup $((cyc / 10)) --cpus 0
done; done; done; done

sec "5. digests"
for mix in 128 mixed; do for n in 1 6 12; do
  d=$(grep '^ncurve-1 ' "$RES/raw.txt" | grep "mix=$mix " | grep " meshes=$n " | field digest | sort -u | wc -l)
  [ "$d" = 1 ] && verdict "ncurve-1 mix=$mix meshes=$n: (a), (b), repeats agree" PASS || verdict "ncurve-1 mix=$mix meshes=$n ($d distinct)" FAIL
done; done
for rate in 0.010 0.100 0.350; do
  d=$(grep '^ready ' "$RES/raw.txt" | grep "rate=$rate " | field digest | sort -u | wc -l)
  [ "$d" = 1 ] && verdict "ready marks rate=$rate: every mode agrees" PASS || verdict "ready marks rate=$rate ($d distinct)" FAIL
done
[ "$FAILS" = 0 ] && verdict "all runs completed" PASS || verdict "all runs completed ($FAILS failed)" FAIL
