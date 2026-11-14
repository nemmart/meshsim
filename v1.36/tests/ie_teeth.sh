#!/bin/bash
# ie_teeth.sh: planted bugs, each of which Ingress/Egress's tests must catch. Not in make check (a rebuild per bug).
# The tree is copied into a scratch directory (the worktree's build-scratch/, or IE_TEETH_DIR), each bug is planted
# there by sed, the tests rebuilt (clang release) and run: test_ingress_egress (the unit, from 17.6 and the RTL unit's
# timing) and test_rtl_stream (the chip: the host interface and the UART through the master's core, at the RTL chip's
# cycles). A bug is caught when a test fails; the table names the tests that failed.
#
# The bugs are slips of the UART's line (bit order, a bit's length, the sample point, the synchroniser, the divisor's
# idle point, the stop bit, the overrun), of the host interface (TREADY, TLAST, the words below the top, the pops, the
# handshakes), of the stalls and of the loads that pop, in the model and in the core's path to it.
# Usage: tests/ie_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or its sed plants nothing. JOBS sets make's -j (8).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-8}
SCRATCH=${IE_TEETH_DIR:-$SIM/../build-scratch}
mkdir -p "$SCRATCH"
W=$(mktemp -d -p "$SCRATCH" ie-teeth.XXXXXX); trap 'rm -rf "$W"' EXIT
(cd "$SIM" && tar cf - --exclude=./build --exclude=./results .) | (cd "$W" && tar xf -)
B=build/clang-release
TESTS="test_ingress_egress test_rtl_stream"
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
H=src/units/ingress_egress.h
# name ~ file ~ sed expression [~ file ~ sed expression ...]
BUGS=(
  "tx: the most significant bit first~$H~s/tx_bits = uint16_t(0x100 | b);/tx_bits = uint16_t(0x100 | uint8_t((b * 0x0202020202ULL \& 0x010884422010ULL) % 1023));/"
  "tx: a stop bit of 0~$H~s/tx_bits = uint16_t(0x100 | b);/tx_bits = uint16_t(b);/"
  "tx: no back-to-back frames (an idle cycle between)~$H~s/const bool pop = (!tx_busy || frame_ends) \&\& tx_n > 0;/const bool pop = !tx_busy \&\& tx_n > 0;/"
  "tx: the start bit a cycle long~$H~s/tx_cycles_left = tx_bit_divisor = uint16_t(divisor);/tx_bit_divisor = uint16_t(divisor); tx_cycles_left = uint16_t(divisor + 1);/"
  "tx: the live divisor mid-frame~$H~s/tx_cycles_left = tx_bit_divisor; tx_bits >>= 1;/tx_cycles_left = uint16_t(divisor); tx_bits >>= 1;/"
  "tx: a divisor stored in the pop's cycle taken by its frame~$H~s/case UART_DIVISOR: divisor = v \& 0xFFFF; return 1;/case UART_DIVISOR: divisor = v \& 0xFFFF; if (tx_busy \&\& tx_bits == (0x100 | (tx_bits \& 0xFF)) \&\& tx_cycles_left == tx_bit_divisor) tx_cycles_left = tx_bit_divisor = uint16_t(divisor); return 1;/"
  "tx: UART_TX stalls with a place free~$H~s/return (w == UART_TX \&\& tx_n >= FIFO) ||/return (w == UART_TX \&\& tx_n >= FIFO - 1) ||/"
  "tx: a stalled UART_TX store released a cycle late (not in the cycle after the pop)~$H~s/return (w == UART_TX \&\& tx_n >= FIFO) ||/return (w == UART_TX \&\& (tx_n >= FIFO || (tx_n == FIFO - 1 \&\& tx_busy \&\& tx_cycles_left == tx_bit_divisor \&\& tx_bits >= 0x100))) ||/"
  "tx: UART_STATUS counts the byte on the line~$H~s/case UART_STATUS: v = tx_n | rx_n << 8/case UART_STATUS: v = (tx_n + tx_busy) | rx_n << 8/"
  "rx: the most significant bit first~$H~s/rx_data = uint8_t((rx_line ? 0x80 : 0) | rx_data >> 1);/rx_data = uint8_t((rx_line ? 1 : 0) | rx_data << 1);/"
  "rx: the first sample a cycle early~$H~s/rx_cycles_left = divisor + (divisor >> 1);  /rx_cycles_left = divisor + (divisor >> 1) - 1;/"
  "rx: the first sample a cycle late~$H~s/rx_cycles_left = divisor + (divisor >> 1);  /rx_cycles_left = divisor + (divisor >> 1) + 1;/"
  "rx: sampling at a bit's start~$H~s/rx_cycles_left = divisor + (divisor >> 1);  /rx_cycles_left = divisor;                 /"
  "rx: one synchronising flip-flop~$H~s/rx_line_last = rx_line; rx_line = rx_meta; rx_meta = pin;/rx_line_last = rx_line; rx_line = pin; rx_meta = pin;/"
  "rx: the live divisor mid-frame~$H~s/rx_bit++; rx_cycles_left = rx_bit_divisor;/rx_bit++; rx_cycles_left = divisor;/"
  "rx: the stop bit unchecked~$H~s/if (frame_ends \&\& !rx_line) { framing = true; framing_at = t; }/if (false) { framing = true; framing_at = t; }/"
  "rx: a full FIFO's frame dropped without rx_overrun~$H~s/{ rx_overrun = true; overrun_at = t; }/{ overrun_at = t; }/"
  "rx: no overrun (a 17th byte taken)~$H~s/else if (frame_ends \&\& rx_n >= FIFO) {/else if (frame_ends \&\& rx_n >= FIFO \&\& false) {/"
  "rx: a frame started by a low level~$H~s/const bool frame_starts = !rx_busy \&\& rx_line_last \&\& !rx_line;/const bool frame_starts = !rx_busy \&\& !rx_line;/"
  "rx: a clear wins over a framing error of its cycle~$H~s/if ((v >> 17 \& 1) \&\& framing_at != t) framing = false;/if (v >> 17 \& 1) framing = false;/"
  "rx: framing_error cleared by bit 16~$H~s/if ((v >> 17 \& 1) \&\& framing_at != t) framing = false;/if ((v >> 16 \& 1) \&\& framing_at != t) framing = false;/"
  "rx: UART_RX pops a byte that entered in the load's cycle~$H~s/if ((stepped_at == t ? rx_n_start : rx_n) > 0)/if (rx_n > 0)/"
  "the reset divisor off by one~$H~s/        divisor = reset_divisor;/        divisor = reset_divisor + 1;/"
  "the events uart_rx and uart_tx swapped~$H~s/ev.live(EV_UART_RX, rx_n > 0);/ev.live(EV_UART_TX, rx_n > 0);/; s/ev.live(EV_UART_TX, tx_n < FIFO);/ev.live(EV_UART_RX, tx_n < FIFO);/"
  "stream: lvds_in_tready 1 in the first cycle after the reset~$H~s/o.lvds_in_tready = in_ready_ff \&\& in_n < STREAM_DEPTH;/o.lvds_in_tready = in_n < STREAM_DEPTH;/"
  "stream: lvds_in_tready with one place too many~$H~s/o.lvds_in_tready = in_ready_ff \&\& in_n < STREAM_DEPTH;/o.lvds_in_tready = in_ready_ff \&\& in_n <= STREAM_DEPTH;/"
  "stream: TLAST lost on a send~$H~s/b.last = w == STREAM_SEND_LAST;/b.last = false;/"
  "stream: TLAST lost on a beat in~$H~s/b.last = in.lvds_in_tlast;/b.last = false;/"
  "stream: STREAM_OUT_W0-W2 not cleared after a send~$H~s/^            out_low\[0\] = out_low\[1\] = out_low\[2\] = 0;\$//"
  "stream: a low word stored one word off~$H~s/if (unsigned(w - STREAM_OUT_W0) < top_word()) out_low\[w - STREAM_OUT_W0\] = v;/if (unsigned(w - STREAM_OUT_W0) < top_word()) out_low[(w - STREAM_OUT_W0 + 1) % 3] = v;/"
  "stream: the top word's bits past TDATA sent~$H~s/k == top ? v \& top_mask() : 0;/k == top ? v : 0;/"
  "stream: a pop on STREAM_IN_W0 instead of STREAM_RECEIVE alone~$H~s/bool load_pops(int w) const { return w == UART_RX || w == STREAM_RECEIVE; }/bool load_pops(int w) const { return w == UART_RX || w == STREAM_RECEIVE || w == STREAM_IN_W0; }/; s/} else if (w == STREAM_RECEIVE) {/} else if (w == STREAM_RECEIVE || w == STREAM_IN_W0) {/"
  "stream: STREAM_RECEIVE pops a beat that entered in the load's cycle~$H~s/if ((stepped_at == t ? in_n_start : in_n) > 0)/if (in_n > 0)/"
  "stream: a send stalls with a place free~$H~s/((w == STREAM_SEND || w == STREAM_SEND_LAST) \&\& out_n >= STREAM_DEPTH);/((w == STREAM_SEND || w == STREAM_SEND_LAST) \&\& out_n >= STREAM_DEPTH - 1);/"
  "stream: the head leaves without TREADY~$H~s/if (o.lvds_out_tvalid \&\& in.lvds_out_tready) {/if (o.lvds_out_tvalid) {/"
  "stream: stream_last at bit 3~$H~s/in_q\[in_head\].last ? 1u << 2 : 0; }/in_q[in_head].last ? 1u << 3 : 0; }/"
  "stream: stream_last the newest beat's~$H~s/uint32_t status_bits(int64_t) const { return in_n > 0 \&\& in_q\[in_head\].last/uint32_t status_bits(int64_t) const { return in_n > 0 \&\& in_q[(in_head + in_n - 1) % STREAM_DEPTH].last/"
  "stream: the events stream_in and stream_out swapped~$H~s/ev.live(EV_STREAM_IN, in_n > 0);/ev.live(EV_STREAM_OUT, in_n > 0);/; s/ev.live(EV_STREAM_OUT, out_n < STREAM_DEPTH);/ev.live(EV_STREAM_IN, out_n < STREAM_DEPTH);/"
  "the core tells the slot of no load that pops~src/units/tile_ctl.h~s/if (own_bit(n, Win::R_POPS, w)) c.put(u.periph, CoreOp::load(n, w));//"
  "the slot applies a load's pop a cycle late (before the step it follows)~src/units/periph_slot.cpp~s/s.visit(n, \[\&\](auto\& p) { p.load_effect(t - 1, int(w)); });/s.visit(n, [\&](auto\& p) { p.load_effect(t, int(w)); });/"
)
miss=0
printf '%-72s %s\n' "planted bug" "caught by"
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
  printf '%-72s %s\n' "$what" "$r"
  case "$r" in " -"|" (no build"*) miss=1 ;; esac
done
[ $miss = 0 ] && echo "teeth: every planted bug caught" || echo "teeth: a planted bug went uncaught"
exit $miss
