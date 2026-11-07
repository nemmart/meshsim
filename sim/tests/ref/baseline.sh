#!/bin/bash
# baseline.sh MESHSIM DELIVERY OUT -- the baseline the new framework is judged against (sim/planning/DECISIONS.md D8, D13):
# ReplayBench (all 64 tiles replaying; sim-step-cost) on segment 0 of evm-zone-29 (windows 0-63, as Quest 120), REPEATS
# repeats per JVM (default 3), as
#   one  1 JVM on one P-core (CPUs 0-1: the core and its hyperthread),
#   p12  12 JVMs at once on CPUs 0-11 (the six P-cores and their hyperthreads; taskset -c 0-11),
#   all16 16 JVMs at once on all CPUs 0-15 (the four E-cores 12-15 included).
# MESHSIM is a wip/meshsim tree with build/ and build/gold compiled and sw/replay/build built; DELIVERY the v1.26.1
# deliveries (ConvertDelivery's output). Each configuration runs alone, one after the other. Per JVM and repeat the log's
# "the replay from cycle C: S s, U us/cycle" gives U, microseconds per chip-cycle; a JVM's steady rate is the mean of its
# repeats after the first (the first carries the JIT's warm-up), and the aggregate is the sum over the JVMs of 1e6 / U
# chip-cycles per second. Sampled every second: the load average, each JVM's peak RSS, and the CPU its busiest thread is
# on; per CPU, the busy share over the configuration (/proc/stat). Writes OUT/<config>.* and OUT/BASELINE.txt.
set -u
M=$(cd "$1" && pwd); D=$2; OUT=$(mkdir -p "$3" && cd "$3" && pwd); REP=${REPEATS:-3}
cd "$M"
TB="-Xmx${HEAP:-3g} -Dreplay.tables=$M/../tables/replayer -Dreplay.build=$M/sw/replay/build"
sampler() {   # NAME: until OUT/NAME.running goes
  while [ -e "$OUT/$1.running" ]; do
    echo "T $(date +%s) $(cut -d' ' -f1-3 /proc/loadavg)"
    for p in $(cat "$OUT/$1.pids"); do
      [ -d /proc/$p ] || continue
      h=$(awk '/VmHWM/{print $2}' /proc/$p/status 2>/dev/null)
      c=$(cat /proc/$p/task/*/stat 2>/dev/null | sed 's/^.*) //' | awk '{u=$12+$13; if (u>m) {m=u; c=$37}} END{print c}')
      echo "P $p ${h:-0} ${c:--1}"
    done
    sleep 1
  done > "$OUT/$1.samples"
}
cpustat() { grep '^cpu[0-9]' /proc/stat | awk '{t=0; for(i=2;i<=NF;i++) t+=$i; print $1, t, $5+$6}'; }
run() {   # NAME N CPUS
  local n=$1 k=$2 cpus=$3 i
  echo "== $n: $k JVM(s) on CPUs $cpus, $(date -u +%T)"
  cpustat > "$OUT/$n.stat0"; : > "$OUT/$n.pids"; touch "$OUT/$n.running"
  for i in $(seq 1 "$k"); do
    taskset -c "$cpus" java $TB -cp build:build/gold ReplayBench ${BENCH:-"$D" evm-zone-29 --first 0 --repeats "$REP"} > "$OUT/$n.$i.log" 2>&1 &
    echo $! >> "$OUT/$n.pids"
  done
  sampler "$n" & local sp=$!
  for p in $(cat "$OUT/$n.pids"); do wait "$p"; done
  rm -f "$OUT/$n.running"; wait "$sp"; cpustat > "$OUT/$n.stat1"
}
CONFIGS=${CONFIGS:-one:1:0-1 p12:12:0-11 all16:16:0-15}   # NAME:JVMS:CPUS (tests: a short list, BENCH="synthetic 20 8 3")
for c in $CONFIGS; do IFS=: read -r a b d <<< "$c"; run "$a" "$b" "$d"; done

summary() {   # NAME
  local n=$1
  echo "== $n"
  # per JVM: each repeat's U; the steady mean (repeats after the first); its peak RSS; the CPUs its busiest thread was seen on
  local i=0
  for p in $(cat "$OUT/$n.pids"); do
    i=$((i + 1))
    us=$(grep -o 'the replay from cycle [0-9]*: [0-9.]* s, [0-9.]* us/cycle' "$OUT/$n.$i.log" | awk '{print $(NF-1)}' | tr '\n' ' ')
    ok=$(grep -c '^repeat [0-9]*: OK' "$OUT/$n.$i.log")
    rss=$(awk -v p=$p '$1=="P" && $2==p && $3>m {m=$3} END{printf "%.2f", m/1048576}' "$OUT/$n.samples")
    cpus=$(awk -v p=$p '$1=="P" && $2==p && $4>=0 {c[$4]++} END{for (k in c) printf "%s:%d ", k, c[k]}' "$OUT/$n.samples")
    ecore=$(awk -v p=$p '$1=="P" && $2==p && $4>=0 {n++; if ($4>=12) e++} END{printf "%.0f", n ? 100*e/n : 0}' "$OUT/$n.samples")
    echo "$us" | awk -v j=$i -v ok=$ok -v rss=$rss -v cpus="$cpus" -v e=$ecore '{s=0; for(r=2;r<=NF;r++) s+=$r; m=NF>1 ? s/(NF-1) : $1;
      printf "jvm %2d: us/cycle by repeat %s; steady %.2f us/cycle = %.0f chip-cycles/s; %d of %d repeats OK; peak RSS %s GB; on E-cores %s%% of samples; CPUs %s\n", j, $0, m, 1e6/m, ok, NF, rss, e, cpus}'
  done > "$OUT/$n.jvms"
  cat "$OUT/$n.jvms"
  awk '{for(i=1;i<=NF;i++) if ($i=="steady") {u=$(i+1); s+=1e6/u; n++; a+=u; if (u>mx) mx=u; if (mn==0||u<mn) mn=u}}
    END{printf "%d JVMs: steady us/cycle per JVM mean %.2f (min %.2f, max %.2f); in total %.0f chip-cycles/s\n", n, a/n, mn, mx, s}' "$OUT/$n.jvms"
  awk '$1=="T" {if ($3>m) m=$3; n++} END{printf "load average (1 min) at most %.1f over %d s\n", m, n}' "$OUT/$n.samples"
  paste "$OUT/$n.stat0" "$OUT/$n.stat1" | awk '{dt=$5-$2; di=$6-$3; b=dt ? 100*(dt-di)/dt : 0; printf "%s %.0f%%  ", $1, b; if ($1=="cpu11") printf "\n"} END{printf "\n"}'
}
{ echo "ReplayBench on segment 0 of evm-zone-29, $REP repeats per JVM, heap ${HEAP:-3g}; $(date -u)"; for c in $CONFIGS; do summary "${c%%:*}"; done; } > "$OUT/BASELINE.txt"
cat "$OUT/BASELINE.txt"
