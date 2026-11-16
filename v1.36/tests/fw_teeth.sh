#!/bin/bash
# fw_teeth.sh: planted bugs, each of which the chip's tests of meshsim's firmware must catch. Not in make check (a
# firmware build and the chip's tests per bug; about ten minutes). fw/ is copied into a scratch directory, each bug is
# planted there by sed, the images rebuilt (fw/build.sh, its parts sort, cc, replay and tracer, the RISC-V toolchain on
# PATH or named by RISCV_PREFIX) and the chip's tests (clang release, built here first) run on them (FW_IMAGES):
# test_sort sort_16_kv32, test_memory and test_segment segment_small. A bug is caught when a test fails or stops; the
# table names the tests that failed.
#
# The bugs are slips in each program v1.36 carries over from v1.35 (cc/cc1.c, replay/replay_ctl.c, tracer/tracer_ctl.c,
# crt0.S, the sort's port and schedule), in the port layer (port/mesh.h), and in the two programs new in v1.36
# (cc/memory.c, cc/cc_mem.c).
# Usage: tests/fw_teeth.sh [FILTER] (from anywhere): only the bugs whose name contains FILTER; exit 1 if a bug goes
# uncaught or its sed plants nothing. JOBS sets make's -j (8); TEETH_DIR the scratch directory (default: a new
# temporary one, removed at the end).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-8}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
B=build/clang-release
(cd "$SIM" && make -s CC_NAME=clang MODE=release -j"$JOBS" $B/test_sort $B/test_memory $B/test_segment > "$W/make.log" 2>&1) \
  || { cat "$W/make.log"; echo "teeth: the tests didn't build"; exit 2; }
