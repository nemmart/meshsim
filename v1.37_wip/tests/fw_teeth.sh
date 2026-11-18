#!/bin/bash
# fw_teeth.sh: planted bugs, each of which the chip's tests of meshsim's firmware must catch. Not in make check (a
# firmware build and the chip's tests per bug; about ten minutes). fw/ is copied into a scratch directory, each bug is
# planted there by sed, the images rebuilt (fw/build.sh, its parts sort, cc, replay and tracer, the RISC-V toolchain on
# PATH or named by RISCV_PREFIX) and the chip's tests (clang release, built here first) run on them (FW_IMAGES):
# test_sort sort_16_kv32, test_memory and test_segment's generated segments. A bug is caught when a test fails or
# stops; the table names the tests that failed. Not planted: a handshake sent early or not waited for (ARMED,
# STREAMS_ARMED), whose race these runs' timings never provoke, so that no test can catch it; likewise the reply sent
# without waiting for the tracer tile's record (30.6), which lands long before the master has every result. (WORKER4's unplanted bug in
# cc/master.c, the count of words loaded with a transfer in flight, went with the thresholds: WORKER7.) A bug that
# breaks one of the forward count's undefined cases is caught by the simulator's stop, "died".
#
# The bugs are slips in each program v1.36 carries over from v1.35 (cc/cc1.c, replay/replay_ctl.c, tracer/tracer_ctl.c,
# crt0.S, the sort's port and schedule), in the port layer (port/mesh.h), and in the two programs new in v1.36
# (cc/memory.c, cc/cc_mem.c). v1.37: and in the master tile's program for the host's frames (cc/master.c, with
# include/host.h), which test_master alone runs: a bug there is run against test_master, the others as before; a bug
# in the tracer tile's program against both, since its result carries its last record to v1.37's master alone (30.6).
# Stage 5 (the INTEGRATOR): the reply (30.7) and the tracer tile's result (30.6), checked at the host's end.
# Usage: tests/fw_teeth.sh [FILTER] (from anywhere): only the bugs whose name FILTER, an extended regular expression,
# matches (e.g. '^(cc1|crt0):'); exit 1 if a bug goes
# uncaught or its sed plants nothing. JOBS sets make's -j (8); TEETH_DIR the scratch directory (default: a new
# temporary one, removed at the end).
set -u
SIM=$(cd "$(dirname "$0")/.." && pwd)
FILTER=${1:-}
JOBS=${JOBS:-8}
if [ -n "${TEETH_DIR:-}" ]; then W=$TEETH_DIR; rm -rf "$W"; mkdir -p "$W"; else W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; fi
B=build/clang-release
(cd "$SIM" && make -s CC_NAME=clang MODE=release -j"$JOBS" $B/test_sort $B/test_memory $B/test_segment $B/test_master > "$W/make.log" 2>&1) \
  || { cat "$W/make.log"; echo "teeth: the tests didn't build"; exit 2; }
