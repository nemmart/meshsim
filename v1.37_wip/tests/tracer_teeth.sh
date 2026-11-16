#!/bin/bash
# tracer_teeth.sh: planted bugs, each of which the Tracer's tests must catch. Not in make check (a rebuild per bug).
# The tree is copied into a scratch directory, each bug is planted there by sed, the Tracer's tests rebuilt (clang
# release) and run: test_tracer_suites (the three suites against their logs), test_tracer_rules, test_tracer,
# test_tracer_parts and test_tracer_rig. A bug is caught when a test fails; the table names the tests that failed.
#
# The bugs are of two kinds: v1.35's behaviour where v1.36 changed it (the inventory's A10.1-A10.10: each of
# test_tracer_rules' a10 tests must fail on it), and timing or rule slips of the model of the RTL (a row's cycles, a
# redirect, a fill, a probe, the buffer's place, the credits).
# Usage: tests/tracer_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or its sed plants nothing. JOBS sets make's -j (16).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-16}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
(cd "$SIM" && tar cf - --exclude=./build .) | (cd "$W" && tar xf -)
B=build/clang-release
TESTS="test_tracer_suites test_tracer_rules test_tracer test_tracer_parts test_tracer_rig"
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
T=src/units/tracer.cpp
H=src/units/tracer.h
# name ~ file ~ sed expression [~ file ~ sed expression ...]
BUGS=(
  "memory system: an LPDDR miss 81 cycles, not 80~src/units/mem_system.h~s/int hit = 6, miss = 80,/int hit = 6, miss = 81,/"
  "FileStore: a path not owned reads as a hole~src/units/file_store.cpp~s/w.status = owned ? ST_OK : ST_NOT_OWNED;/w.status = owned ? ST_OK : ST_HOLE;/"
  "RVC: c_imm6 drops immediate bit 4~src/units/rvc.h~s/((h >> 2) \& 0x1f); }/((h >> 2) \& 0x0f); }/"
  "the rig: computeIfAbsent appends (the page order)~tests/tracer_rig.cpp~s/b.insert(b.begin(), key);/b.push_back(key);/"
  "A10.1 EVENTS at v1.35's bit numbers~$H~s/EV_ADVICE = 2, EV_EXP = 3, EV_NEWEXP = 4, EV_SVC_STATE = 5,/EV_ADVICE = 3, EV_EXP = 5, EV_NEWEXP = 6, EV_SVC_STATE = 7,/; s/EV_SVC_CODE = 6, EV_SVC_RLOG = 7, EV_SVC_WLOG = 8, EV_BITS = 9 };/EV_SVC_CODE = 8, EV_SVC_RLOG = 9, EV_SVC_WLOG = 10, EV_BITS = 11 };/~$T~s/kDepths\[Tracer::EV_BITS\] = {1, 1, 2, 2, 1, 0, 0, 0, 0};/kDepths[Tracer::EV_BITS] = {1, 1, 0, 2, 0, 2, 1, 0, 0, 0, 0};/; s/kFlags\[Tracer::EV_BITS\] = {false, false, true, true, false, false, false, false, false};/kFlags[Tracer::EV_BITS] = {false, false, false, true, false, true, false, false, false, false, false};/"
  "A10.1 ERRORS at v1.35's bit numbers~$H~s/ERR_OVERRUN = 0, ERR_INVALID = 1, ERR_ADVICE_UNDER = 2, ERR_MAP = 3, ERR_STORE = 4, ERR_PAGE = 5,/ERR_OVERRUN = 1, ERR_INVALID = 2, ERR_ADVICE_UNDER = 4, ERR_MAP = 7, ERR_STORE = 8, ERR_PAGE = 9,/; s/ERR_RANGE = 6, ERR_ASSERT = 7, ERR_MISALIGNED = 8, ERR_WRITE_BACK = 9, ERR_BITS = 10 };/ERR_RANGE = 10, ERR_ASSERT = 11, ERR_MISALIGNED = 12, ERR_WRITE_BACK = 13, ERR_BITS = 14 };/"
  "A10.2 a write-back cannot fail (its status ignored)~$T~s/if (write_response_valid \&\& f.write_status == MemSystem::WRITE_ERROR)/if (false \&\& write_response_valid \&\& f.write_status == MemSystem::WRITE_ERROR)/"
  "A10.3 one bus: no read while a write is outstanding~$T~s/const bool read_free = !f.read_request_valid \&\& !f.read_outstanding \&\& !dropped_presented;/const bool read_free = !f.read_request_valid \&\& !f.read_outstanding \&\& !dropped_presented \&\& !f.write_request_valid \&\& !f.write_outstanding;/"
  "A10.4 the buffer's place free at the request~$T~s/const bool read_for_evict = in_miss \&\& !xd.taking_back \&\& xd.victim_dirty \&\& !f.write_held;/const bool read_for_evict = in_miss \&\& !xd.taking_back \&\& xd.victim_dirty \&\& (!f.write_held || f.write_requested);/"
  "A10.4 a miss on the buffer's line reads it from memory~$T~s/xd.taking_back = f.write_held \&\& f.write_line_kept \&\& f.write_line == xd.line;/xd.taking_back = false;/"
  "A10.4 the state write frees a requested write's place~$T~s/if (!fill_.write_requested) fill_.write_held = false;/fill_.write_held = false;/"
  "A10.5 EXPANSION_OFFSET live, the state word's offset~$T~s/s_exp_offset_ = expansion_offset_; s_exp_opcode_ = presented_;/s_exp_offset_ = state_offset(); s_exp_opcode_ = presented_;/"
  "A10.6 an offset of 2^32 or more goes to the LRU and the probe~$T~s/xj.too_far = (r.offset >> 32) != 0;/xj.too_far = false;/"
  "A10.7 the record's spare bits held~$T~/if (wd == 0) hi \&= 0x0000FFFFFFFFFFFFull;/d; /if (wd == 1) hi \&= 0x1FFFFFFFFFFFFFFFull;/d; /if (wd == 2) lo = 0;/d; /if (wd == REC_FILES) hi \&= 0x0000007FFFFFFFFFull;/d"
  "A10.8 both logs end at the log scan's end~$T~s/wlog_given_ == wlog_values_};/wlog_given_ == wlog_values_ \&\& rlog_given_ == rlog_values_};/"
  "A10.9 the live events stay up after a stop~$T~s/ev.live(EV_SVC_STATE, q0_holding_ \&\& !stopped_);/ev.live(EV_SVC_STATE, q0_holding_);/; s/oq\[q\].credits > 0 \&\& !stopped_);/oq[q].credits > 0);/"
  "A10.9 dma_done posted after a stop (the bench's DMA)~tests/tracer_block.cpp~s/if (ending \&\& !tr.stopped() \&\& dma_done_ < 1) dma_done_++;/if (ending \&\& dma_done_ < 1) dma_done_++;/"
  "A10.10 the reset keeps the caches and the jump LRU~$T~/^void Tracer::reset/,/^}/ s/icache_.clear(); dcache_.clear(); jump_lru_.clear(); i_seen_.clear();//"
  "A10.10 the reset keeps the hash's constants~$T~/^void Tracer::reset/,/^}/ s/run_config_ = hash_mul_ = hash_step_ = hash_lines_ = 0;/run_config_ = 0;/"
  "A10.10 the reset keeps ERRORS~$T~/^void Tracer::reset/,/^}/ s/rows_left_ = rows_done_ = 0; errors_ = 0; n_errors_logged = 0;/rows_left_ = rows_done_ = 0;/"
  "A10.10 START with no state write runs~$T~s/if (!state_written_) ub(t, \"START with no state write/if (false) ub(t, \"START with no state write/"
  "A10.10 START zeroes the statistics counters~$T~s/rows_left_ = n; rows_done_ = 0; runs++; run_start_at = t;/rows_left_ = n; rows_done_ = 0; runs++; run_start_at = t; for (int i = 0; i < STAT_N; i++) stat[i] = 0;/"
  "a state write after a reset flagged as a rewrite~$T~s/if (state_written_ \&\& advice_left() > 0) violate/if (advice_left() > 0) violate/"
  "rows: an S row's ALU a cycle late (5 cycles a row)~$T~s/int done_after = r.mul ? 4 : 2;/int done_after = r.mul ? 4 : 3;/"
  "a redirect of 4: the successor fetched when the ALU is done~$T~s/const bool known_now = (in.known || redirect) \&\& free \&\& rows_left_ != 0;/const bool known_now = (in.known || redirect || resolved) \&\& free \&\& rows_left_ != 0;/"
  "an instruction miss a cycle short (no READ_LINE)~$T~s/                        xf.state = XF_READ_LINE;/                        xf.state = XF_READ;/"
  "a data miss a cycle long (its read wanted a cycle late)~$T~s/const bool data_read_want = xd.state == XD_EVICT || xd.state == XD_REQUEST;/const bool data_read_want = xd.state == XD_REQUEST;/"
  "the probe: a line compared in its response's cycle~$T~s/const bool map_line_valid = f.map_line_valid;/const bool map_line_valid = map_read_done;/"
  "the probe: two lines at most~$H~s/static constexpr int PROBE_LINES = 3;/static constexpr int PROBE_LINES = 2;/"
  "an ERRORS bit: LD aligned on 4 bytes~$T~s/const bool misaligned = d_row_start \&\& (r.mem_addr \& 7) != 0;/const bool misaligned = d_row_start \&\& (r.mem_addr \& 3) != 0;/"
  "the output path: a transfer one entry past its credits~$T~s/if (words > covered)/if (words > covered + 1)/"
  "done a cycle early (the DMA's busy not waited for)~$T~s/bool drained = !q0_held \&\& !dma_busy(T);/bool drained = !q0_held;/"
  "the wake rule: nothing in flight wakes an idle Tracer~$H~s/bool must_step() const { return running_ || !quiet() || inputs_.n > 0 || landing_.n > 0; }/bool must_step() const { return running_ || inputs_.n > 0 || landing_.n > 0; }/"
  "the fill port: a write-back's line not written~src/units/mem_system.h~s/            store->write_line(walk_.data_line, data);\$/            (void)data;/"
)
miss=0
printf '%-66s %s\n' "planted bug" "caught by"
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
  printf '%-66s %s\n' "$what" "$r"
  case "$r" in " -"|" (no build"*) miss=1 ;; esac
done
[ $miss = 0 ] && echo "teeth: every planted bug caught" || echo "teeth: a planted bug went uncaught"
exit $miss
