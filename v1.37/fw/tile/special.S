# special (kind 4): the tile's boundary with its peripherals and its receive side, on
# a special tile's base (base_tile_special_test: the master tile's id, its router's
# attach port its own, peripherals 1-3 of types 1, 2 and 3, the second and third with
# a DMA on the router's east and north inputs, a receive side for target 0, no
# barrier). The test plays the peripherals at the tile's ports: each reg interface's
# rdata a constant of its own, as below, and each store it sees recorded; and it
# plays the request tree's root.
#
#   1. The straps, in PARAMS, and STATUS, 0: the tile started from its image placed
#      before the reset ended (6.5, boot_local); then FILL_REPORTS reports of their own
#      number, REPORT_FILL, numbered down in word 3, more than the request queue holds,
#      and STATUS.request_full after them.
#   2. Loads from each peripheral's control block, its DMA block (0 for peripheral 1,
#      which has none) and its own blocks, each the constant its interface returns.
#   3. Stores to each kind of block, which the test checks in order; two of them stall
#      until the peripheral's ready (17.6: STREAM_SEND, UART_TX).
#   4. WRITEs into each peripheral's region, from the write unit to the tile itself,
#      which reach its write port (17.3); counted, as every write is.
#   5. REPORT_WAIT with the DMEM word the test's DMA packet lands at; the test then
#      sends a WRITE on peripheral 2's DMA input, to the tile, counted on counter 7,
#      and one on peripheral 3's, to tile 0, which leaves by the attach port.
#   6. Each peripheral's event, which the test raises in turn, reaching EVENTS.
#   7. A request the test sends on the receiving request link: request_pending, the
#      request read and popped.
# A pass reports STATUS; the test then raises peripheral 2's error, whose NMI sends
# the boot ROM handler's reports.
#
# Arguments (tile_map.h), placed with the image: 1, PARAMS as the test expects it.
#
# Checks (REPORT_FAIL with the check's number; tile_map.h):
#   1    PARAMS is argument 1
#   2    STATUS is 0
#   3    STATUS.request_full after the fill reports
#   10-18 part 2's loads
#   30   part 5: the DMA packet's word
#   40-43 part 7: the request's words 0 to 3
#   44   part 7: request_pending clear after the pop

#include "tile_map.h"

/* The constants the test's peripherals return: peripheral n's control block and DMA
 * block, and own block index i (6(n - 1) + b - 2 for block b). */
#define CONTROL_RDATA(n) (0xC0000000 + (n))
#define DMA_RDATA(n) (0xD0000000 + (n))
#define OWN_RDATA(i) (0xE0000000 + (i))

/* The request the test sends (part 7). */
#define REQUEST_WORD_0 0x13579000
#define REQUEST_WORD_1 0x2468ACE0
#define REQUEST_WORD_2 0xFDB97531
#define REQUEST_WORD_3 0x0ECA8642

/* Part 1's reports: more than REQUEST_QUEUE_DEPTH, 4 (11.1), and their number. */
#define FILL_REPORTS 16
#define REPORT_FILL 3

/* The DMA packet's word (part 5). */
#define DMA_WORD 0xD3ADB33F

    .bss
    .balign 16
dma_landing:
    .space 16

    .text
    .globl main
main:
    lw      s0, PARAMS(zero)
    andi    s1, s0, PARAMS_TILE_ID
    slli    s1, s1, HDR_DST             # a header register's dst: me
    lw      t1, ARGUMENT(1)(zero)
    expect_equal s0, t1, 1
    lw      t1, STATUS(zero)
    expect  t1, 0, 2

    # ---- 1: request_full, from the request unit: FILL_REPORTS reports, two stores
    # each (word 0, then REQUEST_SEND with word 3; the push cleared words 1 and 2),
    # sent faster than the tree takes their eight beats each, so that the queue
    # fills; the last send's store stalls until there is room, which it takes, so the
    # queue is full after it for at least a request's eight beats ----
    li      s2, FILL_REPORTS
    li      t1, REPORT_WORD_0(REPORT_FILL)
