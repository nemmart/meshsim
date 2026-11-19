#!/bin/bash
# make_task.sh: writes a Quest task for the runner from the link's sources.
#   make_task.sh <number> <item> <commit> <out dir>      item: campaign, campaign2, long-noise, searches-a, searches-b, search-big1
#   make_task.sh <number> <item> - <out dir>             the same from the files as they are, with no git
# Which link: the folder this script is in, if that is a link folder (a copy that stands alone, as v1.37's link/);
# else this repository's updater_wip/link (the script's home is updater_wip/planning/sessions/WORKER8/tasks/).
# The task holds the link's sources as a kit (base64 of a tar), builds them on the runner, and runs the item. It
# writes only results/<task>/; its scratch is in /tmp and is removed first and last; it is safe to run again.
# An item the task does not know builds the kit, runs the header's tests and the cases, and stops: a check of the kit.
set -eu
NUM=$1; ITEM=$2; REV=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=$NUM-link-w8-$ITEM
KIT=$(mktemp)
if [ "$REV" = - ]; then
    # The files as they are: the link folder itself, without what it builds. The same bytes each time.
    [ -f "$HERE/link_end.cpp" ] && DIR=$HERE || DIR=$HERE/../../../../link
    DIR=$(cd "$DIR" && pwd); LINK=link; WHAT="link/, its files as they were"
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --exclude=link/build --transform "s|^$(basename "$DIR")|link|" \
        -C "$(dirname "$DIR")" -cf - "$(basename "$DIR")" | gzip -9n > "$KIT"
else
    ROOT=$(git -C "$HERE" rev-parse --show-toplevel)
    if [ -f "$HERE/link_end.cpp" ]; then LINK=$(git -C "$HERE" rev-parse --show-prefix); LINK=${LINK%/}; else LINK=updater_wip/link; fi
    git -C "$ROOT" archive --format=tar "$REV" "$LINK" | gzip -9n > "$KIT"
    WHAT="$LINK at $(git -C "$ROOT" rev-parse --short=8 "$REV")"
