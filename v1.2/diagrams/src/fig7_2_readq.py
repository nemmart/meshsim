# Figure 7.2: the SRAM read queue (fixed-priority choice among three readers, steering, no storage) and the tile's
# side of the area interface (one request queue per area, two pushes a cycle, write first; row accesses; per-chunk write
# enables). Item 7 of the RTL-level list. No alignment (4.1): an access from any chunk that crosses a row boundary is
# two parts, row and row + 1; in a group run a spanning read costs a cycle and sets drift.
from common import *

W, H = 1300, 2000
f = Fig(W, H, "Figure 7.2. The SRAM read queue and the area interface: the readers' priority, steering, and each area's request queue",
        "v1.0 (spec/machine-spec.md §4.1, §4.5, §4.13) — one tile; accesses from any chunk; the areas' internals are out of scope")

CTL = DASH["ctrl"]
RED, RED_L = "#C0392B", "#FBE9E7"


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


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
# A. the SRAM read queue
# =============================================================================================
AY = 76
AH = 650
frame(16, AY, W - 32, AH)
f.text(30, AY + 24, "A. The SRAM read queue: fixed priority, steering, a second-part register (§4.5, §4.1's area interface)", 14, "start", "bold")

CL = [("1  GATHER engine", "gather values (fig 7.5)", GREEN_L, GREEN),
      ("2  address generator", "each visit's request (figs 6.4, 6.2)", GREEN_L, GREEN),
      ("3  controller read port", "the read port's queue (item 9)", PURPLE_L, PURPLE)]
CY0 = AY + 60
CH = 62
for i, (nm, sub, fill, st) in enumerate(CL):
    y = CY0 + i * (CH + 22)
    f.box(30, y, 200, CH, nm, [sub, "holds valid + request until taken"], fill=fill, stroke=st, sw=1.4, size=10, tsize=11.5)
f.text(30, CY0 + 3 * (CH + 22) + 6, "request = {chunk[19:0], dest addr, width, mode, dst}", 10.5, "start", fill=MUTED)

# priority select
PS = (300, CY0, 210, 3 * CH + 44)
f.box(*PS, "priority select", ["grant_1 = valid_1", "grant_2 = valid_2 & ¬valid_1", "grant_3 = valid_3 & ¬valid_1 & ¬valid_2", "",
                               "a one-hot mux picks the", "granted request; any = OR of valids", "", "no bypass: a refused grant", "blocks the others this cycle"],
      fill="white", stroke=INK, sw=1.5, size=10, tsize=11.5)
for i in range(3):
    y = CY0 + i * (CH + 22) + CH / 2
    f.line([(230, y - 8), (PS[0] - 1, y - 8)], INK, 1.6, end=True)
    f.line([(PS[0], y + 10), (238, y + 10)], ORANGE, 1.3, end=True, dash=CTL)
    f.text(244, y + 24, f"taken_{i + 1}", 9.5, "start", fill=ORANGE)
f.text(236, CY0 + 18, "valid, req", 9.5, "start", fill=MUTED)

# steering
SX = 560
SEL_Y = PS[1] + PS[3] / 2
f.line([(PS[0] + PS[2], SEL_Y), (SX - 1, SEL_Y)], INK, 2.2, end=True)
f.text(PS[0] + PS[2] + 6, SEL_Y - 8, "granted", 10, "start", fill=MUTED)
ST = (SX, SEL_Y - 70, 230, 172)
f.box(*ST, "steering (part 0)", ["area = chunk[19:17]  (3:8 decode)", "row = chunk[16:2]", "chunk = chunk[1:0], width → token",
                        "n = 1, 2, 4 chunks (32, 64, 128 bits)", "span = chunk[1:0] + n > 4: load part 1", "in range = chunk < size · 2^17",
                        "ok = space & ¬p1v & (in range ? ready[area] : 1)"], fill=BOX, stroke=INK, sw=1.5, size=10, tsize=11.5)
