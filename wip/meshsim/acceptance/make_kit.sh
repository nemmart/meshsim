#!/usr/bin/env bash
# make_kit.sh -- builds the acceptance kit (meshsim WORKER11) from wip/meshsim: meshsim.jar (src + the replay tests,
# Java 17 bytecode), hops.txt, the boot ROM, the prebuilt replay controller, the universal tables with their manifest,
# the scripts. ./acceptance/make_kit.sh OUT.tgz   (run from wip/meshsim after sw/build.sh and sw/replay/build.sh)
set -eu
cd "$(dirname "$0")/.."
OUTF=$(realpath -m "${1:?usage: make_kit.sh OUT.tgz}")
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/w11-kit/classes" "$K/w11-kit/sw" "$K/w11-kit/replay" "$K/w11-kit/tables/replayer"
javac --release 17 -nowarn -d "$K/w11-kit/classes" src/*.java
javac --release 17 -nowarn -d "$K/w11-kit/classes" -cp "$K/w11-kit/classes" test/T.java test/Replay*.java test/StageB.java test/SyntheticDelivery.java
(cd "$K/w11-kit/classes" && jar cf ../meshsim.jar .); rm -rf "$K/w11-kit/classes"
cp hops.txt "$K/w11-kit/"; cp sw/bootrom.hex "$K/w11-kit/sw/"
cp sw/replay/build/replay_ctl.code.bin sw/replay/build/replay_ctl.data.bin sw/replay/build/replay_ctl.sym "$K/w11-kit/replay/"
cp ../tables/manifest.json "$K/w11-kit/tables/"
cp ../tables/replayer/{decode_table,rom,templates,template_index}.bin "$K/w11-kit/tables/replayer/"
cp acceptance/run_shard.sh acceptance/calibrate.sh acceptance/README.md "$K/w11-kit/"
(cd "$K/w11-kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
tar -C "$K" -czf "$OUTF" w11-kit
echo "kit $OUTF: $(stat -c %s "$OUTF") bytes, sha256 $(sha256sum "$OUTF" | cut -d' ' -f1)"
