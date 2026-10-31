# Figure 2.4: the conditions — where each of TILE_CONDITIONS' 11 bits is set, the set-only register with its
# clear on entering HALT, the error line, the read-back, and the overflow detector shared by conditions 1, 2, 3, 5 and 8.

from common import *

W, H = 1300, 2000
f = Fig(W, H, "Figure 2.4. The conditions: where each TILE_CONDITIONS bit is set, the register, and the error line",
        "v1.0 (spec/machine-spec.md §4.13, §6.6, §8.2) — one tile")

CTL = DASH["ctrl"]
RED, RED_L = "#C0392B", "#FBE9E7"


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def bubble(x, y, c=INK):
    f.raw(f'<circle cx="{x - 4:.1f}" cy="{y:.1f}" r="4" fill="white" stroke="{c}" stroke-width="1.3"/>')


def gate_and(x, yc, n, label=None, c=INK, w=36):
    h = max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} L{x + w - h / 2},{y0} A{h / 2},{h / 2} 0 0 1 {x + w - h / 2},{y1} L{x},{y1} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2 - 4, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def gate_or(x, yc, n, label=None, c=INK, w=38, h=None):
    h = h or max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} Q{x + w * 0.55},{y0} {x + w},{yc} Q{x + w * 0.55},{y1} {x},{y1} Q{x + 9},{yc} {x},{y0} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def reg(x, y, w, h, title, body=(), fill=SAND, stroke=INK, size=10.5, tsize=11.5):
    f.box(x, y, w, h, title, body, fill=fill, stroke=stroke, sw=1.5, size=size, tsize=tsize)
    cx = x + 16
    f.raw(f'<polyline points="{cx - 6},{y + h} {cx},{y + h - 8} {cx + 6},{y + h}" fill="none" stroke="{stroke}" stroke-width="1.2"/>')


def frame(x, y, w, h):
    f.rect(x, y, w, h, "white", GREY, 1, rx=10)


def lbl(x, y, s, anchor="end", c=INK, size=10.5, weight="normal"):
    f.text(x, y + 4, s, size, anchor, weight, fill=c)


def kind(x, y, k):
    col = OPEN_C if k == "H" else INK
    f.raw(f'<circle cx="{x}" cy="{y}" r="9" fill="white" stroke="{col}" stroke-width="1.3"/>')
    f.text(x, y + 4, k, 10.5, "middle", "bold", fill=col)


# =============================================================================================
# A. the twelve sources and the register
# =============================================================================================
AY = 76
ROWH = 40
SOURCES = [
    # bit, name, unit, expression lines, kind, figure
    (0, "drift", "address generator", ["group run & request offered", "& ¬taken by the SRAM read queue"], "P", "fig 6.4"),
    (1, "router overflow", "routers (every tile)", ["OR over link, eject and C&C FIFOs:", "push & full (panel C)"], "P", "router figure"),
    (2, "write-engine input overflow", "traffic director", ["modes 2–4 accepted & the 4-entry", "input FIFO full (panel C)"], "P", "fig 5.1"),
    (3, "read-port overflow", "controller read port", ["a SRAM_RD_ADDR_QUEUE store", "& no free entry (panel C)"], "P", "read-port figure"),
    (4, "unexpected arrival", "down-counters", ["OR over the three counters:", "dec_i & zero_i"], "P", "fig 2.3 A"),
    (5, "table request overflow", "PTR and HIST request FIFOs", ["push & full on either FIFO", "(panel C)"], "H", "fig 5.2"),
    (6, "bus error", "controller", ["misaligned, unmapped or instruction-memory", "access; a store to a read-only register *"], "P", "controller"),
    (7, "illegal instruction", "controller", ["an instruction outside RV32I + Zmmul", "+ WFI (§4.11)"], "P", "controller"),
    (8, "GATHER queue overflow", "GATHER engine", ["mode 1 accepted & the 16-entry", "request queue full (panel C)"], "H", "GATHER figure"),
    (9, "bad mode", "traffic director", ["a packet accepted with mode 0", "or 8–15; it is dropped"], "P", "fig 5.1"),
    (10, "done early", "the done pulse", ["done_p & nz_any (any counter ≠ 0)"], "P", "fig 2.3 D"),
]
NA = len(SOURCES)
frame(16, AY, W - 32, 112 + NA * ROWH)
f.text(30, AY + 24, "A. The twelve sources, the set-only register and the error line (§4.13, §6.6, §8.2)", 14, "start", "bold")
# column heads
HY = AY + 50
for x, s in [(30, "bit, condition, unit"), (262, "set this cycle when"), (590, "kind"), (626, "detector drawn in")]:
    f.text(x, HY, s, 10.5, "start", "bold", fill=MUTED)
