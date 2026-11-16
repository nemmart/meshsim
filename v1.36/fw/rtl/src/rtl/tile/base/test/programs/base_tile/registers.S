# registers (kind 0): every module's registers and tables, reached through
# register_bus, each at its owner. The program stores a value of its own to every
# R/W word of the tile window's blocks 1, 2, 3, 5 and 6, to EVENTS_MASK, and to an
# entry of every table, all before it loads any back, so that a store that reached
# another owner, or a word that reached another, reads back wrong; then it loads each,
# which must return the value as the owner's fields keep it (5.4: the bits above a
# register's fields, or its table's, read 0). It reads the tile's straps and states
# in block 0, and the request unit's received words, and checks that every module's
# load reaches it: READ_FREE, the read queue's depth.
#
# Arguments (tile_map.h): 1, PARAMS as the test expects it from the tile's straps;
# 2 and 3, the tile's EVENTS_PEEK and STATUS at the start, as the test expects them.
#
# Checks (REPORT_FAIL with the check's number; tile_map.h):
#   1    PARAMS is argument 1
#   2    mhartid is PARAMS.tile_id
#   3    STATUS is argument 3 (on a compute tile 0: the release has cleared arrived)
#   4    ERRORS is 0
#   5    EVENTS_PEEK is argument 2
#   6    EVENTS_MASK is its reset value
#   10-15 the tile window's R/W words read back: DMA_CONFIG, WRITE_HDR,
#        WRITE_DEST_ADDR, READ_HDR, READ_DEST_ADDR, RECEIVE_CONFIG
#   16   GATHER_CONFIG
#   17   EVENTS_MASK
#   18   READ_FREE is 64, the read queue's depth
#   19-25 COUNTER_1 to COUNTER_7 are 0
#   26-28 REQUEST_IN_W0 to W2 are 0: no request received
#   30-36 the tables read back: STREAM_SRC_ADDR, STREAM_DEST_ADDR, STREAM_HDR,
#        GROUP's word 0 and word 1, PTR, HIST
# A pass reports PARAMS, mhartid and STATUS.

#include "tile_map.h"

/* The entries the tables' stores take: not entry 0, so that every word bit of an
 * index matters somewhere. */
#define STREAM_ENTRY 0x5A
#define GROUP_ENTRY 0x2D
#define PTR_ENTRY 0xA5
#define HIST_ENTRY 0x3C

    .text
    .globl main
