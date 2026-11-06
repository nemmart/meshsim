#!/usr/bin/env bash
# make_baselines_task.sh BASE_SORTRUNNER_DIR TASKNAME > task.sh -- the baselines at GO, READY and error latency 8 (WORKER28,
# P1's latency commit), from the repo root on branch sim-idle-skip; WORKER22's make_regress_task.sh (B1's regression) with
# its header changed. BASE_SORTRUNNER_DIR: WORKER8's sort runner as 110v carried it (its meshsim.jar is rebuilt here): (1) run_tests.sh's quick suite and its full-mode extras (tools/run_quick.sh, run_full.sh);
# (2) the full-size sort p4/uniform-k128 (WORKER8's runner, meshsim.jar rebuilt from this tree), 1,229,008 cycles at
# latency 1 (now re-recorded); (3) the 64-window Stage B smoke, as Quest 105 ran it: both w9 deliveries converted in scratch, windows 0-63 of
# each in Stages A and B, F every 16th. The kit (wip/ precompiled for Java 21 with every image built and gold's classes in
# build/gold; sortrunner/; the acceptance kit w11-kit/) is embedded after an explicit exit, SHA-256 checked.
# (WORKER25's make_suite_kit.sh and make_suite_task.sh, with the acceptance kit and gold added.)
set -eu
BASE=$(realpath "$1"); NAME=$2
R=$(pwd); K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/wip"
for d in meshsim sort sw tables gold; do rsync -a --exclude results --exclude build --exclude '.git' "$R/wip/$d" "$K/wip/"; done
M="$K/wip/meshsim"; mkdir -p "$M/results"
(cd "$M" && sh sw/build.sh >/dev/null && sh sw/build_cc.sh >/dev/null && sh sw/build_recipes.sh >/dev/null && sh sw/ext/sort/build.sh >/dev/null && sh sw/replay/build.sh >/dev/null && sh sw/tracer/build.sh >/dev/null)
mkdir -p "$M/build/gold"; javac --release 21 -nowarn -d "$M/build" "$M"/src/*.java
javac --release 21 -nowarn -d "$M/build/gold" $(find "$K/wip/gold/src" -name '*.java') 2>&1 | grep -v '^Note:' || true
grep -v '^#' "$M/test/PENDING" | awk 'NF{print "test/"$1}' > "$K/pend"
(cd "$M" && javac --release 21 -nowarn -d build -cp build $(ls test/*.java | grep -v -x -F -f "$K/pend")) 2>&1 | grep -v '^Note:' || true
cp "$M/tools/run_quick.sh" "$M/tools/run_full.sh" "$M/"
rsync -a "$BASE/" "$K/sortrunner/"
S="$K/sortrunner/kit"
for f in sort.code.bin sort.data.bin sort.sym; do cmp -s "$M/sw/ext/sort/build/$f" "$S/image/$f" || { echo "sort image $f differs from the base's" >&2; exit 1; }; done
cmp -s "$M/sw/bootrom.hex" "$S/sw/bootrom.hex" || { echo "boot ROM differs from the base's" >&2; exit 1; }
(cd "$M/build" && jar cf "$S/meshsim.jar" .)
NEW=$(sha256sum "$S/meshsim.jar" | cut -d' ' -f1)
sed -i "s/^[0-9a-f]\{64\}  meshsim.jar$/$NEW  meshsim.jar/; 1s/.*/# meshsim sim-idle-skip (WORKER28, latency 8) kit: SHA-256 of every input a run reads (java Inputs check HASHES)/" "$S/HASHES"
grep -q "$NEW  meshsim.jar" "$S/HASHES" || { echo "HASHES: no meshsim.jar line" >&2; exit 1; }
(cd "$R/wip/meshsim" && ./acceptance/make_kit.sh "$K/acc.tgz" >/dev/null) && tar -xzf "$K/acc.tgz" -C "$K" && rm "$K/acc.tgz"
(cd "$R" && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), built $(date -u +%F)") > "$K/VERSION"
tar -C "$K" -czf "$K/kit.tgz" wip sortrunner w11-kit VERSION
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER28, the full regression on main (the commit in VERSION: P1's stage 1 merged, GO/READY/error at
# 8, stage_delay 64; stage 1 on), the last step before the freeze: WORKER22's B1 regression (110v) plus T2, re-recording
# every number the latencies move:
#  1. run_tests.sh's quick suite (tools/run_quick.sh, 8 at a time) and its full-mode extras (tools/run_full.sh, 4 at a time),
#     the B1 suites included (MemorySystemTest, RvcTest, TracerTest);
#  2. the full-size sort p4/uniform-k128 (WORKER8's runner, meshsim.jar rebuilt from the branch; the sort image and boot ROM
#     checked byte-identical to the base's), 1,229,008 cycles at latency 1, now re-recorded;
#  3. the 64-window Stage B smoke, as Quest 105: both w9 deliveries (\$DATA/w9-deliveries, tasks 103/104) converted to
#     v1.26.1 records in scratch, windows 0-63 of each in Stages A and B, F every 16th;
#  4. T2 on segment 0 of evm-zone-29 and cyclone-29, raw, all 64 windows, stage 1 on (\$DATA/w26-jumpmap, task 109,
#     checked first; every chip's fingerprint kept), each under timeout 30m.
# The kit is embedded below, SHA-256 checked. Writes only results/$NAME/. Scratch in /tmp, removed first. ~20 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w28-baselines; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cat "\$W/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; free -g | head -2
# ---- 1. the suites
cd "\$W/wip/meshsim" || exit 2
P=8 ./run_quick.sh "\$W/out"; RQ=\$?
P=4 ./run_full.sh "\$W/out"; RF=\$?
mkdir -p "\$RES/suites"; cp "\$W"/out/* "\$RES/suites/" 2>/dev/null
# ---- 2. the sort
cd "\$W/sortrunner" || exit 2
printf 'p4/uniform-k128\\n' > "\$W/p4list"
bash ./run_p4.sh -j 1 -m 6g -l "\$W/p4list" "\$W/p4"; RS=\$?
mkdir -p "\$RES/p4"; cp "\$W"/p4/* "\$RES/p4/" 2>/dev/null
# ---- 3. the 64-window Stage B smoke (as 105)
cd "\$W/w11-kit" || exit 2
sha256sum -c --quiet HASHES || { echo "acceptance kit files differ from HASHES"; exit 2; }
DATA=\$HOME/data
[ -d "\$DATA/w9-deliveries" ] || DATA=\$REPO/data
SRC=\$DATA/w9-deliveries; RA=0
for PS in evm-zone-29:7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f cyclone-29:f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982; do
  P=\${PS%%:*}; SM=\${PS#*:}
  GOT=\$(sha256sum "\$SRC/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ "\$GOT" = "\$SM" ] && [ "\$GOT" = "\$(cat "\$SRC/\$P.COMPLETE" 2>/dev/null)" ] || { echo "\$P: the delivery isn't complete or differs"; RA=2; continue; }
  java -Xmx2g -cp meshsim.jar ConvertDelivery "\$SRC" "\$W/v1261" "\$P" > "\$RES/convert-\$P.log" 2>&1 || { echo "conversion of \$P failed"; RA=1; continue; }
  tail -1 "\$RES/convert-\$P.log"
  ./run_shard.sh -d "\$W/v1261" -p "\$P" -M "\$W/v1261/\$P.manifest.json" -f 0 -n 64 -j 16 -x 16 "\$RES/smoke-\$P" || RA=1
  cat "\$RES/smoke-\$P/VERDICT.txt" 2>/dev/null
done
# ---- 4. T2 on segment 0 of both workloads, all 64 windows, stage 1 on (WORKER28: TracerT2, two JVMs at once)
D26=\$DATA/w26-jumpmap; RT=0; PIDS=""
cd "\$W/wip/meshsim" || exit 2                 # the full chip reads hops.txt and sw/ from here
for PS in evm-zone-29:57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d cyclone-29:ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba; do
  P=\${PS%%:*}; S26=\${PS#*:}
  [ "\$(sha256sum "\$D26/\$P/manifest.sha256" 2>/dev/null | cut -d' ' -f1)" = "\$S26" ] && [ "\$(cat "\$D26/\$P.COMPLETE" 2>/dev/null)" = "\$S26" ] || { echo "\$P: WORKER26's inputs aren't complete or differ (task 109)"; RT=2; continue; }
  (cd "\$D26/\$P" && sha256sum -c --quiet manifest.sha256) || { echo "\$P: WORKER26's files differ from their manifest"; RT=2; continue; }
  ( a=\$(date +%s); timeout -s TERM 30m java -Xmx5g -Dmeshsim.digest="\$RES/t2-\$P.digest" -Dreplay.tables="\$W/wip/tables/replayer" -cp build:build/gold TracerT2 "\$SRC" "\$P" --w26 "\$D26/\$P" --segments 0 --quiet --csv "\$RES/t2-\$P-true.csv" > "\$RES/t2-\$P.log" 2>&1; r=\$?; echo "t2-\$P: exit \$r, \$((\$(date +%s) - a)) s" >> "\$RES/t2-\$P.log"; exit \$r ) &
  PIDS="\$PIDS \$!"
done
for X in \$PIDS; do wait \$X || RT=1; done
for P in evm-zone-29 cyclone-29; do tail -2 "\$RES/t2-\$P.log" 2>/dev/null; cat "\$RES/t2-\$P-true.csv" 2>/dev/null; done
echo "run_quick.sh exit \$RQ, run_full.sh exit \$RF, run_p4.sh exit \$RS, the smoke \$RA, T2 \$RT at \$(date -u)"
grep -h "TOTAL" "\$W"/p4/VERDICT.txt 2>/dev/null | sed 's/^/p4 (1,229,008 cycles at latency 1): /'
cat "\$W/out/VERDICT.txt" "\$W/out/FULL-VERDICT.txt" "\$W/p4/VERDICT.txt" 2>/dev/null
rm -rf "\$W"
[ \$RQ = 0 ] && [ \$RF = 0 ] && [ \$RS = 0 ] && [ \$RA = 0 ] && [ \$RT = 0 ]; exit \$?
__KIT_BEGIN__
EOF
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
