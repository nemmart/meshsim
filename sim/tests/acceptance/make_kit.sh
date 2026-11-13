#!/usr/bin/env bash
# make_kit.sh -- builds the acceptance kit (v1.35, WORKER25; v1.3's acceptance/make_kit.sh, meshsim WORKER11) from the
# committed sim/ at HEAD (commit first): the sources the kit's programs build from (Makefile, src/, tools/, tests/'s
# top-level sources and makefiles, tests/acceptance/, bench/'s makefiles, which the Makefile includes), the data they read (data/tables with its manifest, data/hops.txt,
# the boot ROM, the replay controller's images: tests/m3-images/replay_ctl, fw/images/log8k and oldmap), and the
# scripts (run_shard.sh, calibrate.sh, build.sh, README.md) at the top. No jar: the runner builds it (./build.sh).
#   tests/acceptance/make_kit.sh OUT.tgz
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/../.."
OUTF=$(realpath -m "${1:?usage: make_kit.sh OUT.tgz}")
TOP=$(git rev-parse --show-toplevel); REV=$(git rev-parse --short HEAD)
[ -z "$(git status --porcelain -- . | grep -v '^??')" ] || echo "make_kit.sh: uncommitted changes in sim/ are not in the kit (HEAD $REV)" >&2
K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/w25-kit"
( cd "$TOP" && git archive --format=tar HEAD -- sim/Makefile sim/src sim/tools sim/data/tables sim/data/hops.txt sim/fw/boot/bootrom.hex \
    sim/fw/images/log8k sim/fw/images/oldmap ':(glob)sim/tests/*.cpp' ':(glob)sim/tests/*.h' ':(glob)sim/tests/*.mk' sim/tests/acceptance \
    ':(glob)sim/tests/m3-images/replay_ctl.*' ':(glob)sim/bench/**/*.mk' ) | tar -xf - -C "$K/w25-kit"
for need in sim/Makefile sim/tests/replay_shard_main.cpp sim/tests/acceptance/shard_summary.cpp sim/tools/delivery/synthetic_delivery.cpp \
            sim/data/tables/manifest.json sim/data/tables/replayer/decode_table.bin sim/tests/m3-images/replay_ctl.code.bin sim/fw/images/log8k/replay_ctl.code.bin; do
  [ -e "$K/w25-kit/$need" ] || { echo "make_kit.sh: the kit lacks $need" >&2; exit 1; }
done
cp tests/acceptance/run_shard.sh tests/acceptance/calibrate.sh tests/acceptance/build.sh tests/acceptance/README.md "$K/w25-kit/"
echo "sim/ $REV" > "$K/w25-kit/VERSION"
(cd "$K/w25-kit" && find . -type f ! -name HASHES | sort | xargs sha256sum > HASHES)
( cd "$K" && tar --sort=name --mtime=@0 --owner=0 --group=0 -cf - w25-kit ) | gzip -9n > "$OUTF"
echo "kit $OUTF: $(stat -c %s "$OUTF") bytes, sha256 $(sha256sum "$OUTF" | cut -d' ' -f1), sim/ $REV"