f.text(800, HY, "TILE_CONDITIONS", 11, "middle", "bold", fill=RED)

RX, RW = 760, 80           # register stack
BUSX = 880                 # Q bus
R0 = AY + 62
for i, (b, name, unit, expr, k, fig) in enumerate(SOURCES):
    y = R0 + i * ROWH
    yc = y + ROWH / 2
    if i % 2 == 0:
        f.rect(22, y + 1, 720, ROWH - 2, "#FAF9F5", "none", 0, rx=0)
    f.text(30, yc - 3, f"{b}", 12, "start", "bold", fill=RED)
    f.text(54, yc - 3, name, 11.5, "start", "bold")
    f.text(54, yc + 12, unit, 10.5, "start", fill=MUTED)
    for j, s in enumerate(expr):
        f.text(262, (yc - 3 + j * 14) if len(expr) > 1 else yc + 4, s, 10.5, "start")
    kind(600, yc, k)
    f.text(626, yc + 4, fig, 10.5, "start", fill=MUTED, italic=True)
    # set line into the bit cell
    f.line([(712, yc), (RX - 1, yc)], RED, 1.5, end=True)
    f.text(RX - 14, yc - 5, f"set[{b}]", 9, "end", fill=RED)
    # the cell
    reg(RX, y + 4, RW, ROWH - 8, f"Q[{b}]", [], fill=RED_L, stroke=RED, tsize=11)
    f.line([(RX + RW, yc), (BUSX, yc)], RED, 1.4, end=False)
    dot(BUSX, yc, RED)
# the bus
yb0, yb1 = R0 + ROWH / 2, R0 + (NA - 1) * ROWH + ROWH / 2
f.line([(BUSX, yb0), (BUSX, yb1)], RED, 3, end=False)
f.text(BUSX + 8, yb0 - 8, "Q[10:0]", 10.5, "start", "bold", fill=RED)

# clear on entering HALT: edge detect on halt, OR chip reset, into every cell
CLY = AY + 112 + NA * ROWH - 30
# drawn to the right of the bus, feeding a clear line up the stack's left edge
# error line: OR-reduce
EY = R0 + 2 * ROWH
ig = gate_or(930, EY, 1, h=56)
f.line([(BUSX, EY), (934, EY)], RED, 2.4, end=False)
f.text(948, EY + 4, "≥1", 11, "middle", "bold", fill=RED)
f.text(930 + 19, EY - 34, "OR over 11 bits", 10, "middle", fill=MUTED)
f.line([(968, EY), (1010, EY)], RED, 2, end=True)
f.box(1012, EY - 40, 254, 80, "error line (1 bit) → tile edge", ["my TILE_CONDITIONS ≠ 0; figure 1.2's OR tree", "over 64 tiles (no equal latency) feeds", "C&C's sticky error bit, a wake for C&C"],
      fill=RED_L, stroke=RED, sw=1.4, size=10.5, tsize=11.5)
# read-back
RBY = R0 + 6 * ROWH
f.line([(BUSX, RBY), (1010, RBY)], RED, 2, end=True)
f.box(1012, RBY - 46, 254, 92, "read-back: load 0xFFFFF804", ["data = {21'b0, Q[10:0]}: bits 11–31 read 0",
                                                              "a store is a bus error (condition 6),", "and nothing is written *", "(provisional offset, §8)"],
      fill="white", stroke=RED, sw=1.4, size=10.5, tsize=11.5)
