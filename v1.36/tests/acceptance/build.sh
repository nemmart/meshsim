#!/usr/bin/env bash
# build.sh -- builds the acceptance kit's programs (v1.35) from the kit's sim/ sources, with clang (D18) and
# the suite build (MODE=suite: release flags with the checker on, so the machine's violations reach the verdict as in
# v1.3): replay_shard (Stage A and B per window), shard_summary (SUMMARY.txt, VERDICT.txt) and
# synthetic_delivery (calibrate.sh's delivery), into sim/build/clang-suite/. The v1.3 kit carried a jar instead.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
command -v clang++ > /dev/null || { echo "build.sh: needs clang++ (apt install clang lld)"; exit 2; }
make -s -C "$HERE/sim" -j"$(nproc)" MODE=suite CC_NAME=clang \
  build/clang-suite/replay_shard build/clang-suite/shard_summary build/clang-suite/synthetic_delivery
echo "built: $(cd "$HERE/sim/build/clang-suite" && ls replay_shard shard_summary synthetic_delivery | paste -sd' ') ($(clang++ --version | head -1))"