main:
    # ---- 1-6: the straps and the states ----
    lw      s0, ARGUMENT(1)(zero)   # PARAMS expected
    lw      s1, ARGUMENT(2)(zero)   # EVENTS_PEEK expected
    lw      s10, ARGUMENT(3)(zero)  # STATUS expected
    lw      s2, PARAMS(zero)
    expect_equal s2, s0, 1
    csrr    t1, mhartid
    andi    t2, s2, PARAMS_TILE_ID
    expect_equal t1, t2, 2
    lw      s3, STATUS(zero)
    expect_equal s3, s10, 3
    lw      t1, ERRORS(zero)
    expect  t1, 0, 4
    lw      t1, EVENTS_PEEK(zero)
    expect_equal t1, s1, 5
    lw      t1, EVENTS_MASK(zero)
    expect  t1, EVENTS_MASK_RESET, 6

    # ---- Stores: every R/W word of the tile window's blocks, each its own value ----
    li      t1, 0xFFFFFFFF
    sw      t1, DMA_CONFIG(zero)
    sw      t1, WRITE_HDR(zero)
    li      t1, 0x12345678
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, 0xFFFFFFFF
    sw      t1, READ_HDR(zero)
    li      t1, 0x0000AB30
    sw      t1, READ_DEST_ADDR(zero)
    li      t1, 0xFFFFFFFF
    sw      t1, RECEIVE_CONFIG(zero)
    li      t1, 0xFFFFFFFF
    sw      t1, GATHER_CONFIG(zero)
    li      t1, 0xFFFF6AA5
    sw      t1, EVENTS_MASK(zero)

    # ---- Stores: an entry of every table, each its own value ----
    li      s4, STREAM_SRC_ADDR_BASE
    li      t1, 0xFFFFFFFF
    sw      t1, 4 * STREAM_ENTRY(s4)
    li      s5, STREAM_DEST_ADDR_BASE
    li      t1, 0xDEADBEEF
    sw      t1, 4 * STREAM_ENTRY(s5)
    li      s6, STREAM_HDR_BASE
    li      t1, 0xFFFFFFFF
    sw      t1, 4 * STREAM_ENTRY(s6)
    li      s7, GROUP_BASE
    li      t1, 0xFFFFFFFF
    sw      t1, 8 * GROUP_ENTRY(s7)
    li      t1, 0x5555AAC3
    sw      t1, 8 * GROUP_ENTRY + 4(s7)
    li      s8, PTR_BASE
    li      t1, 0xFFFFFFFF
    sw      t1, 4 * PTR_ENTRY(s8)
    li      s9, HIST_BASE
    li      t1, 0x00012345
    sw      t1, 4 * HIST_ENTRY(s9)

    # ---- 10-17: the tile window's words read back ----
    lw      t1, DMA_CONFIG(zero)
    expect  t1, 0x001FFFFF, 10      # [20:0]
    lw      t1, WRITE_HDR(zero)
    expect  t1, 0x80FC | DST_MASK, 11 # counter, mode, dst, no_increment; size reads 0
    lw      t1, WRITE_DEST_ADDR(zero)
    expect  t1, 0x12345678, 12
    lw      t1, READ_HDR(zero)
    expect  t1, 0x1F, 13            # size, counter
    lw      t1, READ_DEST_ADDR(zero)
    expect  t1, 0x0000AB30, 14
    lw      t1, RECEIVE_CONFIG(zero)
    expect  t1, 0x3F, 15            # the two digits
    lw      t1, GATHER_CONFIG(zero)
    expect  t1, 0x00FFFFFF, 16      # record_base, value_size
    lw      t1, EVENTS_MASK(zero)
    expect  t1, 0x2AA5, 17          # one bit per event, 14
    li      t1, EVENTS_MASK_RESET
    sw      t1, EVENTS_MASK(zero)

    # ---- 18-28: the words no store reached ----
    lw      t1, READ_FREE(zero)
    expect  t1, 64, 18
    lw      t1, COUNTER(1)(zero)
    expect  t1, 0, 19
    lw      t1, COUNTER(2)(zero)
    expect  t1, 0, 20
    lw      t1, COUNTER(3)(zero)
    expect  t1, 0, 21
    lw      t1, COUNTER(4)(zero)
    expect  t1, 0, 22
    lw      t1, COUNTER(5)(zero)
    expect  t1, 0, 23
    lw      t1, COUNTER(6)(zero)
    expect  t1, 0, 24
    lw      t1, COUNTER(7)(zero)
    expect  t1, 0, 25
    lw      t1, REQUEST_IN_W0(zero)
    expect  t1, 0, 26
    lw      t1, REQUEST_IN_W1(zero)
    expect  t1, 0, 27
    lw      t1, REQUEST_IN_W2(zero)
    expect  t1, 0, 28

    # ---- 30-36: the tables read back ----
    lw      t1, 4 * STREAM_ENTRY(s4)
    expect  t1, 0x001FFFFF, 30      # [20:0]
    lw      t1, 4 * STREAM_ENTRY(s5)
    expect  t1, 0xDEADBEEF, 31
    lw      t1, 4 * STREAM_ENTRY(s6)
    expect  t1, 0xFFFF80FF | DST_MASK, 32 # header, dst, count
    lw      t1, 8 * GROUP_ENTRY(s7)
    expect  t1, 0x3FFFFFFF, 33      # rotations, first_stream, streams
    lw      t1, 8 * GROUP_ENTRY + 4(s7)
    expect  t1, 0xC3, 34            # gap
    lw      t1, 4 * PTR_ENTRY(s8)
    expect  t1, 0x001FFFFF, 35      # a word address
    lw      t1, 4 * HIST_ENTRY(s9)
    expect  t1, 0x00012345, 36      # 17 bits

    mv      a0, s2
    csrr    a1, mhartid
    mv      a2, s3
    j       pass
