# noc (kind 1): a round trip through the router for each of the tile's senders and
# each of its receivers' paths, and the events that end each wait reaching the core.
# Every packet but the last part's goes to the tile itself: from the inject unit to
# the router's local input, back by the loopback queue to the eject and the receive
# unit. Each is counted, and the program waits for its counter's event in a WFI
# (tile_map.h's wait_events), which only the tile control unit's meip ends.
#
#   1. The counters: for each counter n, a 32-bit WRITE from the write unit into
#      DMEM, counted on counter n; the wait for counter_n_done; then
#      all_counters_done, which the last drain left set.
#   2. The scratchpad: a 128-bit WRITE from the write unit into a scratchpad row, by
#      the write engine; then the read unit reads the row back into DMEM. Then a
#      second row, its word 2 written over by a 32-bit WRITE, read back.
#   3. The DMA unit: a stream transfer of that row's four words into DMEM, as 32-bit
#      WRITEs; the waits for dma_done and for its counter.
#   4. The gather unit: a GATHER from the write unit, whose value, that row (k = 2,
#      one 128-bit read), the gather unit appends at PTR[me]; read back.
#   5. AUGMENT, whose ordinal comes from the gather unit's record_base.
#   6. Packets out: a 32-bit WRITE to each tile the arguments name, which the test
#      checks on the router's outputs; then REPORT_WAIT, counter 6 armed, and the
#      wait for the test's COUNT packets, one on each of the router's inputs, on
#      counter 6.
#
# Arguments (tile_map.h): 1-4, the tiles to send to in part 6, each a tile id or
# 0xFFFF_FFFF for none; 5, how many COUNT packets the test sends back.
#
# Checks (REPORT_FAIL with the check's number; tile_map.h):
#   1-7    part 1: counter n's DMEM word
#   8      part 1: all_counters_done set (EVENTS_PEEK's bit)
#   11-17  part 1: COUNTER_n back at 0
#   20-23  part 2: the row's words, read back
#   24     part 2: READ_FREE back at the read queue's depth, 64, the read injected
#   25-28  part 2: a second row's words after a 128-bit WRITE, then a 32-bit WRITE
#          to its word 2, read back
#   30-33  part 3: the DMA's four words
#   40-43  part 4: the gathered value's words, read back
#   44     part 4: PTR[me] advanced by the value, 4 words
#   50-51  part 5: the record's key and ordinal
#   60     part 6: COUNTER_6 back at 0
# A pass reports the tile id.

#include "tile_map.h"

#define SCRATCHPAD_ROW 0x40 /* the round trip's row: a byte offset in the scratchpad */
#define GATHER_ROW 0x80 /* where the gathered value lands */
#define AUGMENT_ROW 0xC0 /* where the record lands */
#define PARTIAL_ROW 0x100 /* part 2's row written in part */
#define PARTIAL_WORD 0x5A5AA5A5
#define GATHER_ORDINAL 3 /* the value's ordinal: record_base 12 words below the row */
#define AUGMENT_ORDINAL 4 /* the record's: record_base 8 words below its row */
#define STREAM_ENTRY 3 /* the DMA's stream */

#define WORD_0 0x0F1E2D3C
#define WORD_1 0x4B5A6978
#define WORD_2 0x8796A5B4
#define WORD_3 0xC3D2E1F0
#define AUGMENT_KEY 0x600DCAFE

    .bss
    .balign 16
counted:                                # part 1: a word per counter
    .space 32
read_back:                              # parts 2, 4 and 5: the read unit's rows
    .space 16
dma_words:                              # part 3
    .space 16

    .text
    .globl main
main:
    lw      s0, PARAMS(zero)
    andi    s0, s0, PARAMS_TILE_ID      # me
    slli    s1, s0, HDR_DST             # a header register's dst: me
    # The DMA's dst: relative on a compute tile, so 0 is the tile itself; absolute
    # on a special tile (8.3).
    li      s2, 0
    li      t0, TILE_COUNT
    bltu    s0, t0, 1f
    mv      s2, s0
