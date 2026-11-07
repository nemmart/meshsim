# Task body: W6's mesh benchmark (sourced by the task after the kit is unpacked; see mk_task.sh).
cd "$K" || exit 2
sec "build (gcc -O2, -O3, -O2 -march=native; clang -O2)"
make -j8 all-builds > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; exit 2; }
ls build
B=build/gcc-O2/mesh
C1="--cycles 300000 --warmup 30000"     # one mesh, rate 0.35
C2="--cycles 1000000 --warmup 100000"   # one mesh, rate 0.1
CS="--cycles 10000 --warmup 1000"       # 128 meshes

sec "1. saturation: offered against accepted (lw=a e1 rt, injection queue 64 deep, 1 run each)"
for r in 0.30 0.33 0.35 0.36 0.37 0.40 0.50; do
  run "sat" "$P1" $B --rate $r --injdepth 64 --cycles 200000 --warmup 50000 --cpus "$P1"
done

sec "2. the matrix: one mesh, one thread (CPU 0), ready marks, 3 repeats"
for rep in 1 2 3; do for lw in a b; do for dl in e1 e2; do for w in rt fx; do for mix in 128 mixed; do
  run "matrix" "$P1" $B --lw $lw --dl $dl --width $w --mix $mix --rate 0.35 $C1 --cpus "$P1"
  run "matrix" "$P1" $B --lw $lw --dl $dl --width $w --mix $mix --rate 0.1 $C2 --cpus "$P1"
done; done; done; done; done

sec "choosing the best destination layout and width for the pair (median, rate 0.35, a and b, both mixes)"
BEST=$(grep '^matrix ' "$RES/raw.txt" | grep 'rate=0.350' | awk '{
    for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
    k = v["dl"] " " v["width"] " " v["lw"] " " v["mix"]; n[k]++; x[k, n[k]] = v["ns_per_mesh_cycle"] }
  END { for (k in n) { split(k, f, " "); m = med(x, k, n[k]); s[f[1] " " f[2]] += m }
        best = ""; for (c in s) { printf "  %s: sum of medians %.1f ns\n", c, s[c] > "/dev/stderr"; if (best == "" || s[c] < s[best]) best = c }
        print best }
  function med(x, k, c,   a, i, j, t) { for (i = 1; i <= c; i++) a[i] = x[k, i]
        for (i = 1; i <= c; i++) for (j = i + 1; j <= c; j++) if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
        return a[int((c + 1) / 2)] }')
DL=${BEST% *}; WD=${BEST#* }
echo "best: dl=$DL width=$WD"

sec "3. ready marks against polling (best layout; one mesh; 3 repeats)"
for rep in 1 2 3; do for lw in a b; do for mix in 128 mixed; do
  run "poll" "$P1" $B --lw $lw --dl $DL --width $WD --ready poll --mix $mix --rate 0.35 $C1 --cpus "$P1"
  run "poll" "$P1" $B --lw $lw --dl $DL --width $WD --ready poll --mix $mix --rate 0.1 $C2 --cpus "$P1"
done; done; done

sec "4. compilers and flags (best layout, rate 0.35, one mesh; 3 repeats; -O2 gcc is the matrix)"
for rep in 1 2 3; do for bld in gcc-O3 gcc-native clang-O2; do for lw in a b; do for mix in 128 mixed; do
  run "flags-$bld" "$P1" build/$bld/mesh --lw $lw --dl $DL --width $WD --mix $mix --rate 0.35 $C1 --cpus "$P1"
done; done; done; done

sec "5. phase shares (rdtsc per phase; not a timing run)"
for lw in a b; do for mix in 128 mixed; do
  run "phases-1" "$P1" $B --lw $lw --dl $DL --width $WD --mix $mix --rate 0.35 --cycles 100000 --warmup 10000 --phases --cpus "$P1"
  run "phases-128" "$P1" $B --lw $lw --dl $DL --width $WD --mix $mix --rate 0.35 --meshes 128 --cycles 2000 --warmup 200 --phases --cpus "$P1"
done; done

sec "6. scale: 128 independent meshes (rate 0.35; P: 1 thread, $NP = one per P core, $NPT = with HT; E: 1 and $NE; 3 repeats)"
for rep in 1 2 3; do for lw in a b; do for mix in 128 mixed; do
  V="--lw $lw --dl $DL --width $WD --mix $mix --rate 0.35 --meshes 128 $CS"
  run "scale-P1" "$P_CPUS" $B $V --cpus "$P1"
  run "scale-P$NP" "$P_CPUS" $B $V --cpus "$P_ONE" --threads $NP
  run "scale-P$NPT" "$P_CPUS" $B $V --cpus "$P_CPUS" --threads $NPT
  [ "$NE" -gt 0 ] && run "scale-E1" "$E_CPUS" $B $V --cpus "$E1"
  [ "$NE" -gt 0 ] && run "scale-E$NE" "$E_CPUS" $B $V --cpus "$E_CPUS" --threads $NE
done; done; done
sec "7. digests: every variant, rate and mix agree; the scale runs agree across thread counts"
for key in "mix=128 rate=0.350" "mix=mixed rate=0.350" "mix=128 rate=0.100" "mix=mixed rate=0.100"; do
  n=$(grep -E '^(matrix|poll|flags-)' "$RES/raw.txt" | grep "$key" | grep -v 'meshes=128' | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests one-mesh $key" PASS || verdict "digests one-mesh $key ($n distinct)" FAIL
done
for mix in 128 mixed; do
  n=$(grep '^scale-' "$RES/raw.txt" | grep "mix=$mix " | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "digests 128 meshes mix=$mix, all thread counts" PASS || verdict "digests 128 meshes mix=$mix ($n distinct)" FAIL
done

sec "8. perf record: the busiest case (128 meshes, 128-bit, rate 0.35, 1 thread), a and b"
for lw in a b; do
  taskset -c "$P1" perf record -q -F 4000 -o "$W/perf-$lw.data" -- $B --lw $lw --dl $DL --width $WD --mix 128 --rate 0.35 \
    --meshes 128 --cycles 4000 --warmup 400 --cpus "$P1" > /dev/null 2>&1
  perf report -i "$W/perf-$lw.data" --stdio --no-children --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' \
    | head -25 > "$RES/perf-report-$lw.txt"
  echo "-- lw=$lw"; cat "$RES/perf-report-$lw.txt"
done
[ "$FAILS" = 0 ] && verdict "all runs completed" PASS || verdict "all runs completed ($FAILS failed)" FAIL