build() {   # the images of fw/ as $W/fw holds it, into $W/out
  (cd "$W" && PARTS="sort cc replay tracer" OUT="$W/out" bash fw/build.sh --build-only > "$W/build.log" 2>&1) || return 1
  mkdir -p "$W/out/sort" && cp "$SIM/fw/images/sort/sort.keys" "$W/out/sort/"
}
run() {   # prints the failing tests, "-" if none
  local f="" t rc
  for t in "test_sort sort_16_kv32" "test_memory" "test_segment segment_small"; do
    (cd "$SIM" && FW_IMAGES="$W/out" timeout 300 ./$B/$t > "$W/run.out" 2>&1) 2> /dev/null
    rc=$?
    [ $rc = 0 ] && continue
    f="$f ${t%% *}"
    [ $rc -ge 124 ] && f="$f(died:$rc)"   # a timeout, or a FATAL outside a test's child
  done
  echo "${f:- -}"
}
fresh() { rm -rf "$W/fw" && cp -r "$SIM/fw" "$W/fw" && rm -rf "$W/fw/build"; }
fresh; build || { cat "$W/build.log"; echo "teeth: the pristine firmware didn't build"; exit 2; }
r=$(run)
[ "$r" = " -" ] || { echo "teeth: the pristine firmware fails: $r"; exit 2; }
echo "pristine: all pass"
# name ~ file ~ sed expression
BUGS=(
  "cc1: a result's reply on no counter~cc/cc1.c~s/send_to(ROUTER_HDR_W(who, MODE_WRITE, REQ_REPLY_CTR(w0)), REQ_ARG(w0) << 4);/send_to(ROUTER_HDR_W(who, MODE_WRITE, CTR_NONE), REQ_ARG(w0) << 4);/"
  "cc1: a result's reply at the word above its address~cc/cc1.c~s/REQ_REPLY_CTR(w0)), REQ_ARG(w0) << 4);/REQ_REPLY_CTR(w0)), (REQ_ARG(w0) << 4) + 4);/"
  "cc1: the load's COUNT on no counter~cc/cc1.c~s/STREAM_COUNT_W(1, SIZE_128, CTR_LOAD, MODE_COUNT, d)/STREAM_COUNT_W(1, SIZE_128, CTR_NONE, MODE_COUNT, d)/"
  "cc1: the image's data from DMEM's byte 0~cc/cc1.c~s/REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets/REGION_DMEM, STREAM_COUNT_W(b->dmem_packets/"
  "cc1: one release fewer in CC_PHASE~cc/cc1.c~s/} while (reps-- > 1);/} while (reps-- > 2);/"
  "cc1: a report's number read from v1.35's place~cc/cc1.c~s/#define REQ_SEQ(w0)  REQ_NUMBER(w0)/#define REQ_SEQ(w0)  (((w0) >> 21) \& 0x7FFu)/"
  "cc1: CC_WAIT without the LOADs after the results~cc/cc1.c~s/if (mbox\[6\] >= n \&\& mbox\[4\] >= TILES_LOADING + n) break;/if (mbox[6] >= n) break;/"
  "replay_ctl: a wait that keeps only its last wake's events~replay/replay_ctl.c~s/do { __asm__ volatile (\"wfi\" ::: \"memory\"); e |= tile->EVENTS; } while ((e \& ev) != ev);/do { __asm__ volatile (\"wfi\" ::: \"memory\"); e = tile->EVENTS; } while ((e \& ev) != ev);/"
  "replay_ctl: no ARMED~replay/replay_ctl.c~/REQ_OP_ARMED << 10);/d"
  "replay_ctl: STREAMS_ARMED before the stream counter is armed~replay/replay_ctl.c~s/if (n) tile->WRITE_COUNTER\[RL_CTR_STREAMS - 1\] = n;/request_send(0, 0, 0, REQ_TARGET(TARGET_CC3) | REQ_OP_STREAMS_ARMED << 10); if (n) tile->WRITE_COUNTER[RL_CTR_STREAMS - 1] = n;/"
  "replay_ctl: the read log's size from the code's field~replay/replay_ctl.c~s/hdr\[RL_H_RLOG_N\] = record\[4\] >> 16;/hdr[RL_H_RLOG_N] = record[4] \& 0xFFFFu;/"
  "tracer_ctl: ARMED taken for STREAMS_ARMED~tracer/tracer_ctl.c~s/if (t < 64 \&\& REQ_OP(w0) == REQ_OP_ARMED) armed_set/if (t < 64 \&\& REQ_OP(w0) == REQ_OP_ARMED) streams_set/"
  "tracer_ctl: the streams without STREAMS_ARMED~tracer/tracer_ctl.c~s/} else if (armed(streams_set, tn)) {/} else if (1) {/"
  "tracer_ctl: a state piece to tile n + 1 without its ARMED~tracer/tracer_ctl.c~s/(last || armed(armed_set, to0))/(last || armed(armed_set, tn))/"
  "tracer_ctl: an advice refill one short~tracer/tracer_ctl.c~s/    adv_next += n;/    adv_next += n - 1;/"
  "tracer_ctl: the streams on the record's counter~tracer/tracer_ctl.c~s/dma(q, c, tn, sram_addr(base + (sent\[q\] << 2)), RL_CTR_STREAMS);/dma(q, c, tn, sram_addr(base + (sent[q] << 2)), RL_CTR_STATE);/"
  "crt0: the reply's counter not armed~crt0.S~/sw    t0, X0REL(OFF_WRITE_COUNTER_1 + 4 \* (CTR_REPLY - 1))(zero)/d"
  "crt0: no jump to the reset PC~crt0.S~s/    jr    t0/4:  j     4b/"
  "crt0: the result as a LOAD~crt0.S~s/#define REQ_EXIT  ((1 << 10) \* REQ_OP_RESULT/#define REQ_EXIT  ((1 << 10) * REQ_OP_LOAD/"
  "port: the Tracer's state_ready at code_ready's bit~port/mesh.h~s/#define TR_EV_SVC_STATE    (1u << 5)/#define TR_EV_SVC_STATE    (1u << 6)/"
  "port: all_arrived at the watchdog's bit~port/mesh.h~s/#define CC1_EV_ALL_READY   (1u << 2)/#define CC1_EV_ALL_READY   (1u << 3)/"
  "port: RELEASE one word low~port/mesh.h~s/    MESH_RW READY_EXPECT\[2\];/    MESH_RW READY_EXPECT[1];/"
  "port: DMA_HDR's count at the queue~port/mesh.h~s/(((uint32_t)(count) \& 0xFFFu) << 15) | (((uint32_t)(queue) \& 0x3u) << 27)/(((uint32_t)(count) \& 0xFFFu) << 16) | (((uint32_t)(queue) \& 0x3u) << 27)/"
  "sort: the format taken for the records~sort/port/port.c~s/S.n_in = ARGS\[ARG_RECORDS\];/S.n_in = ARGS[ARG_FORMAT];/"
  "sort: one layer of K1 fewer~sort/program/sw/main.c~s/for (unsigned layer = 0; layer < LAYERS; layer++) phase(CMD_K1, layer);/for (unsigned layer = 1; layer < LAYERS; layer++) phase(CMD_K1, layer);/"
  "sort: the result's reply on no counter~sort/port/port.c~s/REQ_CC1(REQ_OP_RESULT, CTR_REPLY, REPLY_WORD(DST_CC1) >> 4)/REQ_CC1(REQ_OP_RESULT, CTR_NONE, REPLY_WORD(DST_CC1) >> 4)/"
  "memory: a READ's reply not sent~cc/memory.c~s/    if (r->reply_counter != 0) {/    if (0) {/"
  "memory: a write job one beat short~cc/memory.c~s/rw_engine->RW_CONFIG = RW_CONFIG_KIND(RW_KIND_WRITE) | RW_CONFIG_BEATS(r->beats);/rw_engine->RW_CONFIG = RW_CONFIG_KIND(RW_KIND_WRITE) | RW_CONFIG_BEATS(r->beats - 1);/"
  "memory: the part's counter named wrong in the reply~cc/memory.c~s/rw_engine->RW_REPLY_SEND = BUFFER_COUNTER;/rw_engine->RW_REPLY_SEND = BUFFER_COUNTER + 1;/"
  "cc_mem: a beat's words in the wrong order~cc/cc_mem.c~s/write128(word | 3, word | 2, word | 1, word);/write128(word, word | 1, word | 2, word | 3);/"
  "cc_mem: the credits' requests in one slot~cc/cc_mem.c~s/write_request(0, 0, address_of(mem_summary.answers\[r\]));/write_request(0, 0, address_of(mem_summary.answers[first]));/"
  "cc_mem: a READ's data on the reply's counter~cc/cc_mem.c~s/| MEMORY_BEATS(beats) | MEMORY_DATA_COUNTER(1));/| MEMORY_BEATS(beats) | MEMORY_DATA_COUNTER(2));/"
)
bad=0
printf '%-74s %s\n' "bug" "failing tests"
for entry in "${BUGS[@]}"; do
  IFS='~' read -r name file expr <<< "$entry"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  fresh
  sed -i "$expr" "$W/fw/$file"
  if cmp -s "$W/fw/$file" "$SIM/fw/$file"; then printf '%-74s %s\n' "$name" "NOT PLANTED (the sed matched nothing)"; bad=1; continue; fi
  if ! build; then printf '%-74s %s\n' "$name" "caught by the build"; continue; fi
  r=$(run)
  [ "$r" = " -" ] && { r="UNCAUGHT"; bad=1; }
  printf '%-74s %s\n' "$name" "$r"
done
[ $bad = 0 ] || { echo "teeth: FAILED"; exit 1; }
echo "teeth: every bug caught"
