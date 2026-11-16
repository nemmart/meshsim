#!/bin/sh
# Writes the Quest task for WORKER2's packets: make_task.sh <number> <meshsim commit> <main's commit> <out task file>
# The kit holds two trees: m/, a git archive of the commit (gold with the packets, and the frozen files its tests
# read), and ref/, gold's sources at main's commit, before this work, for test/packets/frames_unchanged.sh. It is
# embedded in the task as base64 with its SHA-256; the inputs come from Quest task 111's own kit (jolt-asic-planning
# fa59d9a), checked. Quest 200's frame files' SHA-256s (WORKER1, updater-w1 562fd725) are written into the task.
set -eu
N=$1; C=$2; MAIN=$3; OUT=$4
cd "$(git rev-parse --show-toplevel)"
K=$(mktemp -d)
mkdir -p "$K/k/m" "$K/k/ref"
git archive "$C" updater_wip/pipeline/gold v1.36/pipeline/gold/src v1.36/pipeline/gold/test/guests v1.36/pipeline/jumpmap/test \
  v1.3/planning/sessions/WORKER18-data/v13/segment-files-v1.3.sha256 | tar -x -C "$K/k/m"
git archive "$MAIN" updater_wip/pipeline/gold/src | tar -x -C "$K/k/ref"
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -C "$K/k" -cf - m ref | gzip -n > "$K/kit.tgz"
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1)
NAME=$N-w2-packets
cat > "$OUT" <<TASK
#!/bin/bash
# Task $NAME -- meshsim updater_wip WORKER2: the link's packets (meshsim updater-w2 at ${C}).
#   build: gold's build.sh: its tests as before (the frames on every guest, FramesTest, the segments command byte-identical
#     to v1.36 gold's), every guest frame file also wrapped in packets and read back (80 runs), and test/packets/packets.sh
#     (the committed test vectors regenerated and checked by check_vectors.py; the commands; PacketsTest);
#   small, 29: test/frames/workloads.sh on EVM Zone trace-23 and Cyclone input/22 with the hard cuts, and on both workloads
#     at 2^29: each frame file of its three configurations (as Quest 200) wrapped in packets, read back and compared byte
#     for byte; the packets, the overhead and both SHA-256s kept in <name>-<configuration>.packets.txt;
#   frames-as-200: the 12 frame files' SHA-256s equal to Quest 200's;
#   frames-unchanged: test/packets/frames_unchanged.sh: the frames command's file, side file and output byte-identical to
#     main's gold (${MAIN}) on every guest four times and on the small inputs.
# The inputs are Quest task 111's kit (tasks/111-w27-inputs-evm-zone-29.sh, jolt-asic-planning fa59d9a), checked by SHA-256.
# Reads no persistent data and writes none; scratch in /tmp, removed at the start and the end. Safe to re-run. ~12 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD
RES=\$REPO/results/$NAME; mkdir -p "\$RES"
SCRATCH="/tmp/gold-frames-guests /tmp/gold-old-unchanged /tmp/gold-v136-classes /tmp/gold-packets /tmp/gold-frames-unchanged /tmp/gold-ref-classes"
W=/tmp/w2-$N; rm -rf "\$W" \$SCRATCH; mkdir -p "\$W"
echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1; python3 --version
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
[ "\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1)" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' tasks/111-w27-inputs-evm-zone-29.sh | sed '1d;\$d' | base64 -d > "\$W/kit111.tgz" || exit 2
[ "\$(sha256sum "\$W/kit111.tgz" | cut -d' ' -f1)" = "3ddbcebf08ff8cf3aaf75c1b88fd5eceb93a2a62ce2c942b42ef55f14c0a00b3" ] || { echo "task 111's kit differs"; exit 2; }
mkdir -p "\$W/i" && tar -xzf "\$W/kit111.tgz" -C "\$W/i" k/jap || exit 2
T=\$W/i/k/jap/tracer
cd "\$W/m/updater_wip/pipeline/gold"
V=\$RES/VERDICTS; : > "\$V"
step() { name=\$1; shift; t0=\$(date +%s); "\$@" > "\$RES/\$name.txt" 2>&1; rc=\$?
  echo "\$name: \$([ \$rc = 0 ] && echo PASS || echo FAIL) (\$(( \$(date +%s) - t0 )) s)" | tee -a "\$V"; tail -3 "\$RES/\$name.txt"; }
as200() {  # the frame files' SHA-256s, from their side files, against Quest 200's
  bad=0
  while read name sha; do
    got=\$(grep -o '"file_sha256": "[0-9a-f]*"' "\$RES/\$name.frames.json" | cut -d'"' -f4)
    [ "\$got" = "\$sha" ] && echo "\$name.frames \$got: as Quest 200's" || { echo "\$name.frames \$got: Quest 200's is \$sha: DIFFERS"; bad=1; }
  done <<'SHAS'
small/evm-zone-23-full bd7c771b52fd5bc94f3dee6d2051bcea886f1323173b8a6a65c370c6f42d885c
small/evm-zone-23-base b209f2b08b5d9b778455024046a78024cd5d78bae3f467906d9044f6c0ff2bfd
small/evm-zone-23-base256 57e219b92afca8bc9c2bb79d640f85fb53b496908658d96c6bfb1e8965a5852c
small/cyclone-22-full 85d338d018e525657b9046b09a08816d341509f54d38366317fdd5a73fcb7b8f
small/cyclone-22-base 6f63e10e82825ce85d564a1eb7197f1836caec651ab25f27f59d3d97aae54eb5
small/cyclone-22-base256 04a2287c4a5f378608176e09ac5b9ae5e786551684aad9ebb65397535bdd4f81
29/evm-zone-29-full 2d714a59b45dd14e8a1f0008f2450f8896fc990c75c639cb63b124c37b9d6aa7
29/evm-zone-29-base 813b5410aaf4449c7c0e403c2566e5bd940084c581d7cfd3ebd75b8581f6061b
29/evm-zone-29-base256 4fedc03c4a3c510bbbefdd5ca913ef5334c1bde5fdb41f6dc724cb5fb309cf29
29/cyclone-29-full 3b2c76494ffb5aab615d36f5764cc60a19bdfaf9b3f09ae1f9152c2f020501e8
29/cyclone-29-base 470990a540436176e5d2b0f34ba05e909b33fe06eb23e1cad65ee9305fa08d8d
29/cyclone-29-base256 6cb32b5a895f29ea04a8d1921b67ac5202d7e647cff3426b9a27ae8793f61c7c
SHAS
  return \$bad
}
step build ./build.sh
cp /tmp/gold-frames-guests.log "\$RES/guests.txt" 2>/dev/null
cp /tmp/gold-packets.log "\$RES/packets.txt" 2>/dev/null
step small sh test/frames/workloads.sh "\$T" "\$RES/small" small
step 29 sh test/frames/workloads.sh "\$T" "\$RES/29" 29
step frames-as-200 as200
step frames-unchanged sh test/packets/frames_unchanged.sh "\$W/ref/updater_wip/pipeline/gold" \$T/evm-zone-4e3451c1d8c5 \$T/evm-zone-4e3451c1d8c5/inputs/trace-23-812ded164e49 test/cuts/evm-zone-23-hardcuts.cuts \$T/cyclone \$T/cyclone/input/22/input-default test/cuts/cyclone-22-hardcuts.cuts
cd "\$REPO"; rm -rf "\$W" \$SCRATCH
cat "\$V"; echo "end \$(date -u)"
! grep -q FAIL "\$V"
exit \$?
__KIT_BEGIN__
TASK
base64 -w 76 "$K/kit.tgz" >> "$OUT"
echo "__KIT_END__" >> "$OUT"
rm -rf "$K"
echo "wrote $OUT ($(wc -c < "$OUT") bytes), kit $SHA"
