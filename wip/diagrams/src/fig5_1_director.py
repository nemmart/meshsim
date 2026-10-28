# Figure 5.1: the traffic director — one packet a cycle, routed by its 4-bit mode; the counters;
# and where each mode's payload lands (write-engine index and augmenting; port-B memory, word and banks).
from common import *

W, H = 1300, 1400
f = Fig(W, H, "Figure 5.1. The traffic director: routing by mode, and how each mode's address is formed",
        "v1.0 (spec/machine-spec.md §3.1, §4.3, §4.8, §4.9, §4.11, §4.12, §4.15) — the only path into the tile")

CTL = DASH["ctrl"]
RED = "#C0262D"


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def poly(pts, fill="white", stroke=INK, sw=1.4):
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    f.raw(f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')


def chip(x, y, w, label, fill=BOX, stroke=EDGE, size=10.5, mono=False):
    f.rect(x, y, w, 22, fill, stroke, 1.1, rx=4)
    f.text(x + w / 2, y + 15, label, size, "middle", "bold", mono=mono)


# =================================================================================================
# A. routing
# =================================================================================================
f.text(30, 92, "A. One packet a cycle, routed by its mode alone (§4.8)", 14, "start", "bold")
ES = (30, 160, 140, 84)
f.box(*ES, "eject selector", ["round-robin over the", "router's eject FIFOs", "(figures 4.1, 4.2)"], fill=BOX, stroke=INK, size=10.5, tsize=12.5)
DEC = (232, 124, 120, 270)
f.box(*DEC, "mode decode", ["4-bit mode", "→ one of five", "outputs"], fill="white", stroke=INK, sw=1.6, size=10.5, tsize=12.5)
f.line([(ES[0] + ES[2], 202), (DEC[0] - 2, 202)], INK, 2.4, end=True)
f.lines(ES[0] + ES[2] + 6, 230, ["src, mode,", "addr, payload"], 10.5, fill=MUTED, lh=13)

DR = DEC[0] + DEC[2]
# mode 1 -> GATHER request queue
GQ = (540, 132, 220, 46)
f.line([(DR, 155), (GQ[0] - 2, 155)], GREEN, 2, end=True)
f.text(DR + 10, 149, "mode 1", 11, "start", "bold", fill=GREEN)
f.box(*GQ, "GATHER request queue", ["16 entries (§4.4)"], fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12)
# modes 2, 3, 4 -> write engine FIFO
WF = (540, 192, 220, 46)
f.line([(DR, 215), (WF[0] - 2, 215)], BLUE, 2, end=True, dash=DASH["write"])
f.text(DR + 10, 209, "modes 2, 3, 4", 11, "start", "bold", fill=BLUE)
f.box(*WF, "write engine input FIFO", ["4 entries; overflow: condition 2"], fill=BLUE_L, stroke=BLUE, size=10.5, tsize=12)
f.line([(WF[0] + 110, WF[1] + WF[3]), (WF[0] + 110, 262), (330, 262), (330, 470)], BLUE, 1.8, end=False, dash=DASH["write"])
# modes 5, 6, 7 -> port B
f.line([(DR, 286), (650, 286), (650, 470)], PURPLE, 2.2, end=False)
f.text(DR + 10, 280, "modes 5, 6, 7", 11, "start", "bold", fill=PURPLE)
f.text(658, 280, "port B (panel B2)", 11, "start", "bold", fill=PURPLE)
f.lines(380, 318, ["read-port results arrive here as mode-7 packets", "the tile sent itself, through the loopback (§4.3)"], 10.5, fill=GREEN, lh=13)
# modes 0, 8-15 -> dropped, bad-mode condition
DP = (412, 374, 150, 30)
f.line([(DR, 389), (DP[0] - 2, 389)], GREY, 1.6, end=True)
f.text(DR + 4, 383, "0, 8–15", 10.5, "start", "bold", fill=MUTED)
f.rect(*DP, BOX, GREY, 1.2, rx=4)
f.text(DP[0] + 75, DP[1] + 20, "dropped; bad-mode condition", 10.5, "middle", fill=MUTED)

# counters
CB = (800, 132, 470, 166)
f.box(*CB, "counters and command landed (§4.12)", [
    ("DMA_SRAM_REMAINING − 1: each accepted mode-2, 3 or 4 packet", {}),
    ("DMA_CM_REMAINING − 1: each accepted mode-5 packet (64 bits count 1)", {}),
    ("DMA_REPLY_REMAINING − 1: each mode-7 packet (read-port results included)", {}),
    ("never for src = 64 (C&C), in any counter", {"weight": "bold"}),
    ("a decrement at 0 sets the unexpected-arrival condition (bit 4) instead", {}),
    ("command landed ← a mode-6 write to slot 0 (addr[7:0] = 0)", {}),
    ("modes 1 and 6 are never counted; modes 0 and 8–15 set the bad-mode condition (bit 9)", {"fill": MUTED}),
], fill=SAND, stroke=ORANGE, size=11, tsize=12.5, align="start", pad=14, valign="top")
f.line([(DEC[0] + 60, DEC[1] + DEC[3]), (DEC[0] + 60, 420), (1035, 420), (1035, CB[1] + CB[3] + 2)], ORANGE, 1.4, end=True, dash=CTL)
f.text(700, 414, "count enables: mode, src = 64, accepted", 10.5, "start", fill=ORANGE)

# =================================================================================================
# B1. write engine
# =================================================================================================
BY = 470
B1 = (30, BY, 610, 420)
f.rect(*B1, "white", BLUE, 1.3, rx=10)
f.text(B1[0] + 14, BY + 24, "B1. Write engine: modes 2, 3 and 4 (§4.9, §4.15)", 13, "start", "bold", fill=BLUE)
# input chips
CY = BY + 42
chip(46, CY, 64, "src (7)")
chip(118, CY, 150, "payload[31:0]: the key")
chip(276, CY, 118, "payload[63:32]")
chip(420, CY, 208, "DMA_CONFIG: digit selectors", SAND, ORANGE)
# digit selects
DS1 = (118, BY + 96, 150, 50)
DS2 = (118, BY + 300, 150, 50)
f.box(*DS1, "digit select", ["byte d of the key, d = the", "bucket digit selector"], fill="white", stroke=INK, size=10, tsize=11.5)
f.box(*DS2, "digit select", ["byte d, d = the histogram", "digit selector"], fill="white", stroke=INK, size=10, tsize=11.5)
f.line([(193, CY + 22), (193, DS1[1] - 2)], INK, 1.6, end=True)
dot(160, CY + 48)
f.line([(160, CY + 48), (100, CY + 48), (100, DS2[1] + 25), (DS2[0] - 2, DS2[1] + 25)], INK, 1.4, end=True)
# index mux
IM0, IM1 = 300, 340
poly([(IM0, BY + 70), (IM1, BY + 86), (IM1, BY + 138), (IM0, BY + 154)])
f.text(IM0 + 9, BY + 96, "0", 10, "middle", "bold", fill=MUTED)
f.text(IM0 + 9, BY + 138, "1", 10, "middle", "bold", fill=MUTED)
f.line([(78, CY + 22), (78, BY + 92), (IM0 - 2, BY + 92)], INK, 1.6, end=True)
f.line([(DS1[0] + DS1[2], BY + 134), (IM0 - 2, BY + 134)], INK, 1.6, end=True)
f.text(DS1[0] + DS1[2] + 4, BY + 128, "t", 11, "start", "bold")
f.line([(IM0 + 20, BY + 172), (IM0 + 20, BY + 149)], ORANGE, 1.2, end=True, dash=CTL)
f.text(IM0 + 26, BY + 182, "mode = 3", 10, "start", fill=ORANGE)
# PTR
PT = (380, BY + 88, 120, 52)
f.line([(IM1, BY + 112), (PT[0] - 2, BY + 112)], INK, 2, end=True)
f.text(IM1 + 6, BY + 106, "i (8)", 10.5, "start", "bold")
f.box(*PT, "PTR[i]", ["read, then += 2", "(figure 5.2)"], fill=PURPLE_L, stroke=PURPLE, size=10, tsize=12)
f.line([(PT[0] + PT[2], BY + 114), (624, BY + 114), (624, BY + 200)], INK, 2, end=False)
f.text(PT[0] + PT[2] + 6, BY + 108, "chunk addr a (20)", 10.5, "start", "bold")
# augmenting: ordinal = (a - DMA_RECORD_BASE_ADDRESS) >> 1
dot(560, BY + 114)
AT = (410, BY + 168, 150, 50)
f.line([(560, BY + 114), (560, BY + 154), (485, BY + 154), (485, AT[1] - 2)], INK, 1.4, end=True)
f.box(*AT, "ordinal", ["(a − DMA_RECORD_BASE_ADDRESS) >> 1"], fill="white", stroke=INK, size=10, tsize=11.5)
HM0, HM1 = 370, 470
poly([(HM0, BY + 246), (HM1, BY + 246), (HM1 - 18, BY + 272), (HM0 + 18, BY + 272)])
f.text(HM0 + 26, BY + 259, "0", 10, "middle", "bold", fill=MUTED)
f.text(HM1 - 20, BY + 259, "1", 10, "middle", "bold", fill=MUTED)
f.line([(286, CY + 22), (286, BY + 228), (396, BY + 228), (396, BY + 244)], INK, 1.4, end=True)
f.line([(452, AT[1] + AT[3]), (452, BY + 244)], INK, 1.4, end=True)
f.line([(HM1 - 8, BY + 236), (HM1 + 30, BY + 236)], ORANGE, 1.2, end=False, dash=CTL)
f.line([(HM1 + 30, BY + 236), (HM1 + 30, BY + 259), (HM1 - 9, BY + 259)], ORANGE, 1.2, end=True, dash=CTL)
f.text(HM1 + 36, BY + 240, "mode = 4", 10, "start", fill=ORANGE)
# the write
WR = (370, BY + 290, 254, 46)
f.line([(420, BY + 272), (420, WR[1] - 2)], INK, 1.6, end=True)
f.text(426, BY + 284, "high 32 bits", 10, "start")
f.line([(624, BY + 200), (624, WR[1] - 2)], INK, 2, end=True)
f.box(*WR, "64-bit write at chunk a", ["→ its area's queue; two if it spans a row (7.2)"], fill=BLUE_L, stroke=BLUE, size=10.5, tsize=12)
# HIST
CN = (370, BY + 354, 150, 46)
f.line([(DS2[0] + DS2[2], DS2[1] + 25), (340, DS2[1] + 25), (340, CN[1] + 23), (CN[0] - 2, CN[1] + 23)], INK, 1.6, end=True)
f.text(DS2[0] + DS2[2] + 4, DS2[1] + 19, "c", 11, "start", "bold")
f.box(*CN, "HIST[c] += 1", ["(figure 5.2)"], fill=PURPLE_L, stroke=PURPLE, size=10, tsize=12)
f.lines(530, BY + 372, ["only if the histogram", "digit selector ≠ 0"], 10, fill=ORANGE, lh=12)
f.lines(46, BY + 384, ["low 32 bits: the key,", "unchanged"], 10.5, fill=MUTED, lh=13)

# =================================================================================================
# B2. port B
# =================================================================================================
B2 = (660, BY, 610, 420)
f.rect(*B2, "white", PURPLE, 1.3, rx=10)
f.text(B2[0] + 14, BY + 24, "B2. Port B: modes 5, 6 and 7", 13, "start", "bold", fill=PURPLE)
chip(676, CY, 88, "addr[11]")
chip(772, CY, 100, "addr[10:0]")
chip(880, CY, 88, "addr[7:0]")
chip(976, CY, 64, "mode")
chip(1048, CY, 206, "payload: 64 bits (mode 5), else low 32")
# mailbox word
MB = (860, BY + 92, 128, 50)
f.line([(924, CY + 22), (924, MB[1] - 2)], INK, 1.6, end=True)
f.box(*MB, "0x700 +", ["slot → mailbox word"], fill="white", stroke=INK, size=10, tsize=12)
# word mux
WM0, WM1 = 760, 960
poly([(WM0, BY + 166), (WM1, BY + 166), (WM1 - 24, BY + 194), (WM0 + 24, BY + 194)])
f.text(822, BY + 180, "0", 10, "middle", "bold", fill=MUTED)
f.text(924, BY + 180, "1", 10, "middle", "bold", fill=MUTED)
f.line([(822, CY + 22), (822, BY + 164)], INK, 1.6, end=True)
f.line([(924, MB[1] + MB[3]), (924, BY + 164)], INK, 1.6, end=True)
f.line([(1008, CY + 22), (1008, BY + 180), (WM1 - 11, BY + 180)], ORANGE, 1.2, end=True, dash=CTL)
f.text(1014, BY + 176, "mode = 6", 10, "start", fill=ORANGE)
f.text(866, BY + 210, "w: word (11 bits)", 10.5, "start", "bold")
# memory select
MS = (1060, BY + 220, 196, 56)
f.box(*MS, "memory select", ["instruction if mode 5 and", "addr[11] = 0; else data"], fill="white", stroke=INK, size=10, tsize=11.5)
f.line([(720, CY + 22), (720, BY + 150), (1158, BY + 150), (1158, MS[1] - 2)], INK, 1.4, end=True)
# bank logic
BK = (700, BY + 226, 340, 112)
f.line([(860, BY + 194), (860, BK[1] - 2)], INK, 2, end=True)
f.box(*BK, "banks: even words | odd words (§4.11)", [
    ("one word (modes 6 and 7):", {"weight": "bold"}),
    "bank w[0], row w >> 1",
    ("two words w, w + 1 (mode 5):", {"weight": "bold"}),
    "even bank row (w + 1) >> 1, odd bank row w >> 1;",
    "the low half goes to bank w[0]",
], fill=BOX, stroke=INK, size=10.5, tsize=12, align="start", pad=12, valign="top")
OUTB = (700, BY + 356, 556, 46)
f.line([(870, BK[1] + BK[3]), (870, OUTB[1] - 2)], PURPLE, 2.2, end=True)
f.line([(1158, MS[1] + MS[3]), (1158, OUTB[1] - 2)], INK, 1.4, end=True)
f.line([(1150, CY + 22), (1150, BY + 136), (1250, BY + 136), (1250, OUTB[1] - 2)], INK, 1.4, end=True)
f.text(1244, BY + 300, "data", 10.5, "end")
f.box(*OUTB, "port B write: two 32-bit banks of instruction or data memory", ["cm_latency 2; cm_idle when the pipeline is empty (figure 3.1)"],
      fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=12)

# =================================================================================================
# C. per-mode table
# =================================================================================================
TY = BY + 420 + 40
f.text(30, TY, "C. Every mode: the fields it uses and where it lands", 14, "start", "bold")
cols = ["mode", "goes to", "fields used", "address formed", "written", "counted in"]
rows = [
    ["1", "GATHER engine", "payload: key, ordinal", "none here: the engine reads DMA_RECORD_BASE_ADDRESS + 2k·ordinal (fig. 7.4)", "—", "— (its values)"],
    ["2", "write engine", "src, payload", "PTR[src], then += 2", "64 bits", "DMA_SRAM_REMAINING"],
    ["3", "write engine", "payload's bucket digit", "PTR[t], then += 2", "64 bits", "DMA_SRAM_REMAINING"],
    ["4", "write engine", "src, payload[31:0]", "PTR[src], then += 2; the ordinal replaces bits 63–32", "64 bits", "DMA_SRAM_REMAINING"],
    ["5", "port B", "addr[11:0], payload", "addr[11]: memory; words w, w + 1 with w = addr[10:0]", "64 bits", "DMA_CM_REMAINING"],
    ["6", "port B", "addr[7:0], payload[31:0]", "data word 0x700 + addr[7:0]; slot 0 sets command landed", "32 bits", "—"],
    ["7", "port B", "addr[11:0], payload[31:0]", "data word addr[10:0] (addr[11] = 1)", "32 bits", "DMA_REPLY_REMAINING"],
    ["read port", "a mode-7 packet", "SRAM_RD_DEST_ADDR → addr", "sent to the tile itself, then as mode 7", "32 bits", "DMA_REPLY_REMAINING"],
    ["0, 8–15", "dropped", "—", "— (bad-mode condition, TILE_CONDITIONS bit 9)", "—", "—"],
]
CX = [30, 110, 230, 420, 950, 1040, 1270]
RY, RH = TY + 12, 23
for i, row in enumerate([cols] + rows):
    y0 = RY + i * RH
    for c in range(6):
        f.rect(CX[c], y0, CX[c + 1] - CX[c], RH, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
        f.text(CX[c] + 8, y0 + 16, row[c], 11, "start", "bold" if (i == 0 or c == 0) else "normal")

# notes
y = RY + 10 * RH + 26
notes = [
    "The director accepts one packet a cycle and routes it by mode alone, and never stalls: an overflow of the write-engine input FIFO sets condition 2. Port B has no other writer.",
    "Read-port results share the tile's loopback and eject path with every arriving packet, and the network has no flow control, so no packet from outside the tile, "
    "other than C&C's mode-6 command words, may arrive while read-port results are outstanding (§4.3, invariant 14). The hardware tolerates only 15 results against a "
    "full-rate stream, or 32 per burst at half rate [meas: meshsim].",
    "Programming errors with no hardware check (the simulator checks them): a mode-5 address outside both memories (invariant 11); a mode-7 address in the mailbox "
    "or outside data memory (invariants 12, 13); mode 5 into the mailbox.",
    "The packed 12-bit map (mode 5's addresses, and mode 7's as SRAM_RD_DEST_ADDR writes them, §4.3) "
    "and the mailbox at 0x5C00–0x5FFF (data words 0x700–0x7FF).",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4

# ---------------- interface (moved here from the interface figure, 12 October) -----------------
from rtl import R as _R
IY = y + 16
f.text(30, IY + 4, "Interface", 14, "start", "bold")
IY = _R(f).table(30, IY + 16, [30, 250, 340, 640, 1270], [["signal", "width", "from → to", "meaning"],
    ["packet", "102", "eject selector (fig 4.3) → here", "one per cycle; the director never stalls"],
    ["push, ordinal", "1, 20", "here → GATHER queue (fig 7.5)", "mode 1"],
    ["push, src, mode, payload", "1, 7, 4, 64", "here → write engine FIFO", "modes 2–4"],
    ["port B: addr, wdata, we[1:0], memory", "11, 64, 2, 1", "here → controller memories (fig 3.1)", "modes 5–7; cm_latency 2; cm_idle when empty"],
    ["dec_S, dec_C, dec_R; slot0", "1 each", "here → counters, landed (figs 2.3, 2.2)", "counted packets (src ≠ 64); a mode-6 write to slot 0"],
    ["set[9]", "1", "here → TILE_CONDITIONS (fig 2.4)", "mode 0 or 8–15: dropped"]])
LY = IY + 26
f.line([(30, LY), (70, LY)], GREEN, 2, end=True)
f.text(78, LY + 4, "read side", 11.5, "start", fill=MUTED)
f.line([(170, LY), (210, LY)], BLUE, 2, end=True, dash=DASH["write"])
f.text(218, LY + 4, "write side", 11.5, "start", fill=MUTED)
f.line([(320, LY), (360, LY)], PURPLE, 2, end=True)
f.text(368, LY + 4, "port B", 11.5, "start", fill=MUTED)
f.line([(450, LY), (490, LY)], ORANGE, 1.4, end=True, dash=CTL)
f.text(498, LY + 4, "control and selects", 11.5, "start", fill=MUTED)

H = LY + 92
f.h = H
f.caption(["Every packet is routed by its 4-bit mode: mode 1 to the GATHER engine, 2–4 to the write engine (which forms the address from PTR), 5–7 to port B",
           "(which forms memory, word and banks from addr); 0 and 8–15 are dropped with the bad-mode condition. Read-port results are mode-7 packets. Counting happens on acceptance."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §3.1, §4.3, §4.8, §4.9, §4.11, §4.12, §4.13, §4.15.")
f.save("fig5_1_director.svg")
