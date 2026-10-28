# Figure 1.1: the system above the chip — a controller FPGA, 16 cards, each a card FPGA with 8 ASIC leaves;
# every ASIC is figure 1.2's chip plus its own DDR4. System level from the lead; not in machine-spec.md yet.
from common import *

W, H = 1300, 900
f = Fig(W, H, "Figure 1.1. The system: 16 cards of 8 ASICs under a controller FPGA",
        "system level, from the lead; not in machine-spec.md yet — each ASIC is figure 1.2's chip with its own DDR4")
f.tag(W - 24, 30, "system level: not yet in the spec", "end")

FPGA_F, FPGA_E = SAND, ORANGE
ASIC_F, ASIC_E = PURPLE_L, PURPLE

# ---------------- controller FPGA and the 16 cards -------------------------------------------
CF = (490, 84, 320, 60)
f.box(*CF, "controller FPGA", ["the root of the system"], fill=FPGA_F, stroke=FPGA_E, sw=1.8, size=11, tsize=14)

CW, CG, CY, CH = 66, 10, 222, 50
cx0 = (W - (16 * CW + 15 * CG)) / 2
BUS = 186
for k in range(16):
    x = cx0 + k * (CW + CG)
    sx = CF[0] + 20 + k * (CF[2] - 40) / 15          # its own point on the FPGA's edge: one link per card
    f.line([(sx, CF[1] + CF[3]), (x + CW / 2, CY - 2)], INK, 1.4, end=True)
    hl = k == 0
    f.rect(x, CY, CW, CH, FPGA_F if hl else BOX, FPGA_E if hl else EDGE, 1.8 if hl else 1.2, rx=6)
    f.text(x + CW / 2, CY + 22, "card", 10.5, "middle", fill=MUTED)
    f.text(x + CW / 2, CY + 38, str(k), 13, "middle", "bold")
f.text(CF[0] + CF[2] + 16, 120, "16 independent links, one per card (interface to be decided)", 11, "start", fill=MUTED)

# ---------------- card 0, expanded --------------------------------------------------------------
EX = (30, 330, 1240, 380)
f.rect(*EX, "white", ORANGE, 1.4, rx=12, dash="7 4")
f.text(EX[0] + 16, EX[1] + 24, "card 0, expanded (every card is the same)", 13, "start", "bold", fill=ORANGE)
# zoom lines from card 0 to the expanded box
f.line([(cx0, CY + CH), (EX[0] + 4, EX[1])], ORANGE, 1, end=False, dash="3 3")
f.line([(cx0 + CW, CY + CH), (EX[0] + 260, EX[1])], ORANGE, 1, end=False, dash="3 3")

KF = (530, 356, 240, 58)
f.box(*KF, "card FPGA", ["the root of the card"], fill=FPGA_F, stroke=FPGA_E, sw=1.8, size=11, tsize=14)

AW, AG, AY, AH = 140, 12, 494, 74
ax0 = (W - (8 * AW + 7 * AG)) / 2
TB = 458
f.text(KF[0] + KF[2] + 16, 390, "8 independent links, one per ASIC (interface to be decided)", 11, "start", fill=MUTED)
for k in range(8):
    x = ax0 + k * (AW + AG)
    sx = KF[0] + 20 + k * (KF[2] - 40) / 7           # its own point on the card FPGA's edge
    f.line([(sx, KF[1] + KF[3]), (x + AW / 2, AY - 2)], INK, 1.6, end=True)
    f.box(x, AY, AW, AH, f"ASIC {k}", ["64-tile mesh + C&C", "(figure 1.2)"], fill=ASIC_F, stroke=ASIC_E, sw=1.6, size=10.5, tsize=13)
    D = (x + 22, AY + AH + 44, AW - 44, 44)
    f.line([(x + AW / 2, AY + AH), (x + AW / 2, D[1])], INK, 1.6, end=False)
    f.box(*D, "DDR4", ["2–4 GB (?)"], fill=BOX, stroke=INK, size=10.5, tsize=12)
f.text(W / 2, EX[1] + EX[3] - 16, "each ASIC has its own DDR4; how it attaches to the chip is not in the spec yet", 10.5, "middle", fill=MUTED)