1:  sw      t1, REQUEST_OUT_W0(zero)
    sw      s2, REQUEST_SEND(zero)
    addi    s2, s2, -1
    bnez    s2, 1b
    lw      t1, STATUS(zero)
    andi    t1, t1, STATUS_REQUEST_FULL
    expect  t1, STATUS_REQUEST_FULL, 3

    # ---- 2: loads ----
    lw      t1, PERIPHERAL_WINDOW(1) + 0x004(zero) # STATUS
    expect  t1, CONTROL_RDATA(1), 10
    lw      t1, PERIPHERAL_WINDOW(2) + 0x00C(zero) # EVENTS
    expect  t1, CONTROL_RDATA(2), 11
    lw      t1, PERIPHERAL_WINDOW(3) + 0x000(zero) # PARAMS
    expect  t1, CONTROL_RDATA(3), 12
    lw      t1, PERIPHERAL_WINDOW(1) + 0x040(zero) # no DMA: 0
    expect  t1, 0, 13
    lw      t1, PERIPHERAL_WINDOW(2) + 0x040(zero) # DMA_HDR
    expect  t1, DMA_RDATA(2), 14
    lw      t1, PERIPHERAL_WINDOW(3) + 0x044(zero) # DMA_DEST_ADDR
    expect  t1, DMA_RDATA(3), 15
    lw      t1, PERIPHERAL_WINDOW(1) + 0x080(zero) # ERROR_LINES_0: own block 2
    expect  t1, OWN_RDATA(0), 16
    lw      t1, PERIPHERAL_WINDOW(1) + 0x114(zero) # STREAM_IN_W0: own block 4
    expect  t1, OWN_RDATA(2), 17
    lw      t1, PERIPHERAL_WINDOW(2) + 0x080(zero) # RW_CONFIG: own block 2
    expect  t1, OWN_RDATA(6), 18

    # ---- 3: stores, in the order the test expects them ----
    li      t1, 0x11
    sw      t1, PERIPHERAL_WINDOW(1) + 0x014(zero) # EVENTS_MASK
    li      t1, 0x1
    sw      t1, PERIPHERAL_WINDOW(3) + 0x018(zero) # START
    li      t1, 0x22
    sw      t1, PERIPHERAL_WINDOW(2) + 0x044(zero) # DMA_DEST_ADDR
    li      t1, 0x1
    sw      t1, PERIPHERAL_WINDOW(3) + 0x048(zero) # DMA_START
    li      t1, 0x33
    sw      t1, PERIPHERAL_WINDOW(1) + 0x0A4(zero) # LED
    li      t1, 0x44
    sw      t1, PERIPHERAL_WINDOW(2) + 0x084(zero) # RW_LPDDR_ADDR
    li      t1, 0x55
    sw      t1, PERIPHERAL_WINDOW(1) + 0x10C(zero) # STREAM_SEND: stalls
    li      t1, 0x66
    sw      t1, PERIPHERAL_WINDOW(1) + 0x0A8(zero) # UART_TX: stalls

    # ---- 4: WRITEs into each peripheral's region ----
    li      t1, 3
    sw      t1, COUNTER(1)(zero)
    li      t1, HEADER(MODE_WRITE, 1, 0)
    or      t1, t1, s1
    sw      t1, WRITE_HDR(zero)
    li      t1, PERIPHERAL_1_BASE + 0x10
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, 0xA1
    sw      t1, WRITE_SEND_32(zero)
    li      t1, PERIPHERAL_1_BASE + 0x01000020 # peripheral 2's region
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, 0xB1
    sw      t1, WRITE_W0(zero)
    li      t1, 0xB2
    sw      t1, WRITE_SEND_64(zero)
    li      t1, PERIPHERAL_1_BASE + 0x02000030 # peripheral 3's region
    sw      t1, WRITE_DEST_ADDR(zero)
    li      t1, 0xC1
    sw      t1, WRITE_W0(zero)
    li      t1, 0xC2
    sw      t1, WRITE_W1(zero)
    li      t1, 0xC3
    sw      t1, WRITE_W2(zero)
    li      t1, 0xC4
    sw      t1, WRITE_SEND_128(zero)
    wait_events EVENT_COUNTER_DONE(1)

    # ---- 5: the DMAs' packets ----
    li      t1, 1
    sw      t1, COUNTER(7)(zero)
    la      a0, dma_landing
    li      a1, 0
    li      a2, 0
    report  REPORT_WAIT, a0, a1, a2
    wait_events EVENT_COUNTER_DONE(7)
    la      t0, dma_landing
    lw      t1, 0(t0)
    expect  t1, DMA_WORD, 30

    # ---- 6: each peripheral's event, live ----
    wait_events EVENT_PERIPHERAL(1)
    wait_events EVENT_PERIPHERAL(2)
    wait_events EVENT_PERIPHERAL(3)

    # ---- 7: a request received ----
    wait_events EVENT_REQUEST_PENDING
    lw      t1, REQUEST_IN_W0(zero)
    expect  t1, REQUEST_WORD_0, 40
    lw      t1, REQUEST_IN_W1(zero)
    expect  t1, REQUEST_WORD_1, 41
    lw      t1, REQUEST_IN_W2(zero)
    expect  t1, REQUEST_WORD_2, 42
    lw      t1, REQUEST_RECEIVE(zero)
    expect  t1, REQUEST_WORD_3, 43
    lw      t1, EVENTS_PEEK(zero)
    andi    t1, t1, EVENT_REQUEST_PENDING
    expect  t1, 0, 44

    lw      a0, STATUS(zero)
    li      a1, 0
    li      a2, 0
    j       pass
