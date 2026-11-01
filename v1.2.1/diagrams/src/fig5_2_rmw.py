# Figure 5.2: how PTR and HIST are incremented — one read-modify-write per cycle, the same-index bypass,
# and port B shared between the increment's write-back and the controller (increment first).
# Port arrangement decided with the lead on 3 October 2026 (machine-spec.md §4.10).
from common import *

W, H = 1300, 1060
f = Fig(W, H, "Figure 5.2. Incrementing PTR and HIST: read on port A, write back on port B one cycle later",
        "v1.0 (spec/machine-spec.md §4.9, §4.10) — "
        "PTR on the left, HIST on the right")

CTL = (ORANGE, DASH["ctrl"])      # select and enable signals
CORE_FILL, CORE_EDGE = "#FCEFC7", "#B8860B"


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def poly(pts, fill="white", stroke=INK, sw=1.4):
    p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    f.raw(f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')


def panel(X, name, bits, k, engine_title, engine_body, ctrl_body, ctrl_reads, notes):
    # ---- heading -------------------------------------------------------------------------
    f.text(X + 16, 98, name, 14, "start", "bold")

    # ---- sources: the engine (top left) and the controller (top right) ------------------
    E = (X + 16, 112, 230, 56)
    C = (X + 400, 112, 184, 56)
    f.box(*E, engine_title, engine_body, fill=BOX, stroke=INK, size=10.5, tsize=12.5)
    f.box(*C, "controller (table window)", ctrl_body, fill=CORE_FILL, stroke=CORE_EDGE, sw=1.4, size=10.5, tsize=12.5)
    IX, RQ = X + 40, X + 200                         # idx [7:0] and req from the engine

    # ---- the memory ------------------------------------------------------------------------
    M = (X + 330, 196, 120, 150)
    mL, mR, mT, mB = M[0], M[0] + M[2], M[1], M[1] + M[3]
    f.rect(*M, PURPLE_L, PURPLE, 1.6)
    f.text(mL + 60, 290, name.split(" ")[0], 14, "middle", "bold")
    f.text(mL + 60, 306, f"256 × {bits} bits", 10.5, "middle", fill=MUTED)
    f.text(mL + 60, 321, "dual-port macro", 10.5, "middle", fill=MUTED)
    f.text(mL + 7, 214, "A: read", 11, "start", "bold", fill=PURPLE)
    f.text(mL + 7, 270, "enable = req", 10, "start", fill=ORANGE)
    f.text(mR - 7, 340, "B: read/write", 11, "end", "bold", fill=PURPLE)

    # ---- registers (cycle t -> t+1) ----------------------------------------------------
    LAR = (X + 14, 400, 150, 46)
    RR = (X + 180, 400, 76, 46)
    SR = (X + 236, 476, 84, 46)
    reg = dict(fill="white", stroke=INK, sw=1.6, size=10, tsize=11.5)
    f.box(*LAR, "last address register", ["loads idx when req"], **reg)
    f.box(*RR, "req", ["register"], **reg)
    f.box(*SR, "same", ["register"], **reg)

    # ---- idx: port A address, eq comp, last address register -----------------------
    f.line([(IX, E[1] + E[3]), (IX, LAR[1] - 2)], INK, 2, end=True)
    f.text(IX + 7, 188, "idx [7:0]", 11, "start", "bold")
    dot(IX, 220)
    f.line([(IX, 220), (mL - 2, 220)], INK, 2, end=True)
    f.text(IX + 16, 214, "read address (cycle t)", 10, "start", fill=MUTED)
    CMP = (X + 70, 284, 110, 56)
    f.box(*CMP, "eq comp", ["idx = last address", "and req(t−1)?"], fill="white", stroke=INK, size=10, tsize=11.5)
    dot(IX, 300)
    f.line([(IX, 300), (CMP[0] - 2, 300)], INK, 1.6, end=True)
    f.line([(X + 100, LAR[1]), (X + 100, CMP[1] + CMP[3] + 2)], INK, 1.6, end=True)
    f.text(X + 104, 372, "last address", 10, "start")

    # ---- req: port A enable, last address register's load, req register ---------------
    f.line([(RQ, E[1] + E[3]), (RQ, RR[1] - 2)], ORANGE, 1.8, end=True, dash=CTL[1])
    f.text(RQ + 7, 188, "req", 11, "start", "bold", fill=ORANGE)
    dot(RQ, 253, ORANGE)
    f.line([(RQ, 253), (mL - 2, 253)], ORANGE, 1.6, end=True, dash=CTL[1])
    dot(RQ, 380, ORANGE)
    f.line([(RQ, 380), (X + 150, 380), (X + 150, LAR[1] - 2)], ORANGE, 1.6, end=True, dash=CTL[1])
    f.text(X + 146, 396, "load", 9.5, "end", fill=ORANGE)

    # ---- same: inverted into the gate, and into the same register ----------------------
    f.line([(CMP[0] + CMP[2], 312), (X + 278, 312), (X + 278, SR[1] - 2)], ORANGE, 1.6, end=True, dash=CTL[1])
    f.text(X + 284, 360, "same", 10.5, "start", fill=ORANGE)

    # ---- req(t-1): eq comp, port B select and write, last value's load ------------
    f.line([(RR[0], 423), (X + 172, 423), (X + 172, CMP[1] + CMP[3] + 2)], ORANGE, 1.6, end=True, dash=CTL[1])
    f.line([(RR[0] + RR[2], 423), (X + 536, 423), (X + 536, 506), (X + 526, 506)], ORANGE, 1.6, end=True, dash=CTL[1])
    dot(X + 492, 423, ORANGE)
    f.line([(X + 492, 423), (X + 492, 344)], ORANGE, 1.6, end=True, dash=CTL[1])
    f.text(X + 498, 404, "req(t−1)", 10.5, "start", fill=ORANGE)

    # ---- port A read data, operand mux, incrementer, last value ----------------------
    OMt, OMb = 480, 510
    poly([(X + 330, OMt), (X + 450, OMt), (X + 425, OMb), (X + 355, OMb)])
    f.text(X + 360, OMt + 13, "0", 10.5, "middle", "bold", fill=MUTED)
    f.text(X + 420, OMt + 13, "1", 10.5, "middle", "bold", fill=MUTED)
    f.line([(X + 360, mB), (X + 360, OMt - 2)], INK, 2, end=True)
    f.text(X + 366, 372, "A: read data", 10.5, "start")
    f.line([(SR[0] + SR[2], 499), (X + 343, 499)], ORANGE, 1.6, end=True, dash=CTL[1])
    INC = (X + 340, 530, 100, 42)
    f.line([(X + 390, OMb), (X + 390, INC[1] - 2)], INK, 2, end=True)
    f.box(*INC, f"+ {k}", ["incrementer"], fill=BOX, stroke=INK, size=10, tsize=15)
    L = (X + 452, 480, 72, 52)
    f.box(*L, "last value", ["one cycle"], fill="white", stroke=INK, sw=1.6, size=10, tsize=11)
    BUS_D, BUS_I = 600, 640
    f.line([(X + 390, INC[1] + INC[3]), (X + 390, BUS_D), (X + 550, BUS_D), (X + 550, 344), (X + 517, 344)], INK, 2, end=True)
    dot(X + 390, BUS_D)
    dot(X + 488, BUS_D)
    f.line([(X + 488, BUS_D), (X + 488, L[1] + L[3] + 2)], INK, 1.6, end=True)
    f.line([(X + 488, L[1]), (X + 488, 466), (X + 420, 466), (X + 420, OMt - 2)], INK, 1.6, end=True)
    f.text(X + 396, 594, f"value + {k}", 10.5, "start", "bold")

    # ---- last address -> port B address ------------------------------------------------
    f.line([(X + 90, LAR[1] + LAR[3]), (X + 90, BUS_I), (X + 566, BUS_I), (X + 566, 330), (X + 517, 330)], INK, 1.6, end=True)
    f.text(X + 98, 634, "last address → port B address", 10.5, "start")

    # ---- port B mux ----------------------------------------------------------------------
    poly([(X + 470, 290), (X + 515, 266), (X + 515, 354), (X + 470, 330)])
    f.text(X + 505, 286, "0", 10.5, "middle", "bold", fill=MUTED)
    f.text(X + 505, 335, "1", 10.5, "middle", "bold", fill=MUTED)
    f.line([(X + 470, 310), (mR + 2, 310)], INK, 2.2, end=True)
    f.text(X + 520, 325, "addr", 10, "start", fill=MUTED)
    f.text(X + 520, 358, "data", 10, "start", fill=MUTED)
    f.line([(X + 582, C[1] + C[3]), (X + 582, 282), (X + 517, 282)], PURPLE, 2, end=True)
    f.text(X + 576, 230, "addr, data,", 10.5, "end", fill=PURPLE)
    f.text(X + 576, 244, "read/write" if ctrl_reads else "write", 10.5, "end", fill=PURPLE)
    if ctrl_reads:
        f.line([(X + 430, mT), (X + 430, C[1] + C[3] + 2)], PURPLE, 2, end=True)
        f.text(X + 424, 190, "B: read data → loads", 10.5, "end", fill=PURPLE)

    # ---- notes ---------------------------------------------------------------------------
    y = 686
    for i, n in enumerate(notes):
        ls = wrap(n, 600, 11.5)
        for j, s in enumerate(ls):
            f.text(X + 16 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
            y += 15
        y += 4
    return y


def frame(X, bottom):
    f.raw(f'<rect x="{X}" y="74" width="600" height="{bottom - 74}" rx="12" fill="none" stroke="{GREY}" stroke-width="1.2"/>')


y1 = panel(30, "PTR — the write engine's landing pointers", 20, 2,
      "write engine", ["req, and idx = src (modes 2, 4) or the", "bucket digit (mode 3); 16-entry FIFO"],
      ["PTR at 0x2400: write only"], False,
      ["req always reads port A and loads the last address register; req(t−1) gives port B to the increment (address, data, write), otherwise port B is the controller's.",
       "A controller write that meets an increment is lost. Software sets PTR only between phases (§4.10).",
       "Port B's read data is unused: the controller never reads PTR.",
       "Why the eq comp also needs req(t−1): the bypass is only for a write still landing. Without it, a stale last address (from before the controller set the entry between phases, or junk after reset) could match, and the mux would take a stale last value."])
y2 = panel(670, "HIST — count-on-arrival's counters", 17, 1,
      "count-on-arrival", ["req, and idx = the histogram digit", "(modes 2–4); 16-entry FIFO"],
      ["HIST at 0x2000: read and write"], True,
      ["Port B is shared the same way, and the controller's loads read through it too.",
       "A controller load that meets an increment returns the value the engine wrote, at the engine's address, not the one asked for. "
       "Software reads and clears HIST only between phases (§4.10).",
       "A count past 17 bits is an error (invariant 6).",
       "Why the eq comp also needs req(t−1): the bypass is only for a write still landing. Without it, a stale last address (from before the controller set the entry between phases, or junk after reset) could match, and the mux would take a stale last value."])

PB = max(y1, y2) + 8
frame(30, PB)
frame(670, PB)

# ---------------- the macro requirement ------------------------------------------------------
RED = "#C0262D"
f.rect(30, PB + 16, 1240, 50, "#FDECEC", RED, 2.2, rx=6)
f.text(46, PB + 38, "IMPORTANT: on a read/write address collision, the write must win.", 15, "start", "bold", fill=RED)
f.text(46, PB + 57, "When idx(t) = last address, port A reads the entry port B is writing in the same cycle. The read data is ignored, so it may be garbage, "
       "but the write must land intact. Confirm against the memory compiler's datasheet.", 11.5, "start", fill=RED)
PB += 66

# ---------------- worked example ---------------------------------------------------------------
TY = PB + 36
f.text(30, TY, "Example: idx 7, 7, 3 arrive on cycles t, t+1, t+2 (k = 2 for PTR, 1 for HIST; req = 0 at t−1)", 13, "start", "bold")
cols = ["", "cycle t", "cycle t+1", "cycle t+2", "cycle t+3"]
rows = [
    ["req, idx", "1, 7", "1, 7", "1, 3", "0"],
    ["same", "0", "1 (7 = last address)", "0", "—"],
    ["port A", "read [7]", "read [7], ignored", "read [3]", "—"],
    ["operand mux", "—", "0: port A data = v7", "1: last = v7 + k", "0: port A data = v3"],
    ["port B", "free for the controller", "write [7] ← v7 + k", "write [7] ← v7 + 2k", "write [3] ← v3 + k"],
]
CX = [30, 200, 465, 730, 995, 1270]
RY = TY + 14
RH = 22
for ci in range(5):
    f.rect(CX[ci], RY, CX[ci + 1] - CX[ci], RH, BOX, EDGE, 0.8, rx=0)
    f.text(CX[ci] + 8, RY + 15, cols[ci], 11.5, "start", "bold")
for ri, r in enumerate(rows):
    y0 = RY + RH * (ri + 1)
    for ci in range(5):
        f.rect(CX[ci], y0, CX[ci + 1] - CX[ci], RH, "white", EDGE, 0.8, rx=0)
        f.text(CX[ci] + 8, y0 + 15, r[ci], 11.5, "start", "bold" if ci == 0 else "normal",
               fill=PURPLE if (r[0] == "port B" and ci == 1) else INK)

# ---------------- legend -------------------------------------------------------------------------
# ---------------- interface (moved here from the interface figure, 12 October) -----------------
from rtl import R as _R
IY = RY + RH * 6 + 26
f.text(30, IY + 4, "Interface", 14, "start", "bold")
IY = _R(f).table(30, IY + 16, [30, 250, 340, 640, 1270], [["signal", "width", "from → to", "meaning"],
    ["req, idx", "1, 8", "write engine / count-on-arrival → here", "an increment of PTR[idx] (+2) or HIST[idx] (+1)"],
    ["port A: addr, rdata", "8, 20 or 17", "here and the table macro (both ways)", "read on cycle t"],
    ["port B: addr, wdata, we", "8, 20 or 17, 1", "here → the table macro", "write back on t + 1; the controller's otherwise"],
    ["value", "20 (PTR)", "here → write engine (fig 5.1)", "the chunk address the write lands at"],
    ["table window: addr, wdata, we / rdata", "8, 32, 1 / 17", "controller (fig 3.2) and port B", "PTR write only; HIST read and write"]])
LY = IY + 26
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "engine datapath", 11.5, "start", fill=MUTED)
f.line([(200, LY), (240, LY)], PURPLE, 2, end=True)
f.text(248, LY + 4, "controller (table window)", 11.5, "start", fill=MUTED)
f.line([(420, LY), (460, LY)], ORANGE, 1.6, end=True, dash=CTL[1])
f.text(468, LY + 4, "select and enable", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="610" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(620, LY + 4, "junction (lines that cross without a dot don't connect)", 11.5, "start", fill=MUTED)

H = LY + 108
f.h = H
f.caption(["Each table does one read-modify-write per cycle: port A reads the entry on cycle t, and port B writes the incremented value back on t+1. Port B is shared",
           "with the controller, and the increment wins. When the same entry is incremented twice in a row, the read result is ignored and the last value is used.",
           "req starts the read and loads the last address register; req(t−1) drives the write."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.9, §4.10.")
f.save("fig5_2_rmw.svg")
