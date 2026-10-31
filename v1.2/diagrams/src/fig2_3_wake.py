# Figure 2.3: WFI and wake at RTL level — the three down-counters, the local wake and its qualifier,
# the done gate, WFI control, and the conditions they set (unexpected arrival, done early). v1.0: no wake timer.

from common import *

W, H = 1300, 2000
f = Fig(W, H, "Figure 2.3. WFI and wake: the down-counters, the local wake and the done gate",
        "v1.0 (spec/machine-spec.md §4.12, §4.13, §6.3, §8) — one tile; every register clocks on the tile clock")

CTL = DASH["ctrl"]
RED = "#C0392B"                 # conditions


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


def bubble(x, y, c=INK):
    """inversion bubble whose right edge touches x"""
    f.raw(f'<circle cx="{x - 4:.1f}" cy="{y:.1f}" r="4" fill="white" stroke="{c}" stroke-width="1.3"/>')


def gate_and(x, yc, n, label=None, c=INK, w=36):
    """AND gate, inputs on the left at evenly spaced ys, output at (x + w, yc). Returns input ys."""
    h = max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} L{x + w - h / 2},{y0} A{h / 2},{h / 2} 0 0 1 {x + w - h / 2},{y1} L{x},{y1} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2 - 4, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def gate_or(x, yc, n, label=None, c=INK, w=38):
    h = max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} Q{x + w * 0.55},{y0} {x + w},{yc} Q{x + w * 0.55},{y1} {x},{y1} Q{x + 9},{yc} {x},{y0} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def mux(x, y, h, sel_label=None, w=30):
    """2:1 mux, inputs 0 (top) and 1 (bottom) on the left, output on the right, select on the top edge"""
    f.raw(f'<polygon points="{x},{y} {x + w},{y + 10} {x + w},{y + h - 10} {x},{y + h}" fill="white" stroke="{INK}" stroke-width="1.4"/>')
    f.text(x + 7, y + h * 0.25 + 4, "0", 10, "start", "bold")
    f.text(x + 7, y + h * 0.75 + 4, "1", 10, "start", "bold")
    return (x + w, y + h / 2), y + h * 0.25, y + h * 0.75


def reg(x, y, w, h, title, body=(), fill=SAND, stroke=INK, size=10.5):
    """a clocked register: box with a clock wedge on its bottom edge"""
    f.box(x, y, w, h, title, body, fill=fill, stroke=stroke, sw=1.5, size=size, tsize=11.5)
    cx = x + 18
    f.raw(f'<polyline points="{cx - 6},{y + h} {cx},{y + h - 8} {cx + 6},{y + h}" fill="none" stroke="{stroke}" stroke-width="1.2"/>')


def panel_title(x, y, s):
    f.text(x, y, s, 14, "start", "bold")


def frame(x, y, w, h):
    f.rect(x, y, w, h, "white", GREY, 1, rx=10)


def lbl(x, y, s, anchor="end", c=INK, size=10.5, weight="normal"):
    f.text(x, y + 4, s, size, anchor, weight, fill=c)


# =============================================================================================
# A. one down-counter
# =============================================================================================
AY = 78
frame(16, AY, W - 32, 352)
panel_title(30, AY + 24, "A. A down-counter: DMA_SRAM_REMAINING shown; DMA_CM_REMAINING and DMA_REPLY_REMAINING are the same (§4.12, §8.7)")

# inputs
lbl(30, AY + 52, "st_S: a store to DMA_SRAM_REMAINING", "start", c=ORANGE)
lbl(30, AY + 70, "dec_S: an accepted counted packet", "start", c=ORANGE)
lbl(150, AY + 152, "wdata[19:0]")
S_ST, S_DEC, S_WD = AY + 52, AY + 70, AY + 152

