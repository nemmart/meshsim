# Instruction-set edge cases that need exact encodings, built with the pinned GNU
# assembler and run through start.S. main returns 0 when every check passes, or
# the number of the first failing check.
#
# Trap handler: counts every trap in s9, records mcause and mtval in s10 and s11,
# then resumes after the trapping instruction. IMEM cannot be read as data, so it
# learns that instruction's length from mtval for an illegal instruction (its
# encoding: bits 1:0 == 11 means 32 bits); every other trapping instruction here is
# 32 bits (the file is norvc). Check 47 requires exactly the traps this program
# causes on purpose, so an unexpected fault anywhere (even a stack store) cannot
# be skipped silently.

    .text
    .option norvc
    # No linker relaxation: it would shorten calls to c.jal and move the aligned
    # labels below (the trap handler, whose address goes into mtvec) off alignment.
    .option norelax

# Fail with check number \number unless \register == \expected (clobbers t6).
.macro expect register, expected, number
    li      t6, \expected
    beq     \register, t6, 1f
    li      a0, \number
    j       done
1:
.endm

# Fail with check number \number unless \first == \second.
.macro expect_equal first, second, number
    beq     \first, \second, 1f
    li      a0, \number
    j       done
1:
.endm

    .globl main
main:
    la      t0, trap_handler
    csrw    mtvec, t0
    li      s9, 0                       # traps taken so far
    addi    sp, sp, -16
    sw      ra, 12(sp)

    # ---- 1-6: all six CSR forms on mscratch ----
    li      t0, 0x0f0f0f0f
    csrrw   t1, mscratch, t0            # CSRRW
    csrrs   t1, mscratch, zero          # read without writing
    expect  t1, 0x0f0f0f0f, 1
    li      t0, 0x30000003
    csrrs   t1, mscratch, t0            # CSRRS returns the old value
    expect  t1, 0x0f0f0f0f, 2
    li      t0, 0x0f00000f
    csrrc   t1, mscratch, t0            # CSRRC
    expect  t1, 0x3f0f0f0f, 3
    csrrwi  t1, mscratch, 21            # CSRRWI
    expect  t1, 0x300f0f00, 4
    csrrsi  t1, mscratch, 8             # CSRRSI: 21 | 8 = 29
    csrrci  t1, mscratch, 1             # CSRRCI returns 29, leaves 28
    expect  t1, 0x0000001d, 5
    csrr    t1, mscratch
    expect  t1, 0x0000001c, 6

    # ---- 7-9: read-only CSRs: reads never trap, only real writes do ----
    li      s10, 0
    csrrs   t1, mhartid, zero           # rs1 = x0: no write, no trap
    csrrsi  t1, mhartid, 0              # zero immediate: no write, no trap
    expect  s10, 0, 7
    csrrw   zero, mhartid, t1           # a real write: illegal instruction
    expect  s10, 2, 8
    csrr    t1, misa                    # misa: RV32 with I and C, no M, no A
    expect  t1, 0x40000104, 9

    # ---- 10-19: unimplemented extensions trap with the original encoding ----
    # The image audit accepts unimplemented instructions only between these labels.
    .globl unimplemented_instructions_start
unimplemented_instructions_start:
    .option push
    .option arch, +m, +a, +zifencei, +f
    li      s10, 0
    div     t1, t2, t3
    expect  s10, 2, 10
    la      t2, divide_encoding
    lw      t2, 0(t2)                   # the same div, as data in DMEM
    expect_equal s11, t2, 11
    li      s10, 0
    remu    t1, t2, t3
    expect  s10, 2, 12
    li      s10, 0
    amoadd.w t1, t2, (sp)
    expect  s10, 2, 13
    li      s10, 0
    lr.w    t1, (sp)
    expect  s10, 2, 14
    li      s10, 0
    fence.i
    expect  s10, 2, 15
    li      s10, 0
    flw     ft0, 0(sp)
    expect  s10, 2, 16
    .option pop
    li      s10, 0
    .insn   2, 0x6000                   # C.FLW: a 16-bit illegal instruction
    expect  s10, 2, 17
    expect  s11, 0x6000, 18             # mtval holds the 16 bits, not an expansion
    li      s10, 0
    .half   0x0000                      # the all-zero instruction: 16 bits (bits 1:0 = 00), illegal
    expect  s10, 2, 19
    .globl unimplemented_instructions_end
unimplemented_instructions_end:

    # ---- 20-23: ECALL and EBREAK resume after the instruction ----
    li      s10, 0
    ecall
    expect  s10, 11, 20
    expect  s11, 0, 21
    li      s10, 0
    la      t5, breakpoint
