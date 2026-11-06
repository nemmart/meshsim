#!/bin/bash
# make_quick_task.sh REPO TASKNAME > task.sh -- stage 1's exactness on the quick suite, on the runner (WORKER28): every step
# of run_tests.sh's quick suite twice, stage 1 off and on (WORKER28/tools/prove_skip.sh, 8 steps at a time), and each
# shuffled step with stage 1 on against its unshuffled base with stage 1 off (shuffle_vs_unshuffled.sh). The kit: wip/meshsim
# (classes compiled for Java 21, every tile image built) and wip/tables, embedded, SHA-256 checked.
set -e
R=$(cd "$1" && pwd); NAME=$2; K=$(mktemp -d); trap 'rm -rf "$K"' EXIT
mkdir -p "$K/wip"; rsync -a --exclude results --exclude build "$R/wip/meshsim" "$R/wip/tables" "$K/wip/"
M="$K/wip/meshsim"; mkdir -p "$M/build"
(cd "$M" && sh sw/build.sh >/dev/null && sh sw/build_cc.sh >/dev/null && sh sw/build_recipes.sh >/dev/null && sh sw/ext/sort/build.sh >/dev/null 2>&1 && sh sw/replay/build.sh >/dev/null && sh sw/tracer/build.sh >/dev/null)
javac --release 21 -nowarn -d "$M/build" "$M"/src/*.java
grep -v '^#' "$M/test/PENDING" | awk 'NF{print "test/"$1}' > "$K/pend"
(cd "$M" && javac --release 21 -nowarn -d build -cp build $(ls test/*.java | grep -v -x -F -f "$K/pend")) 2>&1 | grep -v '^Note:' || true
cp "$R/WORKER28/tools/prove_skip.sh" "$R/WORKER28/tools/shuffle_vs_unshuffled.sh" "$M/"
(cd "$M" && awk '/^if \[ "\$MODE" != quick \]/{full=1} /^fi$/{full=0} !full && /^LOG=/ && /\$J /' run_tests.sh | sed 's/^LOG=\([^ ]*\).*\$J /\1|/; s/\$Q/quick/' > quick.steps)
(cd "$R" && echo "meshsim $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD)), built $(date -u +%F)") > "$K/VERSION"
tar -C "$K" -czf "$K/kit.tgz" wip VERSION
SHA=$(sha256sum "$K/kit.tgz" | cut -d' ' -f1); N=$(wc -l < "$M/quick.steps")
cat <<EOT
#!/bin/bash
# Task $NAME -- meshsim WORKER28, stage 1's exactness on the quick suite (branch sim-idle-skip): each of the $N steps of
# run_tests.sh's quick suite twice, -Dmeshsim.skip=false and =true, 8 steps at a time, the logs compared with wall times
# masked; then each shuffled step with stage 1 on against its unshuffled base with stage 1 off (the seed's banner
# masked), the sort's pair included. Writes only results/$NAME/. Scratch in /tmp, removed first. Every step under
# timeout 25 min. Expected ~15 min.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/w28-quick; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cat "\$W/VERSION"; echo "start \$(date -u) on \$(hostname), \$(nproc) threads"; java -version 2>&1 | head -1
cd "\$W/wip/meshsim" || exit 2
mkdir -p "\$W/out"
export STEP_LIMIT=1500
cat quick.steps | xargs -d '\n' -P 8 -I{} sh -c 'echo "{}" | bash ./prove_skip.sh "\$0" > "\$0/v.\$(echo "{}" | cut -d"|" -f1)"' "\$W/out"
cat "\$W"/out/v.* | sort > "\$RES/VERDICTS.txt"; cat "\$RES/VERDICTS.txt"
EXTRA="sort:sort-shuf" bash ./shuffle_vs_unshuffled.sh "\$W/out" | tee "\$RES/SHUFFLE.txt"
mkdir -p "\$RES/logs"; for f in "\$W"/out/*.log; do gzip -c "\$f" > "\$RES/logs/\$(basename "\$f").gz"; done
S=\$(grep -c ' SAME ' "\$RES/VERDICTS.txt"); D=\$(grep -c ' DIFF ' "\$RES/VERDICTS.txt"); U=\$(grep -c ': SAME' "\$RES/SHUFFLE.txt")
echo "on/off: \$S of $N same, \$D different; shuffled-on vs unshuffled-off: \$U of \$(grep -c . "\$RES/SHUFFLE.txt") same; \$(date -u)"
rm -rf "\$W"
[ "\$S" = $N ] && [ "\$U" = "\$(grep -c . "\$RES/SHUFFLE.txt")" ]; exit \$?
__KIT_BEGIN__
EOT
base64 -w 76 "$K/kit.tgz"
echo "__KIT_END__"