# decrementer, muxes, register
REGX, REGY, REGW, REGH = 520, AY + 96, 170, 58
DEC_BOX = (250, AY + 100, 70, 34)
M1X, M1Y, M1H = 350, AY + 84, 60
M2X, M2Y, M2H = 440, AY + 98, 66
o1, a1, b1 = mux(M1X, M1Y, M1H)
o2, a2, b2 = mux(M2X, M2Y, M2H)
f.box(*DEC_BOX, "− 1", [], fill=BOX, stroke=INK, sw=1.3, tsize=12)
reg(REGX, REGY, REGW, REGH, "DMA_SRAM_REMAINING", ["20-bit register"])

QY = REGY + REGH / 2
FBY = AY + 196
# register output, feedback to hold and to the decrementer
f.line([(REGX + REGW, QY), (740, QY)], INK, 1.8, end=False)
dot(740, QY)
f.line([(740, QY), (740, FBY), (222, FBY), (222, a1), (M1X - 1, a1)], INK, 1.6, end=True)
dot(222, DEC_BOX[1] + 17)
f.line([(222, DEC_BOX[1] + 17), (DEC_BOX[0] - 1, DEC_BOX[1] + 17)], INK, 1.6, end=True)
f.line([(DEC_BOX[0] + DEC_BOX[2], DEC_BOX[1] + 17), (338, DEC_BOX[1] + 17), (338, b1), (M1X - 1, b1)], INK, 1.6, end=True)
f.text(230, FBY - 6, "Q (hold)", 10, "start", fill=MUTED)
f.line([o1, (420, o1[1]), (420, a2), (M2X - 1, a2)], INK, 1.6, end=True)
f.line([(156, S_WD), (430, S_WD), (430, b2), (M2X - 1, b2)], INK, 1.6, end=True)
f.line([o2, (REGX - 1, o2[1])], INK, 1.8, end=True)
f.text(REGX - 6, o2[1] - 6, "D", 10, "end", fill=MUTED)
# selects
f.line([(236, S_DEC), (M1X + 15, S_DEC), (M1X + 15, M1Y + 7)], ORANGE, 1.4, end=True, dash=CTL)
dot(M1X + 15, S_DEC, ORANGE)
f.line([(236, S_ST), (M2X + 15, S_ST), (M2X + 15, M2Y + 7)], ORANGE, 1.4, end=True, dash=CTL)
f.text(M2X + 22, M2Y - 4, "store wins", 10, "start", fill=ORANGE)

# zero detect
ZB = (770, QY - 17, 64, 34)
f.line([(740, QY), (ZB[0] - 1, QY)], INK, 1.6, end=True)
f.box(*ZB, "= 0", [], fill=BOX, stroke=INK, sw=1.3, tsize=12)
ZX = ZB[0] + ZB[2]
f.line([(ZX, QY), (870, QY)], INK, 1.4, end=False)
dot(870, QY)
f.text(ZX + 6, QY - 6, "zero_S", 10, "start", fill=MUTED)
# done_S = zero & mem_idle
ins = gate_and(920, QY + 7, 2)
f.line([(870, QY), (870, ins[0]), (919, ins[0])], INK, 1.4, end=False)
f.line([(870, AY + 236), (896, AY + 236), (896, ins[1]), (919, ins[1])], INK, 1.4, end=False)
lbl(866, AY + 236, "mem_idle (cm_idle for the other two)")
f.line([(956, QY + 7), (1010, QY + 7)], INK, 1.6, end=True)
lbl(1016, QY + 7, "done_S → B", "start", weight="bold")
# unexpected_S = dec & zero
iu = gate_and(920, S_DEC + 7, 2)
f.line([(M1X + 15, S_DEC), (919, S_DEC)], ORANGE, 1.4, end=False, dash=CTL)
f.line([(870, QY), (870, iu[1]), (919, iu[1])], INK, 1.4, end=False)
f.line([(956, S_DEC + 7), (1010, S_DEC + 7)], RED, 1.6, end=True)
lbl(1016, S_DEC + 7, "unexp_S → D (condition 4)", "start", c=RED, weight="bold")
# nz_S
f.line([(870, QY), (870, QY + 44), (1010, QY + 44)], INK, 1.4, end=True)
bubble(885, QY + 44)
lbl(1016, QY + 44, "nz_S = ¬zero_S → B, D", "start", weight="bold")