# taken
TK = (SX, ST[1] + ST[3] + 22, 200, 74)
f.line([(SX + 100, ST[1] + ST[3]), (SX + 100, TK[1] - 1)], ORANGE, 1.4, end=True, dash=CTL)
f.box(*TK, "taken = any & ok", ["taken_i = grant_i & ok → client i", "push one token (figure 7.3)", "space = a free token"], fill="white", stroke=ORANGE, sw=1.4, size=10, tsize=11.5)
f.line([(TK[0], TK[1] + 33), (PS[0] + PS[2] / 2, TK[1] + 33), (PS[0] + PS[2] / 2, PS[1] + PS[3] + 1)], ORANGE, 1.4, end=True, dash=CTL)
f.text(PS[0] + PS[2] / 2 + 6, PS[1] + PS[3] + 20, "back to the granted client", 10, "start", fill=ORANGE)
# the part 1 register: a span's second row
AQX = 860
RB = AQX + 8
P1 = (SX, TK[1] + TK[3] + 22, 230, 74)
f.box(*P1, "part 1 register (p1v)", ["a span's second row: chunk[19:2] + 1, tag", "(the next area after an area's last row)",
                                     "pushed the next cycle, before any client"], fill="#EAF2FB", stroke=BLUE, sw=1.4, size=10, tsize=11.5)
f.line([(SX + 216, ST[1] + ST[3]), (SX + 216, P1[1] - 1)], BLUE, 1.4, end=True)
f.text(SX + 210, ST[1] + ST[3] + 16, "span", 9.5, "end", fill=BLUE)
P1Y = P1[1] + 37
f.line([(SX + P1[2], P1Y), (RB, P1Y)], GREEN, 1.6, end=False); dot(RB, P1Y, GREEN)
f.text(SX + P1[2] + 4, P1Y - 6, "part 1 (first)", 9.5, "start", fill=GREEN)

# area queues: three vertical buses, every area tapped
RB, WB, YB = AQX + 8, AQX - 8, AQX + 24          # read bus, write bus, ready bus
f.text(AQX - 20, AY + 52, "one request queue per area (×8): the tile's side", 11.5, "start", "bold")
f.text(AQX - 20, AY + 68, "buses: read (green), write (blue, dashed), ready (orange)", 10, "start", fill=MUTED)
AYS = [(0, AY + 86), (1, AY + 196), (7, AY + 326)]
for a_, y in AYS:
    f.rect(AQX + 60, y, 280, 84, BOX, INK, 1.4, rx=6)
    f.text(AQX + 200, y + 18, f"area {a_} request queue", 11.5, "middle", "bold")
    f.text(AQX + 200, y + 34, "depth area_queue_depth (8 in meshsim)", 10, "middle", fill=MUTED)
    f.text(AQX + 200, y + 50, "two pushes a cycle: the write first", 10, "middle")
    f.text(AQX + 200, y + 66, "serves in arrival order, one row a cycle", 10, "middle")
    f.line([(RB, y + 26), (AQX + 59, y + 26)], GREEN, 1.6, end=True); dot(RB, y + 26, GREEN)
    f.line([(WB, y + 46), (AQX + 59, y + 46)], BLUE, 1.6, end=True, dash=DASH["write"]); dot(WB, y + 46, BLUE)
    f.line([(AQX + 59, y + 70), (YB, y + 70)], ORANGE, 1.2, end=False, dash=CTL); dot(YB, y + 70, ORANGE)
    f.line([(AQX + 340, y + 42), (AQX + 400, y + 42)], INK, 1.8, end=True)
f.text(AQX + 200, AY + 296, "⋮  areas 2–6 alike", 11, "middle", fill=MUTED)
f.text(AQX + 400, AY + 432, "to the array (out of scope)", 10, "end", fill=MUTED)
# read bus from the steering
f.line([(ST[0] + ST[2], SEL_Y - 30), (RB, SEL_Y - 30)], GREEN, 2, end=False); dot(RB, SEL_Y - 30, GREEN)
f.line([(RB, AY + 86 + 26), (RB, P1Y)], GREEN, 2, end=False)
f.text(ST[0] + ST[2] + 4, SEL_Y - 38, "read push", 9.5, "start", fill=GREEN)
# ready bus back to the steering
f.line([(YB, AY + 86 + 70), (YB, AY + 326 + 70)], ORANGE, 1.4, end=False, dash=CTL)
f.line([(YB, SEL_Y + 30), (ST[0] + ST[2] + 1, SEL_Y + 30)], ORANGE, 1.4, end=True, dash=CTL); dot(YB, SEL_Y + 30, ORANGE)
f.text(ST[0] + ST[2] + 6, SEL_Y + 46, "ready_r[7:0]", 10, "start", fill=ORANGE)
# write bus from the write engine, entering from below
WIN = P1[1] + P1[3] + 30
f.line([(WB, AY + 86 + 46), (WB, WIN)], BLUE, 2, end=False, dash=DASH["write"])
f.line([(SX, WIN), (WB, WIN)], BLUE, 2, end=False, dash=DASH["write"]); dot(WB, WIN, BLUE)
f.text(SX, WIN + 16, "the SRAM write queue: the write engine's 64-bit writes from any chunk (fig 5.1 B1); ready_w back", 10, "start", fill=BLUE)
# ready rules and the left-hand notes
f.lines(SX, WIN + 44, [
    "ready_w = count < depth;  ready_r = count + push_w < depth (count before the cycle's",
    "pop, 4.13's rule). A write not ready stalls the write engine; its 4-entry FIFO absorbs it.",
    "An area serves its head every cycle it holds one, so after any cycle it holds at most",
    "depth − 1 requests: 7 at the depth of 8 [meas: MemTest] (§4.1).",
], 10.5, lh=15)
f.lines(30, AY + 452, [
    "A read refused by a full area is not taken:",
    "the client holds it; in a group run that is drift",
    "(fig 6.2). A span's part 1 is sent the next cycle,",
    "ahead of every client (none is taken that cycle):",
    "in a group run that costs a cycle and sets drift.",
    "Out of range (a smaller device): taken with no",
    "area access; it reads 0, in order.",
], 10.5, lh=15, fill=MUTED)

