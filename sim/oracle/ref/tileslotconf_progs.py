#!/usr/bin/env python3
# tileslotconf_progs.py (WORKER15): writes tests/tileslotconf/*.prog, the programs of PeripheralSlotTest's groups as
# TileSlotConf runs them (oracle/ref/TileSlotConf.java on ref's tile, tests/test_tileslotconf.cpp on ours).
#
# A .prog: "tile K" (the tile under test), "periph test|replayer" (its peripheral 1), "cycles N", "rom W W ..." (the boot
# ROM, every tile's: a prelude sends the other tiles to a WFI loop), "poke C OP" (between cycles: at C, before the cycle
# runs: post, credit, level1, level0, fault on the test peripheral), "inject C SRC ADDR LO HI" (a mode-0 128-bit write
# to the tile, from tile SRC, put into the network at C: ref's Net.at; ours puts it in the tile's eject at the cycle ref's
# director accepted it, minus one, read from ref's observations), "dump A N" (N data-memory words from A).
import os, sys

def addi(rd, rs1, imm): return (imm & 0xFFF) << 20 | rs1 << 15 | rd << 7 | 0x13
def andi(rd, rs1, imm): return (imm & 0xFFF) << 20 | rs1 << 15 | 7 << 12 | rd << 7 | 0x13
def srli(rd, rs1, sh): return sh << 20 | rs1 << 15 | 5 << 12 | rd << 7 | 0x13
def lui(rd, imm20): return (imm20 & 0xFFFFF) << 12 | rd << 7 | 0x37
def lw(rd, rs1, off): return (off & 0xFFF) << 20 | rs1 << 15 | 2 << 12 | rd << 7 | 0x03
def sw(rs2, rs1, off): return ((off >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | 2 << 12 | (off & 31) << 7 | 0x23
def csrr(rd, csr): return csr << 20 | 2 << 12 | rd << 7 | 0x73
def b(f3, rs1, rs2, off): return ((off >> 12) & 1) << 31 | ((off >> 5) & 63) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | ((off >> 1) & 15) << 8 | ((off >> 11) & 1) << 7 | 0x63
def beq(rs1, rs2, off): return b(0, rs1, rs2, off)
def bne(rs1, rs2, off): return b(1, rs1, rs2, off)
def jal0(off): return ((off >> 20) & 1) << 31 | ((off >> 1) & 0x3FF) << 21 | ((off >> 11) & 1) << 20 | ((off >> 12) & 0xFF) << 12 | 0x6F
WFI, MCYCLE = 0x10500073, 0xB00

class P:
    def __init__(s, tile):
        s.w = []
        s.res = 0x100
        # prelude: x7 the block, x1 peripheral 1's window; another tile idles (WFI, forever)
        s.w += [addi(7, 0, -0x800), addi(1, 0, -0x600), lw(31, 7, 0), srli(31, 31, 20), andi(31, 31, 127),
                addi(30, 0, tile), beq(31, 30, 12), WFI, jal0(-4)]
    def e(s, *ins): s.w += list(ins)
    def keep(s, reg):                        # the next result word
        s.w.append(sw(reg, 0, s.res)); s.res += 4
    def stamp(s, reg=29):
        s.w.append(csrr(reg, MCYCLE)); s.keep(reg)
    def load(s, off, base=1, reg=10):        # a window word (or a block register with base 7), kept
        s.w.append(lw(reg, base, off)); s.keep(reg)
    def delay(s, n, reg=20):
        s.e(addi(reg, 0, n), addi(reg, reg, -1), bne(reg, 0, -4))
    def poll(s, off, base=1, mask_reg=0, reg=21):   # until the word is non-zero (under x[mask_reg], if given)
        s.w.append(lw(reg, base, off))
        if mask_reg: s.w.append(0x00007033 | mask_reg << 20 | reg << 15 | reg << 7)   # and reg, reg, mask
        s.w.append(beq(reg, 0, -8 if mask_reg else -4))
    def end(s): s.e(WFI, jal0(-4))

def W(word): return 4 * word
PARAMS, STATUS, ERRORS, PULSE_RUN, PULSE_DMA, EVENTS, EVENTS_PEEK, EVENTS_MASK, DMA_HDR, DMA_ADDR = range(10)
B_ERRORS, B_EVENTS_PEEK, B_EVENTS_MASK = 4 * 2, 4 * 7, 4 * 8

def window():
    t = 5; p = P(t)
    p.load(0, base=7)                                  # the unit's PARAMS
    p.load(W(PARAMS)); p.load(W(STATUS))
    p.e(sw(0, 1, W(PULSE_RUN)))                        # PULSE_RUN 0: nothing
    p.e(addi(6, 0, 1)); p.stamp(); p.e(sw(6, 1, W(PULSE_RUN))); p.load(W(STATUS))
    p.e(sw(6, 1, W(PULSE_RUN)))                        # a run while running: ignored, flagged
    p.load(W(32))
    p.e(lui(3, 1), addi(3, 3, 0xABC - 0x1000), sw(3, 1, W(33))); p.load(W(33))
    p.load(W(12)); p.load(W(100)); p.e(sw(6, 1, W(31)))   # reserved, undefined, a reserved store
    p.load(W(PULSE_RUN))
    p.delay(30); p.load(W(STATUS)); p.stamp(); p.load(W(ERRORS))
    p.poll(W(ERRORS)); p.stamp()                       # the fault (poked at 400)
    p.load(W(ERRORS)); p.load(B_ERRORS, base=7)
    p.end()
    return t, "test", 700, p.w, [(400, "fault")], [], (0x100, 32)

def events():
    t = 6; p = P(t)
    p.load(W(EVENTS_MASK))
    p.poll(W(EVENTS_PEEK)); p.stamp(); p.load(W(EVENTS_PEEK))      # posts at 200
    p.load(B_EVENTS_PEEK, base=7)
    p.e(addi(11, 0, 0b11100), sw(11, 1, W(EVENTS_MASK)), addi(0, 0, 0)); p.load(B_EVENTS_PEEK, base=7)
    p.stamp(); p.load(W(EVENTS)); p.load(W(EVENTS_PEEK)); p.load(W(EVENTS_PEEK)); p.stamp()
    p.delay(60)                                                     # level 0 at 450
    p.load(W(EVENTS)); p.load(W(EVENTS_PEEK)); p.load(W(EVENTS_PEEK)); p.load(B_EVENTS_PEEK, base=7); p.stamp()
    p.load(B_ERRORS, base=7)
    p.end()
    pokes = [(200, "post"), (200, "post"), (200, "credit"), (200, "credit"), (200, "level1"), (260, "credit"), (450, "level0")]
    return t, "test", 800, p.w, pokes, [], (0x100, 32)

def dma():
    t = 10; p = P(t)
    hdr = 7 | 1 << 12 | 65 << 25 | 0 << 22 | 5 << 19
    p.e(lui(12, (hdr + 0x800) >> 12), addi(12, 12, ((hdr & 0xFFF) ^ 0x800) - 0x800), sw(12, 1, W(DMA_HDR)))
    p.e(addi(13, 0, 0x400), sw(13, 1, W(DMA_ADDR)), addi(14, 0, 2), sw(14, 1, W(EVENTS_MASK)), addi(6, 0, 1))
    p.stamp(); p.e(sw(6, 1, W(PULSE_DMA))); p.load(W(STATUS))
    p.delay(10); p.load(W(DMA_ADDR)); p.load(W(STATUS)); p.stamp(); p.load(W(EVENTS)); p.load(W(EVENTS_PEEK))
    p.e(sw(0, 1, W(DMA_HDR)), sw(6, 1, W(PULSE_DMA))); p.delay(3); p.load(W(EVENTS_PEEK)); p.stamp()
    p.end()
    return t, "test", 600, p.w, [], [], (0x100, 32)

def landing():
    t = 7; p = P(t)
    p.delay(80); p.load(W(STATUS)); p.end()
    return t, "test", 500, p.w, [], [(100, 3, 0x04000400, 1, 2), (130, 9, 0x04123450, 3, 4), (160, 9, 0x05000000, 5, 6)], (0x100, 8)

def replayer():
    t = 3; p = P(t)
    p.load(W(PARAMS)); p.load(W(STATUS)); p.load(W(EVENTS_PEEK))
    p.e(addi(15, 0, 0b111100)); p.poll(W(EVENTS_PEEK), mask_reg=15); p.stamp(); p.load(W(EVENTS_PEEK))   # the state word (injected)
    p.e(addi(6, 0, 1), sw(6, 1, W(PULSE_RUN)), addi(0, 0, 0)); p.load(W(EVENTS_PEEK))
    p.load(W(4)); p.load(W(32)); p.load(W(33))
    p.e(lui(16, 0x3010), sw(16, 1, W(32)), addi(0, 0, 0), addi(0, 0, 0)); p.load(W(32)); p.stamp()
    p.end()
    return t, "replayer", 600, p.w, [], [(100, 2, 0x04000000, 0, 0)], (0x100, 32)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "tileslotconf")
os.makedirs(out, exist_ok=True)
for name, f in [("window", window), ("events", events), ("dma", dma), ("landing", landing), ("replayer", replayer)]:
    tile, per, cycles, rom, pokes, injects, (a, n) = f()
    if len(rom) > 64: raise SystemExit(f"{name}: {len(rom)} words, the boot ROM holds 64")
    with open(os.path.join(out, name + ".prog"), "w") as fh:
        fh.write(f"# tileslotconf program v1: {name} (PeripheralSlotTest's group), oracle/ref/tileslotconf_progs.py\n")
        fh.write(f"tile {tile}\nperiph {per}\ncycles {cycles}\nrom {' '.join('%08x' % w for w in rom)}\n")
        for c, op in pokes: fh.write(f"poke {c} {op}\n")
        for c, src, addr, lo, hi in injects: fh.write(f"inject {c} {src} {addr:x} {lo:x} {hi:x}\n")
        fh.write(f"dump {a:x} {n}\n")
print("tileslotconf: programs written to", out)
