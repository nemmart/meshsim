/* item 113, case 2: SC.D with rd = x0 (after an LR.D that sets the reservation, so the SC succeeds).
 * jolt-cpp (isa/virtual.cpp:682, SC.D exempt from the remap): the SC.D is 12 rows, with v40 = success, v41 = mem,
 *   v42 = diff:
 *   LUI v40, 0x80000000 ; VirtualAssertLTE 0, v40, a0 ; XOR v40, v33, a0 ; SLTIU v40, v40, 1 ; LD v41, a0, 0 ;
 *   SUB v42, a1, v41 ; MUL v42, v42, v40 ; ADD v42, v41, v42 ; SD a0, v42, 0 ; ADDI v33, x0, 0 ; ADDI v32, x0, 0 ;
 *   XORI x0, v40, 1
 *   The last row names x0: rd_reg 0, rd 0 (the write is dropped). The SD's pre-value is 0x1122334455667788 and its
 *   post-value a1 = 0x55. The LR.D before it (rd = t1) is the ordinary 5-row expansion, its LD writing t1.
 * 735355e (source reading, unconfirmed): SC.D isn't exempt there, so rd = x0 is remapped to v40, the temporaries
 *   shift to v41-v43, and the last row is XORI v40, v41, 1 with rd = 0 (success) written into v40. */
#include "common.inc"
    la    a0, buf
    li    a1, 0x55
    MARK(0x111)
    lr.d  t1, (a0)
    sc.d  x0, a1, (a0)
    MARK(0x222)
    ld    t2, 0(a0)              /* 0x55: the store happened */
    j     .
    .data
    .balign 8
buf: .dword 0x1122334455667788