# the three counters
TY = AY + 262
cols = ["counter", "dec_i: decremented on acceptance of", "idle term in done_i", "store strobe"]
rows = [["DMA_SRAM_REMAINING", "modes 2–4 (gather values included), src ≠ 64", "mem_idle", "st_S"],
        ["DMA_CM_REMAINING", "mode 5 (a 64-bit packet counts 1), src ≠ 64", "cm_idle", "st_C"],
        ["DMA_REPLY_REMAINING", "mode 7 (read-port results included), src ≠ 64", "cm_idle", "st_R"]]
CXs = [30, 190, 520, 680, 800]
for i, row in enumerate([cols] + rows):
    y0 = TY + i * 21
    for c in range(4):
        f.rect(CXs[c], y0, CXs[c + 1] - CXs[c], 21, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
        f.text(CXs[c] + 7, y0 + 15, row[c], 10.5, "start", "bold" if (i == 0 or c == 0) else "normal")
f.lines(822, TY + 14, [
    "Next state: st ? wdata : (dec ? max(Q − 1, 0) : Q).",
    "unexp = dec & (Q = 0): an arrival is judged against the",
    "old count, then a same-cycle store overwrites it, and",
    "the arrival is not counted.",
    "An arrival at 0 holds 0 rather than wrapping.",
], 10.5, lh=13.5)

# =============================================================================================
# B. local wake
# =============================================================================================
BY = AY + 368
frame(16, BY, 628, 330)
panel_title(30, BY + 24, "B. The local wake (§4.12)")

AD_Y = BY + 92
ia = gate_and(110, AD_Y, 3, "AND")
for y, s in zip(ia, ["done_S", "done_C", "done_R"]):
    f.line([(76, y), (109, y)], INK, 1.4, end=False)
    lbl(72, y, s)
f.line([(146, AD_Y), (190, AD_Y)], INK, 1.6, end=False)
dot(190, AD_Y)
f.text(152, AD_Y - 6, "all_done", 10, "start", fill=MUTED)
# all_done_q register and the edge AND
reg(204, BY + 128, 104, 40, "all_done_q", [], fill=BOX)
f.line([(190, AD_Y), (190, BY + 148), (203, BY + 148)], INK, 1.4, end=True)
ie = gate_and(330, AD_Y + 7, 2, "edge")
f.line([(190, AD_Y), (190, ie[0]), (329, ie[0])], INK, 1.4, end=False)
f.line([(308, BY + 148), (318, BY + 148), (318, ie[1]), (322, ie[1])], INK, 1.4, end=False)
bubble(330, ie[1])
# qualifier register
QR = (150, BY + 190, 210, 74)
reg(*QR, "qualifier register", ["set: nz_any (any counter ≠ 0)",
                                  "clear: set_lw, st_cnt, done_p, halt",
                                  "clear wins"])
# the set AND
SX = 430
isl = gate_and(SX, AD_Y + 7, 3, "set_lw")
f.line([(366, AD_Y + 7), (SX - 1, isl[1])], INK, 1.6, end=False)
f.line([(QR[0] + QR[2], QR[1] + 26), (400, QR[1] + 26), (400, isl[2]), (SX - 1, isl[2])], INK, 1.4, end=False)
f.line([(SX - 18, BY + 46), (SX - 18, isl[0]), (SX - 8, isl[0])], ORANGE, 1.4, end=False, dash=CTL)
bubble(SX, isl[0], ORANGE)
f.text(SX - 18, BY + 42, "gate (D)", 10, "middle", fill=ORANGE)
# the latch
LW = (510, BY + 60, 122, 80)
f.line([(SX + 36, AD_Y + 7), (LW[0] - 1, AD_Y + 7)], INK, 1.6, end=True)
reg(*LW, "local-wake", ["latch", "clear: st_cnt,", "consume, done_p,", "halt; clear wins"], size=10)
f.line([(LW[0] + 61, LW[1] + LW[3]), (LW[0] + 61, BY + 196)], INK, 1.6, end=True)
lbl(LW[0] + 61, BY + 210, "Q → wake (D)", "middle", weight="bold")
f.lines(380, BY + 254, ["st_cnt = st_S | st_C | st_R", "nz_any = nz_S | nz_C | nz_R",
                         "a count of 0 never makes an edge:", "the qualifier is never set"], 10.5, lh=14, fill=MUTED)

# =============================================================================================
# C. the wake rules (the wake timer is deleted in v1.0)
# =============================================================================================
CX0 = 656
frame(CX0, BY, W - CX0 - 16, 330)
panel_title(CX0 + 14, BY + 24, "C. The wake rules (§4.12)")
f.lines(CX0 + 14, BY + 54, [
    "wake = go_q | local-wake Q. GO enters through one register, go_q:",
    "a GO pulse at t wakes a waiting WFI at t + 1.",
    "A latch set at the end of cycle c releases a waiting WFI in c + 1:",
    "its next instruction executes that cycle (panel E).",
    "One wake consumes the local-wake latch; a WFI issued with the",
    "latch already set falls straight through.",
    "The done gate: from the done pulse until GO the latch cannot be set,",
    "and the done pulse clears it, so only GO wakes wait_for_command().",
    "Clears: a counter store, consume, the done pulse, halt; a clear wins",
    "over a set in the same cycle.",
    ("A synchronised start uses identical code from WFI to the", {"fill": MUTED}),
    ("start store on every tile (§6.3, figure 2.1).", {"fill": MUTED}),
], 11, lh=17)

# =============================================================================================
# D. the done gate, WFI control and the conditions
# =============================================================================================
DY = BY + 348
frame(16, DY, W - 32, 268)
panel_title(30, DY + 24, "D. The done gate, WFI control and the conditions (§4.12, §4.13, §6.3)")
# gate
ig = gate_and(130, DY + 80, 2, "gate")
for y, s in zip(ig, ["done (status bit)", "RUN"]):
    f.line([(118, y), (129, y)], INK, 1.4, end=False)
    lbl(114, y, s)
f.line([(166, DY + 80), (210, DY + 80)], ORANGE, 1.6, end=True, dash=CTL)
lbl(216, DY + 80, "gate → B", "start", c=ORANGE, weight="bold")
f.lines(30, DY + 122, ["done is set by the done pulse, cleared by GO,", "forced to 1 in HALT and cleared on entering",
                       "RUN; so done & RUN is exactly \"from the done", "pulse until GO\", and the gate is 0 in HALT.",
                       "No new register."], 10.5, lh=14, fill=MUTED)

# wake OR and WFI control
WX = 420
iw = gate_or(WX, DY + 80, 2, "wake")
for y, s in zip(iw[1:], ["local-wake Q"]):
    f.line([(WX - 40, y), (WX + 4, y)], INK, 1.4, end=False)
    lbl(WX - 44, y, s)
reg(300, DY + 36, 60, 24, "go_q", [], fill=BOX)
lbl(294, DY + 48, "GO", weight="bold")
f.line([(360, DY + 48), (372, DY + 48), (372, iw[0]), (WX + 4, iw[0])], INK, 1.4, end=False)
f.line([(WX + 38, DY + 80), (500, DY + 80)], INK, 1.6, end=False)
dot(500, DY + 80)
# in-WFI register and wfi_active
WR = (300, DY + 196, 150, 58)
reg(*WR, "in-WFI register", ["set: wfi_issue & ¬wake", "clear: wake"], fill=PURPLE_L, stroke=PURPLE)
OAX = 466
iwa = gate_or(OAX, DY + 150, 2)
f.text(OAX + 18, DY + 182, "wfi_active", 10, "middle", fill=MUTED)
f.line([(392, iwa[0]), (OAX + 4, iwa[0])], PURPLE, 1.4, end=False)
lbl(388, iwa[0], "wfi_issue (the core)", c=PURPLE)
f.line([(WR[0] + WR[2], WR[1] + 29), (458, WR[1] + 29), (458, iwa[1]), (OAX + 4, iwa[1])], PURPLE, 1.4, end=False)
f.text(WR[0] + WR[2] + 4, WR[1] + 44, "in_wfi", 10, "start", fill=PURPLE)
f.line([(OAX + 38, DY + 150), (530, DY + 150), (530, DY + 66)], PURPLE, 1.4, end=False)
# consume = wfi_active & wake ; stall = wfi_active & ~wake
ic = gate_and(560, DY + 73, 2, "consume")
f.line([(530, ic[0]), (559, ic[0])], PURPLE, 1.4, end=False)
f.line([(500, DY + 80), (559, ic[1])], INK, 1.4, end=False)
f.line([(596, DY + 73), (660, DY + 73)], ORANGE, 1.6, end=True, dash=CTL)
lbl(666, DY + 73, "consume → B (clears the latch)", "start", c=ORANGE, weight="bold")
iss = gate_and(560, DY + 128, 2)
f.text(578, DY + 156, "stall", 10, "middle", fill=MUTED)
dot(530, iss[0], PURPLE)
f.line([(530, iss[0]), (559, iss[0])], PURPLE, 1.4, end=False)
f.line([(500, DY + 80), (500, iss[1]), (552, iss[1])], INK, 1.4, end=False)
bubble(560, iss[1])
f.line([(596, DY + 128), (660, DY + 128)], PURPLE, 1.6, end=True)
lbl(666, DY + 128, "stall the core this cycle", "start", c=PURPLE, weight="bold")
f.lines(560, DY + 182, ["wake = GO | local_latch. A waiting WFI's next instruction",
                        "executes in the first cycle wake = 1: GO at t → t + 1 through go_q;",
                        "a latch set at the end of c → c + 1. One wake consumes the latch;",
                        "a WFI issued with it set falls straight through."], 10.5, lh=14, fill=MUTED)

# conditions
KX = 990
io = gate_or(KX, DY + 60, 3)
for y, s in zip(io, ["unexp_S", "unexp_C", "unexp_R"]):
    f.line([(KX - 44, y), (KX + 4, y)], RED, 1.3, end=False)
    lbl(KX - 48, y, s, c=RED)
ide = gate_and(KX, DY + 140, 2)
for y, s in zip(ide, ["done_p", "nz_any"]):
    f.line([(KX - 44, y), (KX - 1, y)], INK, 1.3, end=False)
    lbl(KX - 48, y, s)
MC = (1110, DY + 40, 156, 160)
f.box(*MC, "TILE_CONDITIONS", ["set-only bits;", "cleared on", "entering HALT", "", "4 unexpected arrival", "10 done early"],
      fill="#FBE9E7", stroke=RED, sw=1.4, size=10.5, tsize=11.5)
for y0, b in [(DY + 60, "4"), (DY + 140, "10")]:
    f.line([(KX + 38, y0), (MC[0] - 1, y0)], RED, 1.6, end=True)
    f.text(MC[0] - 8, y0 - 5, b, 10, "end", "bold", fill=RED)
f.text(KX - 48, DY + 230, "Done early tests the counters, not their idle bits.", 10.5, "start", fill=MUTED)

# =============================================================================================
# E. cycle timing
# =============================================================================================
EY = DY + 286
frame(16, EY, W - 32, 332)
panel_title(30, EY + 24, "E. Cycle timing (§4.12): a latch set at the end of cycle c releases a waiting WFI in c + 1; a late WFI falls through")

def wave(x0, y0, cw, ncyc, title, rows):
    """rows: (label, kind, data) — kind 'pulse': set of cycles high; 'bus': list of values per cycle; 'level': list of 0/1"""
    f.text(x0, y0, title, 11.5, "start", "bold")
    LX = x0 + 96
    for k in range(ncyc):
        cx = LX + k * cw
        f.text(cx + cw / 2, y0 + 20, "t" if k == 0 else f"t+{k}", 10.5, "middle", "bold", fill=MUTED)
        f.raw(f'<line x1="{cx}" y1="{y0 + 26}" x2="{cx}" y2="{y0 + 26 + 26 * len(rows)}" stroke="#ECEAE3" stroke-width="1"/>')
    for r, (label, kind, data, c) in enumerate(rows):
        y = y0 + 28 + r * 26
        f.text(x0, y + 15, label, 10.5, "start", "bold")
        hi, lo = y + 4, y + 20
        if kind == "bus":
            for k, v in enumerate(data):
                xa, xb = LX + k * cw, LX + (k + 1) * cw
                f.raw(f'<polygon points="{xa},{y + 12} {xa + 4},{hi} {xb - 4},{hi} {xb},{y + 12} {xb - 4},{lo} {xa + 4},{lo}" '
                      f'fill="{c if v not in ("", "0") else "white"}" stroke="{EDGE}" stroke-width="1"/>')
                f.text((xa + xb) / 2, y + 16, v, 10, "middle")
        else:
            pts = []
            for k, v in enumerate(data):
                yy = hi if v else lo
                xa = LX + k * cw
                if pts:
                    pts.append((xa, pts[-1][1]))
                pts += [(xa, yy), (xa + cw, yy)]
            f.line(pts, c, 1.8, end=False)
    return LX

cw = 62
L1 = wave(30, EY + 54, cw, 8, "(i) the last packet lands while the core waits", [
    ("core", "bus", ["WFI", "WFI", "WFI", "WFI", "WFI", "next", "", ""], PURPLE_L),
    ("CM count", "bus", ["1", "1", "0", "0", "0", "0", "0", "0"], BOX),
    ("cm_idle", "lvl", [1, 1, 0, 0, 1, 1, 1, 1], INK),
    ("all_done", "lvl", [0, 0, 0, 0, 1, 1, 1, 1], INK),
    ("set_lw (edge)", "lvl", [0, 0, 0, 0, 1, 0, 0, 0], ORANGE),
    ("latch Q", "lvl", [0, 0, 0, 0, 0, 1, 0, 0], ORANGE),
    ("wake, consume", "lvl", [0, 0, 0, 0, 0, 1, 0, 0], INK),
])
L2 = wave(650, EY + 54, cw, 8, "(ii) the copy ends first; the WFI comes late", [
    ("core", "bus", ["work", "work", "work", "work", "WFI", "next", "", ""], PURPLE_L),
    ("all_done", "lvl", [0, 1, 1, 1, 1, 1, 1, 1], INK),
    ("set_lw (edge)", "lvl", [0, 1, 0, 0, 0, 0, 0, 0], ORANGE),
    ("latch Q", "lvl", [0, 0, 1, 1, 1, 0, 0, 0], ORANGE),
    ("wake", "lvl", [0, 0, 1, 1, 1, 0, 0, 0], INK),
    ("consume", "lvl", [0, 0, 0, 0, 1, 0, 0, 0], INK),
])
f.lines(30, EY + 284, ["(i) The last counted packet (here mode 5, DMA_CM_REMAINING) is accepted at t+1: the count reads 0 from t+2, and its port-B write is in flight until cm_idle returns at t+4.",
                       "all_done rises there, a qualified edge sets the latch at the end of t+4, and in t+5 its Q releases the waiting WFI. (ii) The latch holds the edge; the WFI at t+4 finds it set and falls through."],
        10.5, lh=14, fill=MUTED)

# =============================================================================================
# F. interface
# =============================================================================================
FY = EY + 354
panel_title(30, FY, "F. Interface: what the wake logic takes and drives")
cols = ["signal", "width", "from → to", "meaning"]
rows = [
    ["st_S, st_C, st_R", "1 each", "register decode → here", "a store to that counter this cycle (st_cnt = st_S | st_C | st_R)"],
    ["wdata[19:0]", "20", "controller → here", "the store's data for the counters"],
    ["done_p", "1", "register decode → here", "a store with DMA_PULSE [0] set (start acts first, then done)"],
    ["dec_S, dec_C, dec_R", "1 each", "traffic director → here", "a counted packet accepted this cycle: mode match and src ≠ 64 (figure 5.1)"],
    ["mem_idle, cm_idle", "1 each", "write path, port B → here", "the big-SRAM write path is empty; the port-B pipeline is empty (§4.1, §4.12)"],
    ["GO, RUN", "1 each", "C&C's nets → here", "GO: one-cycle pulse, the same cycle on every tile; halt = ¬RUN"],
    ["done", "1", "status → here", "the done status bit (TILE_STATUS [7])"],
    ["wfi_issue", "1", "controller → here", "the WFI instruction executes this cycle"],
    ["stall", "1", "here → controller", "hold the core in WFI this cycle"],
    ["cond_set[4], [10]", "1 each", "here → TILE_CONDITIONS", "set unexpected arrival, done early"],
    ["the three counts", "20 each", "here → register reads", "DMA_SRAM_, DMA_CM_ and DMA_REPLY_REMAINING for loads (the value at the end of the previous cycle)"],
]
CXi = [30, 270, 380, 590, 1270]
for i, row in enumerate([cols] + rows):
    y0 = FY + 12 + i * 21
    for c in range(4):
        f.rect(CXi[c], y0, CXi[c + 1] - CXi[c], 21, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
        f.text(CXi[c] + 7, y0 + 15, row[c], 10.5, "start", "bold" if (i == 0 or c == 0) else "normal")

# =============================================================================================
# notes, legend, caption
# =============================================================================================
y = FY + 12 + (len(rows) + 1) * 21 + 26
notes = [
    "Reset and HALT: the latch, the qualifier and the gate are 0 (halt is in every clear), which covers \"cleared on HALT → RUN\". "
    "On entering RUN no counter is set, so nothing can make an edge before the first GO.",
    "The done gate blocks only the latch's set term; GO goes, through go_q, straight to the wake OR, so GO always wakes. The done pulse also clears the latch and the qualifier, so nothing leaks into wait_for_command().",
    "In the latch and the qualifier a clear beats a set in the same cycle: each of their clears (a counter store, consume, the done pulse, halt) wins over an edge.",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4
LY = y + 14
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "datapath and wake terms", 11.5, "start", fill=MUTED)
f.line([(260, LY), (300, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(308, LY + 4, "control, selects and clears", 11.5, "start", fill=MUTED)
f.line([(500, LY), (540, LY)], PURPLE, 1.6, end=True)
f.text(548, LY + 4, "the core", 11.5, "start", fill=MUTED)
f.line([(630, LY), (670, LY)], RED, 1.6, end=True)
f.text(678, LY + 4, "conditions", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="780" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(790, LY + 4, "junction (crossings without a dot don't connect); wedge: clocked register", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["WFI wakes on GO or on the local wake: the latched edge of all three counters done, qualified by a counter having been non-zero. One wake",
           "consumes the latch. From the done pulse until GO the gate holds the latch's set term off, so only GO can wake wait_for_command(). Counter",
           "stores, consume, the done pulse and HALT clear the latch; a clear wins over a set in the same cycle."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.12, §4.13, §6.3, §8.")
f.save("fig2_3_wake.svg")
