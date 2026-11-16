# barrier (kind 2): a second barrier, after the boot ROM's, as a phase ends one: the
# image's fixed schedule (6.5, 7.3). Phase 1 reports REPORT_WAIT, then arrives and
# waits for the release in a WFI, as the boot ROM does. The test, as the master tile,
# releases the tile once its BARRIER_ARRIVE pulses, which it may only once the core
# waits in its WFI: the program spends a delay loop between its arrival and its WFI,
# so the BARRIER_ARRIVE comes no sooner. Phase 2 then runs, with no word from the
# test but the release: its argument came with the load.
#
# Arguments: 1, a value of the test's, which phase 2 reports.
#
# Checks (REPORT_FAIL with the check's number; tile_map.h):
#   1    STATUS is 0 at the start: the boot ROM's release cleared arrived
#   2    STATUS after the arrival, before the test has had the report: arrived
#        alone
#   3    after the release: STATUS is 0 again
#   4    after the release: EVENTS_PEEK holds no release, which the wait's load
#        consumed
# A pass, from phase 2, reports argument 1 and STATUS.

#include "tile_map.h"

#define DELAY_LOOPS 1000 /* at least 2 cycles each: base_tile_test.rs's ARRIVAL_DELAY_LEAST */

    .text
    .globl main
main:
    lw      t1, STATUS(zero)
    expect  t1, 0, 1
    li      a0, 0
    li      a1, 0
    li      a2, 0
    report  REPORT_WAIT, a0, a1, a2

    # ---- The barrier: arrive, and wait for the release ----
    li      t1, 1
    sw      t1, ARRIVE(zero)
    lw      t1, STATUS(zero)            # sees the arrival (6.4)
    expect  t1, STATUS_ARRIVED, 2
    # Out of the canonical wait for a while after the arrival: the tile is ready only
    # once the core waits in its WFI (7.3), DELAY_LOOPS loop turns later.
    li      t1, DELAY_LOOPS
delay:
    addi    t1, t1, -1
    bnez    t1, delay
wait_release:
    wfi                                 # the arrival gate: only the release wakes it
    lw      t1, EVENTS(zero)            # consumes the release
    andi    t1, t1, EVENT_RELEASE
    beqz    t1, wait_release

    # ---- Phase 2 ----
    lw      t1, STATUS(zero)
    expect  t1, 0, 3
    lw      t1, EVENTS_PEEK(zero)
    andi    t1, t1, EVENT_RELEASE
    expect  t1, 0, 4
    lw      a0, ARGUMENT(1)(zero)
    lw      a1, STATUS(zero)
    li      a2, 0
    j       pass
