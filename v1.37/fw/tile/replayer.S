# replayer (kind 5): a short pass through the compute tile's replayer, peripheral 1
# (rtl/tile/base/spec.md, 17.8), run by the core as 17.8.13 has it, every input
# reaching the replayer's region by WRITE packets from the write unit to the tile
# itself (17.3), counted on counter 1; base_tile_test takes the rows from the tile's
# replay_row port and checks them.
#
#   1. The tables (17.8.9): ROM words 0 (the padding row), 1 (the shared NOP row) and
#      2, an ADDI template of the program's own (opcode ADDI, rd 129, rs1 130, rs2
#      132, format I, report_c, sign_imm); the decode table's row 16, entry 128,
#      ADDI's (17.8.6: {00100, 000, 0, 0}), class 0 at ROM word 2.
#   2. The record: the state word (pc PASS_PC, instr_idx PASS_INDEX, row offset 0),
#      the stream word (a raw code stream of one entry, four values; empty logs),
#      image words 0-2 (x1 = 100, x2 = 200, x3 = 0x33, x4 = 0x44), and the ignored
#      words 66 and 67; then the code queue's one entry: ADDI x3, x1, 7; ADDI x4,
#      x3, 17; ADDI x1, x2, 257; a padding word.
#   3. RUN_CONFIG: 4 rows, rd_prev; once counter 1 drains, START; the wait for
#      EVENTS.done, on the tile's EVENTS.peripheral_1.
#   4. The replayer's ERRORS 0, STATUS 0, ROWS_RUN 4, and the tile's ERRORS 0.
# A pass reports ROWS_RUN, the replayer's PARAMS and EVENTS_PEEK.
#
# Checks (REPORT_FAIL with the check's number; tile_map.h):
#   1    the replayer's PARAMS: type 4
#   2    its ERRORS after the pass: 0
#   3    its STATUS after the pass: 0
#   4    ROWS_RUN after the pass: 4
#   5    the tile's ERRORS: 0

#include "tile_map.h"

/* The replayer's window (17.1, 17.8.1), as x0-relative offsets. */
#define RP PERIPHERAL_WINDOW(1)
#define RP_PARAMS (RP + 0x00)
#define RP_STATUS (RP + 0x04)
#define RP_ERRORS (RP + 0x08)
#define RP_EVENTS (RP + 0x0C)
#define RP_EVENTS_PEEK (RP + 0x10)
#define RP_EVENTS_MASK (RP + 0x14)
#define RP_START (RP + 0x18)
#define RP_RUN_CONFIG (RP + 0x80)
#define RP_ROWS_RUN (RP + 0x84)
#define RP_DONE 0x1
#define RUN_CONFIG_RD_PREV (1 << 25)

/* The pass. */
#define PASS_ROWS 4
#define PASS_PC 0x1000
#define PASS_INDEX 5

/* The packets: 3 ROM words, 1 decode table row, 6 record words (the state and
 * stream words, image words 0-2, word 66... and 67), 1 code entry. */
#define PACKETS 12

    .text
    .globl main
main:
    lw      t1, RP_PARAMS(zero)
    andi    t1, t1, 0xFF
    expect  t1, 4, 1
    lw      s0, PARAMS(zero)
    andi    s0, s0, PARAMS_TILE_ID
    slli    s0, s0, HDR_DST
    ori     t1, s0, HEADER(MODE_WRITE, 1, SIZE_128)
    sw      t1, WRITE_HDR(zero)
    li      t1, PACKETS
    sw      t1, COUNTER(1)(zero)

    # Each packet: its address, then its four words from the table below.
    la      s1, packets
    li      s2, PACKETS
1:
    lw      t1, 0(s1)
    sw      t1, WRITE_DEST_ADDR(zero)
    lw      t1, 4(s1)
    sw      t1, WRITE_W0(zero)
    lw      t1, 8(s1)
    sw      t1, WRITE_W1(zero)
    lw      t1, 12(s1)
    sw      t1, WRITE_W2(zero)
    lw      t1, 16(s1)
    sw      t1, WRITE_SEND_128(zero)
    addi    s1, s1, 20
    addi    s2, s2, -1
    bnez    s2, 1b

    li      t1, PASS_ROWS | RUN_CONFIG_RD_PREV
    sw      t1, RP_RUN_CONFIG(zero)
    wait_events EVENT_COUNTER_DONE(1)
    li      t1, RP_DONE
    sw      t1, RP_EVENTS_MASK(zero)
    li      t1, 1
    sw      t1, RP_START(zero)
2:
    li      t1, EVENT_PERIPHERAL(1)
    sw      t1, EVENTS_MASK(zero)
    wfi
    lw      t1, RP_EVENTS(zero)
    andi    t1, t1, RP_DONE
    beqz    t1, 2b

    lw      t1, RP_ERRORS(zero)
    expect  t1, 0, 2
    lw      t1, RP_STATUS(zero)
    expect  t1, 0, 3
    lw      a0, RP_ROWS_RUN(zero)
    expect  a0, PASS_ROWS, 4
    lw      t1, ERRORS(zero)
    expect  t1, 0, 5
    lw      a1, RP_PARAMS(zero)
    lw      a2, RP_EVENTS_PEEK(zero)
    j       pass

/* A packet: its region address, then its payload's words 0 to 3. */
.macro packet offset, w0, w1, w2, w3
    .word   PERIPHERAL_1_BASE + (\offset), \w0, \w1, \w2, \w3
.endm

/* An instruction ADDI rd, rs1, imm, encoded. */
#define ADDI_WORD(rd, rs1, imm) (((imm) << 20) | ((rs1) << 15) | ((rd) << 7) | 0x13)

    .data
    .balign 4
packets:
    # ROM word 0: the padding row (NOP, every code 0, hold [45]).
    packet  0x2000, 0, 1 << 13, 0, 0
    # ROM word 1: the shared NOP row: ADDI (2), rd 0, rs1 0, rs2 128, report_c [53].
    packet  0x2010, 0x80000002, 1 << 21, 0, 0
    # ROM word 2: ADDI, rd 129, rs1 130, rs2 132; format I [50:48], report_c [53],
    # sign_imm [62].
    packet  0x2020, 0x84828102, (1 << 16) | (1 << 21) | (1 << 30), 0, 0
    # The decode table's row 16: entry 128, class 0, start 2.
    packet  0x1100, 2 << 2, 0, 0, 0
    # The state word: pc, instr_idx, row offset 0.
    packet  0x000, PASS_PC, 0, PASS_INDEX, 0
    # The stream word: code_words 1 [15:0], code_values 4 [64:48]; raw.
    packet  0x010, 1, 4 << 16, 0, 0
    # Image words 0-2: x0, x1; x2, x3; x4, x5.
    packet  0x020, 0, 0, 100, 0
    packet  0x030, 200, 0, 0x33, 0
    packet  0x040, 0x44, 0, 0, 0
    # The record's words 66 and 67: accepted and ignored.
    packet  0x420, 0xDEADBEEF, 0xFEEDFACE, 0x01234567, 0x89ABCDEF
    packet  0x430, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
    # The code queue's entry, part 1: four raw code values.
    packet  0x100000, ADDI_WORD(3, 1, 7), ADDI_WORD(4, 3, 17), ADDI_WORD(1, 2, 257), 0