fi
SHA=$(sha256sum "$KIT" | cut -d' ' -f1)
{
cat <<HEAD
#!/bin/bash
# Task $NAME -- meshsim updater_wip WORKER8: the host link's protocol on its own ($WHAT;
# made by make_task.sh), item: $ITEM.
#   campaign    the random runs: 16 slices at once, each a range of seeds for an hour, 2 million cycles of traffic
#               and noise a seed, chains of 1, 2, 4 and 16; then the slices added up. Any failing seed is a FAIL.
#   campaign2   the same with forged headers among the damage (8 slices), and with bits flipped in senders' memory (8).
#   long-noise  16 runs with bursts of noise up to 300 ms (3e8 cycles), on chains of 1 and 2.
#   searches-a, searches-b   the searches of the shrunken model too large for a session's machine (searches.sh big2
#               to big18), all of a set at once, depth-first, 55 minutes each.
#   search-big1 the largest (searches.sh big1), breadth-first and depth-first at once, three hours.
# From an embedded kit. Writes only results/$NAME/. Scratch in /tmp, removed first and at the end. Safe to re-run.
set -u
cd "\$(dirname "\$0")/.."
REPO=\$PWD; RES=\$REPO/results/$NAME; mkdir -p "\$RES"
W=/tmp/link-w8-$NAME; rm -rf "\$W"; mkdir -p "\$W"
sed -n '/^__KIT_BEGIN__\$/,/^__KIT_END__\$/p' "\$0" | sed '1d;\$d' | base64 -d > "\$W/kit.tgz" || exit 2
GOT=\$(sha256sum "\$W/kit.tgz" | cut -d' ' -f1); echo "kit sha256 \$GOT"
[ "\$GOT" = "$SHA" ] || { echo "kit sha256 mismatch (want $SHA)"; exit 2; }
tar -xzf "\$W/kit.tgz" -C "\$W" || exit 2
cd "\$W/$LINK" || exit 2
ITEM=$ITEM; FAIL=0; T0=\$(date +%s)
verdict() { echo "VERDICT \$1: \$2"; echo "\$1 \$2" >> "\$RES/VERDICTS"; case \$2 in PASS*) ;; *) FAIL=1;; esac; }
: > "\$RES/VERDICTS"
{ echo "kit: meshsim, $WHAT"; echo "host \$(hostname); \$(nproc) CPUs"; g++ --version | head -1; free -g | sed -n 2p; } | tee "\$RES/machine.txt"
make -j8 all > "\$RES/build.log" 2>&1 && build/test_header > "\$RES/test_header.txt" 2>&1 && build/linkcase cases/*.case > "\$RES/cases.txt" 2>&1
if [ \$? = 0 ]; then verdict build PASS; else verdict build FAIL; rm -rf "\$W"; exit 1; fi
HEAD
cat <<'BODY'
# The memory there is: the task's own limit if it has one, else what the machine has free.
LIM=$(cat "/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup | head -1)/memory.max" 2>/dev/null || echo max)
FREE=$(awk '/MemAvailable/ {print $2 * 1024}' /proc/meminfo)
if [ "$LIM" = max ] || [ "$LIM" -gt "$FREE" ]; then LIM=$FREE; fi
echo "memory for the task: $LIM bytes" | tee -a "$RES/machine.txt"
# search <name> <bytes> <bytes a state> <seconds> <list> [linksearch's options]: one line of searches.sh, with its own
# limits of memory and time. It stops by itself at either, and its line then says NOT complete and how many waited.
search() {
    name=$1; mem=$2; each=$3; secs=$4; list=$5; shift 5
    ( ulimit -v $((mem / 1024)); MAX=$((mem / 10 * 8 / each)) SECS=$secs LINKSEARCH="build/linksearch $*" ./searches.sh "$list" > "$RES/search-$name.txt" 2>&1 )
}
searches() {       # the lists named, all at once, depth-first (few states waiting; room is taken for every state at the start)
    n=$#
    for l in "$@"; do search $l $((LIM / 100 * 95 / n)) 142 3300 $l --deep & done
    wait
    cat "$RES"/search-big*.txt | tee "$RES/searches.txt"
    if grep -q 'FAILS\|NO RESULT' "$RES/searches.txt"; then verdict searches "FAIL (searches.txt)"; else verdict searches "PASS ($(grep -c 'NOT complete' "$RES/searches.txt") not complete)"; fi
}
case $ITEM in
campaign)
    for k in $(seq 0 15); do
        a=$((100000 + k * 10000))
        build/linkrun --seeds $a $((a + 9999)) --cycles 2000000 --seconds 3600 > "$RES/slice-$k.txt" 2>&1 &
    done
    wait
    python3 sum_runs.py "$RES"/slice-*.txt > "$RES/campaign.txt" 2>&1 && verdict campaign PASS || verdict campaign "FAIL (a seed failed: campaign.txt)"
    head -4 "$RES/campaign.txt"
    ;;
campaign2)
    for k in $(seq 0 7); do
        a=$((300000 + k * 10000))
        build/linkrun --seeds $a $((a + 9999)) --cycles 2000000 --seconds 3600 --forge > "$RES/forge-$k.txt" 2>&1 &
        build/linkrun --seeds $((a + 100000)) $((a + 109999)) --cycles 2000000 --seconds 3600 --flips > "$RES/flips-$k.txt" 2>&1 &
    done
    wait
    python3 sum_runs.py "$RES"/forge-*.txt > "$RES/forge.txt" 2>&1 && verdict forge PASS || verdict forge "FAIL (a seed failed: forge.txt)"
    python3 sum_runs.py "$RES"/flips-*.txt > "$RES/flips.txt" 2>&1 && verdict flips PASS || verdict flips "FAIL (a seed failed: flips.txt)"
    head -4 "$RES/forge.txt" "$RES/flips.txt"
    ;;
long-noise)
    for k in $(seq 1 10); do build/linkrun --seed $((200 + k)) --nodes 1 --cycles 1500000000 --long-noise -v > "$RES/long-$k.txt" 2>&1 & done
    for k in $(seq 11 16); do build/linkrun --seed $((200 + k)) --nodes 2 --cycles 700000000 --long-noise -v > "$RES/long-$k.txt" 2>&1 & done
    wait
    python3 sum_runs.py "$RES"/long-*.txt > "$RES/long-noise.txt" 2>&1 && verdict long-noise PASS || verdict long-noise "FAIL (long-noise.txt)"
    head -4 "$RES/long-noise.txt"
    ;;
searches-a) searches big2 big3 big4 big5 big6 big7 big8 big9 ;;
searches-b) searches big10 big11 big12 big13 big14 big15 big16 big17 big18 ;;
search-big1)
    # The one search, twice: breadth-first as before for 1,400 seconds, to say how many states wait at that point; then
    # depth-first for 10,500 seconds (the same states, few of them waiting, room for 346 million in 62 GB: this is the
    # one that can finish). 11,900 seconds and the build, of the task's 12,600.
    search big1-breadth $((LIM / 100 * 60)) 480 1400 big1
    search big1-deep $((LIM / 100 * 95)) 142 10500 big1 --deep
    cat "$RES"/search-big1-*.txt | tee "$RES/searches.txt"
    if grep -q 'FAILS\|NO RESULT' "$RES/searches.txt"; then verdict search "FAIL (searches.txt)"
    elif grep -q ' holds$' "$RES/searches.txt"; then verdict search "PASS (complete)"
    else verdict search "PASS (not complete: no failure in the states tried)"; fi
    ;;
esac
echo "took $(( $(date +%s) - T0 )) s" | tee "$RES/TIMES"
cd /; rm -rf "$W"
exit $FAIL
BODY
echo "__KIT_BEGIN__"; base64 -w 100 "$KIT"; echo "__KIT_END__"
} > "$OUT/$NAME.sh"
rm -f "$KIT"
echo "$OUT/$NAME.sh: kit $SHA"
