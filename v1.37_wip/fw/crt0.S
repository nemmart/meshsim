/* Program start-up (meshsim v1.2; v1.36): entered from the boot ROM's jump (6.5) with the code in program memory and .rodata/.data
 * in data memory. Sets the stack, zeroes .bss, calls main(); when main returns, its return value goes to the master
 * tile as a request (v1.36 has no exit word: REQ_OP_RESULT, port/mesh.h), its reply awaited on counter 3, and the
 * image ends: a jump to the reset PC (6.5), whose boot ROM sends the next LOAD. */
#include "mesh.h"
#define REQ_EXIT  ((1 << 10) * REQ_OP_RESULT + (1 << 18) * CTR_REPLY + (1 << 21) * (REPLY_WORD(DST_CC1) >> 4))
    .section .text.init
    .globl _start
_start:
    li    sp, STACK_TOP
    la    t1, _bss_start
    la    t2, _bss_end
1:  bgeu  t1, t2, 2f
    sw    zero, 0(t1)
    addi  t1, t1, 4
    j     1b
2:  call  main
    li    t0, 1                                         /* the reply's counter, armed first (11.4) */
    sw    t0, X0REL(OFF_WRITE_COUNTER_1 + 4 * (CTR_REPLY - 1))(zero)
    sw    a0, X0REL(OFF_REQUEST_SET_W1)(zero)           /* word 1 main's return value; word 3 ERRORS */
    lw    t1, X0REL(OFF_ERRORS)(zero)
    li    t0, REQ_EXIT
    sw    t0, X0REL(OFF_REQUEST_SET_W0)(zero)
    sw    t1, X0REL(OFF_REQUEST_SEND)(zero)
    li    t0, 1 << (6 + CTR_REPLY)                      /* EV_CTR(CTR_REPLY), consumed by the load of EVENTS */
    sw    t0, X0REL(OFF_EVENTS_MASK)(zero)
3:  wfi
    lw    t1, X0REL(OFF_EVENTS)(zero)
    and   t1, t1, t0
    beqz  t1, 3b
    li    t0, RESET_PC                                  /* the image's end (6.5), every counter at 0 */
    jr    t0

/* the master's end of the run (sim_exit, mesh.h), which the test reads */
    .section .data
    .balign 4
    .globl sim_exit_word
sim_exit_word:
    .word 0, 0
