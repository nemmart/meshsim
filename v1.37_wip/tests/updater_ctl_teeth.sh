#!/bin/bash
# updater_ctl_teeth.sh: planted bugs, each of which the Updater's controller's tests must catch (updater_wip, WORKER6).
# Not in make check (a rebuild per bug). The tree is copied into a scratch directory, each bug planted there by sed in
# src/units/updater_ctl.h, test_updater_ctl rebuilt (clang release) and run. A bug is caught when a test fails or the
# test dies (a test piece's check stops the simulator); the table names the tests that failed.
#
# The bugs: an update before its page's write is answered; the owned bits not cleared in a copied index page; a half
# taken as covered when it is not; the type counting run_left alone (a run ending inside a run of values); the decoder
# taking a word before it needs one; the Tracer's copy after the image; the next level's entry caught from the wrong
# beat; an LRU hit not moved to the front; WLAST a beat early; an owned page with both halves dirty written by one;
# PAGE_COUNT not counted down; the free page credit every 64 entries; a hole's data page of all ones; the FIFO taking a
# word when full.
# Usage: tests/updater_ctl_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug
# goes uncaught or its sed plants nothing. JOBS sets make's -j (8); TEETH_DIR the scratch directory (default: a new
# temporary one, removed at the end).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-8}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$W" && tar xf -)
B=build/clang-release
T=test_updater_ctl
build() { (cd "$W" && make -s CC_NAME=clang MODE=release -j"$JOBS" $B/$T > "$W/build.log" 2>&1); }
run() {   # prints the failing tests, "-" if none
  local f rc
  (cd "$W" && timeout 900 ./$B/$T > "$W/$T.out" 2>&1) 2> /dev/null
  rc=$?
  f="$(grep -oE '^FAIL +[a-z0-9_]+' "$W/$T.out" | awk '{print $2}' | tr '\n' ' ')"
  [ $rc -ge 124 ] && f="$f$T(died:$rc) "
  [ $rc != 0 ] && [ -z "$f" ] && f="$T(exit:$rc) "
  echo "${f:- -}"
}
build || { cat "$W/build.log"; echo "teeth: the pristine tree didn't build"; exit 2; }
r=$(run)
[ "$r" = " -" ] || [ "$r" = "-" ] || { echo "teeth: the pristine tree fails: $r"; exit 2; }
echo "pristine: all pass"
H=src/units/updater_ctl.h
# name ~ sed expression on $H
BUGS=(
  "an update sent before its page's write is answered (B)~s/if (q.wr_active || q.aw_valid || q.w_valid || q.w_pending) { d.stat\[S_STALL_MEMORY\]++; break; }/if (q.aw_valid || q.w_valid || q.w_pending) { d.stat[S_STALL_MEMORY]++; break; }/"
  "a copied index page's owned bits kept~s/if (e != NONE) b\[h\] \&= ~(uint64_t(OWNED) << (32 \* l));/(void)e;/"
  "a half taken as covered when it is not~s/if (q.out_type != T_WORDS) d.have\[half\] = true;/if (true) d.have[half] = true;/"
  "the type from run_left alone (WORD_COUNT not counted)~s/const uint32_t n = q.run_left < q.word_count ? q.run_left : q.word_count;/const uint32_t n = q.run_left;/"
  "the decoder taking a word whenever it holds fewer than 66 bits~s/(need2 == 0 || nb < need2)/(need2 == 0 || nb < 66)/"
  "the image's page before the Tracer's copy's~s/d.level = 0; d.side = TRACER; d.state = LEVEL;/d.level = 0; d.side = IMAGE; d.state = LEVEL;/"
  "the next level's entry caught from the wrong beat~s/q.rd_catch >> 2 == (q.rd_row \& 0xFF)/q.rd_catch >> 3 == (q.rd_row \& 0xFF)/"
  "an LRU hit not moved to the front~s/for (unsigned k = i; k > 0; k--) d.lru\[k\] = q.lru\[k - 1\];/(void)i;/"
  "WLAST a beat early~s/d.w_last = q.wr_rows_left == 1;/d.w_last = q.wr_rows_left == 2;/"
  "an owned page with both halves dirty written by one half~s/if (q.dirty\[0\] \&\& q.dirty\[1\]) write_burst/if (false) write_burst/"
  "PAGE_COUNT not counted down~s/d.page_count = q.page_count - 1;/d.page_count = q.page_count;/"
  "the free page credit every 64 entries~s/if (d.fp_left == FREE_HALF)/if (d.fp_left == 64)/"
  "a hole's new data page written as all ones~s/(data ? W_ZERO : W_ONES)/W_ONES/"
  "the FIFO taking a word when it is full~s/d.ready = n_after < FIFO;/d.ready = true;/"
)
uncaught=0
printf "%-75s %s\n" "bug" "caught by"
for b in "${BUGS[@]}"; do
  name=${b%%~*}; expr=${b#*~}
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  cp "$SIM/$H" "$W/$H"
  sed -i "$expr" "$W/$H"
  if cmp -s "$SIM/$H" "$W/$H"; then printf "%-75s %s\n" "$name" "NOT PLANTED (the sed matched nothing)"; uncaught=1; continue; fi
  if ! build; then printf "%-75s %s\n" "$name" "(does not build)"; uncaught=1; continue; fi
  r=$(run)
  if [ "$r" = " -" ] || [ "$r" = "-" ]; then printf "%-75s %s\n" "$name" "UNCAUGHT"; uncaught=1; else printf "%-75s %s\n" "$name" "$r"; fi
done
cp "$SIM/$H" "$W/$H"
exit $uncaught