build() {   # the images of fw/ as $W/fw holds it, into $W/out
  (cd "$W" && PARTS="sort cc replay tracer" OUT="$W/out" bash fw/build.sh --build-only > "$W/build.log" 2>&1) || return 1
  mkdir -p "$W/out/sort" && cp "$SIM/fw/images/sort/sort.keys" "$W/out/sort/"
}
run() {   # run [FILE]: prints the failing tests, "-" if none; a bug in v1.37's master program is test_master's alone
  local f="" t rc
  local tests=("test_sort sort_16_kv32" "test_memory" "test_segment segment_")
  case ${1:-} in cc/master.c|include/host.h) tests=("test_master master_") ;; ''|tracer/tracer_ctl.c) tests+=("test_master master_") ;; esac
  for t in "${tests[@]}"; do
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
  "cc1: the tracer tile's result unanswered~cc/cc1.c~s/REQ_OP_RESULT \&\& who <= DST_CC3/REQ_OP_RESULT \&\& who < 64/"
  "cc1: the load's COUNT on no counter~cc/cc1.c~s/STREAM_COUNT_W(1, SIZE_128, CTR_LOAD, MODE_COUNT, d)/STREAM_COUNT_W(1, SIZE_128, CTR_NONE, MODE_COUNT, d)/"
  "cc1: the image's data from DMEM's byte 0~cc/cc1.c~s/REGION_DMEM + DMEM_FIRST, STREAM_COUNT_W(b->dmem_packets/REGION_DMEM, STREAM_COUNT_W(b->dmem_packets/"
  "cc1: one release fewer in CC_PHASE~cc/cc1.c~s/} while (reps-- > 1);/} while (reps-- > 2);/"
  "cc1: a report's number read from v1.35's place~cc/cc1.c~s/#define REQ_SEQ(w0)  REQ_NUMBER(w0)/#define REQ_SEQ(w0)  (((w0) >> 21) \& 0x7FFu)/"
  "cc1: CC_WAIT without the LOADs after the results~cc/cc1.c~s/if (mbox\[6\] >= n \&\& mbox\[4\] >= TILES_LOADING + n) break;/if (mbox[6] >= n) break;/"
  "replay_ctl: a wait that keeps only its last wake's events~replay/replay_ctl.c~s/do { __asm__ volatile (\"wfi\" ::: \"memory\"); e |= tile->EVENTS; } while ((e \& ev) != ev);/do { __asm__ volatile (\"wfi\" ::: \"memory\"); e = tile->EVENTS; } while ((e \& ev) != ev);/"
  "replay_ctl: no ARMED~replay/replay_ctl.c~/REQ_OP_ARMED << 10);/d"
  "replay_ctl: the totals' counter not armed~replay/replay_ctl.c~/        tile->WRITE_COUNTER\[RL_CTR_TOTALS - 1\] = 2;/d"
  "replay_ctl: the read log's size from the code's field~replay/replay_ctl.c~s/hdr\[RL_H_RLOG_N\] = record\[4\] >> 16;/hdr[RL_H_RLOG_N] = record[4] \& 0xFFFFu;/"
  "tracer_ctl: ARMED taken for STREAMS_ARMED~tracer/tracer_ctl.c~s/if (t < 64 \&\& REQ_OP(w0) == REQ_OP_ARMED) armed_set/if (t < 64 \&\& REQ_OP(w0) == REQ_OP_ARMED) streams_set/"
  "tracer_ctl: window 0's start, words 2-66, a row low~tracer/tracer_ctl.c~s/sram_addr(trec + 8));/sram_addr(trec + 4));/"
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
  "master: the Tracer's copy asked for half its pages~cc/master.c~s/words_mine, 2 \* pages_tracer, final, copy, start/words_mine, pages_tracer, final, copy, start/"
  "master: segment s's run as a new frame's~cc/master.c~s/run_config(lru, 1, 1, image_depth, UPD_ROOT_COPY), words_mine/run_config(lru, 0, 1, image_depth, UPD_ROOT_COPY), words_mine/"
  "master: the cycle-0 image from an empty root~cc/master.c~s/run_config(lru, 0, 0, image_depth, UPD_ROOT_COPY), data_words64/run_config(lru, 0, 0, image_depth, UPD_ROOT_EMPTY), data_words64/"
  "master: word 66 without the table's root~cc/master.c~s/uint32_t w1 = (image_depth >> 1 \& 1) | root_table << 1;/uint32_t w1 = (image_depth >> 1 \& 1);/"
  "master: the advice count one short~cc/master.c~s/write128(advice_values, table_k, table_p3, table_p1);/write128(advice_values - 1, table_k, table_p3, table_p1);/"
  "master: record s + 1 kept for record s~cc/master.c~s/        skip(RECORD_WORDS \* S);/        skip(RECORD_WORDS * (S + 1));/"
  "master: log s - 1 kept for log s~cc/master.c~s/if (k < S) before += rows;/if (k + 1 < S) before += rows;/"
  "master: the page map's last row not written back~cc/master.c~/^        map_flush();$/d"
  "master: page 0 not marked~cc/master.c~s/map_row\[0\] = 1; map_row\[1\] = 0;/map_row[0] = 0; map_row[1] = 0;/"
  "master: the tracer tile's result unanswered~cc/master.c~s/op == OP_RESULT \&\& who <= TILE_TRACER/op == OP_RESULT \&\& who < TILE_TRACER/"
  "master: an update frame's count one long~cc/master.c~s/if (frame_left) forward(frame_left);            \/\* a frame of its header/if (frame_left) forward(frame_left + 1);            \/* a frame of its header/"
  "master: frame 9's count one short~cc/master.c~s/if (frame_left) forward(frame_left);        \/\* one count for the three runs/if (frame_left) forward(frame_left - 1);        \/* one count for the three runs/"
  "master: an update frame dropped, not sent~cc/master.c~s/if (frame_left) forward(frame_left);            \/\* a frame of its header/if (frame_left) forward(HL_FORWARD_DISCARD | frame_left);            \/* a frame of its header/"
  "master: the drops sent to the Updater~cc/master.c~s/    forward(HL_FORWARD_DISCARD | drop);/    forward(drop);/"
  "master: drops in a row as two counts, the second over the first~cc/master.c~s/static void skip(uint32_t n) { drop += n; frame_left -= n; }/static void skip(uint32_t n) { drop += n; frame_left -= n; drop_now(); }/"
  "master: a copy started without the count's 0~cc/master.c~s/        if ((host_link->DMA_FORWARD_COUNT \& HL_FORWARD_COUNT) == 0) {/        if (1) {/"
  "master: a copy's landing not waited for~cc/master.c~/^        wait_counter(CTR_LINK);$/d"
  "master: a run started without the last one's done~cc/master.c~/^    if (run_out) run_wait();$/d"
  "master: a header frame's whole row left unread not seen~cc/master.c~s/if (parse_left || parse_words - parse_at >= 4) bad_frame/if (parse_left) bad_frame/"
  "master: frame 3's version not checked~cc/master.c~s/if (next32() != 1) bad_frame(type, MR_D_VERSION);/(void)next32();/"
  "reply: s and N swapped~cc/master.c~s/write128(updater->PAGE_COUNT, tracer_info\[0\], N, S);/write128(updater->PAGE_COUNT, tracer_info[0], S, N);/"
  "reply: EGRESS_COUNT 71~cc/master.c~s/    host_link->EGRESS_COUNT = ML_REPLY_ROWS;/    host_link->EGRESS_COUNT = ML_REPLY_ROWS - 1;/"
  "reply: record s + 1 from the master's memory for the tracer tile's~cc/master.c~s/    stream(0, ML_REPLY, IE_EGRESS, ML_REPLY_ROWS, SIZE_128, 0, MODE_WRITE, TILE_MASTER);/    stream(0, ML_REPLY, IE_EGRESS, 4, SIZE_128, 0, MODE_WRITE, TILE_MASTER); stream(1, ML_EXPECT, IE_EGRESS, RECORD_WORDS, SIZE_128, 0, MODE_WRITE, TILE_MASTER);/; s/^    transfer(1);$/    transfer(2);/"
  "reply: the record before the reply's header and figures~cc/master.c~s/    stream(0, ML_REPLY, IE_EGRESS, ML_REPLY_ROWS, SIZE_128, 0, MODE_WRITE, TILE_MASTER);/    stream(0, ML_REPLY + 16, IE_EGRESS, RECORD_WORDS, SIZE_128, 0, MODE_WRITE, TILE_MASTER); stream(1, ML_REPLY, IE_EGRESS, 4, SIZE_128, 0, MODE_WRITE, TILE_MASTER);/; s/^    transfer(1);$/    transfer(2);/"
  "reply: the last run's STAT_STALL_DATA for STAT_CYCLES~cc/master.c~s/write128(updater->STAT_STALL_DATA, updater->STAT_CYCLES, hi, lo);/write128(updater->STAT_STALL_DATA, updater->STAT_STALL_DATA, hi, lo);/"
  "tracer_ctl: the result without the record~tracer/tracer_ctl.c~s/hdr\[TL_H_RESULT\], 68, ctr, DST_CC1);/hdr[TL_H_RESULT], 0, ctr, DST_CC1);/"
  "tracer_ctl: the rows traced a window short~tracer/tracer_ctl.c~s/send128(0, status, tracer->ERRORS, tr_results\[TL_R_WINDOWS\] \*/send128(0, status, tracer->ERRORS, (tr_results[TL_R_WINDOWS] - 1) */"
  "cc1: no store to UART_CONTROL, and it prints~cc/cc1.c~s/if (cc1->UART_CONTROL == 0) cc1->UART_CONTROL = uart_setting(CC1_UART_DIVIDER);/;/; s/case CC_UART: cc1->UART_CONTROL = uart_setting(arg); break;/case CC_UART: break;/"
  "host.h: DMA_FORWARD_COUNT's discard at bit 30~include/host.h~s/#define HL_FORWARD_DISCARD          0x80000000u/#define HL_FORWARD_DISCARD          0x40000000u/"
  "host.h: background_service at the foreground's bit~include/host.h~s/#define HL_EVENT_BACKGROUND_SERVICE (1 << 9)/#define HL_EVENT_BACKGROUND_SERVICE (1 << 8)/"
)
bad=0
printf '%-74s %s\n' "bug" "failing tests"
for entry in "${BUGS[@]}"; do
  IFS='~' read -r name file expr <<< "$entry"
  [ -n "$FILTER" ] && ! [[ "$name" =~ $FILTER ]] && continue
  fresh
  sed -i "$expr" "$W/fw/$file"
  if cmp -s "$W/fw/$file" "$SIM/fw/$file"; then printf '%-74s %s\n' "$name" "NOT PLANTED (the sed matched nothing)"; bad=1; continue; fi
  if ! build; then printf '%-74s %s\n' "$name" "caught by the build"; continue; fi
  r=$(run "$file")
  [ "$r" = " -" ] && { r="UNCAUGHT"; bad=1; }
  printf '%-74s %s\n' "$name" "$r"
done
[ $bad = 0 ] || { echo "teeth: FAILED"; exit 1; }
echo "teeth: every bug caught"
