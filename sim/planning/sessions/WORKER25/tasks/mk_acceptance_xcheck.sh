#!/bin/bash
# mk_acceptance_xcheck.sh NNN-name > task.sh (WORKER25, P7b): a Quest task running oracle/acceptance_crosscheck.sh on
# the runner, v1.35's acceptance kit against v1.3's on the same inputs: the 128-window calibration (seed 11, both
# calibrate.sh) and real deliveries: W9's selected windows of evm-zone-29 and cyclone-29 (every one, AB, F on each),
# evm-zone-29 windows 0-127 (AB, F every 32nd) and the selected evm-zone-29 windows with -q 512, converted to v1.26.1 in scratch by v1.35's convert_delivery (as
# Quest 175 checked it equal to v1.3's ConvertDelivery). Embedded: v1.35's kit (tests/acceptance/make_kit.sh, HEAD)
# and the oracle side (sim/ref, sim/oracle, the data and images it reads), both from HEAD. Run from sim/, committed.
set -eu
NAME=$1
cd "$(dirname "$0")/../../../.."
TOP=$(git rev-parse --show-toplevel); REV=$(git rev-parse --short HEAD)
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/b"
tests/acceptance/make_kit.sh "$K/b/kit135.tgz" > /dev/null
( cd "$TOP" && git archive --format=tar HEAD -- sim/ref sim/oracle sim/data/tables sim/fw/images/log8k sim/fw/images/oldmap \
    ':(glob)sim/tests/m3-images/replay_ctl.*' ) | tar -xf - -C "$K/b"