1:
    li      s3, ARGUMENT(1)                 # the tiles to send to

    # ---- 1: the counters, each with its event ----
    .irp    n, 1, 2, 3, 4, 5, 6, 7
    li      t1, 1
    sw      t1, COUNTER(\n)(zero)       # arms it: a store of 1
    li      t1, HEADER(MODE_WRITE, \n, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    la      t2, counted + 4 * (\n - 1)
    sw      t2, WRITE_DEST_ADDR(zero)
    li      t3, 0xC0DE0000 + \n
    sw      t3, WRITE_SEND_32(zero)
    wait_events EVENT_COUNTER_DONE(\n)
    lw      t1, 0(t2)
    expect_equal t1, t3, \n
    lw      t1, COUNTER(\n)(zero)
    expect  t1, 0, 10 + \n
    .endr
    lw      t1, EVENTS_PEEK(zero)
    andi    t1, t1, EVENT_ALL_COUNTERS_DONE
    expect  t1, EVENT_ALL_COUNTERS_DONE, 8
    wait_events EVENT_ALL_COUNTERS_DONE

    # ---- 2: a scratchpad row, written and read back ----
    li      t1, 1
    sw      t1, COUNTER(1)(zero)
    li      t1, HEADER(MODE_WRITE, 1, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    li      t1, SCRATCHPAD_BASE + SCRATCHPAD_ROW
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, WORD_0
    sw      t1, WRITE_W0(zero)
    li      t1, WORD_1
    sw      t1, WRITE_W1(zero)
    li      t1, WORD_2
    sw      t1, WRITE_W2(zero)
    li      t1, WORD_3
    sw      t1, WRITE_SEND_128(zero)
    wait_events EVENT_COUNTER_DONE(1)
    li      a3, WORD_ADDRESS(SCRATCHPAD_ROW)
    jal     ra, read_row
    li      t0, WORD_0
    expect_equal a4, t0, 20
    li      t0, WORD_1
    expect_equal a5, t0, 21
    li      t0, WORD_2
    expect_equal a6, t0, 22
    li      t0, WORD_3
    expect_equal a7, t0, 23
    lw      t1, READ_FREE(zero)             # the read's done freed its entry (10)
    expect  t1, 64, 24

    # ---- 2: a 32-bit WRITE into a row of four words: the write engine's word enable
    # writes its word alone ----
    li      t1, 2
    sw      t1, COUNTER(1)(zero)
    li      t2, SCRATCHPAD_BASE + PARTIAL_ROW
    sw      t2, WRITE_DEST_ADDR(zero)
    li      t1, WORD_0
    sw      t1, WRITE_W0(zero)
    li      t1, WORD_1
    sw      t1, WRITE_W1(zero)
    li      t1, WORD_2
    sw      t1, WRITE_W2(zero)
    li      t1, WORD_3
    sw      t1, WRITE_SEND_128(zero)
    addi    t2, t2, 8                       # its word 2
    sw      t2, WRITE_DEST_ADDR(zero)
    li      t1, PARTIAL_WORD
    sw      t1, WRITE_SEND_32(zero)
    wait_events EVENT_COUNTER_DONE(1)
    li      a3, WORD_ADDRESS(PARTIAL_ROW)
    jal     ra, read_row
    li      t0, WORD_0
    expect_equal a4, t0, 25
    li      t0, WORD_1
    expect_equal a5, t0, 26
    li      t0, PARTIAL_WORD
    expect_equal a6, t0, 27
    li      t0, WORD_3
    expect_equal a7, t0, 28

    # ---- 3: the DMA unit: the row's words as four 32-bit WRITEs into DMEM ----
    li      t0, STREAM_SRC_ADDR_BASE
    li      t1, WORD_ADDRESS(SCRATCHPAD_ROW)
    sw      t1, 4 * STREAM_ENTRY(t0)
    li      t0, STREAM_DEST_ADDR_BASE
    la      t1, dma_words
    sw      t1, 4 * STREAM_ENTRY(t0)
    li      t0, STREAM_HDR_BASE
    li      t1, 4 << 15 | HEADER(MODE_WRITE, 3, SIZE_32) # count 4
    slli    t2, s2, HDR_DST
    or      t1, t1, t2
    sw      t1, 4 * STREAM_ENTRY(t0)
    li      t1, 1 << 9 | STREAM_ENTRY << 1 # a stream transfer of one stream, gap 0
    sw      t1, DMA_CONFIG(zero)
    li      t1, 4
    sw      t1, COUNTER(3)(zero)
    li      t1, 1
    sw      t1, DMA_START(zero)
    wait_events EVENT_DMA_DONE
    wait_events EVENT_COUNTER_DONE(3)
    la      t0, dma_words
    lw      t1, 0(t0)
    expect  t1, WORD_0, 30
    lw      t1, 4(t0)
    expect  t1, WORD_1, 31
    lw      t1, 8(t0)
    expect  t1, WORD_2, 32
    lw      t1, 12(t0)
    expect  t1, WORD_3, 33

    # ---- 4: the gather unit: the row as the value of GATHER_ORDINAL, k = 2 ----
    li      t1, (WORD_ADDRESS(SCRATCHPAD_ROW) - 4 * GATHER_ORDINAL) | 1 << 21
    sw      t1, GATHER_CONFIG(zero)
    li      s4, PTR_BASE
    slli    t0, s0, 2
    add     s4, s4, t0                  # &PTR[me]
    li      t1, WORD_ADDRESS(GATHER_ROW)
    sw      t1, 0(s4)
    li      t1, 1
    sw      t1, COUNTER(4)(zero)
    li      t1, HEADER(MODE_GATHER, 4, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    li      t1, GATHER_ORDINAL          # the payload's bits [51:32]
    sw      t1, WRITE_SEND_64(zero)
    wait_events EVENT_COUNTER_DONE(4)
    lw      t1, 0(s4)
    expect  t1, WORD_ADDRESS(GATHER_ROW) + 4, 44
    li      a3, WORD_ADDRESS(GATHER_ROW)
    jal     ra, read_row
    li      t0, WORD_0
    expect_equal a4, t0, 40
    li      t0, WORD_1
    expect_equal a5, t0, 41
    li      t0, WORD_2
    expect_equal a6, t0, 42
    li      t0, WORD_3
    expect_equal a7, t0, 43

    # ---- 5: AUGMENT: the key and its ordinal, by record_base ----
    li      t1, WORD_ADDRESS(AUGMENT_ROW)
    sw      t1, 0(s4)
    li      t1, WORD_ADDRESS(AUGMENT_ROW) - 2 * AUGMENT_ORDINAL
    sw      t1, GATHER_CONFIG(zero)
    li      t1, 1
    sw      t1, COUNTER(5)(zero)
    li      t1, HEADER(MODE_AUGMENT, 5, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    li      t1, AUGMENT_KEY
    sw      t1, WRITE_SEND_32(zero)
    wait_events EVENT_COUNTER_DONE(5)
    li      a3, WORD_ADDRESS(AUGMENT_ROW)
    jal     ra, read_row
    li      t0, AUGMENT_KEY
    expect_equal a4, t0, 50
    li      t0, AUGMENT_ORDINAL
    expect_equal a5, t0, 51

    # ---- 6: packets out to the tiles the arguments name, then the test's back ----
    lw      t1, ARGUMENT(5)(zero)       # the COUNT packets to come
    sw      t1, COUNTER(6)(zero)
    li      s5, 0                       # the packet's index
send_out:
    slli    t0, s5, 2
    add     t0, t0, s3
    lw      t1, 0(t0)                   # argument 1 + index: a tile, or none
    li      t2, 0xFFFFFFFF
    beq     t1, t2, sent_out
    slli    t1, t1, HDR_DST
    ori     t1, t1, HEADER(MODE_WRITE, 0, 0)
    sw      t1, WRITE_HDR(zero)
    slli    t1, s5, 2
    addi    t1, t1, 0x100               # its address: 0x100 + 4 * index
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, 0x5EED0000
    or      t1, t1, s5
    sw      t1, WRITE_SEND_32(zero)
sent_out:
    addi    s5, s5, 1
    li      t0, 4
    bne     s5, t0, send_out
    li      a0, 0
    li      a1, 0
    li      a2, 0
    report  REPORT_WAIT, a0, a1, a2         # counter 6 armed: the test may send
    wait_events EVENT_COUNTER_DONE(6)
    lw      t1, COUNTER(6)(zero)
    expect  t1, 0, 60

    mv      a0, s0
    li      a1, 0
    li      a2, 0
    j       pass

# ---- The read unit: the 128-bit row at word address a3 into DMEM's read_back, on
# counter 2; its words in a4 to a7. Clobbers t0 to t6. ----
read_row:
    li      t1, HEADER(0, 2, SIZE_128)  # its mode and dst are the unit's
    sw      t1, READ_HDR(zero)
    la      t2, read_back
    sw      t2, READ_DEST_ADDR(zero)
    li      t1, 1
    sw      t1, COUNTER(2)(zero)
    sw      a3, READ_SEND(zero)
    wait_events EVENT_COUNTER_DONE(2)
    lw      a4, 0(t2)
    lw      a5, 4(t2)
    lw      a6, 8(t2)
    lw      a7, 12(t2)
    jalr    zero, 0(ra)