# =============================================================================================
# B. one area's request
# =============================================================================================
BY = AY + AH + 20
frame(16, BY, W - 32, 214)
f.text(30, BY + 24, "B. One area's request, the tile's side of the interface (§4.1)", 14, "start", "bold")
rows = [
    ["field", "width", "read", "write"],
    ["valid / ready", "1 / 1", "from the queue's head / the array accepts this cycle", "the same"],
    ["rw", "1", "0", "1"],
    ["row", "15", "chunk[16:2]; part 1: the next row", "chunk[16:2]; part 1: the next row"],
    ["we", "4", "—", "we[3:0] by chunk[1:0]: 0 → 0011, 1 → 0110, 2 → 1100, 3 → 1000 then 0001 (two pushes, row and row + 1)"],
    ["wdata", "128", "—", "the 64-bit value twice, swapped by chunk[0]: row chunk k = value chunk (k − chunk[0]) mod 2"],
    ["tag, part", "id, 1", "returned with the data: the token, and which of its rows (item 8)", "—"],
    ["rdata, rvalid, rtag, rpart", "128, 1, id, 1", "the whole row; the return path merges a read's rows (figure 7.3)", "—"],
]
table(30, BY + 40, [30, 210, 300, 690, 1270], rows, rh=20)

# =============================================================================================
# C. cycle timing
# =============================================================================================
CYB = BY + 230
frame(16, CYB, W - 32, 320)
f.text(30, CYB + 24, "C. Contention, cycle by cycle: a gather value, the address generator (a group run), the read port, and a write tie", 14, "start", "bold")
cw, LX, ncyc = 110, 190, 6
WY = CYB + 34
for k in range(ncyc):
    cx = LX + k * cw
    f.text(cx + cw / 2, WY + 12, "t" if k == 0 else f"t+{k}", 10.5, "middle", "bold", fill=MUTED)
    f.raw(f'<line x1="{cx}" y1="{WY + 18}" x2="{cx}" y2="{WY + 18 + 26 * 8}" stroke="#ECEAE3" stroke-width="1"/>')