# clear logic
CY0 = R0 + 9 * ROWH - 8
reg(940, CY0 + 30, 74, 34, "halt_q", [], fill=BOX, tsize=11)
ic = gate_and(1060, CY0 + 20, 2, "RUN → HALT")
f.text(944, CY0 + 6, "halt = ¬RUN", 10.5, "start", fill=ORANGE)
f.line([(940, CY0 + 13), (1059, ic[0])], ORANGE, 1.4, end=False, dash=CTL)
dot(1000, CY0 + 13, ORANGE)
f.line([(1000, CY0 + 13), (1000, CY0 + 20), (928, CY0 + 20), (928, CY0 + 47), (939, CY0 + 47)], ORANGE, 1.3, end=True, dash=CTL)
f.line([(1014, CY0 + 47), (1040, CY0 + 47), (1040, ic[1]), (1052, ic[1])], ORANGE, 1.3, end=False, dash=CTL)
bubble(1060, ic[1], ORANGE)
io = gate_or(1130, CY0 + 34, 2, "clr")
f.line([(1096, CY0 + 20), (1134, io[0])], ORANGE, 1.4, end=False, dash=CTL)
f.line([(1100, CY0 + 70), (1120, CY0 + 70), (1120, io[1]), (1134, io[1])], ORANGE, 1.3, end=False, dash=CTL)
lbl(1096, CY0 + 70, "chip reset", "end", c=ORANGE)
f.line([(1168, CY0 + 34), (1200, CY0 + 34), (1200, CY0 + 104)], ORANGE, 1.6, end=False, dash=CTL)
SBY = AY + 112 + NA * ROWH - 34
f.line([(1200, CY0 + 104), (1200, SBY), (RX + RW / 2, SBY), (RX + RW / 2, R0 + NA * ROWH + 2)], ORANGE, 1.6, end=True, dash=CTL)
f.text(RX + RW / 2 + 8, SBY - 6, "clr to all 11 cells: the clear wins over a set in the same cycle", 10.5, "start", fill=ORANGE)
f.text(30, SBY + 10, "* A store to TILE_CONDITIONS is a bus error. P: program error. H: hardware-fault detector; no program on a correct design sets it. 6 and 7 can also mean a fault (a corrupted image).",
       10.5, "start", fill=MUTED)

# =============================================================================================
# B. one bit cell   |   C. the overflow detector
# =============================================================================================
BY = AY + 112 + NA * ROWH + 18
frame(16, BY, 628, 280)
f.text(30, BY + 24, "B. One bit cell: set-only, cleared on entering HALT (§4.13)", 14, "start", "bold")
QY = BY + 110
o1 = gate_or(150, QY - 7, 2, "OR")
lbl(110, o1[0], "set[n]", c=RED, weight="bold")
f.line([(114, o1[0]), (154, o1[0])], RED, 1.5, end=False)
a1 = gate_and(240, QY, 2, "AND")
f.line([(188, QY - 7), (239, a1[0])], INK, 1.5, end=False)
lbl(196, a1[1] + 30, "clr", c=ORANGE, weight="bold")
f.line([(200, a1[1] + 30), (216, a1[1] + 30), (216, a1[1]), (232, a1[1])], ORANGE, 1.4, end=False, dash=CTL)
bubble(240, a1[1], ORANGE)
reg(330, QY - 26, 110, 52, "Q[n]", ["register"], fill=RED_L, stroke=RED)
f.line([(276, QY), (329, QY)], INK, 1.6, end=True)
f.text(300, QY - 6, "D", 10, "middle", fill=MUTED)
f.line([(440, QY), (500, QY)], RED, 1.8, end=True)
dot(470, QY, RED)
f.line([(470, QY), (470, QY + 60), (130, QY + 60), (130, o1[1]), (154, o1[1])], RED, 1.4, end=False)
lbl(506, QY, "Q[n] → bus", "start", c=RED, weight="bold")
f.lines(30, BY + 226, ["D = (Q | set) & ¬clr: once set, a bit holds until the tile enters HALT; a set in the clear's cycle is lost (the",
                       "clear wins). A bit set while halted (say, a bad mode during loading) holds into RUN, where the loader can check it.",
                       "Nothing in RUN clears a bit, so a program cannot hide one."], 10.5, lh=14, fill=MUTED)

CX0 = 656
frame(CX0, BY, W - CX0 - 16, 280)
f.text(CX0 + 14, BY + 24, "C. The overflow detector: conditions 1, 2, 3, 5 and 8 (§4.13)", 14, "start", "bold")
PY = BY + 92
lbl(CX0 + 80, PY - 7, "push", c=INK, weight="bold")
f.line([(CX0 + 84, PY - 7), (CX0 + 110, PY - 7)], INK, 1.5, end=False)
dot(CX0 + 110, PY - 7)
we = gate_and(CX0 + 170, PY, 2, "write enable")
f.line([(CX0 + 110, PY - 7), (CX0 + 169, we[0])], INK, 1.4, end=False)
FB = (CX0 + 260, PY - 30, 150, 60)
f.box(*FB, "FIFO or queue", ["depth D; full = (count = D)"], fill=BOX, stroke=INK, sw=1.4, size=10.5, tsize=11.5)
f.line([(CX0 + 206, PY), (FB[0] - 1, PY)], INK, 1.6, end=True)
FULLY = PY + 52
f.line([(FB[0] + 75, FB[1] + FB[3]), (FB[0] + 75, FULLY)], INK, 1.4, end=False)
f.text(FB[0] + 81, FULLY - 10, "full", 10, "start", fill=MUTED)
dot(FB[0] + 75, FULLY)
f.line([(FB[0] + 75, FULLY), (CX0 + 140, FULLY), (CX0 + 140, we[1]), (CX0 + 162, we[1])], INK, 1.4, end=False)
bubble(CX0 + 170, we[1])
ov = gate_and(CX0 + 170, FULLY + 34, 2, None)
f.line([(CX0 + 110, PY - 7), (CX0 + 110, ov[0]), (CX0 + 169, ov[0])], INK, 1.4, end=False)
f.line([(CX0 + 140, FULLY), (CX0 + 140, ov[1]), (CX0 + 169, ov[1])], INK, 1.4, end=False)
dot(CX0 + 140, FULLY)
f.line([(CX0 + 206, FULLY + 34), (CX0 + 300, FULLY + 34)], RED, 1.6, end=True)
lbl(CX0 + 306, FULLY + 34, "set[n]: the item is dropped", "start", c=RED, weight="bold")
f.lines(CX0 + 14, BY + 226, ["write enable = push & ¬full; set = push & full. The arriving item is dropped on every",
                             "overflow. Full is the count before the cycle's pop, so a push at full overflows even with",
                             "a pop in the same cycle."], 10.5, lh=14, fill=MUTED)

