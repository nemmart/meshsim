#!/usr/bin/env bash
# make_suite_kit.sh -- WORKER20: the kit for the suites task (as WORKER11's 093/094): wip/ with every image prebuilt and
# meshsim compiled at Java 17 (run_quick.sh, run_full.sh at wip/meshsim/), and WORKER8's sortrunner/ with meshsim.jar
# rebuilt from this tree and HASHES updated for the jar (the sort image and boot ROM are checked byte-identical).
#   WORKER20/make_suite_kit.sh BASE_SORTRUNNER_DIR OUT.tgz   (from the repo root; BASE: task 093's sortrunner/, unpacked)
set -eu
BASE=$(realpath "$1"); OUTF=$(realpath -m "$2")
R=$(cd "$(dirname "$0")/.." && pwd); K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/wip"
for d in meshsim sort sw tables; do rsync -a --exclude results --exclude build --exclude '.git' "$R/wip/$d" "$K/wip/"; done
M="$K/wip/meshsim"
(cd "$M" && sh sw/build.sh >/dev/null && sh sw/build_cc.sh >/dev/null && sh sw/build_recipes.sh >/dev/null && sh sw/ext/sort/build.sh >/dev/null && sh sw/replay/build.sh >/dev/null)
mkdir -p "$M/build"; javac --release 17 -nowarn -d "$M/build" "$M"/src/*.java
grep -v '^#' "$M/test/PENDING" | awk 'NF{print "test/"$1}' > "$K/pend"
(cd "$M" && javac --release 17 -nowarn -d build -cp build $(ls test/*.java | grep -v -x -F -f "$K/pend"))
cp "$M/tools/run_quick.sh" "$M/tools/run_full.sh" "$M/"
rsync -a "$BASE/" "$K/sortrunner/"
S="$K/sortrunner/kit"
for f in sort.code.bin sort.data.bin sort.sym; do cmp -s "$M/sw/ext/sort/build/$f" "$S/image/$f" || { echo "sort image $f differs from the base's"; exit 1; }; done
cmp -s "$M/sw/bootrom.hex" "$S/sw/bootrom.hex" || { echo "boot ROM differs from the base's"; exit 1; }
(cd "$M/build" && jar cf "$S/meshsim.jar" .)
NEW=$(sha256sum "$S/meshsim.jar" | cut -d' ' -f1)
sed -i "s/^[0-9a-f]\{64\}  meshsim.jar$/$NEW  meshsim.jar/; 1s/.*/# meshsim v1.26 (WORKER20, branch v1.26-meshsim) kit: SHA-256 of every input a run reads (java Inputs check HASHES)/" "$S/HASHES"
grep -q "$NEW  meshsim.jar" "$S/HASHES" || { echo "HASHES: no meshsim.jar line"; exit 1; }
tar -C "$K" -czf "$OUTF" wip sortrunner
echo "suite kit $OUTF: $(stat -c %s "$OUTF") bytes, sha256 $(sha256sum "$OUTF" | cut -d' ' -f1)"
