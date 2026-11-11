/* reqfull (M1, sim/planning/DECISIONS.md D2): the request FIFO refusing REQUEST_SEND (Controller.execute's stall,
 * RequestPort.canPush; 4.18), the test bench's third event. 190 report requests to C&C 1 (target 0), two stores each
 * (REQUEST_SET_W0, REQUEST_SEND; W0-W2 clear at every push), back to back: one push per 2 cycles against the FIFO's
 * drain of one 8-beat request per 8 cycles, so the 128-entry FIFO fills after about 170 and each later send stalls
 * until the bus frees an entry. Nothing pops C&C 1's receive queue (128 entries; TraceRun leaves C&C's report drain
 * off): the run forwards far fewer than 128, so the FIFO keeps draining. STATUS before and after, mcycle around the burst to res[]; SIM_EXIT = 6. */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    la    s0, res
    li    s1, REQ_OP_REPORT << 10           /* REQ_CC1(REQ_OP_REPORT, 0, 0): target 0, op 1 (mesh.h's form is C) */
    li    s4, 0x5EED
    lw    t1, X0REL(OFF_STATUS)(zero)
    sw    t1, 0(s0)
    csrr  s2, mcycle
    .rept 190
    sw    s1, X0REL(OFF_REQUEST_SET_W0)(zero)
    sw    s4, X0REL(OFF_REQUEST_SEND)(zero)
    .endr
    csrr  s3, mcycle
    lw    t1, X0REL(OFF_STATUS)(zero)
    lw    t2, X0REL(OFF_EVENTS_PEEK)(zero)
    sw    s2, 4(s0)
    sw    s3, 8(s0)
    sw    t1, 12(s0)
    sw    t2, 16(s0)
    li    a0, 6
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
1:  j     1b

    .data
    .balign 4
res: .space 32
