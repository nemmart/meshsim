"""The Tracer's division-family templates, as data (WORKER7; DECISIONS-v1.3 sections 7 and 8).

Each template is the replayer's template for the instruction (the traced rows, exactly as Jolt expands it,
reference tracer jolt-asic-planning 4f36b3d, `isa/virtual.cpp:322-480`) with **untraced rows** inserted
**immediately before the advice row**. The untraced rows compute Jolt's advice value into a bank-1 register with
the Tracer's row ALU (RV64 M division as base operations, section 8, plus the replayer's ALU); the traced advice
row is `VirtualAdvice rd, 0, 0` with `from_log` set and reads the value through `advice_reg`.

Operand symbols (spec 4.19.3; bank-1 codes, DECISIONS-v1.3 section 7):
    'RD', 'RS1', 'RS2'  codes 129, 130, 131: the source instruction's register fields
    't0' .. 't7'        codes 137-144: temporaries, register 40 + k + offset
    'x0'                code 0
    'K'                 code 128: the word's `const` (0 unless given)
    'b0' .. 'b109'      codes 146-255: bank-1 register c (untraced work only)

`gen.py` turns these into 128-bit expansion words; `harness/check_div.cpp` checks them against jolt-cpp.
"""

# Opcode numbers: the tracer's enum order (`isa/riscv.inc`, 4f36b3d). The harness static_asserts every one.
OP = {'NOP': 0, 'ADDI': 2, 'SUB': 3, 'XOR': 4, 'SLTU': 18, 'MUL': 43, 'DIV': 47, 'DIVU': 48, 'REM': 49, 'REMU': 50, 'DIVW': 66, 'DIVUW': 67, 'REMW': 68, 'REMUW': 69, 'VirtualSignExtendWord': 116, 'VirtualZeroExtendWord': 117, 'VirtualAdvice': 126, 'VirtualAssertLTE': 127, 'VirtualAssertValidDiv0': 128, 'VirtualAssertValidUnsignedRemainder': 129, 'VirtualNegateIf': 131, 'VirtualAssertMulUNoOverflow': 134}

# The Tracer row ALU's base operations used only by untraced rows (section 8).
NATIVE_DIV = {'DIV', 'DIVU', 'REM', 'REMU', 'DIVW', 'DIVUW', 'REMW', 'REMUW'}

# The Jolt advice opcode each source instruction's advice row has (the replayer writes it as VirtualAdvice).
JOLT_ADVICE = {
    'DIV': 'VirtualAdviceDiv', 'DIVU': 'VirtualAdviceDivU', 'REM': 'VirtualAdviceRem',
    'REMU': 'VirtualAdviceDivU', 'DIVW': 'VirtualAdviceDivW', 'DIVUW': 'VirtualAdviceDivUW',
    'REMW': 'VirtualAdviceRemW', 'REMUW': 'VirtualAdviceDivUW',
}


def T(op, rd, rs1, rs2, const=0):
    """A traced row."""
    return dict(op=op, rd=rd, rs1=rs1, rs2=rs2, const=const, untraced=False, advice=None)


def U(op, rd, rs1, rs2, const=0):
    """An untraced row (bank-1 rd; never reaches a replayer)."""
    return dict(op=op, rd=rd, rs1=rs1, rs2=rs2, const=const, untraced=True, advice=None)


def A(rd, reg):
    """The traced advice row: VirtualAdvice rd, 0, 0, from_log, reading bank-1 register `reg` via advice_reg."""
    return dict(op='VirtualAdvice', rd=rd, rs1='K', rs2='K', const=0, untraced=False, advice=reg)


# The checks every signed division shares (DIV and DIVW, after the advice row).
def _signed_checks(dividend, divisor, q, dmag, smag, qsign, qmag, rem):
    return [
        T('VirtualAssertValidDiv0', 'K', divisor, q),
        T('VirtualNegateIf', dmag, dividend, dividend),
        T('VirtualNegateIf', smag, divisor, divisor),
        T('XOR', qsign, dividend, divisor),
        T('VirtualNegateIf', qmag, qsign, q),
        T('VirtualAssertMulUNoOverflow', 'K', qmag, smag),
        T('MUL', rem, qmag, smag),
        T('VirtualAssertLTE', 'K', rem, dmag),
        T('SUB', rem, dmag, rem),
        T('VirtualAssertValidUnsignedRemainder', 'K', rem, smag),
    ]