# ---------------- ASIC 0, expanded: a tiny figure 1.2 -----------------------------------------
E2 = (30, 760, 1240, 350)
f.rect(*E2, "white", PURPLE, 1.4, rx=12, dash="7 4")
f.text(E2[0] + 16, E2[1] + 24, "ASIC 0, expanded: figure 1.2's chip (every ASIC is the same)", 13, "start", "bold", fill=PURPLE)
f.line([(ax0, AY + AH), (E2[0] + 4, E2[1])], PURPLE, 1, end=False, dash="3 3")
f.line([(ax0 + AW, AY + AH), (E2[0] + 240, E2[1])], PURPLE, 1, end=False, dash="3 3")
f.raw('<g transform="translate(0,30)">')
# the chip boundary
CHP = (270, 792, 590, 272)
f.rect(*CHP, PURPLE_L, PURPLE, 1.6, rx=10)
f.text(CHP[0] + CHP[2] - 12, CHP[1] + 20, "the ASIC", 12, "end", "bold", fill=PURPLE)
# the 8 x 8 mesh: tile idx at column idx mod 8, row idx div 8
TW_, TH_, TG = 30, 20, 10
MX0, MY0 = 520, 816
tx = lambda c: MX0 + c * (TW_ + TG)
ty = lambda r: MY0 + r * (TH_ + TG)
for r in range(8):
    for c in range(8):
        if c < 7:
            f.line([(tx(c) + TW_, ty(r) + TH_ / 2), (tx(c + 1), ty(r) + TH_ / 2)], INK, 1, end=False)
        if r < 7:
            f.line([(tx(c) + TW_ / 2, ty(r) + TH_), (tx(c) + TW_ / 2, ty(r + 1))], INK, 1, end=False)
for r in range(8):
    for c in range(8):
        i = r * 8 + c
        hl = i == 0
        f.rect(tx(c), ty(r), TW_, TH_, SAND if hl else "white", ORANGE if hl else INK, 1.4 if hl else 1, rx=3)
        if i in (0, 7, 56, 63):
            f.text(tx(c) + TW_ / 2, ty(r) + 14, str(i), 9.5, "middle", "bold")
# C&C on tile 0's fifth port, the memory subsystem, the card FPGA's link
CC = (290, 806, 170, 54)
f.box(*CC, "C&C (address 64)", ["RV32 sequencer"], fill=SAND, stroke=ORANGE, sw=1.4, size=10.5, tsize=12)
f.line([(CC[0] + CC[2], 826), (MX0 - 2, 826)], INK, 1.6, end=True, start=True)
f.text((CC[0] + CC[2] + MX0) / 2, 820, "fifth port", 9.5, "middle", fill=MUTED)
MS = (290, 900, 170, 54)
f.box(*MS, "memory subsystem", ["hangs off C&C (§7)"], fill=BOX, stroke=INK, size=10.5, tsize=12)
f.line([(MS[0] + MS[2] / 2, MS[1] - 1), (CC[0] + CC[2] / 2, CC[1] + CC[3] + 1)], INK, 1.2, end=True, start=True, dash="4 3")
# DDR4 outside the chip
DD = (70, 900, 150, 54)
f.box(*DD, "DDR4", ["2–4 GB (?)"], fill=BOX, stroke=INK, size=10.5, tsize=12)
f.line([(DD[0] + DD[2], 927), (CHP[0] - 2, 927)], INK, 1.2, end=False, dash="4 3")
f.lines(DD[0], DD[1] + DD[3] + 18, ["attachment to the chip", "not specified yet"], 10.5, fill=MUTED, lh=13)
f.lines(890, 830, [
    "64 identical tiles on the circulant mesh;",
    "tile 0's router has a fifth port to C&C.",
    "Each link carries one 102-bit packet per",
    "cycle each way; the ±1 ring and the column",
    "wraparound are drawn in figure 1.2.",
    "",
    "Each tile: router, controller, engines",
    "and 4 MB of big SRAM (parts 2–7).",
], 11.5, fill=INK, lh=16)
f.raw("</g>")
f.line([(CC[0] + 85, E2[1] + 36), (CC[0] + 85, CC[1] + 30 - 2)], INK, 1.6, end=True, start=True)
f.text(CC[0] + 93, E2[1] + 54, "the card FPGA's link", 10.5, "start", fill=MUTED)

# ---------------- totals and notes -------------------------------------------------------------
TY = E2[1] + E2[3] + 34
f.text(30, TY, "Totals", 13, "start", "bold")
f.text(100, TY, "16 cards × 8 ASICs = 128 ASICs;   128 × 64 tiles = 8,192 tiles;   128 DDR4s of 2–4 GB each (?)", 12.5, "start")
notes = [
    "Each ASIC is figure 1.2's chip: 64 tiles on an 8 × 8 mesh, C&C (address 64), and the memory subsystem hanging off C&C. Its C&C program RAM is loaded once from the FPGA at boot (§6.6).",
    "Above the chip nothing is specified yet: the card and controller links, what the FPGAs do in a sort, and how the DDR4 connects to the chip.",
]
y = TY + 26
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4

H = y + 80
f.h = H
f.caption(["A controller FPGA has its own link to each of 16 cards. Each card is a card FPGA with its own link to each of 8 ASICs, each figure 1.2's 64-tile chip with its own DDR4:",
           "128 ASICs and 8,192 tiles in all. ASIC 0 is opened up to show its 8 × 8 mesh, C&C and memory subsystem."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §2, §6.6, §7 for the chip's side; above the chip is not specified (§13 item 10) and is drawn from the lead's description.")
f.save("fig1_1_system_cards.svg")
