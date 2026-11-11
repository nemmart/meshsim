/* Minimal test environment for meshsim v1.2: no CSRs, no traps. PASS/FAIL end with a store to the simulator-only exit
   word (8.15, 0xFFFFF9F8): 1 (pass) or 0 (fail, a1 = test number). Code links at 0x01000000 (program memory), data at
   0x00000010 (data memory, past its reserved first 16 bytes). */
#ifndef _MESHSIM_RISCV_TEST_H
#define _MESHSIM_RISCV_TEST_H
#define RVTEST_RV32U .macro init; .endm
#define RVTEST_RV64U RVTEST_RV32U
#define RVTEST_CODE_BEGIN .section .text.init; .align 2; .globl _start; _start: init; li gp, 0; j test_begin; test_begin:
#define RVTEST_CODE_END
#define TESTNUM gp
#define RVTEST_PASS li a0, 1; sw a0, -0x608(zero); 1: j 1b
#define RVTEST_FAIL mv a1, TESTNUM; li a0, 0; sw a0, -0x608(zero); 1: j 1b
#define RVTEST_DATA_BEGIN .section .data; .align 4; .global begin_signature; begin_signature:
#define RVTEST_DATA_END .align 4; .global end_signature; end_signature:
#endif
