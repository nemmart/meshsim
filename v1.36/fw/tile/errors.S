# errors (kind 3): one error of ERRORS (7.4), raised by its module, reaching the
# core. Argument 1 names it, by its bit; the program raises it and parks, and
# then the NMI (6.2), or the exception's trap, takes the core to the boot ROM's
# handler, which sends reports 24 (ERRORS, mcause, mepc) and 25 (mtval, STATUS, ra)
# and parks. The test checks those and the tile's error line. Each error is
# terminal, so a run raises one. The program waits for it in start.S's park, whose
# WFI, with mie 0, only the NMI wakes.
#
#   0 router_overflow      nothing here: the test floods the router's eject from
#                          every link at once
#   1 bad_packet           a WRITE to region 0xFF, from the write unit to the tile
#   2 counter_underflow    a COUNT on counter 1, which is 0
#   3 drift                a group transfer started while the gather unit is busy,
#                          so the read arbiter refuses its visits (8.4, 14.1)
#   4 gather_overflow      GATHERs from the write unit, faster than the gather unit
#                          reads their values, past its queue's 64 (13)
#   5 read_overflow        READ_SENDs while the gather unit's reads hold the read
#                          arbiter, past the read queue's 64 (10)
#   6 early_arrival        ARRIVE with counter 1 non-zero
#   7 access_fault         a load of an unmapped word of the tile window
#   8 illegal_instruction  an all-zero instruction
#   9 receive_underflow    REQUEST_RECEIVE on a tile with no receive queue
#  10 peripheral_1         the replayer's (17.8): 257 pushes into its code queue,
#                          from the write unit to the tile, with no pass to pop
#                          them: the last overruns it, ERRORS.queue_overrun
#
# The gathers (3 to 5): k = 8, four 128-bit reads a value, the values at record_base
# 0 and appended at PTR[me], word address 0x100, uncounted; ordinal 0, the payload of a
# WRITE_SEND_64 of 0 after WRITE_W0 cleared by the last send (9).
#
# Checks: none of its own; an error not raised leaves the program parked, which the
# test sees as a missing report.

#include "tile_map.h"

#define GATHERS_TO_BUSY 32 /* 128 reads: the arbiter busy for 128 cycles */
#define GATHERS_TO_OVERFLOW 120 /* one a cycle, against one value every 4 cycles */
#define READS_TO_OVERFLOW 80

    .bss
    .balign 16
read_landing:                           # read_overflow's reads land here, late
    .space 4 * READS_TO_OVERFLOW

    .text
    .globl main
main:
    lw      s0, PARAMS(zero)
    andi    s0, s0, PARAMS_TILE_ID
    slli    s1, s0, HDR_DST             # a header register's dst: me
    lw      s2, ARGUMENT(1)(zero)       # the error's bit
    la      t0, errors
    slli    t1, s2, 2
    add     t0, t0, t1
    lw      t0, 0(t0)
    jalr    zero, 0(t0)

    .section .rodata
    .balign 4
errors:
    .word   park, bad_packet, counter_underflow, drift, gather_overflow
    .word   read_overflow, early_arrival, access_fault, illegal_instruction
    .word   receive_underflow, peripheral_1

    .text
bad_packet:
    li      t1, HEADER(MODE_WRITE, 0, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    li      t1, 0xFFFF0000
    sw      t1, WRITE_DEST_ADDR(zero)
    sw      zero, WRITE_SEND_32(zero)
    j       park

counter_underflow:
    li      t1, HEADER(MODE_COUNT, 1, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    sw      zero, WRITE_SEND_32(zero)
    j       park

drift:
    jal     ra, gather_setup
    .rept   GATHERS_TO_BUSY
    sw      zero, WRITE_SEND_64(zero)
    .endr
    # GROUP[0]: four rotations of stream 0 alone, gap 0; STREAM[0]: four COUNT
    # elements, uncounted, to the tile itself (dst 0 relative on a compute tile).
    li      t0, GROUP_BASE
    li      t1, 4                       # R 4, first_stream 0, streams 1
    sw      t1, 0(t0)
    sw      zero, 4(t0)
    li      t0, STREAM_SRC_ADDR_BASE
    sw      zero, 0(t0)
    li      t0, STREAM_HDR_BASE
    li      t1, 4 << 15 | HEADER(MODE_COUNT, 0, SIZE_32)
    sw      t1, 0(t0)
    li      t1, 1 << 9 | 1              # a group transfer of group 0 alone
    sw      t1, DMA_CONFIG(zero)
    li      t1, 1
    sw      t1, DMA_START(zero)
    j       park

gather_overflow:
    jal     ra, gather_setup
    .rept   GATHERS_TO_OVERFLOW
    sw      zero, WRITE_SEND_64(zero)
    .endr
    j       park

read_overflow:
    jal     ra, gather_setup
    .rept   GATHERS_TO_BUSY
    sw      zero, WRITE_SEND_64(zero)
    .endr
    li      t1, HEADER(0, 0, SIZE_32)
    sw      t1, READ_HDR(zero)
    la      t1, read_landing
    sw      t1, READ_DEST_ADDR(zero)
    .rept   READS_TO_OVERFLOW
    sw      zero, READ_SEND(zero)
    .endr
    j       park

early_arrival:
    li      t1, 1
    sw      t1, COUNTER(1)(zero)
    sw      t1, ARRIVE(zero)
    j       park

access_fault:
    lw      t1, UNMAPPED_TILE_WORD(zero)
    j       park

illegal_instruction:
    .word   0
    j       park

receive_underflow:
    lw      t1, REQUEST_RECEIVE(zero)
    j       park

# One more push than the replayer's code queue holds (17.8.8): uncounted 128-bit
# WRITEs to its part 1.
peripheral_1:
    ori     t1, s1, HEADER(MODE_WRITE, 0, SIZE_128)
    sw      t1, WRITE_HDR(zero)
    li      t1, PERIPHERAL_1_BASE + 0x100000
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t2, 257
1:
    sw      t2, WRITE_SEND_128(zero)
    addi    t2, t2, -1
    bnez    t2, 1b
    j       park

# ---- The gathers' setup (above): GATHER_CONFIG, PTR[me] and WRITE_HDR. Clobbers t0
# and t1. ----
gather_setup:
    li      t1, 7 << 21                 # record_base 0, k = 8
    sw      t1, GATHER_CONFIG(zero)
    li      t0, PTR_BASE
    slli    t1, s0, 2
    add     t0, t0, t1
    li      t1, 0x100
    sw      t1, 0(t0)                   # PTR[me]
    li      t1, HEADER(MODE_GATHER, 0, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    jalr    zero, 0(ra)
