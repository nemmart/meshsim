#!/bin/bash
# rw_engine_teeth.sh: planted bugs, each of which the RW engine's tests must catch. Not in make check (a rebuild per
# bug). The tree is copied into a scratch directory, each bug is planted there by sed, the RW engine's tests rebuilt
# (clang release) and run: test_rw_engine (the RTL's unit test ported, against its reference model and the RTL's
# fingerprints, and the engine in the slot unit) and test_rtl_memory (the RTL's memory run on the chip, against the RTL
# chip's handshakes). A bug is caught when a test fails; the table names the tests that failed.
#
# The bugs are slips of the model of the RTL's rw_engine_unit (src/units/rw_engine.h): the bursts' split and the room
# rule, the job's end, the write data and WLAST, the write port's lanes, the reply, the error, the reset, the registers;
# and one of the slot's DMA (src/units/periph_slot.cpp), the end that waits for the packet register.
# Usage: tests/rw_engine_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug
# goes uncaught or its sed plants nothing. JOBS sets make's -j (8); TEETH_DIR the scratch directory (default: a new
# temporary one, removed at the end).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-8}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$W" && tar xf -)
B=build/clang-release
TESTS="test_rw_engine test_rtl_memory"
TARGETS=""; for t in $TESTS; do TARGETS="$TARGETS $B/$t"; done
build() { (cd "$W" && make -s CC_NAME=clang MODE=release -j"$JOBS" $TARGETS > "$W/build.log" 2>&1); }
run() {   # prints the failing tests, "-" if none
  local f="" t rc
  for t in $TESTS; do
    (cd "$W" && timeout 600 ./$B/$t > "$W/$t.out" 2>&1) 2> /dev/null
    rc=$?
    [ $rc = 0 ] && continue
    f="$f $(grep -oE '^FAIL +[a-z0-9_]+' "$W/$t.out" | awk '{print $2}' | tr '\n' ' ')"
    [ $rc -ge 124 ] && f="$f$t(died:$rc) "   # a timeout, or a FATAL outside a test's child
  done
  echo "${f:- -}"
}
build || { cat "$W/build.log"; echo "teeth: the pristine tree didn't build"; exit 2; }
r=$(run)
[ "$r" = " -" ] || { echo "teeth: the pristine tree fails: $r"; exit 2; }
echo "pristine: all pass"
H=src/units/rw_engine.h
S=src/units/periph_slot.cpp
# name ~ file ~ sed expression [~ file ~ sed expression ...]
BUGS=(
  "bursts: every burst a whole group (the job's last beat ignored)~$H~s/const unsigned burst_beats = raise_left < group_rest ? raise_left : group_rest;/const unsigned burst_beats = group_rest;/"
  "bursts: the first beat's offset left out of the group's rest (a burst across its group, and 4 KiB)~$H~s/const unsigned group_rest = BURST_BEATS - raise_offset;/const unsigned group_rest = BURST_BEATS;/"
  "bursts: the next group not stepped (the read's order)~$H~s/raise_group = (raise_group + 1) \& (beat_mask() >> 2);/raise_group = (raise_group + 0) \& (beat_mask() >> 2);/"
  "bursts: the offset kept after the first burst~$H~s/            raise_offset = 0;/            raise_offset = raise_offset + 0;/"
  "bursts: AxLEN one more~$H~s/burst_length = burst_beats - 1;/burst_length = burst_beats;/"
  "bursts: AW raised for a read~$H~s/ar_valid = (raise \&\& !write) || (ar_valid \&\& !in.arready);/ar_valid = (raise \&\& write) || (ar_valid \&\& !in.arready);/"
  "bursts: a channel freed without its ready~$H~s/const bool channel_free = write ? !aw_valid || in.awready : !ar_valid || in.arready;/const bool channel_free = true;/"
  "bursts: the first raised a cycle late (not in START's next cycle)~$H~s/const bool raise = busy \&\& raise_left != 0 \&\& channel_free \&\& (write || room);/const bool raise = busy \&\& raise_left != 0 \&\& channel_free \&\& (write || room) \&\& t > start_t + 1;/; s/        busy = true;/        busy = true; start_t = p;/; s/    bool busy = false, lpddr_error = false;/    bool busy = false, lpddr_error = false; int64_t start_t = 0;/"
  "the room rule: one beat more than the queue's room~$H~s/const bool room = free0 >= in_flight + burst_beats;/const bool room = free0 + 1 >= in_flight + burst_beats;/"
  "the room rule: one beat short~$H~s/const bool room = free0 >= in_flight + burst_beats;/const bool room = free0 >= in_flight + burst_beats + 1;/"
  "the room rule: without the beats in flight~$H~s/const bool room = free0 >= in_flight + burst_beats;/const bool room = free0 >= burst_beats;/"
  "the room rule: the DMA's take of the cycle counted as room at its start~$H~s/unsigned free_at_start(int64_t t) const { return cap - count - (taken_t == t ? 1 : 0); }/unsigned free_at_start(int64_t) const { return cap - count; }/"
  "the end: a read's done a cycle early (its last beat still in flight)~$H~s/const bool work_end = busy \&\& raise_left == 0 \&\& in_flight == (answer ? 1u : 0u);/const bool work_end = busy \&\& raise_left == 0 \&\& in_flight <= 1u;/"
  "the end: done posted a cycle late~$H~s/if (work_end) { busy = false; ev.post(t, EV_DONE); }/if (work_end) { busy = false; ev.post(t + 1, EV_DONE); }/"
  "the end: a write ends before LPDDR's last answer (its bursts not in flight)~$H~s/const unsigned raised = write ? 1 : burst_beats;/const unsigned raised = write ? 0 : burst_beats;/"
  "the end: a read's and a write's answers swapped~$H~s/const bool answer = write ? in.bvalid : in.rvalid;/const bool answer = write ? in.rvalid : in.bvalid;/"
  "the end: a job of 0 beats ends in START's cycle~$H~s/        busy = true;/        busy = config_beats != 0; if (!busy) ev.post(p, EV_DONE);/"
  "the write data: the pending row taken over a held beat~$H~s/const bool take = pending \&\& (!w_valid || in.wready);/const bool take = pending;/"
  "the write data: WLAST at the group's beat 2~$H~s/const bool take_last = rows_left == 1 || fetch_offset == BURST_BEATS - 1;/const bool take_last = rows_left == 1 || fetch_offset == BURST_BEATS - 2;/"
  "the write data: WLAST without the job's end~$H~s/const bool take_last = rows_left == 1 || fetch_offset == BURST_BEATS - 1;/const bool take_last = fetch_offset == BURST_BEATS - 1;/"
  "the write data: the row read again after the register took it~$H~s/if (fetch) { pending_lo = buffer\[2 \* size_t(fetch_row_after)\]; pending_hi = buffer\[2 \* size_t(fetch_row_after) + 1\]; }/if (fetch) { pending_lo = buffer[2 * size_t(fetch_row)]; pending_hi = buffer[2 * size_t(fetch_row) + 1]; }/"
  "the write data: WVALID not held without WREADY~$H~s/w_valid = take || (w_valid \&\& !in.wready);/w_valid = take;/"
  "the job: its kind read live from RW_CONFIG~$H~s/const bool write = job_kind == KIND_WRITE;/const bool write = config_kind == KIND_WRITE;/"
  "the write port: a 32-bit WRITE in the wrong lane~$H~s/const unsigned lane = offset >> 2 \& 3;/const unsigned lane = offset >> 3 \& 3;/"
  "the write port: a 64-bit WRITE's half by bit 2~$H~s/                r\[lane >> 1\] = lo;/                r[lane \& 1] = lo;/"
  "the write port: a 32-bit WRITE takes the word above its payload~$H~s/ | (lo \& 0xFFFFFFFF) << shift;/ | (lo >> 32 \& 0xFFFFFFFF) << shift;/"
  "the write port: the reserved size writes the row~$H~s/default: violate(V_WRITE_SIZE); break;/default: r[0] = lo; r[1] = hi; break;/"
  "the reply: RW_REPLY_W0 not cleared by the send~$H~/^                reply_w0 = 0;\$/d"
  "the reply: its words swapped~$H~s/q\[1\].push(uint64_t(v) << 32 | reply_w0, 0)/q[1].push(uint64_t(reply_w0) << 32 | v, 0)/"
  "the reply: pushed into queue 0~$H~s/q\[1\].push(uint64_t(v) << 32 | reply_w0, 0)/q[0].push(uint64_t(v) << 32 | reply_w0, 0)/"
  "the reply: a push into a full queue 1 kept (the queue two deep)~$H~s/q\[1\].cap = 1; q\[1\].e = new uint64_t\[2\]();/q[1].cap = 2; q[1].e = new uint64_t[4]();/"
  "the error: not sticky (live)~$H~s/if ((in.rvalid \&\& in.rresp != 0) || (in.bvalid \&\& in.bresp != 0)) lpddr_error = true;/lpddr_error = (in.rvalid \&\& in.rresp != 0) || (in.bvalid \&\& in.bresp != 0);/"
  "the error: the reads' alone~$H~s/if ((in.rvalid \&\& in.rresp != 0) || (in.bvalid \&\& in.bresp != 0)) lpddr_error = true;/if (in.rvalid \&\& in.rresp != 0) lpddr_error = true;/"
  "the error: SLVERR alone~$H~s/if ((in.rvalid \&\& in.rresp != 0) || (in.bvalid \&\& in.bresp != 0)) lpddr_error = true;/if ((in.rvalid \&\& in.rresp == 2) || (in.bvalid \&\& in.bresp == 2)) lpddr_error = true;/"
  "the reset: RW_REPLY_W0 kept~$H~/^        reply_w0 = 0;\$/d"
  "the reset: the output queues kept~$H~s/        for (Queue\& x : q) { x.head = 0; x.count = 0; x.taken_t = INT64_MIN; }/        for (Queue\& x : q) { x.taken_t = INT64_MIN; }/"
  "the reset: the error kept~$H~s/        busy = false; lpddr_error = false;/        busy = false;/"
  "the registers: RW_LPDDR_ADDR's upper bits kept~$H~s/case RW_LPDDR_ADDR: config_lpddr = v \& beat_mask(); return 1;/case RW_LPDDR_ADDR: config_lpddr = v; return 1;/"
  "the registers: RW_CONFIG's beats from bit 0~$H~s/case RW_CONFIG: config_kind = v \& 1; config_beats = v >> 1 \& 0x3FF; return 1;/case RW_CONFIG: config_kind = v \& 1; config_beats = v \& 0x3FF; return 1;/"
  "the registers: PARAMS type 3~$H~s/int params() const { return 2; }/int params() const { return 3; }/"
  "the slot's DMA: the end not waiting for the packet register~$S~/^        if (d.full) return;\$/d"
  "the slot's DMA: an entry taken with the packet register full (the router not ready)~$S~s/    if (d.full) { d.waits++; return; }//"
  "the slot's DMA: the packet leaves without packet_ready~$S~s/    if (d.full \&\& ready) {                                     \/\/ leaving/    if (d.full \&\& (ready || true)) {/"
)
miss=0
printf '%-88s %s\n' "planted bug" "caught by"
for b in "${BUGS[@]}"; do
  IFS='~' read -r -a f <<< "$b"
  what=${f[0]}
  case "$what" in *"$FILTER"*) ;; *) continue ;; esac
  planted=1; files=""
  for ((k = 1; k + 1 < ${#f[@]}; k += 2)); do
    file=${f[k]}; expr=${f[k + 1]}
    cp "$W/$file" "$W/$file.orig"; files="$files $file"
    sed -i "$expr" "$W/$file"
    cmp -s "$W/$file" "$W/$file.orig" && planted=0
  done
  if [ $planted = 0 ]; then r="NOT PLANTED (a sed matched nothing)"; miss=1
  elif build; then r=$(run); else r=" (no build: $(grep -m1 -oE 'error: .{0,60}' "$W/build.log"))"; fi
  for file in $files; do mv "$W/$file.orig" "$W/$file"; touch "$W/$file"; done
  printf '%-88s %s\n' "$what" "$r"
  case "$r" in " -"|" (no build"*) miss=1 ;; esac
done
[ $miss = 0 ] && echo "teeth: every planted bug caught" || echo "teeth: a planted bug went uncaught"
exit $miss
