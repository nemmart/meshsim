#!/bin/sh
# assemble_runner.sh [-k] -- refresh ../runner's prebuilt payloads from this tree (run from meshsim/, after run_tests.sh
# has built everything): runner/suites/meshsim (the classes, the RV32 tile images, hops.txt and data/, every file the
# full-size suites read), and with -k runner/p4/kit too (the kit, via assemble.sh: a new jar means new HASHES, so the
# p4 results no longer match it). The scripts and READMEs in runner/ are kept.
set -e
R=$(cd .. && pwd)/runner
[ -d "$R/p4" ] && [ -d "$R/suites" ] || { echo "no ../runner"; exit 1; }
[ "${1:-}" = -k ] && { rm -rf "$R/p4/kit" && sh kit/assemble.sh "$R/p4/kit" > /dev/null; }
S="$R/suites/meshsim"; rm -rf "$S"; mkdir -p "$S/sw/ext/sort"
(cd build && tar --exclude='keys-*' -cf - .) | (mkdir -p "$S/build" && cd "$S/build" && tar -xf -)
cp -r sw/build "$S/sw/build"; cp -r sw/ext/sort/build "$S/sw/ext/sort/build"
cp hops.txt "$S/"; cp -r data "$S/data"
for f in data/one99.txt data/trace.txt data/zipfraw.txt sw/build/a2a.code.bin; do [ -f "$S/$f" ] || { echo "missing $S/$f"; exit 1; }; done
echo "runner payloads refreshed: $(du -sh "$R/p4/kit" | cut -f1) kit, $(du -sh "$S" | cut -f1) suites"