wrows = [
    ("valid_1 (gather), area 2", "lvl", [1, 0, 0, 0, 0, 0]),
    ("valid_2 (addr gen), area 5", "lvl", [1, 1, 1, 0, 0, 0]),
    ("valid_3 (read port), area 1", "lvl", [1, 1, 1, 1, 0, 0]),
    ("grant", "bus", ["1", "2", "2", "3", "", ""]),
    ("ready[area]", "lvl", [1, 0, 1, 1, 0, 0]),
    ("taken", "lvl", [1, 0, 1, 1, 0, 0]),
    ("area 1 pushes", "bus", ["", "", "", "write, read", "", ""]),
    ("drift (bit 0)", "lvl", [0, 0, 1, 1, 1, 1]),
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
        f.line(pts, RED if label.startswith("drift") else (ORANGE if label in ("taken", "ready[area]") else INK), 1.8, end=False)
f.lines(LX + ncyc * cw + 20, WY + 34, [
    "t: the gather value wins; area 2 is ready, so it is taken.",
    "t+1: the address generator is granted, but area 5 is full:",
    "not taken, and the read port gets nothing (no bypass).",
    "Drift is set at the end of t+1 (a group run, fig 6.2).",
    "t+2: area 5 has room; the request is taken.",
    "t+3: the read port is granted. Area 1 also gets a write",
    "from the write engine: both push, the write first.",
], 10.5, lh=16, fill=MUTED)

# =============================================================================================
# D. interface
# =============================================================================================
DY = CYB + 340
f.text(30, DY, "D. Interface", 14, "start", "bold")
rows = [
    ["signal", "width", "from → to", "meaning"],
    ["valid_i, req_i", "1, —", "clients 1–3 → here", "a request held until taken: chunk, dest addr, width, mode, dst"],
    ["taken_i", "1", "here → client i", "the request was accepted this cycle"],
    ["read push, area", "—, 3", "here → area queue", "row, tag, part; one read a cycle at most (part 0, or a span's part 1)"],
    ["write push", "—", "write engine (fig 5.1) → area queue", "row, we[3:0], wdata; ready_w back; a write crossing a row: two pushes"],
    ["ready_r[7:0], ready_w[7:0]", "8, 8", "area queues → here, write engine", "count before the pop below depth (ready_r counts a same-cycle write)"],
    ["token", "—", "here → token FIFO (figure 7.3)", "dest addr, width, chunk[1:0], span, mode, dst, tag, zero (out of range)"],
    ["size − 1", "3", "TILE_STATUS [31:29] → here", "big SRAM in 512 KB areas: chunk ≥ size · 2^17 is out of range"],
]
yD = table(30, DY + 12, [30, 230, 310, 560, 1270], rows)

y = yD + 26
notes = [
    "No request storage in the SRAM read queue beyond the part 1 register: a refused request waits in its client, and with no bypass nobody else is served that cycle. Fixed priority still keeps "
    "the address generator unstarved in a digit all-to-all, where only a waiting read port competes with it (§4.5). A full token FIFO (figure 7.3) also refuses, with the same effect (§4.1).",
    "The write side never waits for the reads: on a tie the write goes in first (writes can't wait; reads can). A write that finds its area full "
    "stalls the write engine; a sustained overload overflows its 4-entry input FIFO, condition 2 (figure 2.4), since the traffic director never stalls.",
    "Alignment: none required (§4.1). A read or write may start at any chunk; one that crosses a row boundary is two area accesses, row and row + 1 (the next area "
    "after an area's last row): a read's part 1 comes from the part 1 register the next cycle, a write's second push from the write engine. Cost: one extra area access. "
    "In a group run a spanning read also costs a cycle and sets drift (§4.1, figure 2.4 bit 0): whether a read spans depends on each tile's own buffer addresses, "
    "so tiles would fall out of lockstep. Group runs use row-aligned elements; stream runs have no timetable and are unaffected.",
    "Out of range on a smaller device: a read (or a span's out-of-range part) is taken with no area access, its chunks read 0; a write is dropped. Neither sets a condition.",
]
for n in notes:
    for j, s in enumerate(wrap(n, 1230, 11.5)):
        f.text(30 + (12 if j else 0), y, ("• " if j == 0 else "") + s, 11.5, "start")
        y += 15
    y += 4
LY = y + 14
f.line([(30, LY), (70, LY)], GREEN, 2, end=True)
f.text(78, LY + 4, "reads", 11.5, "start", fill=MUTED)
f.line([(140, LY), (180, LY)], BLUE, 1.8, end=True, dash=DASH["write"])
f.text(188, LY + 4, "writes", 11.5, "start", fill=MUTED)
f.line([(260, LY), (300, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(308, LY + 4, "handshakes", 11.5, "start", fill=MUTED)
f.line([(410, LY), (450, LY)], PURPLE, 1.8, end=True)
f.text(458, LY + 4, "the controller", 11.5, "start", fill=MUTED)
f.raw(f'<circle cx="580" cy="{LY}" r="3.2" fill="{INK}"/>')
f.text(590, LY + 4, "junction (crossings without a dot don't connect)", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["The SRAM read queue grants the highest-priority valid reader, steers its request to the area chunk[19:17], and takes it if that area has room; a read",
           "that crosses a row boundary sends its second row the next cycle. Each area has one request queue for reads and writes in arrival order, two pushes a",
           "cycle with the write first, and serves one 128-bit row a cycle; writes use per-chunk enables, and reads return the row with its tag and part."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.1, §4.5, §4.13.")
f.save("fig7_2_readq.svg")
