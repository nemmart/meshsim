#!/usr/bin/env bash
# make_suite_task.sh SUITE_KIT.tgz COMMIT > 108-w25-v1261-suites.sh: WORKER25's suites task, as WORKER20's 102b (run_quick,
# run_full, then the full-size sort p4/uniform-k128, expected 1,229,008 cycles), with the kit embedded and SHA-256 checked.
set -eu
KIT=$1; C=$2; SHA=$(sha256sum "$KIT" | cut -d' ' -f1)
cat <<HDR
#!/bin/bash
# Task 108 -- meshsim v1.26.1 suites (WORKER25): branch v1.26.1 at $C, as WORKER20's 102b:
#  1. run_tests.sh's quick suite (tools/run_quick.sh, 8 at a time) and its full-mode extras (tools/run_full.sh, 4 at a time);
#  2. the full-size sort p4/uniform-k128 (WORKER8's runner kit, jar rebuilt from the branch; the sort image and boot ROM
#     byte-identical to 102b's), expected 1,229,008 cycles (DECISIONS-v1.26 s2 and s3: the sort unchanged).
# The kit (wip/ at Java 17 with every image prebuilt; sortrunner/) is embedded below, SHA-256 checked; built by
# WORKER25/tasks/make_suite_kit.sh. ~25 min.
set -u
cd "\$(dirname "\$0")/.."
RES=\$PWD/results/108-w25-v1261-suites; mkdir -p "\$RES"
WANT=$SHA
W=/tmp/w25-108; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "\$WANT" ] || { echo "kit sha256 mismatch (want \$WANT)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1
cd "\$W/wip/meshsim" || exit 2
P=8 ./run_quick.sh "\$W/out"; RQ=\$?
P=4 ./run_full.sh "\$W/out"; RF=\$?
cd "\$W/sortrunner" || exit 2
printf 'p4/uniform-k128\n' > "\$W/p4list"
bash ./run_p4.sh -j 1 -m 6g -l "\$W/p4list" "\$W/p4"; RS=\$?
echo "run_quick.sh exit \$RQ, run_full.sh exit \$RF, run_p4.sh exit \$RS at \$(date -u)"
grep -h "TOTAL" "\$W"/p4/VERDICT.txt 2>/dev/null | sed 's/^/p4 (expected 1,229,008 cycles): /'
mkdir -p "\$RES/p4"; cp "\$W"/out/* "\$RES/" 2>/dev/null; cp "\$W"/p4/* "\$RES/p4/" 2>/dev/null
cat "\$W/out/VERDICT.txt" "\$W/out/FULL-VERDICT.txt" "\$W/p4/VERDICT.txt" 2>/dev/null
rm -rf "\$W"
[ \$RQ = 0 ] && [ \$RF = 0 ] && [ \$RS = 0 ]; exit \$?
__KIT_BEGIN__
HDR
base64 -w 76 "$KIT"
echo "__KIT_END__"