TEMPLATES = {
    # DIVU (isa/virtual.cpp:365). Jolt's advice = RV64 DIVU in every case: one untraced row.
    'DIVU': [
        U('DIVU', 'b0', 'RS1', 'RS2'),
        A('t0', 'b0'),
        T('VirtualAssertValidDiv0', 'K', 'RS2', 't0'),
        T('VirtualAssertMulUNoOverflow', 'K', 't0', 'RS2'),
        T('MUL', 't1', 't0', 'RS2'),
        T('VirtualAssertLTE', 'K', 't1', 'RS1'),
        T('SUB', 't1', 'RS1', 't1'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't1', 'RS2'),
        T('ADDI', 'RD', 't0', 'K'),
    ],
    # REMU (:381): the same advice as DIVU (the quotient).
    'REMU': [
        U('DIVU', 'b0', 'RS1', 'RS2'),
        A('t0', 'b0'),
        T('VirtualAssertMulUNoOverflow', 'K', 't0', 'RS2'),
        T('MUL', 't0', 't0', 'RS2'),
        T('VirtualAssertLTE', 'K', 't0', 'RS1'),
        T('SUB', 't0', 'RS1', 't0'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't0', 'RS2'),
        T('ADDI', 'RD', 't0', 'K'),
    ],
    # DIV (:322). Jolt's advice = RV64 DIV, both edge cases included: one untraced row.
    'DIV': [
        U('DIV', 'b0', 'RS1', 'RS2'),
        A('t0', 'b0'),
        *_signed_checks('RS1', 'RS2', 't0', 't1', 't2', 't3', 't4', 't5'),
        T('ADDI', 'RD', 't0', 'K'),
    ],
    # REM (:346). Jolt's advice is |trunc(rs1/rs2)| (the quotient's magnitude), 0 when rs2 = 0.
    # |DIV| gives it except at rs2 = 0 (|-1| = 1): multiply by (rs2 != 0).
    'REM': [
        U('DIV', 'b0', 'RS1', 'RS2'),
        U('VirtualNegateIf', 'b0', 'b0', 'b0'),
        U('SLTU', 'b1', 'x0', 'RS2'),
        U('MUL', 'b0', 'b0', 'b1'),
        A('t0', 'b0'),
        T('VirtualNegateIf', 't1', 'RS1', 'RS1'),
        T('VirtualNegateIf', 't2', 'RS2', 'RS2'),
        T('VirtualAssertMulUNoOverflow', 'K', 't0', 't2'),
        T('MUL', 't3', 't0', 't2'),
        T('VirtualAssertLTE', 'K', 't3', 't1'),
        T('SUB', 't3', 't1', 't3'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't3', 't2'),
        T('VirtualNegateIf', 'RD', 'RS1', 't3'),
    ],
    # DIVUW (:395). Jolt zero-extends its advice; RV64 DIVUW sign-extends: fix up with VirtualZeroExtendWord.
    'DIVUW': [
        T('VirtualZeroExtendWord', 't0', 'RS1', 'K'),
        T('VirtualZeroExtendWord', 't1', 'RS2', 'K'),
        U('DIVUW', 'b0', 'RS1', 'RS2'),
        U('VirtualZeroExtendWord', 'b0', 'b0', 'K'),
        A('t2', 'b0'),
        T('VirtualAssertMulUNoOverflow', 'K', 't2', 't1'),
        T('MUL', 't3', 't2', 't1'),
        T('VirtualAssertLTE', 'K', 't3', 't0'),
        T('SUB', 't3', 't0', 't3'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't3', 't1'),
        T('VirtualSignExtendWord', 't3', 't2', 'K'),
        T('VirtualAssertValidDiv0', 'K', 't1', 't3'),
        T('ADDI', 'RD', 't3', 'K'),
    ],
    # REMUW (:416): the same advice as DIVUW.
    'REMUW': [
        T('VirtualZeroExtendWord', 't0', 'RS1', 'K'),
        T('VirtualZeroExtendWord', 't1', 'RS2', 'K'),
        U('DIVUW', 'b0', 'RS1', 'RS2'),
        U('VirtualZeroExtendWord', 'b0', 'b0', 'K'),
        A('t2', 'b0'),
        T('VirtualAssertMulUNoOverflow', 'K', 't2', 't1'),
        T('MUL', 't2', 't2', 't1'),
        T('VirtualAssertLTE', 'K', 't2', 't0'),
        T('SUB', 't2', 't0', 't2'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't2', 't1'),
        T('VirtualSignExtendWord', 'RD', 't2', 'K'),
    ],
    # DIVW (:434). Jolt's advice differs from RV64 DIVW only at INT32_MIN / -1 (zero-extended 2^31 against
    # sign-extended). A 64-bit DIV of the sign-extended operands, which the first two traced rows already hold
    # in t0 and t1, gives Jolt's value in every case: one untraced row.
    'DIVW': [
        T('VirtualSignExtendWord', 't0', 'RS1', 'K'),
        T('VirtualSignExtendWord', 't1', 'RS2', 'K'),
        U('DIV', 'b0', 't0', 't1'),
        A('t2', 'b0'),
        *_signed_checks('t0', 't1', 't2', 't3', 't4', 't5', 't6', 't7'),
        T('VirtualSignExtendWord', 'RD', 't2', 'K'),
    ],
    # REMW (:462). Jolt's advice is zx32(|trunc(s32/s32)|), 0 when the divisor's low word is 0. As REM, on the
    # sign-extended operands in t0 and t1.
    'REMW': [
        T('VirtualSignExtendWord', 't0', 'RS1', 'K'),
        T('VirtualSignExtendWord', 't1', 'RS2', 'K'),
        U('DIV', 'b0', 't0', 't1'),
        U('VirtualNegateIf', 'b0', 'b0', 'b0'),
        U('SLTU', 'b1', 'x0', 't1'),
        U('MUL', 'b0', 'b0', 'b1'),
        A('t2', 'b0'),
        T('VirtualNegateIf', 't3', 't0', 't0'),
        T('VirtualNegateIf', 't4', 't1', 't1'),
        T('VirtualAssertMulUNoOverflow', 'K', 't2', 't4'),
        T('MUL', 't5', 't2', 't4'),
        T('VirtualAssertLTE', 'K', 't5', 't3'),
        T('SUB', 't5', 't3', 't5'),
        T('VirtualAssertValidUnsignedRemainder', 'K', 't5', 't4'),
        T('VirtualNegateIf', 'RD', 't0', 't5'),
    ],
}

# Every DIV/REM template's rd = x0 policy: 1, the shared NOP row (virtual); DIV/REM have no side effect beyond rd
# (isa/virtual.cpp:19-55), so with rd = x0 the template isn't used at all (spec 4.19.4).
RD0 = {name: 1 for name in TEMPLATES}
FORMAT_R = 0
