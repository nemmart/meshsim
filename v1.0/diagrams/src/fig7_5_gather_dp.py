# Figure 7.5: the GATHER engine's datapath at RTL level — the request queue and its overflow detector, the start
# address (DMA_RECORD_BASE_ADDRESS + 2k·ordinal), the read counter, issue to the SRAM read queue, the return token,
# GATHER busy, a cycle-level example and the interface. Item 6 of the RTL-level list.
from common import *

W, H = 1300, 2000
f = Fig(W, H, "Figure 7.5. The GATHER engine's datapath: the request queue, the start address, the k reads and their tokens",
        "v1.0 (spec/machine-spec.md §4.4, §4.5, §4.13) — one tile; gathers are local only")

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


def gate_or(x, yc, n, label=None, c=INK, w=38):
    h = max(28, 14 * n + 6)
    y0, y1 = yc - h / 2, yc + h / 2
    f.raw(f'<path d="M{x},{y0} Q{x + w * 0.55},{y0} {x + w},{yc} Q{x + w * 0.55},{y1} {x},{y1} Q{x + 9},{yc} {x},{y0} Z" '
          f'fill="white" stroke="{c}" stroke-width="1.4"/>')
    if label:
        f.text(x + w / 2, y0 - 4, label, 10, "middle", fill=MUTED)
    return [yc + (i - (n - 1) / 2) * 14 for i in range(n)]


def mux(x, y, h, w=30):
    f.raw(f'<polygon points="{x},{y} {x + w},{y + 10} {x + w},{y + h - 10} {x},{y + h}" fill="white" stroke="{INK}" stroke-width="1.4"/>')
    f.text(x + 7, y + h * 0.25 + 4, "0", 10, "start", "bold")
    f.text(x + 7, y + h * 0.75 + 4, "1", 10, "start", "bold")
    return (x + w, y + h / 2), y + h * 0.25, y + h * 0.75


def reg(x, y, w, h, title, body=(), fill=SAND, stroke=INK, size=10.5, tsize=11.5):
    f.box(x, y, w, h, title, body, fill=fill, stroke=stroke, sw=1.5, size=size, tsize=tsize)
    cx = x + 16
    f.raw(f'<polyline points="{cx - 6},{y + h} {cx},{y + h - 8} {cx + 6},{y + h}" fill="none" stroke="{stroke}" stroke-width="1.2"/>')


def frame(x, y, w, h):
    f.rect(x, y, w, h, "white", GREY, 1, rx=10)


def lbl(x, y, s, anchor="end", c=INK, size=10.5, weight="normal"):
    f.text(x, y + 4, s, size, anchor, weight, fill=c)


