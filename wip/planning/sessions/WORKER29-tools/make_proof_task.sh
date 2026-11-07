#!/bin/bash
# make_proof_task.sh BASE_REPO NEW_REPO TASKNAME > task.sh -- WORKER29's exactness proof of an engine change on the runner:
# the new build against the base build (two checkouts, e.g. main and sim-step-cost), every run compared by its masked log
# and every chip's fingerprint (-Dmeshsim.digest), then the speed of each, alone on the box.
#   1. run_tests.sh's quick suite, every step with each build (WORKER29/tools/prove_same.sh, 8 steps at a time);
#   2. the full-size sort p4/uniform-k128 (64 x 65,536 keys, ready=8) with each;
#   3. T2 segment 0 of evm-zone-29 and cyclone-29, raw, all 64 windows (stage 1 on), with each; the CSVs compared too;
#   4. ReplayBench on windows 0-63 of both workloads (all 64 tiles replaying), with each;
#   5. the speed: ReplayBench evm-zone-29 (3 repeats) and the full-size sort, base then new, one JVM at a time.
# The kit: the new checkout's wip/meshsim (its classes, every tile image built), wip/sort, wip/sw, wip/tables, wip/gold's
# classes, and the base checkout's classes in wip/meshsim/build-base (both compiled for Java 21).
set -e
B=$(cd "$1" && pwd); R=$(cd "$2" && pwd); NAME=$3; HERE=$(cd "$(dirname "$0")" && pwd); K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/wip"; for d in meshsim sort sw tables; do cp -r "$R/wip/$d" "$K/wip/"; done; rm -rf "$K/wip/meshsim/results" "$K/wip/meshsim/build"
M="$K/wip/meshsim"; mkdir -p "$M/build/gold" "$M/build-base"
(cd "$M" && sh sw/build.sh >/dev/null && sh sw/build_cc.sh >/dev/null && sh sw/build_recipes.sh >/dev/null && sh sw/ext/sort/build.sh >/dev/null 2>&1 && sh sw/replay/build.sh >/dev/null && sh sw/tracer/build.sh >/dev/null)
javac --release 21 -nowarn -d "$M/build/gold" $(find "$R/wip/gold/src" -name '*.java') 2>&1 | grep -v '^Note:' || true
for X in new base; do
  if [ $X = new ]; then S=$R/wip/meshsim; O=$M/build; else S=$B/wip/meshsim; O=$M/build-base; fi
  javac --release 21 -nowarn -d "$O" "$S"/src/*.java 2>&1 | grep -v '^Note:' || true
  grep -v '^#' "$S/test/PENDING" | awk -v s="$S" 'NF{print s"/test/"$1}' > "$K/pend"
  javac --release 21 -nowarn -d "$O" -cp "$O:$M/build/gold" $(ls "$S"/test/*.java | grep -v -x -F -f "$K/pend") 2>&1 | grep -v '^Note:' || true
  for c in SortTest KeyGen TracerT2 ConvertDelivery StageB; do [ -f "$O/$c.class" ] || { echo "$X: no $c.class" >&2; exit 1; }; done
done
[ -f "$M/build-base/ReplayBench.class" ] || javac --release 21 -nowarn -d "$M/build-base" -cp "$M/build-base:$M/build/gold" "$R/wip/meshsim/test/ReplayBench.java"
cp "$HERE/../tools/prove_same.sh" "$M/"
(cd "$M" && awk '/^if \[ "\$MODE" != quick \]/{full=1} /^fi$/{full=0} !full && /^LOG=/ && /\$J /' run_tests.sh | sed 's/^LOG=\([^ ]*\).*\$J /\1|/; s/\$Q/quick/' > quick.steps)
(cd "$B" && echo "base: meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD))"; cd "$R" && echo "new:  meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), built $(date -u +%F)") > "$K/VERSION"
tar -C "$K" -czf "$K/kit.tgz" wip VERSION
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1); N=$(wc -l < "$M/quick.steps")
cat <<EOT
#!/bin/bash
# Task $NAME -- meshsim WORKER29 (P2): the exactness proof of an engine change, the new build against the base
# ($(tr '\n' ';' < "$K/VERSION")): 1. the quick suite ($N steps), 2. the full-size sort p4/uniform-k128, 3. T2 segment 0 of
# evm-zone-29 and cyclone-29 (raw, 64 windows, stage 1 on), 4. ReplayBench on windows 0-63 of both workloads; every run's
# masked log and every chip's fingerprint compared (and T2's CSVs). 5. The speed: ReplayBench evm-zone-29 (3 repeats) and the
# full-size sort, base then new, alone on the box. Inputs only read, checked first. Writes only results/$NAME/. Scratch in
# /tmp, removed first and at the end. Expected ~35 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
DATA=\${W28_DATA:-\$HOME/data}; [ -d "\$DATA/w26-jumpmap" ] || DATA=\$REPO/data
W=/tmp/w29-proof; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cat "\$W/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; free -g | head -2
PROGS="evm-zone-29:7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f:57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d cyclone-29:f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982:ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba"
D9=\$DATA/w9-deliveries; D26=\$DATA/w26-jumpmap
for PS in \$PROGS; do
  IFS=: read P S9 S26 <<< "\$PS"
  [ "\$(sha256sum "\$D9/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)" = "\$S9" ] && [ "\$(cat "\$D9/\$P.COMPLETE" 2>/dev/null)" = "\$S9" ] || { echo "\$P: the delivery isn't complete or differs (tasks 103/104)"; exit 2; }
  [ "\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)" = "\$S26" ] && [ "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" = "\$S26" ] || { echo "\$P: WORKER26's inputs aren't complete or differ (task 109)"; exit 2; }
  (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's files differ from their manifest"; exit 2; }
  echo "\$P: inputs checked"
done
cd "\$W/wip/meshsim" || exit 2
G=build/gold; CB=build-base:\$G; CN=build:\$G
for PS in \$PROGS; do P=\${PS%%:*}; java -Xmx2g -cp \$CN ConvertDelivery "\$D9" "\$W/v1261" "\$P" > "\$RES/convert-\$P.log" 2>&1 || { echo "conversion of \$P failed"; exit 1; }; tail -1 "\$RES/convert-\$P.log"; done
java -Xmx2g -cp \$CN KeyGen "\$W/keys" --n 65536 --only uniform > "\$RES/keygen.log" 2>&1 || { echo "KeyGen failed"; exit 1; }
mkdir -p "\$W/out"
# ---- 1. the quick suite, 8 steps at a time
export STEP_LIMIT=1500
cat quick.steps | xargs -d '\n' -P 8 -I{} sh -c 'echo "{}" | bash ./prove_same.sh "\$1" "\$2" "\$0" > "\$0/v.\$(echo "{}" | cut -d"|" -f1)"' "\$W/out" "\$CB" "\$CN"
cat "\$W"/out/v.* | sort > "\$RES/VERDICTS.txt"; cat "\$RES/VERDICTS.txt"
# ---- 2-4. the big runs, base and new side by side
TB="-Xmx5g -Dreplay.tables=\$W/wip/tables/replayer -Dreplay.build=\$W/wip/meshsim/sw/replay/build"
PIDS=""
big() {   # big NAME CP -- MAIN ARGS...
  N=\$1; C=\$2; shift 3
  ( a=\$(date +%s); timeout -s TERM 40m java \$TB -Dmeshsim.digest=\$W/out/\$N.digest -cp \$C "\$@" > "\$W/out/\$N.log" 2>&1; r=\$?; echo "\$N: exit \$r, \$((\$(date +%s) - a)) s" >> "\$W/out/\$N.time"; exit \$r ) &
  PIDS="\$PIDS \$!:\$N"
}
for X in base new; do
  [ \$X = base ] && C=\$CB || C=\$CN
  big sort.\$X \$C -- SortTest -1 run label=p4/uniform-k128 keys=\$W/keys/uniform.keys fmt=k128 ready=8
  for PS in \$PROGS; do P=\${PS%%:*}
    big t2-\$P.\$X \$C -- TracerT2 "\$D9" "\$P" --w26 "\$D26/\$P" --segments 0 --quiet --csv "\$W/out/t2-\$P.\$X.csv"
    big rb-\$P.\$X \$C -- ReplayBench "\$W/v1261" "\$P" --first 0 --repeats 1
  done
done
for X in \$PIDS; do wait \${X%%:*}; done
mask() { sed -E 's/[0-9]+(\.[0-9]+)? ?(s|ms|µs|us)\b/#s/g; s/[0-9]+(\.[0-9]+)? us\/cycle/# us\/cycle/g; s/[0-9]+ rows per JVM-second/# rows per JVM-second/g' "\$1"; }
csvmask() { awk -F, -v c=\$2 'BEGIN{OFS=","} {\$c="#"; print}' "\$1" | sort; }
BIG=0; BIGSAME=0
for N in sort t2-evm-zone-29 t2-cyclone-29 rb-evm-zone-29 rb-cyclone-29; do
  BIG=\$((BIG + 1)); V=""
  cmp -s <(mask "\$W/out/\$N.base.log") <(mask "\$W/out/\$N.new.log") || V="\$V DIFF (log)"
  [ -s "\$W/out/\$N.base.digest" ] && cmp -s "\$W/out/\$N.base.digest" "\$W/out/\$N.new.digest" || V="\$V DIFF (fingerprint)"
  case \$N in t2-*) cmp -s <(csvmask "\$W/out/\$N.base.csv" 10) <(csvmask "\$W/out/\$N.new.csv" 10) || V="\$V DIFF (CSV)";; esac
  V=\${V:-SAME}; [ "\$V" = SAME ] && BIGSAME=\$((BIGSAME + 1))
  echo "\$N: \$V; chips \$(wc -l < "\$W/out/\$N.new.digest" 2>/dev/null); \$(cat "\$W/out/\$N.base.time" "\$W/out/\$N.new.time" | tr '\n' ' ')" | tee -a "\$RES/BIG.txt"
done
grep -h "TOTAL" "\$W/out/sort.new.log" | head -1 | cut -c1-90 | tee -a "\$RES/BIG.txt"
grep -h -E "T2 on" "\$W"/out/t2-*.new.log | tee -a "\$RES/BIG.txt"
grep -h -E "^repeat" "\$W"/out/rb-*.new.log | cut -c1-200 | tee -a "\$RES/BIG.txt"
# ---- 5. the speed, one JVM at a time
for X in base new; do
  [ \$X = base ] && C=\$CB || C=\$CN
  java \$TB -cp \$C ReplayBench "\$W/v1261" evm-zone-29 --first 0 --repeats 3 > "\$W/out/speed-rb.\$X.log" 2>&1
  echo "rb evm-zone-29 \$X: \$(grep -o 'the replay from.*' "\$W/out/speed-rb.\$X.log" | sed 's/  \[meas.*//' | tr '\n' ';')" | tee -a "\$RES/SPEED.txt"
  a=\$(date +%s.%N); java \$TB -cp \$C SortTest -1 run label=p4/uniform-k128 keys=\$W/keys/uniform.keys fmt=k128 ready=8 > "\$W/out/speed-sort.\$X.log" 2>&1
  echo "sort \$X: \$(echo "\$(date +%s.%N) - \$a" | bc) s, \$(grep -c '0 failed' "\$W/out/speed-sort.\$X.log") passed line" | tee -a "\$RES/SPEED.txt"
done
mkdir -p "\$RES/logs"; for f in "\$W"/out/*.log "\$W"/out/*.digest; do gzip -c "\$f" > "\$RES/logs/\$(basename "\$f").gz"; done
S=\$(grep -c ' SAME ' "\$RES/VERDICTS.txt")
echo "quick suite: \$S of $N the same; big runs: \$BIGSAME of \$BIG the same; \$(date -u)"
rm -rf "\$W"
[ "\$S" = $N ] && [ \$BIGSAME = \$BIG ]; exit \$?
__KIT_BEGIN__
EOT
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
