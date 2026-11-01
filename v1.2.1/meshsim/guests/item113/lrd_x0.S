/* item 113, case 1: LR.D with rd = x0.
 * jolt-cpp (isa/virtual.cpp:649, LR.D exempt from the rd = x0 remap, :68-88): 5 rows, virtual, the first is_first:
 *   LUI v40, 0x80000000 ; VirtualAssertLTE 0, v40, a0 ; ADDI v33, a0, 0 ; ADDI v32, a0, 0 ; LD x0, a0, 0
 *   The LD row: rd_reg 0, rd 0, rd_prev 0, mem_addr = a0, mem_value 0 (F4), and it consumes one read-log word (the
 *   doubleword at buf). seq_remaining 4..0; pc of every row = the LR.D's address; is_compressed 0.
 * 735355e (source reading, unconfirmed): LR.D isn't exempt there, so rd = x0 is remapped to v40 and the
 *   temporaries shift to v41: LUI v41 ; AssertLTE 0, v41, a0 ; ADDI v33 ; ADDI v32 ; LD v40, a0, 0 with rd = mem
 *   = the doubleword (0x1122334455667788). */
#include "common.inc"
    la    a0, buf
    MARK(0x111)
    lr.d  x0, (a0)
    MARK(0x222)
    j     .
    .data
    .balign 8
buf: .dword 0x1122334455667788