def table(x0, y0, cxs, rows, rh=21, size=10.5, bold_col=0):
    for i, row in enumerate(rows):
        y = y0 + i * rh
        for c in range(len(cxs) - 1):
            f.rect(cxs[c], y, cxs[c + 1] - cxs[c], rh, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
            f.text(cxs[c] + 6, y + rh / 2 + 4, row[c], size, "start", "bold" if (i == 0 or c == bold_col) else "normal")
    return y0 + len(rows) * rh


# =============================================================================================
# A. the datapath
# =============================================================================================
AY = 76
frame(16, AY, W - 32, 492)
f.text(30, AY + 24, "A. The datapath (§4.4): one request at a time, k reads per request, one read offered per cycle", 14, "start", "bold")

# --- input and overflow detector
PY = AY + 92
lbl(30, AY + 60, "from the traffic director (figure 5.1): mode 1 accepted", "start", weight="bold")
lbl(110, PY, "push", c=ORANGE, weight="bold")
f.line([(114, PY), (140, PY)], ORANGE, 1.5, end=False, dash=CTL)
dot(140, PY, ORANGE)
we = gate_and(180, PY + 7, 2, "write enable")
f.line([(140, PY), (179, we[0])], ORANGE, 1.4, end=False, dash=CTL)
lbl(110, PY + 56, "payload[51:32]", weight="bold")
f.text(30, PY + 74, "the ordinal, 20 bits; the key half", 10, "start", fill=MUTED)
f.text(30, PY + 87, "[31:0] and [63:52] are dropped", 10, "start", fill=MUTED)
Q = (270, PY - 28, 150, 120)
f.line([(216, PY + 7), (Q[0] - 1, PY + 7)], ORANGE, 1.5, end=True, dash=CTL)
f.line([(114, PY + 56), (Q[0] - 1, PY + 56)], INK, 1.8, end=True)
f.box(*Q, "request queue", ["16 × 20 bits, FIFO", "", "full = (count = 16),", "the count before", "the cycle's pop"], fill=BOX, stroke=INK, sw=1.5, size=10.5, tsize=11.5)
FULLY = Q[1] + Q[3] + 22
f.line([(Q[0] + 40, Q[1] + Q[3]), (Q[0] + 40, FULLY)], INK, 1.4, end=False)
f.text(Q[0] + 46, Q[1] + Q[3] + 14, "full", 10, "start", fill=MUTED)
dot(Q[0] + 40, FULLY)
f.line([(Q[0] + 40, FULLY), (160, FULLY), (160, we[1]), (172, we[1])], INK, 1.3, end=False)
bubble(180, we[1])
ov = gate_and(180, FULLY + 34, 2)
f.line([(140, PY), (140, ov[0]), (179, ov[0])], ORANGE, 1.3, end=False, dash=CTL)
f.line([(160, FULLY), (160, ov[1]), (179, ov[1])], INK, 1.3, end=False)
dot(160, FULLY)
f.line([(216, FULLY + 34), (250, FULLY + 34)], RED, 1.6, end=True)
lbl(256, FULLY + 34, "set[8]: GATHER queue overflow,", "start", c=RED, weight="bold")
lbl(256, FULLY + 49, "the request dropped (figure 2.4 C)", "start", c=RED)

# --- head: ordinal and valid
HX = Q[0] + Q[2]
HEADY = PY + 20
f.line([(HX, HEADY), (480, HEADY)], INK, 1.8, end=False)
f.text(HX + 6, HEADY - 6, "head: ordinal", 10, "start", fill=MUTED)
dot(480, HEADY)
VALY = PY + 64
f.line([(HX, VALY), (450, VALY), (450, AY + 290), (469, AY + 290)], ORANGE, 1.4, end=True, dash=CTL)
f.text(HX + 6, VALY - 6, "valid (not empty)", 10, "start", fill=ORANGE)

# --- start address: ord × k, << 1, + base
MK = (500, HEADY - 20, 150, 40)
f.line([(480, HEADY), (MK[0] - 1, HEADY)], INK, 1.6, end=True)
f.box(*MK, "× k", ["shift-and-add, k = 1–8"], fill=BOX, stroke=INK, sw=1.3, size=10, tsize=12)
lbl(MK[0] + 75, MK[1] - 22, "k = DMA_CONFIG[26:24] + 1", "middle", c=ORANGE)
f.line([(MK[0] + 75, MK[1] - 16), (MK[0] + 75, MK[1] - 1)], ORANGE, 1.3, end=True, dash=CTL)
SH = (680, HEADY - 18, 56, 36)
f.line([(MK[0] + MK[2], HEADY), (SH[0] - 1, HEADY)], INK, 1.6, end=True)
f.box(*SH, "<< 1", [], fill=BOX, stroke=INK, sw=1.3, tsize=12)
AD = (766, HEADY - 18, 60, 36)
f.line([(SH[0] + SH[2], HEADY), (AD[0] - 1, HEADY)], INK, 1.6, end=True)
f.box(*AD, "+", [], fill=BOX, stroke=INK, sw=1.3, tsize=14)
lbl(AD[0] + 30, HEADY + 50, "DMA_RECORD_BASE_ADDRESS [19:0]", "middle", c=INK, weight="bold")
f.line([(AD[0] + 30, HEADY + 40), (AD[0] + 30, AD[1] + AD[3] + 1)], INK, 1.4, end=True)
f.text(AD[0] + 30, HEADY + 70, "start = base + 2k·ordinal, mod 2^20", 10, "middle", fill=MUTED)

# --- address register with load / +2 mux
MX, MY, MH = 880, HEADY - 50, 76
mo, ma, mb = mux(MX, MY, MH)
REG = (950, HEADY - 36, 140, 52)
f.line([(AD[0] + AD[2], HEADY), (850, HEADY), (850, mb), (MX - 1, mb)], INK, 1.6, end=True)
f.line([mo, (REG[0] - 1, mo[1])], INK, 1.8, end=True)
reg(*REG, "addr register", ["20 bits: the next read"])
QX = REG[0] + REG[2]
f.line([(QX, mo[1]), (1200, mo[1])], GREEN, 2, end=False)
dot(1130, mo[1], GREEN)
P2 = (1100, AY + 50, 60, 26)
f.line([(1130, mo[1]), (1130, P2[1] + P2[3] + 1)], INK, 1.4, end=True)
f.box(*P2, "+ 2", [], fill=BOX, stroke=INK, sw=1.3, tsize=12)
f.line([(1130, P2[1] - 1), (1130, AY + 40), (864, AY + 40), (864, ma), (MX - 1, ma)], INK, 1.4, end=True)
f.text(1166, P2[1] + 18, "the next 64-bit read", 10, "start", fill=MUTED)
f.text(MX - 6, MY + 2, "load", 10, "end", fill=ORANGE)
f.line([(MX + 15, MY - 2), (MX + 15, MY + 7)], ORANGE, 1.3, end=True, dash=CTL)

# --- control: active, i, load, pop
CY = AY + 270
reg(470, CY, 150, 64, "active register", ["set: load", "clear: last (pop)"], fill=GREEN_L, stroke=GREEN)
reg(650, CY, 150, 64, "i counter, 3 bits", ["0 on load", "+1 per read taken"], fill=GREEN_L, stroke=GREEN)
f.box(830, CY + 6, 110, 52, "i = k − 1", ["last read"], fill=BOX, stroke=INK, sw=1.3, size=10, tsize=11.5)
f.line([(800, CY + 32), (829, CY + 32)], INK, 1.4, end=True)
f.lines(470, CY + 92, [
    "load = valid & ¬active: the head's start is computed this cycle,",
    "addr register ← start, i ← 0, active ← 1; the first read goes next cycle.",
    "offer = active. On taken: addr += 2, i += 1, and a token is pushed.",
    "last = active & taken & (i = k − 1): pop the queue, active ← 0.",
    "So consecutive requests have a one-cycle gap between their reads.",
], 10.5, lh=15, fill=MUTED)
f.text(1200, mo[1] - 8, "address", 10, "end", fill=GREEN)

# --- to the SRAM read queue and the token
SQ = (1040, CY - 16, 220, 64)
f.line([(1200, mo[1]), (1200, SQ[1] - 1)], GREEN, 2, end=True)
f.box(*SQ, "SRAM read queue (§4.5)", ["priority 1: above the address generator", "and the read port; offer / taken"], fill=GREEN_L, stroke=GREEN, sw=1.4, size=10, tsize=11.5)
TK = (1040, CY + 70, 220, 64)
f.line([(1150, SQ[1] + SQ[3]), (1150, TK[1] - 1)], GREEN, 1.6, end=True)
f.text(1156, SQ[1] + SQ[3] + 14, "taken: push", 10, "start", fill=GREEN)
f.box(*TK, "token {dst = me, mode 2, addr 0}", ["to the return path (token FIFO, item 8):", "the value goes out as a mode-2 packet"], fill="white", stroke=GREEN, sw=1.4, size=10, tsize=11)

# --- GATHER busy
BZ = gate_or(160, CY + 150, 2, "GATHER busy")
for y, s in zip(BZ, ["queue not empty", "active"]):
    f.line([(124, y), (164, y)], INK, 1.3, end=False)
    lbl(120, y, s)
f.line([(198, CY + 150), (240, CY + 150)], GREEN, 1.6, end=True)
lbl(246, CY + 150, "TILE_STATUS [9]", "start", c=GREEN, weight="bold")
f.text(30, CY + 186, "Reads issued but not landed are not included:", 10.5, "start", fill=MUTED)
f.text(30, CY + 200, "DMA_SRAM_REMAINING covers their landing.", 10.5, "start", fill=MUTED)

# =============================================================================================
# B. cycle timing
# =============================================================================================
BY = AY + 512
frame(16, BY, W - 32, 330)
f.text(30, BY + 24, "B. Two requests, k = 2, cycle by cycle: base B, ordinals 5 and 9; every read taken", 14, "start", "bold")
cw, LX, ncyc = 104, 160, 10
WY = BY + 40
for k in range(ncyc):
    cx = LX + k * cw
    f.text(cx + cw / 2, WY + 12, "t" if k == 0 else f"t+{k}", 10.5, "middle", "bold", fill=MUTED)
    f.raw(f'<line x1="{cx}" y1="{WY + 18}" x2="{cx}" y2="{WY + 18 + 26 * 8}" stroke="#ECEAE3" stroke-width="1"/>')
wrows = [
    ("queue head", "bus", ["5", "5", "5", "9", "9", "9", "", "", "", ""]),
    ("load", "lvl", [1, 0, 0, 1, 0, 0, 0, 0, 0, 0]),
    ("active", "lvl", [0, 1, 1, 0, 1, 1, 0, 0, 0, 0]),
    ("i", "bus", ["", "0", "1", "", "0", "1", "", "", "", ""]),
    ("address offered", "bus", ["", "B + 20", "B + 22", "", "B + 36", "B + 38", "", "", "", ""]),
    ("taken, token", "lvl", [0, 1, 1, 0, 1, 1, 0, 0, 0, 0]),
    ("pop", "lvl", [0, 0, 1, 0, 0, 1, 0, 0, 0, 0]),
    ("GATHER busy", "lvl", [1, 1, 1, 1, 1, 1, 0, 0, 0, 0]),
]
for r_, (label, kind, data) in enumerate(wrows):
    y = WY + 20 + r_ * 26
    f.text(30, y + 15, label, 10.5, "start", "bold")
    hi, lo = y + 4, y + 20
    if kind == "bus":
        for k, v in enumerate(data):
            xa, xb = LX + k * cw, LX + (k + 1) * cw
            f.raw(f'<polygon points="{xa},{y + 12} {xa + 4},{hi} {xb - 4},{hi} {xb},{y + 12} {xb - 4},{lo} {xa + 4},{lo}" '
                  f'fill="{GREEN_L if v else "white"}" stroke="{EDGE}" stroke-width="1"/>')
            f.text((xa + xb) / 2, y + 16, v, 10, "middle")
    else:
        pts = []
        for k, v in enumerate(data):
            yy = hi if v else lo
            xa = LX + k * cw
            if pts:
                pts.append((xa, pts[-1][1]))
            pts += [(xa, yy), (xa + cw, yy)]
        f.line(pts, ORANGE if label in ("load", "pop") else (GREEN if label.startswith("taken") else INK), 1.8, end=False)
f.lines(30, BY + 282, [
    "t: ordinal 5 reaches the head; start = B + 2·2·5 = B + 20 is computed and loaded. t+1, t+2: the two reads, each taken, each pushing a token; the second pops.",
    "t+3: ordinal 9 at the head, start B + 36; its reads at t+4 and t+5. Each value costs about k + 1 cycles through the injection point anyway (§4.4's rate), so the gap costs nothing.",
], 10.5, lh=15, fill=MUTED)

# =============================================================================================
# C. interface
# =============================================================================================
CY2 = BY + 350
f.text(30, CY2, "C. Interface", 14, "start", "bold")
rows = [
    ["signal", "width", "from → to", "meaning"],
    ["push, ordinal", "1, 20", "traffic director (fig 5.1) → here", "a mode-1 packet accepted this cycle; payload[51:32]"],
    ["k − 1", "3", "DMA_CONFIG [26:24] (fig 3.2) → here", "the value size in records, k = 1–8"],
    ["base", "20", "DMA_RECORD_BASE_ADDRESS (fig 3.2) → here", "the value buffer's start in a gather run (one base per run, v1.0)"],
    ["offer, address", "1, 20", "here → SRAM read queue (item 7)", "a 64-bit gather-value read, priority 1"],
    ["taken", "1", "SRAM read queue → here", "the read was accepted this cycle"],
    ["token", "—", "here → return path (item 8)", "dst = me, mode 2, addr 0: the value lands at PTR[me], += 2 (figure 5.1)"],
    ["set[8]", "1", "here → TILE_CONDITIONS (fig 2.4)", "push & full: the request is dropped"],
    ["GATHER busy", "1", "here → TILE_STATUS [9] (fig 3.2)", "queue not empty, or a request part-way through its k reads"],
]
yC = table(30, CY2 + 12, [30, 170, 240, 560, 1270], rows)

y = yC + 26
notes = [
    "Back-pressure is implicit (§4.4): the address generator gets a turn in the SRAM read queue only when no gather value is waiting, so its index reads stop "
    "while requests queue, and the queue holds at most what was in flight. A tile's own sources cannot overflow it [meas: meshsim]: condition 8 means a fault.",
    "Counting is not here: mode-1 requests are not counted, and the gather values count down DMA_SRAM_REMAINING as any mode-2 arrival does (n values: n·k).",
    "× k for k = 1–8 is a sum of at most three shifted copies of the 20-bit ordinal, so two adders (ordinal·k = the sum of ordinal << b over the set bits b of k; k = 8 is one shift). The product is taken mod 2^19 before the << 1.",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4
LY = y + 14
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "datapath", 11.5, "start", fill=MUTED)
f.line([(170, LY), (210, LY)], GREEN, 2, end=True)
f.text(218, LY + 4, "reads and tokens", 11.5, "start", fill=MUTED)
f.line([(360, LY), (400, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(408, LY + 4, "control", 11.5, "start", fill=MUTED)
f.line([(480, LY), (520, LY)], RED, 1.6, end=True)
f.text(528, LY + 4, "condition", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="630" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(640, LY + 4, "junction (crossings without a dot don't connect); wedge: clocked register", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["Each mode-1 request queues only its 20-bit ordinal. At the head, the start DMA_RECORD_BASE_ADDRESS + 2k·ordinal is computed and loaded; the k reads",
           "follow, one offered per cycle at priority 1, each stepping the address by 2 and pushing a token that sends the value back to the tile as mode 2.",
           "A push at full drops the request and sets condition 8; GATHER busy covers the queue and the request in progress."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.4, §4.5, §4.13.")
f.save("fig7_5_gather_dp.svg")
