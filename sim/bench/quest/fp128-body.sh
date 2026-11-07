# Task body: W6's Fp128 benchmark (sourced by the task after the kit is unpacked; see mk_task.sh).
cd "$K" || exit 2
sec "build (gcc -O2, -O3, -O2 -march=native; clang -O2)"
make -j8 all-builds > "$RES/build.log" 2>&1 || { tail -20 "$RES/build.log"; exit 2; }
ls build

sec "0. the fold against the slow reference (10^6 pairs plus edge cases) and against Python, every build"
for bld in gcc-O2 gcc-O3 gcc-native clang-O2; do
  build/$bld/fp128 --check 1000000 && verdict "fold check $bld" PASS || verdict "fold check $bld" FAIL
done
build/gcc-O2/fp128 --dump 100000 | python3 -c "
import sys
p = 2**128 - 159; n = bad = 0
for l in sys.stdin:
    a, b, r = (int(x, 16) for x in l.split()); n += 1
    bad += (a * b) % p != r
print('python:', n, 'products,', bad, 'wrong'); sys.exit(1 if bad or not n else 0)" && verdict "fold against Python" PASS \
  || verdict "fold against Python" FAIL

sec "1. per operation: latency and throughput, every form and build, CPU 0 (and E-core 12, gcc -O2), 3 repeats"
for rep in 1 2 3; do for bld in gcc-O2 gcc-O3 gcc-native clang-O2; do for f in u128 acc limbs; do
  run "micro-$bld" 0 build/$bld/fp128 --micro --form $f --n 100000000
done; done
for f in u128 acc limbs; do run "micro-E-gcc-O2" 12 build/gcc-O2/fp128 --micro --form $f --n 100000000; done; done

sec "2. the toy multiplier unit: 64 units on one thread (CPU 0), every form and hold, 3 repeats"
for rep in 1 2 3; do for f in u128 acc limbs none; do for h in pool inline; do
  run "unit64" 0 build/gcc-O2/fp128 --unit --form $f --hold $h --units 64 --cycles 2000000 --warmup 200000 --cpus 0
done; done; done

sec "2b. the best form under the other builds (64 units, CPU 0, 3 repeats)"
for rep in 1 2 3; do for bld in gcc-O3 gcc-native clang-O2; do for f in u128 acc limbs; do
  run "unit64-$bld" 0 build/$bld/fp128 --unit --form $f --hold pool --units 64 --cycles 2000000 --warmup 200000 --cpus 0
done; done; done

sec "3. scale: 8192 units (a tile's worth on 128 chips); P: 1, 6 (one per P-core), 12 (with HT); E: 1, 4; 3 repeats"
for rep in 1 2 3; do for f in u128 acc limbs none; do for h in pool inline; do
  V="--unit --form $f --hold $h --units 8192 --cycles 25000 --warmup 2500"
  run "scale-P1" 0-11 build/gcc-O2/fp128 $V --cpus 0
  run "scale-P6" 0-11 build/gcc-O2/fp128 $V --cpus 0,2,4,6,8,10 --threads 6
  run "scale-P12" 0-11 build/gcc-O2/fp128 $V --cpus 0-11 --threads 12
  run "scale-E1" 12-15 build/gcc-O2/fp128 $V --cpus 12
  run "scale-E4" 12-15 build/gcc-O2/fp128 $V --cpus 12-15 --threads 4
done; done; done

sec "4. digests: every multiplying form and hold agree, at each scale"
for u in "units=64 " "units=8192 "; do
  n=$(grep -E '^(unit64|scale-)' "$RES/raw.txt" | grep "$u" | grep -v 'form=none' | field digest | sort -u | wc -l)
  [ "$n" = 1 ] && verdict "unit digests $u" PASS || verdict "unit digests $u($n distinct)" FAIL
done

sec "5. perf record: 8192 units, pool, 1 thread, the u128 and limbs forms"
for f in u128 limbs; do
  taskset -c 0 perf record -q -F 4000 -o "$W/perf-$f.data" -- build/gcc-O2/fp128 --unit --form $f --hold pool --units 8192 \
    --cycles 20000 --warmup 2000 --cpus 0 > /dev/null 2>&1
  perf report -i "$W/perf-$f.data" --stdio --no-children --sort symbol 2>/dev/null | grep -v '^#' | grep -v '^$' \
    | head -20 > "$RES/perf-report-$f.txt"
  echo "-- form=$f"; cat "$RES/perf-report-$f.txt"
done
[ "$FAILS" = 0 ] && verdict "all runs completed" PASS || verdict "all runs completed ($FAILS failed)" FAIL