breakpoint:
    ebreak
    expect  s10, 3, 22
    expect  s11, 0, 23                  # the handler subtracts the breakpoint address from mtval

    # ---- 24-25: FENCE does nothing; counters advance ----
    # No WFI here: program_test raises no interrupt, so a WFI would wait forever
    # (core contract). rv32_core_test scenario M checks WFI.
    csrr    t0, mcycle
    csrr    t1, minstret
    fence
    fence   rw, rw
    csrr    t2, minstret
    sub     t2, t2, t1                  # the first csrr and two fences retired in between
    expect  t2, 3, 24
    csrr    t3, mcycle
    sltu    t3, t0, t3
    expect  t3, 1, 25

    # ---- 26-29: software division from libgcc (no divide instruction) ----
    li      a0, -100
    li      a1, 7
    call    __divsi3
    expect  a0, -14, 26
    li      a0, -100
    li      a1, 7
    call    __modsi3
    expect  a0, -2, 27
    li      a0, 0xfffffff0
    li      a1, 0x10
    call    __udivsi3
    expect  a0, 0x0fffffff, 28
    li      a0, 12345
    li      a1, 0
    call    __udivsi3                   # division by zero gives all ones, as DIVU would
    expect  a0, 0xffffffff, 29

    # ---- 30-32: IMEM serves fetches only: rv32_core's data decoder faults a store
    # and a load there, which reach no target. A fetch outside IMEM cannot be
    # checked here: Whisper does not fault one (rv32_core_test's scenario D checks it).
    li      s10, 0
    la      t0, imem_target
    li      t1, 0xdeadbeef
    sw      t1, 0(t0)                   # a store to IMEM: store access fault
    expect  s10, 7, 30
    expect_equal s11, t0, 31            # mtval is the store's address
    li      s10, 0
    lw      t1, 0(t0)                   # a load from IMEM: load access fault
    expect  s10, 5, 32

    # ---- 33-40: Zicond. czero.eqz rd, rs1, rs2 is 0 if rs2 is zero, else rs1;
    # czero.nez the reverse. The whole of rs2 is the condition (bit 31 alone is
    # nonzero); x0 as rs2 is zero; x0 as rd writes nothing and traps nothing (check
    # 47 counts every trap); operands come from an ALU result, a load and a multiply
    # right before; a result goes straight into a branch. riscv-tests' rv32uzicond
    # covers the register combinations.
    li        t0, 0x12345678
    li        t1, 0x80000000             # lui: forwarded into the czero below
    czero.eqz t2, t0, t1
    expect    t2, 0x12345678, 33
    czero.nez t2, t0, t1
    expect_equal t2, zero, 34            # result forwarded into a branch
    czero.eqz t2, t0, zero
    expect    t2, 0, 35
    czero.nez t2, t0, zero
    expect    t2, 0x12345678, 36
    czero.eqz zero, t0, t1               # rd = x0
    sw        zero, 0(sp)
    lw        t3, 0(sp)                  # a zero condition from a load right before
    czero.nez t2, t0, t3
    expect    t2, 0x12345678, 37
    li        t4, 3
    mul       t4, t4, t0                 # 0x369d0368, a multiply right before
    czero.eqz t2, t4, t1
    expect    t2, 0x369d0368, 38
    li        t5, 0xabcd                 # select idiom: c ? t0 : t5
    li        t3, 2                      # c = bit 1 only
    czero.eqz a1, t0, t3
    czero.nez a2, t5, t3
    or        a1, a1, a2
    expect    a1, 0x12345678, 39
    czero.eqz a1, t0, zero               # c = 0
    czero.nez a2, t5, zero
    or        a1, a1, a2
    expect    a1, 0xabcd, 40

    # ---- 41-42: mie and mcause keep exactly their writable bits: mie's is
    # MEIE alone, mcause's the interrupt bit and a 5-bit code. MIE is clear and
    # program_test raises no interrupt, so enabling it takes nothing. ----
    li      t0, -1
    csrw    mie, t0
    csrr    t1, mie
    csrw    mie, zero
    expect  t1, 0x00000800, 41
    csrr    t2, mcause
    csrw    mcause, t0
    csrr    t1, mcause
    csrw    mcause, t2
    expect  t1, 0x8000001f, 42

    # ---- 43-46: conditional branches compare all 32 bits: values that differ in
    # bit 31 alone are unequal, and 0x8000_0000 is below 0 signed and above it
    # unsigned. riscv-tests' BEQ and BNE tests compare no two values that differ in
    # bit 31 alone. ----
    li      t0, 0x80000000
    li      a0, 43
    beq     t0, zero, done              # not taken
    li      a0, 44
    bne     t0, zero, 1f                # taken
    j       done
1:
    li      a0, 45
    bge     t0, zero, done              # not taken: below 0, signed
    li      a0, 46
    bgeu    zero, t0, done              # not taken: 0 is below it, unsigned

    # ---- 47: exactly the 13 deliberate traps above, and no others ----
    expect  s9, 13, 47

    li      a0, 0
done:
    lw      ra, 12(sp)
    addi    sp, sp, 16
    ret

# A word in IMEM that checks 30-32 try to store to and load from: an instruction
# (addi x0, x0, 1445) that is never executed.
    .balign 4
imem_target:
    .word   0x5a500013

# Check 11's div encoded by the assembler as data, in DMEM: data, never executed,
# so the image audit (which disassembles code alone) does not see it.
    .section .data
    .balign 4
divide_encoding:
    .option push
    .option arch, +m
    div     t1, t2, t3
    .option pop
    .text

# ---- Trap handler: record and skip the trapping instruction ----
    .balign 4
trap_handler:
    addi    s9, s9, 1
    csrr    s10, mcause
    csrr    s11, mtval
    csrr    t4, mepc
    li      t6, 3
    bne     s10, t6, 1f
    sub     s11, s11, t5                # breakpoint: mtval must equal its address
1:
    addi    t4, t4, 4                   # a 32-bit instruction, unless:
    li      t6, 2
    bne     s10, t6, 2f
    andi    t6, s11, 3                  # an illegal instruction's encoding
    li      t3, 3
    beq     t6, t3, 2f
    addi    t4, t4, -2                  # a 16-bit illegal instruction
2:
    csrw    mepc, t4
    mret
