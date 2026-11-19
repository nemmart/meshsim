#!/usr/bin/env python3
"""A hand-assembled guest whose segments carry advice and write memory (v1.37, WORKER3): a loop of 120 rounds, each an
unsigned division and a remainder (both take advice values: the quotient and the remainder are advised, then checked)
and a store of their sum, the stores walking across a page boundary of the stack region. Cut into segments of 50 rows,
nearly every segment has advice values and writes memory. Uses gold's guest encoders and ELF writer
(pipeline/gold/test/guests/make_guests.py), as pipeline/jumpmap/test/make_jump_guest.py does.

    python3 make_div_guest.py <out dir>      writes <out dir>/divs/{program.elf, program.json}
"""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../pipeline/gold/test/guests"))
import make_guests as g

S0, A1, A2, A3, A4, A5 = 8, 11, 12, 13, 14, 15

def divu(rd, rs1, rs2): return g.r_type(0x33, rd, 5, rs1, rs2, 1)
def remu(rd, rs1, rs2): return g.r_type(0x33, rd, 7, rs1, rs2, 1)
def add(rd, rs1, rs2): return g.r_type(0x33, rd, 0, rs1, rs2, 0)
def bne(rs1, rs2, off): return g.b_type(0x63, 1, rs1, rs2, off)

def program():
    code = list(g.la(S0, g.STACK_DATA + 0xF00))    # the stores start 256 bytes below a page boundary
    code += [g.addi(A1, 0, 120), g.addi(A2, 0, 1999)]
    loop = len(code)
    code += [divu(A3, A2, A1), remu(A4, A2, A1), add(A5, A3, A4), g.sd(S0, A5, 0), g.addi(S0, S0, 8), g.addi(A2, A2, 13),
             g.addi(A1, A1, -1)]
    code.append(bne(A1, 0, 4 * (loop - len(code))))
    code.append(g.JAL_SELF)
    return code

def main():
    d = os.path.join(sys.argv[1] if len(sys.argv) > 1 else ".", "divs")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "program.elf"), "wb") as f: f.write(g.elf(program()))
    with open(os.path.join(d, "program.json"), "w") as f: json.dump(g.PROGRAM_JSON, f, indent=2); f.write("\n")
    print("divs: written to", d)

if __name__ == "__main__":
    main()
