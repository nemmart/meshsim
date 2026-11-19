// Local replacement for the upstream riscv-tests environment header (env/p/riscv_test.h).
//
// A test is linked like the firmware: start.S sets the stack and global pointers
// (the boot load has written .data into DMEM and zeroed .bss), then calls main,
// the test body. No traps, CSRs or ecall are used: the result is reported with one
// store to the test memory's result device. Keep these addresses in step with
// rv32_core_test_memory.

#ifndef SNARKIFY_RISCV_TEST_H
#define SNARKIFY_RISCV_TEST_H

// Result device (test-only). A store to TEST_PASS_ADDRESS reports success; a store
// to TEST_FAIL_ADDRESS reports failure, with the failing test number as the data.
#define TEST_PASS_ADDRESS 0x20000000
#define TEST_FAIL_ADDRESS 0x20000004

// Register holding the number of the test case currently running.
#define TESTNUM gp

// Both register widths map to the same RV32 setup; the rv32 wrappers redefine
// RVTEST_RV64U as RVTEST_RV32U before including the shared rv64 test bodies.
#define RVTEST_RV32U .macro init; .endm
#define RVTEST_RV64U .macro init; .endm

// norvc: upstream builds these tests without C, so every instruction keeps its
// 32-bit encoding.
// norelax: TESTNUM is gp, so the linker must not rewrite addresses as gp-relative.
#define RVTEST_CODE_BEGIN \
        .section .text.init; \
        .option norvc; \
        .option norelax; \
        .align 2; \
        .globl main; \
main: \
        init; \
        li TESTNUM, 0;

// Execution never reaches this point: TEST_PASSFAIL always branches to pass or fail.
#define RVTEST_CODE_END \
        unimp

#define RVTEST_PASS \
        li t0, TEST_PASS_ADDRESS; \
        sw zero, 0(t0); \
1:      j 1b;

#define RVTEST_FAIL \
        li t0, TEST_FAIL_ADDRESS; \
        sw TESTNUM, 0(t0); \
1:      j 1b;

#define RVTEST_DATA_BEGIN .align 4;

#define RVTEST_DATA_END .align 4;

#endif
