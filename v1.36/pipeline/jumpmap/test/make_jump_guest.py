#!/usr/bin/env python3
"""A hand-assembled guest with one jump of every kind the jump map's static analysis knows, and one it can't see
(WORKER26). Uses gold's guest encoders and ELF writer (wip/gold/test/guests/make_guests.py).

    python3 make_jump_guest.py <out dir>      writes <out dir>/jumps/{program.elf, program.json}

.text: a call through a code pointer in .rodata (data kind), a JAL call (direct, return), a far call AUIPC+JALR
(auipc call), a call through AUIPC+ADDI (auipc address), a call through a pointer made by AUIPC+ADDI+ADDI (seen only in
the run), a counted loop (a backward branch), then jal x0, 0. Each function returns with jalr x0, ra, 0.
"""
import json, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../gold/test/guests"))
import make_guests as g

RA, T1, T2, T3, T4, A0, A1, A2 = 1, 6, 7, 28, 29, 10, 11, 12

def jal(rd, off):
    o = off & 0x1fffff
    return ((o >> 20) & 1) << 31 | ((o >> 1) & 0x3ff) << 21 | ((o >> 11) & 1) << 20 | ((o >> 12) & 0xff) << 12 | rd << 7 | 0x6f
def bne(rs1, rs2, off): return g.b_type(0x63, 1, rs1, rs2, off)
def ret(): return g.jalr(0, RA, 0)

def program():
    # (label or None, instruction builder taking (pc, labels)) pairs; every instruction 4 bytes
    P = []
    def ins(f, label=None): P.append((label, f))
    def hi_lo(target, pc):
        hi = (target - pc + 0x800) >> 12
        return hi, target - pc - (hi << 12)
    ins(lambda pc, L: g.auipc(A0, hi_lo(L["table"], pc)[0]))
    ins(lambda pc, L: g.addi(A0, A0, hi_lo(L["table"], pc - 4)[1]))
    ins(lambda pc, L: g.ld(T1, A0, 0))
    ins(lambda pc, L: g.jalr(RA, T1, 0))                                   # -> f2, a code pointer in .rodata
    ins(lambda pc, L: jal(RA, L["f1"] - pc))                               # -> f1, direct
    ins(lambda pc, L: g.auipc(T2, hi_lo(L["f3"], pc)[0]))
    ins(lambda pc, L: g.jalr(RA, T2, hi_lo(L["f3"], pc - 4)[1]))          # -> f3, far call
    ins(lambda pc, L: g.auipc(T3, hi_lo(L["f4"], pc)[0]))
    ins(lambda pc, L: g.addi(T3, T3, hi_lo(L["f4"], pc - 4)[1]))
    ins(lambda pc, L: g.jalr(RA, T3, 0))                                   # -> f4, an AUIPC+ADDI address
    ins(lambda pc, L: g.auipc(T4, hi_lo(L["f4"], pc)[0]))
    ins(lambda pc, L: g.addi(T4, T4, hi_lo(L["f4"], pc - 4)[1]))
    ins(lambda pc, L: g.addi(T4, T4, L["f5"] - L["f4"]))
    ins(lambda pc, L: g.jalr(RA, T4, 0))                                   # -> f5, only the run sees it
    ins(lambda pc, L: g.addi(A1, 0, 3))
    ins(lambda pc, L: g.addi(A1, A1, -1), "loop")
    ins(lambda pc, L: bne(A1, 0, L["loop"] - pc))                          # a backward branch, taken twice
    ins(lambda pc, L: g.JAL_SELF)
    for f in ("f1", "f2", "f3", "f4", "f5"):
        ins(lambda pc, L: g.addi(A2, A2, 1), f)
        ins(lambda pc, L: ret())
    if len(P) % 2: ins(lambda pc, L: g.addi(0, 0, 0))                       # .rodata 8-byte aligned
    labels = {lab: g.RAM_START + 4 * i for i, (lab, _) in enumerate(P) if lab}
    labels["table"] = g.RAM_START + 4 * len(P)
    code = [f(g.RAM_START + 4 * i, labels) for i, (_, f) in enumerate(P)]
    rodata = struct.pack("<Q", labels["f2"])
    return [(".text", g.words(code), True), (".rodata", rodata, False)]

def main():
    d = os.path.join(sys.argv[1] if len(sys.argv) > 1 else ".", "jumps")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "program.elf"), "wb") as f: f.write(g.elf_sections(program()))
    with open(os.path.join(d, "program.json"), "w") as f: json.dump(g.PROGRAM_JSON, f, indent=2); f.write("\n")
    print("jumps: written to", d)

if __name__ == "__main__":
    main()
