#!/usr/bin/env bash
# make_regress_task.sh BASE_SORTRUNNER_DIR TASKNAME > task.sh -- B1's full regression (WORKER22, milestone 5), from the repo
# root on branch v1.3-tracer: (1) run_tests.sh's quick suite and its full-mode extras (tools/run_quick.sh, run_full.sh);
# (2) the full-size sort p4/uniform-k128 (WORKER8's runner, meshsim.jar rebuilt from this tree), expected 1,229,008
# cycles; (3) the 64-window Stage B smoke, as Quest 105 ran it: both w9 deliveries converted in scratch, windows 0-63 of
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
sed -i "s/^[0-9a-f]\{64\}  meshsim.jar$/$NEW  meshsim.jar/; 1s/.*/# meshsim v1.3-tracer (WORKER22, B1) kit: SHA-256 of every input a run reads (java Inputs check HASHES)/" "$S/HASHES"
grep -q "$NEW  meshsim.jar" "$S/HASHES" || { echo "HASHES: no meshsim.jar line" >&2; exit 1; }
(cd "$R/wip/meshsim" && ./acceptance/make_kit.sh "$K/acc.tgz" >/dev/null) && tar -xzf "$K/acc.tgz" -C "$K" && rm "$K/acc.tgz"
(cd "$R" && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), built $(date -u +%F)") > "$K/VERSION"
tar -C "$K" -czf "$K/kit.tgz" wip sortrunner w11-kit VERSION
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
cat <<EOF
#!/bin/bash
# Task $NAME -- meshsim WORKER22, B1's full regression (milestone 5; branch v1.3-tracer):
#  1. run_tests.sh's quick suite (tools/run_quick.sh, 8 at a time) and its full-mode extras (tools/run_full.sh, 4 at a time),
#     the B1 suites included (MemorySystemTest, RvcTest, TracerTest);
#  2. the full-size sort p4/uniform-k128 (WORKER8's runner, meshsim.jar rebuilt from the branch; the sort image and boot ROM
#     checked byte-identical to the base's), expected 1,229,008 cycles;
#  3. the 64-window Stage B smoke, as Quest 105: both w9 deliveries (\$DATA/w9-deliveries, tasks 103/104) converted to
#     v1.26.1 records in scratch, windows 0-63 of each in Stages A and B, F every 16th.
# The kit is embedded below, SHA-256 checked. Writes only results/$NAME/. Scratch in /tmp, removed first. ~35 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w22-regress; rm -rf "\$W"; mkdir -p "\$W"
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
echo "run_quick.sh exit \$RQ, run_full.sh exit \$RF, run_p4.sh exit \$RS, the smoke \$RA at \$(date -u)"
grep -h "TOTAL" "\$W"/p4/VERDICT.txt 2>/dev/null | sed 's/^/p4 (expected 1,229,008 cycles): /'
cat "\$W/out/VERDICT.txt" "\$W/out/FULL-VERDICT.txt" "\$W/p4/VERDICT.txt" 2>/dev/null
rm -rf "\$W"
[ \$RQ = 0 ] && [ \$RF = 0 ] && [ \$RS = 0 ] && [ \$RA = 0 ]; exit \$?
__KIT_BEGIN__
EOF
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
