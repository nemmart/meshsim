#!/usr/bin/env python3
"""Hand-assembled test guests for the paths no workload or jolt-cpp example reaches (WORKER18, stage 3).

Each guest is a directory with program.elf and program.json, laid out as jolt-cpp's loader requires (loader.cpp:25-190:
ELF64 little-endian RISC-V executable, one PT_LOAD executable segment at 0x8000_0000 aligned to 4096) and, for upstream
Jolt's decoder (jolt-program image/elf.rs: sections, not segments), a .text section covering the same bytes.
Every guest ends with `jal x0, 0`: jolt-cpp ends the run on a JAL with immediate 0 (trace_owner.cpp:277-283), upstream
on the pc stall it causes.

    python3 make_guests.py [out dir]      (default: this script's directory)
"""
import json
import os
import struct
import sys

RAM_START = 0x8000_0000
STACK_DATA = 0x8008_0000      # inside the stack region (stack_size below), used as scratch memory

# ---- RV64 encoders ----
def r_type(op, rd, f3, rs1, rs2, f7): return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op
def i_type(op, rd, f3, rs1, imm): return (imm & 0xfff) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op
def s_type(op, f3, rs1, rs2, imm): return ((imm >> 5) & 0x7f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm & 0x1f) << 7 | op
def b_type(op, f3, rs1, rs2, imm):
    return ((imm >> 12) & 1) << 31 | ((imm >> 5) & 0x3f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | \
           ((imm >> 1) & 0xf) << 8 | ((imm >> 11) & 1) << 7 | op
def addi(rd, rs1, imm): return i_type(0x13, rd, 0, rs1, imm)
def lui(rd, imm20): return (imm20 & 0xfffff) << 12 | rd << 7 | 0x37
def ld(rd, rs1, imm): return i_type(0x03, rd, 3, rs1, imm)
def lw(rd, rs1, imm): return i_type(0x03, rd, 2, rs1, imm)
def sd(rs1, rs2, imm): return s_type(0x23, 3, rs1, rs2, imm)
def auipc(rd, imm20): return (imm20 & 0xfffff) << 12 | rd << 7 | 0x17
def jalr(rd, rs1, imm): return i_type(0x67, rd, 0, rs1, imm)
JAL_SELF = 0x0000006f        # jal x0, 0
EBREAK = 0x00100073
ECALL = 0x00000073
FENCE = 0x0ff0000f           # fence iorw, iorw
FENCE_TSO = 0x8330000f       # fence.tso (funct3 0)
FENCE_I = 0x0000100f         # fence.i (MISC-MEM funct3 1): UNIMP for jolt-cpp (decoder.cpp:1196-1201)
def inline(f7, f3, rd, rs1, rs2): return r_type(0x0B, rd, f3, rs1, rs2, f7)
def assert_eq(rs1, rs2): return b_type(0x5B, 1, rs1, rs2, 0)     # VirtualAssertEQ (decoder.cpp:1251-1268)
A0, A1, A2, A7, T0, SP = 10, 11, 12, 17, 5, 2

def la(rd, target, pc=RAM_START):
    """rd = target, pc-relative (auipc, addi): lui would sign-extend an address >= 0x8000_0000 on RV64."""
    hi = (target - pc + 0x800) >> 12
    return [auipc(rd, hi), addi(rd, rd, target - pc - (hi << 12))]

GUESTS = {
    # EBREAK: jolt-cpp expands it to `JAL vN, 0` (virtual.cpp:1134-1164), which ends the run; its JIT raises ERR_EBREAK
    "ebreak": [addi(A0, 0, 5), EBREAK, addi(A0, 0, 6), JAL_SELF],
    "fence": [addi(A0, 0, 1), FENCE, FENCE_TSO, addi(A0, A0, 1), JAL_SELF],
    # KECCAK256 (no absorb) on a zero state in scratch memory
    "keccak256": la(A0, STACK_DATA) + [inline(1, 0, 0, A0, 0), ld(A1, A0, 0), JAL_SELF],
    # the custom VirtualAssertEQ opcode, passing (a0 == a0)
    "assert_eq_pass": [addi(A0, 0, 7), addi(A1, 0, 7), assert_eq(A0, A1), JAL_SELF],
    # failing (an abort path)
    "assert_eq_fail": [addi(A0, 0, 1), addi(A1, 0, 2), assert_eq(A0, A1), JAL_SELF],
    # raw ECALL with no trap runtime: jolt-cpp detects LegacyRawEcall and traces it as VirtualHostIO
    # (host_io_encoding.cpp); a0 = 0 is no known syscall (ignored, syscalls.cpp)
    "legacy_raw_ecall": [addi(A0, 0, 0), ECALL, addi(A1, 0, 3), JAL_SELF],
    # misaligned doubleword load (abort path)
    "misaligned_ld": la(T0, STACK_DATA) + [ld(A0, T0, 1), JAL_SELF],
    # misaligned narrow load: LW at +2 (abort path; the floored word address keeps the misalignment)
    "misaligned_lw": la(T0, STACK_DATA) + [lw(A0, T0, 2), JAL_SELF],
    # an instruction jolt-cpp decodes as UNIMP, then removes from the bytecode (virtual.cpp:1357-1363)
    "unimp_fence_i": [addi(A0, 0, 1), FENCE_I, addi(A0, A0, 1), JAL_SELF],
    # a jump to the middle of a 4-byte instruction (no expansion starts there)
    "jump_into_gap": [auipc(T0, 0), addi(T0, T0, 10), jalr(0, T0, 0), addi(A0, 0, 1), JAL_SELF],
    # a store into upstream's stack canary, the 128 bytes after the program (program_end = 0x8000_0000 + code size)
    "stack_canary_store": None,    # filled below: needs the code size
}

def csr(funct3, rd, rs1_or_uimm, csr_addr): return csr_addr << 20 | rs1_or_uimm << 15 | funct3 << 12 | rd << 7 | 0x73
MSTATUS, MHARTID = 0x300, 0xF14
UNIMP32 = 0xC0001073         # unimp (csrrw x0, cycle, x0): a standard UNIMP encoding
C_UNIMP_PAIR = 0x00000000    # two c.unimp (0x0000) halfwords: the other standard UNIMP encoding

# The owner's ruling on UPSTREAM-DIFFS D4 (DECISIONS-v1.3 section 17): strict loading rejects any instruction Jolt
# can't prove; the standard UNIMP encodings load as unreachable markers and throw only if executed; permissive mode
# (ACT4) throws on the unprovable ones when executed.
UNPROVABLE = {
    "unprovable_csrrc": csr(3, A0, A1, MSTATUS),
    "unprovable_csrrwi": csr(5, A0, 5, MSTATUS),
    "unprovable_csrrsi": csr(6, A0, 5, MSTATUS),
    "unprovable_csrrci": csr(7, A0, 5, MSTATUS),
    "unprovable_csr_mhartid": csr(2, A0, 0, MHARTID),     # csrr a0, mhartid: a CSR outside the six
    "unprovable_wfi": 0x10500073,
    "unprovable_sret": 0x10200073,
    "unprovable_sfence_vma": 0x12000073,
    "unprovable_undecodable": 0x0000007F,                 # opcode 0x7F: no instruction
}
for name, word in UNPROVABLE.items():
    GUESTS[name] = [addi(A0, 0, 1), word, addi(A0, A0, 1), JAL_SELF]
# standard UNIMP encodings after the program's end, never reached: load and run normally
GUESTS["unimp_unreached"] = [addi(A0, 0, 1), JAL_SELF, C_UNIMP_PAIR]     # c.unimp after the end: loads
GUESTS["unimp32_unreached"] = [addi(A0, 0, 1), JAL_SELF, UNIMP32]        # 0xC0001073 anywhere: rejected (D4)
# executed: falling into each encoding, and jumping to c.unimp
GUESTS["unimp32_executed"] = [addi(A0, 0, 1), UNIMP32, JAL_SELF]
GUESTS["c_unimp_executed"] = [addi(A0, 0, 1), C_UNIMP_PAIR, JAL_SELF]
GUESTS["c_unimp_jump"] = [auipc(T0, 0), addi(T0, T0, 16), jalr(0, T0, 0), JAL_SELF, C_UNIMP_PAIR, JAL_SELF]

def canary_guest():
    end = RAM_START + 4 * 6           # six instructions: program_end is 8-aligned
    return la(T0, end) + [addi(A0, 0, 9), sd(T0, A0, 0), addi(0, 0, 0), JAL_SELF]   # [program_end] = 9

GUESTS["stack_canary_store"] = canary_guest()

PROGRAM_JSON = {
    "max_trace_length": 65536,
    "compressed_chunk_size": 512,
    "mem_conf": {
        "max_untrusted_advice_size": 0, "max_trusted_advice_size": 0, "max_input_size": 8, "max_output_size": 8,
        "stack_size": 0x100000, "memory_size": 0x100000,
    },
}

def elf(code_words):
    text = b"".join(struct.pack("<I", w) for w in code_words)
    shstrtab = b"\0.text\0.shstrtab\0"
    text_off = 0x1000
    shstr_off = text_off + len(text)
    shoff = (shstr_off + len(shstrtab) + 7) & ~7
    ehdr = struct.pack("<4sBBBBB7xHHIQQQIHHHHHH", b"\x7fELF", 2, 1, 1, 0, 0,
                       2, 243, 1, RAM_START, 64, shoff, 0, 64, 56, 1, 64, 3, 2)
    # PT_LOAD, R|X (flags 5), the code at 0x8000_0000, aligned to 4096 (jolt-cpp requires p_align = 4096)
    phdr = struct.pack("<IIQQQQQQ", 1, 5, text_off, RAM_START, RAM_START, len(text), len(text), 0x1000)
    null = bytes(64)
    # .text: PROGBITS, ALLOC|EXECINSTR; .shstrtab: STRTAB
    sh_text = struct.pack("<IIQQQQIIQQ", 1, 1, 6, RAM_START, text_off, len(text), 0, 0, 4, 0)
    sh_str = struct.pack("<IIQQQQIIQQ", 7, 3, 0, 0, shstr_off, len(shstrtab), 0, 0, 1, 0)
    out = bytearray(ehdr + phdr)
    out += bytes(text_off - len(out))
    out += text + shstrtab
    out += bytes(shoff - len(out))
    out += null + sh_text + sh_str
    return bytes(out)

# ---- multi-section guests: several sections back to back inside ONE executable PT_LOAD (the owner's D1 ruling:
# a16z's image and code are by sections; jolt-cpp decodes the whole executable segment) ----
def words(ws): return b"".join(struct.pack("<I", w) for w in ws)

def elf_sections(pieces):
    """pieces: (name, bytes, text?) laid out from 0x8000_0000, each 4-aligned; one PT_LOAD R|X covers them all."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n, _, _ in pieces) + b".shstrtab\0"
    layout, addr = [], RAM_START
    for n, data, text in pieces:
        layout.append((n, addr, data, text))
        addr += (len(data) + 3) & ~3
    image = b"".join(d + bytes(((len(d) + 3) & ~3) - len(d)) for _, d, _ in pieces)
    text_off = 0x1000
    shstr_off = text_off + len(image)
    shoff = (shstr_off + len(names) + 7) & ~7
    nsec = len(pieces) + 2
    ehdr = struct.pack("<4sBBBBB7xHHIQQQIHHHHHH", b"\x7fELF", 2, 1, 1, 0, 0, 2, 243, 1, RAM_START, 64, shoff, 0, 64,
                       56, 1, 64, nsec, nsec - 1)
    phdr = struct.pack("<IIQQQQQQ", 1, 5, text_off, RAM_START, RAM_START, len(image), len(image), 0x1000)
    shdrs = bytes(64)
    name_at = 1
    for n, a, d, text in layout:
        shdrs += struct.pack("<IIQQQQIIQQ", name_at, 1, 6 if text else 2, a, text_off + (a - RAM_START), len(d), 0, 0, 4, 0)
        name_at += len(n) + 1
    shdrs += struct.pack("<IIQQQQIIQQ", name_at, 3, 0, 0, shstr_off, len(names), 0, 0, 1, 0)
    out = bytearray(ehdr + phdr)
    out += bytes(text_off - len(out))
    out += image + names
    out += bytes(shoff - len(out))
    out += shdrs
    return bytes(out)

def d1_rodata_in_text():
    # .text reads a doubleword from .rodata (placed right after it); .rodata isn't decodable (all ones)
    code = la(A0, RAM_START + 16) + [ld(A1, A0, 0), JAL_SELF]
    return [(".text", words(code), True), (".rodata", bytes([0xff] * 8), False)]

def d1_decodable_gap():
    # .text, a jump over .rodata (four decodable words: nop), then .text.b; jolt-cpp decodes the four words as code,
    # a16z doesn't, so every bytecode index after them differs by four
    gap = words([0x00000013] * 4)
    part1 = [addi(A0, 0, 1), 0]                                   # then jal x0, +(4 + 16)
    part1[1] = (((4 + 16) >> 1) & 0x3ff) << 21 | 0x6f              # jal x0, 20 (small positive offset)
    part2 = [addi(A0, A0, 1), addi(A0, A0, 1), addi(A0, A0, 1), JAL_SELF]
    return [(".text", words(part1), True), (".rodata", gap, False), (".text.b", words(part2), True)]

def d12_c_slli_x0():
    # c.slli x0, 1 (0x0006, a HINT) and c.nop (0x0001) between two ADDIs
    code = struct.pack("<I", addi(A0, 0, 1)) + struct.pack("<HH", 0x0006, 0x0001) + words([addi(A0, A0, 1), JAL_SELF])
    return [(".text", code, True)]

# rd = x0 on a side-effect-free instruction: jolt-cpp rewrites it to ADDI x0, x0, 0 flagged virtual (the opcode
# changed, virtual.cpp:174-189); a16z's rewrite isn't flagged (found with d12_c_slli_x0)
GUESTS["rd0_add"] = [addi(A0, 0, 1), r_type(0x33, 0, 0, A0, A0, 0), addi(A0, A0, 1), JAL_SELF]

SECTION_GUESTS = {"d1_rodata_in_text": d1_rodata_in_text(), "d1_decodable_gap": d1_decodable_gap(),
                  "d12_c_slli_x0": d12_c_slli_x0()}

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for name, code in GUESTS.items():
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "program.elf"), "wb") as f:
            f.write(elf(code))
        with open(os.path.join(d, "program.json"), "w") as f:
            json.dump(PROGRAM_JSON, f, indent=2)
            f.write("\n")
        print(f"{name}: {len(code)} instructions")
    for name, pieces in SECTION_GUESTS.items():
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "program.elf"), "wb") as f:
            f.write(elf_sections(pieces))
        with open(os.path.join(d, "program.json"), "w") as f:
            json.dump(PROGRAM_JSON, f, indent=2)
            f.write("\n")
        print(f"{name}: sections " + ", ".join(f"{n} ({len(b)} bytes{', text' if t else ''})" for n, b, t in pieces))

if __name__ == "__main__":
    main()
