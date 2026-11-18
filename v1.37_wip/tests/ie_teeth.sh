#!/bin/bash
# ie_teeth.sh: planted bugs, each of which Ingress/Egress's tests must catch. Not in make check (a rebuild per bug).
# The tree is copied into a scratch directory (the worktree's build-scratch/, or IE_TEETH_DIR), each bug is planted
# there by sed, the tests rebuilt (clang release) and run: test_ingress_egress (the unit, from 17.6 and the RTL unit's
# timing), test_ingress_link and test_packet_vectors (its incoming queues on the black box's link) and test_rtl_stream
# (the UART through the master's core on the chip). A bug is caught when a test fails; the table names the tests that
# failed.
#
# The bugs (v1.37's stage 3, the INTEGRATOR): slips of the UART's line (bit order, a bit's length, the sample point, the
# synchroniser, the divider's idle point, the stop bit, the overrun), of its control (17.6.9: the reset, the enables,
# the stores dropped), of 17.6.1's words and bits, of the incoming queues (the landing a word a cycle, the room), of
# Egress (17.6.8: the count, the credit, the link packets, the errors), of the UB checks, of the stalls and of the loads
# that pop, in the model and in the core's path to it.
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
TESTS="test_ingress_egress test_ingress_link test_packet_vectors test_rtl_stream"
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
  "tx: no back-to-back characters (an idle cycle between)~$H~s/const bool pop = (!tx_busy || frame_ends) \&\& tx_n > 0/const bool pop = !tx_busy \&\& tx_n > 0/"
  "tx: the start bit a cycle long~$H~s/tx_cycles_left = tx_bit_divisor = uint16_t(uart_control \& UC_DIVIDER);/tx_bit_divisor = uint16_t(uart_control \& UC_DIVIDER); tx_cycles_left = uint16_t(tx_bit_divisor + 1);/"
  "tx: the live divider mid-character~$H~s/tx_cycles_left = tx_bit_divisor; tx_bits >>= 1;/tx_cycles_left = uint16_t(uart_control \& UC_DIVIDER); tx_bits >>= 1;/"
  "tx: UART_TX stalls with a place free~$H~s/(uart_control \& UC_ENABLE_WRITE) \&\& tx_n >= FIFO; }/(uart_control \& UC_ENABLE_WRITE) \&\& tx_n >= FIFO - 1; }/"
  "tx: STATUS counts the character on the line~$H~s/s |= tx_n << 8 | rx_n << 16;/s |= (tx_n + tx_busy) << 8 | rx_n << 16;/"
  "tx: enable_write 0 does not hold the TX FIFO~$H~s/\&\& tx_n > 0 \&\& (uart_control \& UC_ENABLE_WRITE);/\&\& tx_n > 0;/"
  "tx: a store with enable_write 0 kept~$H~s/if (!uart_on() || !(uart_control \& UC_ENABLE_WRITE)) return 1;/if (!uart_on()) return 1;/"
  "tx: a store while the UART is in reset kept~$H~s/if (!uart_on() || !(uart_control \& UC_ENABLE_WRITE)) return 1;/if (uart_on() \&\& !(uart_control \& UC_ENABLE_WRITE)) return 1;/"
  "rx: the most significant bit first~$H~s/rx_data = uint8_t((rx_line ? 0x80 : 0) | rx_data >> 1);/rx_data = uint8_t((rx_line ? 1 : 0) | rx_data << 1);/"
  "rx: the first sample a cycle early~$H~s/rx_cycles_left = divider + (divider >> 1);/rx_cycles_left = divider + (divider >> 1) - 1;/"
  "rx: the first sample a cycle late~$H~s/rx_cycles_left = divider + (divider >> 1);/rx_cycles_left = divider + (divider >> 1) + 1;/"
  "rx: sampling at a bit's start~$H~s/rx_cycles_left = divider + (divider >> 1);/rx_cycles_left = divider;/"
  "rx: one synchronising flip-flop~$H~s/rx_line_last = rx_line; rx_line = rx_meta; rx_meta = pin;/rx_line_last = rx_line; rx_line = pin; rx_meta = pin;/"
  "rx: the live divider mid-character~$H~s/rx_bit++; rx_cycles_left = rx_bit_divisor;/rx_bit++; rx_cycles_left = divider;/"
  "rx: the stop bit unchecked~$H~s/if (frame_ends \&\& !rx_line) framing = true;/if (false) framing = true;/"
  "rx: a full FIFO's character dropped without rx_overrun~$H~s/else if (frame_ends \&\& rx_n >= FIFO) rx_overrun = true;/else if (frame_ends \&\& rx_n >= FIFO) {}/"
  "rx: no overrun (a 17th character taken)~$H~s/else if (frame_ends \&\& rx_n >= FIFO) rx_overrun = true;/else if (frame_ends \&\& rx_n > FIFO) rx_overrun = true;/"
  "rx: a character started by a low level~$H~s/const bool frame_starts = !rx_busy \&\& rx_line_last \&\& !rx_line/const bool frame_starts = !rx_busy \&\& !rx_line/"
  "rx: enable_read 0 does not stop the line in~$H~s/\&\& !rx_line \&\& (uart_control \& UC_ENABLE_READ);/\&\& !rx_line;/"
  "rx: UART_RX pops a character that entered in the load's cycle~$H~s/(stepped_at == t ? rx_n_start : rx_n) > 0/rx_n > 0/"
  "uart: the events uart_rx and uart_tx swapped~$H~s/ev.live(EV_UART_RX, uart_on() \&\& rx_n > 0);/ev.live(EV_UART_TX, uart_on() \&\& rx_n > 0);/; s/ev.live(EV_UART_TX, uart_on() \&\& tx_n < FIFO);/ev.live(EV_UART_RX, uart_on() \&\& tx_n < FIFO);/"
  "uart: uart_tx read 1 in the UART's reset~$H~s/ev.live(EV_UART_TX, uart_on() \&\& tx_n < FIFO);/ev.live(EV_UART_TX, tx_n < FIFO);/"
  "uart: a store of 0 does not reset it~$H~s/if (v == 0) uart_reset();//"
  "uart: the enables swapped~$H~s/UC_ENABLE_READ = 1u << 16, UC_ENABLE_WRITE = 1u << 17/UC_ENABLE_READ = 1u << 17, UC_ENABLE_WRITE = 1u << 16/"
  "uart: a divider of 0 not caught (UB)~$H~s/if (v != 0 \&\& (v \& UC_DIVIDER) == 0)/if (false)/"
  "words: PARAMS' version 0 (v1.36's)~$H~s/static constexpr int VERSION = 1;/static constexpr int VERSION = 0;/"
  "words: the queues' words at 80 and 81 (II's)~$H~s/FOREGROUND_WORDS = 64, BACKGROUND_WORDS = 65/FOREGROUND_WORDS = 80, BACKGROUND_WORDS = 81/"
  "words: background_ready at STATUS [4] (II's)~$H~s/ST_BACKGROUND_READY = 1u << 3/ST_BACKGROUND_READY = 1u << 4/"
  "queues: a link packet's words all landing in the cycle it is taken~$H~s/if (landing_n == 1) land(t);/if (landing_n == 1) for (unsigned k = 0; k < link_next.length; k++) land(t);/"
  "queues: the room of the words landed alone~$H~s/return queue_words - queue\[q\].held - queue\[q\].landing;/return queue_words - queue[q].held;/"
  "egress: EGRESS_COUNT counting down as words leave~$H~s/eg_last\[at\] = --egress_count == 0;/eg_last[at] = egress_count == 1;/; s/egress_words_out += n; egress_packets++;/egress_words_out += n; egress_packets++; egress_count -= n;/"
  "egress: a credit every 512 words~$H~s/EGRESS_CREDIT_WORDS = 256/EGRESS_CREDIT_WORDS = 512/"
  "egress: a frame's last word not ending its link packet~$H~s/if (eg_last\[(eg_head + k) % EGRESS_QUEUE_WORDS\]) return k + 1;/if (eg_last[(eg_head + k) % EGRESS_QUEUE_WORDS] \&\& k + 1 == n) return k + 1;/"
  "egress: a word into a full queue kept~$H~s/if (eg_n == EGRESS_QUEUE_WORDS || egress_count == 0)/if (egress_count == 0)/"
  "egress: a word with the count at 0 no error~$H~s/if (eg_n == EGRESS_QUEUE_WORDS || egress_count == 0)/if (eg_n == EGRESS_QUEUE_WORDS)/"
  "egress: a store to EGRESS_COUNT while not 0 not caught (UB)~$H~s/if (egress_count != 0)/if (false)/"
  "egress: a frame's words from two sources not caught (UB, invariant 35)~$H~s/if (egress_src >= 0 \&\& src != egress_src)/if (false)/"
  "slot: INGRESS_QUEUE_WORDS not given to the unit~src/units/periph_slot.cpp~s/ie->queue_words = c.ingress_queue_words;/(void)c;/"
  "bus: UART_RX not an own word of block 1~src/units/register_bus.h~s/if (w == 30 || w == 31) return {Target::PERIPH_OWN/if (w == 30) return {Target::PERIPH_OWN/"
  "slot: a low word's stall not published~src/units/periph_slot.cpp~s/if (s.store_stalls(t, n, w)) bits |= 1u << (2 \* Win::K_STALL + i);//"
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