echo "sim/ $REV" > "$K/b/VERSION"
(cd "$K/b" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
( cd "$K" && tar --sort=name --mtime=@0 --owner=0 --group=0 -cf - b ) | gzip -9n > "$K/bundle.tgz"
SHA=$(sha256sum "$K/bundle.tgz" | cut -d' ' -f1)
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim sim/ WORKER25 (P7b; branch sim-w25 $REV). The acceptance kit, v1.35 against v1.3, on the
# runner (oracle/acceptance_crosscheck.sh): (1) the calibration, both calibrate.sh (128 synthetic windows of 65,536
# rows, seed 11; v1.3's SyntheticDelivery patched as D43), every run's results compared; (2) real deliveries, converted
# to v1.26.1 in scratch by v1.35's convert_delivery: W9's selected windows of evm-zone-29 and cyclone-29 (AB, F on
# every window), evm-zone-29 windows 0-127 (AB, F every 32nd), and evm-zone-29's selected windows with -q 512 (the
# 8 KB-log control). v1.35's kit built with the checker on (MODE=suite). Compared: results.csv (the wall-time column masked),
# VERDICT.txt, SUMMARY.txt's non-timing lines, exit codes, every per-process log. Not a timing task (SUMMARY's rates are
# recorded with the machine, D21, not claimed). Writes only results/$NAME/; scratch in /tmp, removed first and at the
# end; the deliveries are only read. Safe to re-run from the start.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w25-$NAME; rm -rf "\$W"; mkdir -p "\$W"; trap 'rm -rf "\$W"' EXIT
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/bundle.tgz" || exit 2
GOT=\$(sha256sum "\$W/bundle.tgz" | cut -d' ' -f1); echo "bundle sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "bundle sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/bundle.tgz" -C "\$W" && (cd "\$W/b" && sha256sum -c --quiet HASHES) || { echo "bundle files differ from HASHES"; exit 2; }
B=\$W/b; echo "start \$(date -u) on \$(hostname), \$(nproc) CPUs; \$(cat \$B/VERSION); \$(java -version 2>&1 | head -1); \$(clang++ --version | head -1)"
free -g | head -2; df -h /tmp | tail -1
FAIL=0; : > "\$RES/VERDICTS"
verdict() { echo "VERDICT \$1: \$2"; echo "\$1 \$2" >> "\$RES/VERDICTS"; [ "\$2" = PASS ] || FAIL=1; }
sec() { echo; echo "==== \$* (\$(date -u +%T))"; }
DATA=/home/questrunner/data; [ -d "\$DATA/w9-deliveries" ] || DATA=\$REPO/data
D9=\$DATA/w9-deliveries; SEL=\$DATA/w9-deliveries-selected

sec "inputs"
declare -A M9=([evm-zone-29]=7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f [cyclone-29]=f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982)
OKIN=1
for P in evm-zone-29 cyclone-29; do
  h=\$(sha256sum "\$D9/\$P.manifest.json" 2>/dev/null | cut -d' ' -f1)
  [ "\$h" = "\${M9[\$P]}" ] && [ "\$(cut -d' ' -f1 "\$D9/\$P.COMPLETE" 2>/dev/null)" = "\$h" ] || { echo "\$P: delivery manifest missing or differs (\$h)"; OKIN=0; }
  [ -d "\$SEL/\$P" ] || { echo "\$P: no selected windows at \$SEL/\$P"; OKIN=0; }
done
[ \$OKIN = 1 ] && verdict inputs PASS || { verdict inputs FAIL; exit 1; }

sec "the machine (sim/tools/machine.sh, D21) and v1.35's convert_delivery (the kit's sources, clang release)"
mkdir -p "\$W/k" && tar -xzf "\$B/kit135.tgz" -C "\$W/k"
bash "\$W/k/w25-kit/sim/tools/machine.sh" record > "\$RES/machine.txt" 2>&1 || true; head -20 "\$RES/machine.txt"
make -s -C "\$W/k/w25-kit/sim" -j"\$(nproc)" MODE=release CC_NAME=clang build/clang-release/convert_delivery > "\$RES/build.log" 2>&1 \\
  && verdict build PASS || { tail -20 "\$RES/build.log"; verdict build FAIL; exit 1; }
CD=\$W/k/w25-kit/sim/build/clang-release/convert_delivery
conv() {   # conv SRC DST P: convert_delivery (v1.25 to v1.26.1: records converted, streams linked) into DST
  local src=\$1 dst=\$2 p=\$3 mf=""
  for m in "\$src/\$p.manifest.json" "\$src/\$p/manifest.json" "\$src/manifest.json"; do [ -f "\$m" ] && { mf=\$m; break; }; done
  [ -n "\$mf" ] || { echo "\$p: no manifest under \$src"; return 1; }
  "\$CD" "\$src" "\$dst" "\$p" --manifest "\$mf" > "\$RES/convert-\$(basename \$dst)-\$p.log" 2>&1 || { tail -5 "\$RES/convert-\$(basename \$dst)-\$p.log"; return 1; }
  tail -2 "\$RES/convert-\$(basename \$dst)-\$p.log"
}
sec "converting (scratch)"
OKC=1
for P in evm-zone-29 cyclone-29; do conv "\$SEL" "\$W/sel" "\$P" || OKC=0; done
conv "\$D9" "\$W/full" evm-zone-29 || OKC=0
[ \$OKC = 1 ] && verdict convert PASS || { verdict convert FAIL; exit 1; }

sec "the crosscheck: v1.35's kit against v1.3's"
J=\$(( \$(nproc) < 16 ? \$(nproc) : 16 ))
KEEP="\$RES/cases" KIT135="\$B/kit135.tgz" TMPDIR=/tmp bash "\$B/sim/oracle/acceptance_crosscheck.sh" --no-small --calibration \\
  --real evm-sel "\$W/sel" evm-zone-29 "-f 0 -n all -j \$J -s AB -x 1" \\
  --real cyclone-sel "\$W/sel" cyclone-29 "-f 0 -n all -j \$J -s AB -x 1" \\
  --real evm-0-127 "\$W/full" evm-zone-29 "-f 0 -n 128 -j \$J -s AB -x 32" \\
  --real evm-sel-q512 "\$W/sel" evm-zone-29 "-f 0 -n all -j \$J -s AB -x 4 -q 512" 2>&1 | tee "\$RES/crosscheck.log"
[ "\${PIPESTATUS[0]}" = 0 ] && verdict crosscheck PASS || verdict crosscheck FAIL
find "\$RES/cases" -name 'jvm-*.log.gz' -size +2M -delete 2>/dev/null
echo; cat "\$RES/VERDICTS"; [ \$FAIL = 0 ] && echo "ALL PASS" | tee "\$RES/ALL_PASS"
echo "end \$(date -u)"
exit \$FAIL
__KIT_BEGIN__
HEAD
base64 -w 76 "$K/bundle.tgz"
echo "__KIT_END__"