# =============================================================================================
# D. summary
# =============================================================================================
DY = BY + 300
f.text(30, DY, "D. What each condition means for the machine", 14, "start", "bold")
cols = ["bit", "condition", "kind", "what is lost", "does it stop anything?"]
rows = [
    ["0", "drift", "P", "nothing; the tile falls behind its timetable", "no"],
    ["1", "router overflow", "P", "the arriving packet", "no"],
    ["2", "write-engine input overflow", "P", "the arriving packet (a 64-bit write)", "no"],
    ["3", "read-port overflow", "P", "the read request", "no"],
    ["4", "unexpected arrival", "P", "nothing: the packet lands; the counter stays at 0", "no"],
    ["5", "table request overflow", "H", "the PTR or HIST request (an increment)", "no"],
    ["6", "bus error", "P (or fault)", "the access", "halts the tile's controller; engines and router run on"],
    ["7", "illegal instruction", "P (or fault)", "the instruction", "halts the tile's controller; engines and router run on"],
    ["8", "GATHER queue overflow", "H", "the mode-1 request", "no"],
    ["9", "bad mode", "P", "the packet (mode 0 or 8–15 is always dropped)", "no"],
    ["10", "done early", "P", "nothing: done still pulses", "no"],
]
CXs = [30, 74, 290, 380, 760, 1270]
for i, row in enumerate([cols] + rows):
    y0 = DY + 12 + i * 21
    for c in range(5):
        f.rect(CXs[c], y0, CXs[c + 1] - CXs[c], 21, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
        f.text(CXs[c] + 7, y0 + 15, row[c], 10.5, "start", "bold" if (i == 0 or c == 1) else "normal",
               fill=(OPEN_C if (c == 2 and row[c] == "H") else INK))

# notes
y = DY + 12 + (len(rows) + 1) * 21 + 26
notes = [
    "A condition means the software did something wrong, or the hardware is faulty: a correct program on correct hardware reads TILE_CONDITIONS = 0 forever. "
    "Setting one stops nothing except 6 and 7, which halt the controller only.",
    "Entering HALT (the RUN → HALT edge, or chip reset) clears every bit; HALT → RUN does not. A program finds new conditions in a phase by reading TILE_CONDITIONS "
    "at its start and again at its end. C&C learns at once through the error line, and collects the tiles' bits with REPORT (§6.6, a C&C and software protocol).",
    "Detectors for 1, 3 and 8 are drawn in the router, read-port and GATHER figures still to come; this figure fixes their set expressions and what they drop.",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4
LY = y + 14
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "datapath", 11.5, "start", fill=MUTED)
f.line([(160, LY), (200, LY)], RED, 1.6, end=True)
f.text(208, LY + 4, "condition sets, Q and the error line", 11.5, "start", fill=MUTED)
f.line([(450, LY), (490, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(498, LY + 4, "clears", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="570" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(580, LY + 4, "junction (crossings without a dot don't connect); wedge: clocked register", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["TILE_CONDITIONS holds 11 set-only bits, one per error a tile can detect; entering HALT clears them all, and the clear wins over a set in the same cycle.",
           "Their OR is the tile's error line to C&C. Five are overflow detectors that drop the arriving item; two (5 and 8) can only mean a hardware fault.",
           "The register is read-only: a store to it is itself a bus error."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.3, §4.4, §4.7, §4.9–§4.13, §4.16, §6.6, §8.3.")
f.save("fig2_4_conditions.svg")
