# Figure 6.2: the address generator's control — the run state machine and the first request, the stream-run and group-run
# sequencers, their counters, a cycle-level group run with a refusal (drift), and the interface.
# Drives figure 6.3 (reader side) and figure 6.4 (visit datapath).
from common import *

W, H = 1300, 2400
f = Fig(W, H, "Figure 6.2. The address generator's control: the run state machine, the stream-run and group-run sequencers",
        "v1.0 (spec/machine-spec.md §4.2, §4.13, §8.5, §8.6) — drives figure 6.3 (read-ahead) and figure 6.4 (each visit's request)")

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


def frame(x, y, w, h):
    f.rect(x, y, w, h, "white", GREY, 1, rx=10)


def lbl(x, y, s, anchor="end", c=INK, size=10.5, weight="normal"):
    f.text(x, y + 4, s, size, anchor, weight, fill=c)


def state(cx, cy, name, sub=None, w=124, h=48, fill=GREEN_L, stroke=GREEN):
    f.rect(cx - w / 2, cy - h / 2, w, h, fill, stroke, 1.6, rx=22)
    f.text(cx, cy + (-3 if sub else 5), name, 12, "middle", "bold")
    if sub:
        f.text(cx, cy + 13, sub, 9.5, "middle", fill=MUTED)


def arc(x0, y0, x1, y1, bend, c=INK, dash=None, label=None, lx=None, ly=None, lc=None, anchor="middle"):
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2 + bend
    f.path(f"M{x0},{y0} Q{mx},{my} {x1},{y1}", c, 1.5, end=True, dash=dash)
    if label:
        f.text(lx if lx is not None else mx, ly if ly is not None else my, label, 10.5, anchor, fill=lc or c)


def selfloop(cx, top, label, c=INK, side=1, lw=None):
    x0, x1 = cx - 22, cx + 22
    f.path(f"M{x0},{top} C{x0 - 6},{top - 38} {x1 + 6},{top - 38} {x1},{top}", c, 1.4, end=True)
    f.text(cx, top - 36, label, 10.5, "middle", fill=c)


def table(x0, y0, cxs, rows, rh=21, size=10.5, bold_col=0):
    for i, row in enumerate(rows):
        y = y0 + i * rh
        for c in range(len(cxs) - 1):
            f.rect(cxs[c], y, cxs[c + 1] - cxs[c], rh, BOX if i == 0 else "white", EDGE, 0.8, rx=0)
            cell, col = (row[c] if isinstance(row[c], tuple) else (row[c], INK))
            f.text(cxs[c] + 6, y + rh / 2 + 4, cell, size, "start", "bold" if (i == 0 or c == bold_col) else "normal", fill=col)
    return y0 + len(rows) * rh


# =============================================================================================
# A. the run state machine          B. status and the first request (v1.0: arm merged into start)
# =============================================================================================
AY = 76
frame(16, AY, 700, 330)
f.text(30, AY + 24, "A. The run state machine (§4.2, §8.6)", 14, "start", "bold")
SY = AY + 150
XI, XR = 190, 540
state(XI, SY, "IDLE", "no run", fill=BOX, stroke=EDGE)
state(XR, SY, "RUNNING", "reads ahead, then issues", w=170, fill="#A8DCC8")
f.line([(XI + 62, SY - 6), (XR - 87, SY - 6)], INK, 1.6, end=True)
f.text((XI + XR) / 2, SY - 26, "start_p, count > 0", 11, "middle", "bold")
f.text((XI + XR) / 2, SY - 12, "load the run; rd_done ← 0", 10, "middle")
arc(XR, SY + 24, XI, SY + 24, 70, INK, label="the run's last entry walked: rd_done ← 1", ly=SY + 86)
selfloop(XR, SY - 24, "start_p: ignored", INK)
selfloop(XI, SY - 24, "start_p, count = 0: rd_done ← 1", INK)
f.line([(36, SY), (XI - 64, SY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(36, SY + 16, "chip reset", 10, "start", fill=ORANGE)
f.lines(30, AY + 262, [
    "On start (the end of the start store's cycle): load kind, start and count (fig 6.1), begin the read-ahead (fig 6.3); rd_running ← 1, rd_done ← 0.",
    "A start while RUNNING, its lead-in included, is ignored with no condition; meshsim flags it (§4.13's principle).",
    "A state change is taken at the end of the pulse's cycle. In one DMA_PULSE store, start acts before done.",
    "A start with count 0 is a run with no entries: IDLE stays, rd_done is set that cycle, no request, rd_running never rises.",
], 10.5, lh=15, fill=MUTED)

BX0 = 732
frame(BX0, AY, W - BX0 - 16, 330)
f.text(BX0 + 14, AY + 24, "B. Status and the first request (§4.2, §8.2)", 14, "start", "bold")
f.lines(BX0 + 14, AY + 52, ["rd_running = 1 and rd_done = 0 from the end of the start store's cycle,",
                            "so the next instruction's load of TILE_STATUS sees them; rd_running",
                            "stays 1 through the lead-in until the run's last entry is walked, then",
                            "rd_done = 1 (§4.2, §8.2). Count 0: rd_done = 1 at once. Bit 2 is spare."], 10.5, lh=15)
# first-request timeline
TL0, TY0, SC = BX0 + 110, AY + 136, 34
L = 2
def seg(y, x0, n, label, fill):
    f.rect(TL0 + x0 * SC, y, n * SC, 22, fill, EDGE, 1, rx=3)
    f.text(TL0 + (x0 + n / 2) * SC, y + 15, label, 10, "middle")
f.text(BX0 + 14, TY0 + 15, "group run", 10.5, "start", "bold")
seg(TY0, 0, 2 * L + 1, "GROUP read", GREEN_L)
seg(TY0, 2 * L + 1, 2 * L + 1, "STREAM read", GREEN_L)
f.line([(TL0 + (4 * L + 2) * SC, TY0 - 4), (TL0 + (4 * L + 2) * SC, TY0 + 26)], GREEN, 2, end=False)
f.text(TL0 + (4 * L + 2) * SC + 6, TY0 + 15, "first request", 10, "start", fill=GREEN)
f.text(BX0 + 14, TY0 + 55, "stream run", 10.5, "start", "bold")
seg(TY0 + 40, 0, 2 * L + 1, "STREAM read", GREEN_L)
f.line([(TL0 + (2 * L + 1) * SC, TY0 + 36), (TL0 + (2 * L + 1) * SC, TY0 + 66)], GREEN, 2, end=False)
f.text(TL0 + (2 * L + 1) * SC + 6, TY0 + 55, "first request", 10, "start", fill=GREEN)
f.raw(f'<line x1="{TL0}" y1="{TY0 - 10}" x2="{TL0}" y2="{TY0 + 70}" stroke="{ORANGE}" stroke-width="1.6" stroke-dasharray="4 3"/>')
f.text(TL0, TY0 - 14, "the start takes effect (the store's cycle + 1)", 10, "start", fill=ORANGE)
f.lines(BX0 + 14, AY + 238, [
    "A table read is a round trip of 2·L_ts + 1 cycles (L_ts = 2 drawn). The first request",
    "goes out as soon as its entries are in: 4·L_ts + 3 cycles after the start store for a",
    "group run (4·L_ts + 2 after the cycle the start takes effect), 2·L_ts + 2 (2·L_ts + 1)",
    "for a stream run. S_0 is that first-request cycle. No lead-in counter: the sequencer waits in G_WAIT or",
    "S_WAIT (C, D), and the time depends only on the run kind, the tables and L_ts, so it",
    "is the same on every tile and a synchronised start stays in lockstep.",
], 10.5, lh=14, fill=MUTED)

# =============================================================================================
# C. stream-run sequencer             D. group-run sequencer
# =============================================================================================
CY0 = AY + 348
frame(16, CY0, 620, 482)
f.text(30, CY0 + 24, "C. Stream runs: stream after stream (§4.2)", 14, "start", "bold")
sv_y = CY0 + 150
state(110, sv_y, "S_WAIT", "entry not in", w=110, fill="white", stroke=EDGE)
state(330, sv_y, "S_VISIT", "element j", w=110)
state(330, sv_y + 130, "S_GAP", "gapc cycles", w=110, fill=BOX, stroke=EDGE)
f.line([(165, sv_y - 6), (273, sv_y - 6)], INK, 1.5, end=True)
f.text(219, sv_y - 12, "entry in, count > 0", 10, "middle")
selfloop(330, sv_y - 24, "issue; hold until taken", INK)
f.line([(330, sv_y + 24), (330, sv_y + 104)], INK, 1.5, end=True)
f.text(338, sv_y + 56, "last element taken, run_left > 1:", 10, "start")
f.text(338, sv_y + 69, "gapc ← gap after", 10, "start")
arc(110, sv_y + 24, 273, sv_y + 130, 40, INK, label=None)
f.text(96, sv_y + 96, "count = 0,", 10, "start")
f.text(96, sv_y + 109, "run_left > 1", 10, "start")
# from gap to next stream
f.path(f"M{385},{sv_y + 130} C{490},{sv_y + 130} {490},{sv_y} {387},{sv_y + 4}", INK, 1.5, end=True)
f.text(500, sv_y + 66, "gapc = 0:", 10, "start")
f.text(500, sv_y + 79, "next stream,", 10, "start")
f.text(500, sv_y + 92, "entry in", 10, "start")
f.text(30, CY0 + 50, "next stream's entry not in yet → S_WAIT (also from S_VISIT and S_GAP)", 10, "start", fill=MUTED)
state(110, sv_y + 215, "→ IDLE", "run ends (A)", w=110, fill=BOX, stroke=INK)
f.line([(80, sv_y + 24), (80, sv_y + 189)], INK, 1.5, end=True)
f.text(86, sv_y + 160, "entry in, count = 0,", 10, "start")
f.text(86, sv_y + 173, "run_left = 1", 10, "start")
f.line([(275, sv_y + 12), (260, sv_y + 12), (260, sv_y + 215), (167, sv_y + 215)], INK, 1.5, end=True)
f.text(268, sv_y + 190, "last element taken, run_left = 1:", 10, "start")
f.text(268, sv_y + 203, "the last gap-after is not waited", 10, "start")
f.lines(30, CY0 + 438, [
    "A visit takes 1 cycle (2 for 128 bits, two packets). j += 1 per element taken. A refusal holds the visit: no condition",
    "in a stream run. gapc = 0 skips S_GAP. A count-0 stream takes only its gap-after.",
], 10.5, lh=14, fill=MUTED)

DX0 = 652
frame(DX0, CY0, W - DX0 - 16, 482)
f.text(DX0 + 14, CY0 + 24, "D. Group runs: the timetable (§4.2)", 14, "start", "bold")
gy = CY0 + 150
state(DX0 + 90, gy, "G_WAIT", "entry not in", w=110, fill="white", stroke=EDGE)
state(DX0 + 320, gy, "G_ROT", "visit v of rotation r", w=140)
state(DX0 + 320, gy + 130, "G_GAP", "gapc cycles", w=110, fill=BOX, stroke=EDGE)
state(DX0 + 90, gy + 130, "G_ZERO", "R = 0: 1 cycle", w=110)
f.line([(DX0 + 145, gy - 6), (DX0 + 248, gy - 6)], INK, 1.5, end=True)
f.text(DX0 + 196, gy - 12, "entry in, R > 0", 10, "middle")
f.line([(DX0 + 90, gy + 24), (DX0 + 90, gy + 104)], INK, 1.5, end=True)
f.text(DX0 + 96, gy + 70, "entry in, R = 0", 10, "start")
state(DX0 + 90, gy + 230, "→ IDLE", "run ends (A)", w=110, fill=BOX, stroke=INK)
f.line([(DX0 + 90, gy + 154), (DX0 + 90, gy + 204)], INK, 1.5, end=True)
f.text(DX0 + 96, gy + 182, "run_left = 1", 10, "start")
f.line([(DX0 + 256, gy + 24), (DX0 + 256, gy + 230), (DX0 + 147, gy + 230)], INK, 1.5, end=True)
f.text(DX0 + 264, gy + 196, "the last visit time, run_left = 1:", 10, "start")
f.text(DX0 + 264, gy + 209, "the last gap is not waited", 10, "start")
f.line([(DX0 + 145, gy + 130), (DX0 + 263, gy + 130)], INK, 1.5, end=True)
f.text(DX0 + 200, gy + 110, "run_left > 1:", 10, "middle")
f.text(DX0 + 200, gy + 123, "gapc ← gap", 10, "middle")
selfloop(DX0 + 320, gy - 24, "next visit time", INK)
f.line([(DX0 + 320, gy + 24), (DX0 + 320, gy + 104)], INK, 1.5, end=True)
f.text(DX0 + 328, gy + 56, "r = R − 1, v = t − 1, last half,", 10, "start")
f.text(DX0 + 328, gy + 69, "run_left > 1: gapc ← gap", 10, "start")
f.path(f"M{DX0 + 377},{gy + 130} C{DX0 + 500},{gy + 130} {DX0 + 500},{gy} {DX0 + 392},{gy + 4}", INK, 1.5, end=True)
f.text(DX0 + 510, gy + 66, "gapc = 0:", 10, "start")
f.text(DX0 + 510, gy + 79, "next group,", 10, "start")
f.text(DX0 + 510, gy + 92, "entry in", 10, "start")
f.lines(DX0 + 14, CY0 + 424, [
    "Each visit time (1 cycle, 2 for 128 bits): STREAM entry not in → hold (timetable, the same on",
    "every tile); r < count → offer the request, hold until taken; a refusal sets drift (condition 0)",
    "and shifts every later time; r ≥ count → idle. gapc = 0 skips G_GAP. Next entry not in → G_WAIT.",
], 10.5, lh=14, fill=MUTED)

# =============================================================================================
# E. counters
# =============================================================================================
EY = CY0 + 500
f.text(30, EY, "E. The counters (registers in the address generator; e = j or r feeds figure 6.4's address)", 14, "start", "bold")
rows = [
    ["register", "width", "loaded", "steps", "ends (last)"],
    ["run_left", "9", "on start: count", "−1 as each stream or group ends", "1: the last entry; the run ends when it is walked"],
    ["idx (s or g)", "8", "on start: the run's start field", "+1 per stream or group, wrapping within the table (mod 256 or 64)", "—"],
    ["j (stream runs)", "18", "0 at each stream", "+1 per element taken", "j = count − 1"],
    ["r (group runs)", "18", "0 at each group", "+1 per rotation", "r = R − 1"],
    ["v (group runs)", "4", "0 at each rotation", "+1 per visit time", "v = t − 1"],
    ["h", "1", "0 at each visit", "toggles: the second cycle of a 128-bit visit", "h = 1, or the visit is 1 cycle"],
    ["gapc", "3 / 8", "gap after (stream) or gap (group)", "−1 per cycle", "0"],
]
yE = table(30, EY + 12, [30, 150, 210, 450, 840, 1270], rows, rh=21)

# =============================================================================================
# F. a group run, cycle by cycle
# =============================================================================================
FY = yE + 34
frame(16, FY, W - 32, 342)
f.text(30, FY + 24, "F. A group run, cycle by cycle, with one refusal: request-offered cycles (§4.2)", 14, "start", "bold")
f.text(30, FY + 44, "g0: streams a (count 2), b (count 1), t = 2, R = 2, gap 1.  g1: R = 0, gap 0.  g2: stream c (count 1), t = 1, R = 1.  64-bit; S_0 = t, the first-request cycle: 4·L_ts + 3 cycles after the start store.",
       10.5, "start", fill=MUTED)
cw, LX, ncyc = 104, 150, 10
WY = FY + 60
for k in range(ncyc):
    cx = LX + k * cw
    f.text(cx + cw / 2, WY + 12, "t" if k == 0 else f"t+{k}", 10.5, "middle", "bold", fill=MUTED)
    f.raw(f'<line x1="{cx}" y1="{WY + 18}" x2="{cx}" y2="{WY + 18 + 26 * 7}" stroke="#ECEAE3" stroke-width="1"/>')
wrows = [
    ("state", "bus", ["G_ROT", "G_ROT", "G_ROT", "G_ROT", "G_ROT", "G_GAP", "G_ZERO", "G_ROT", "IDLE", "IDLE"],
     [GREEN_L] * 5 + [BOX, GREEN_L, GREEN_L, "white", "white"]),
    ("g, r, v", "bus", ["0, 0, 0", "0, 0, 1", "0, 1, 0", "0, 1, 0", "0, 1, 1", "0: gap", "1", "2, 0, 0", "", ""], None),
    ("visit", "bus", ["a e0", "b e0", "a e1", "a e1", "b idle", "", "", "c e0", "", ""], None),
    ("offered", "lvl", [1, 1, 1, 1, 0, 0, 0, 1, 0, 0], INK),
    ("taken", "lvl", [1, 1, 0, 1, 0, 0, 0, 1, 0, 0], GREEN),
    ("drift (bit 0)", "lvl", [0, 0, 0, 1, 1, 1, 1, 1, 1, 1], RED),
    ("rd_running, rd_done", "bus", ["running"] * 8 + ["done", "done"], ["#A8DCC8"] * 8 + [BOX, BOX]),
]
for r_, (label, kind, data, cc) in enumerate(wrows):
    y = WY + 20 + r_ * 26
    f.text(30, y + 15, label, 10.5, "start", "bold")
    hi, lo = y + 4, y + 20
    if kind == "bus":
        for k, v in enumerate(data):
            xa, xb = LX + k * cw, LX + (k + 1) * cw
            fill = (cc[k] if isinstance(cc, list) else "white") if v else "white"
            f.raw(f'<polygon points="{xa},{y + 12} {xa + 4},{hi} {xb - 4},{hi} {xb},{y + 12} {xb - 4},{lo} {xa + 4},{lo}" '
                  f'fill="{fill}" stroke="{EDGE}" stroke-width="1"/>')
            f.text((xa + xb) / 2, y + 16, v, 10, "middle")
    else:
        pts = []
        for k, v in enumerate(data):
            yy = hi if v else lo
            xa = LX + k * cw
            if pts:
                pts.append((xa, pts[-1][1]))
            pts += [(xa, yy), (xa + cw, yy)]
        f.line(pts, cc, 1.8, end=False)
f.lines(30, FY + 280, [
    "At t+2 the SRAM read queue refuses a's element 1: the visit holds, drift is set at the end of t+2, and every later time moves by one cycle.",
    "Without the refusal g0 ends at t+4 (2·2 + 1 gap), g1 takes t+5 and c issues at t+6. The run ends when its last entry has been walked (g2, at t+7): rd_done ← 1, back to IDLE.",
    "Had the run ended in R = 0 groups, each would still cost its one cycle; the last entry's gap is not waited. An entry not yet read ahead holds the timetable on every tile alike.",
], 10.5, lh=15, fill=MUTED)

# =============================================================================================
# G. interface
# =============================================================================================
GY = FY + 362
f.text(30, GY, "G. Interface", 14, "start", "bold")
rows = [
    ["signal", "width", "from → to", "meaning"],
    ["start_p", "1", "DMA_PULSE [1] (fig 3.2) → here", "one-cycle pulse; acts before done within one store"],
    ["kind, start, count", "1, 8, 9", "DMA_CONFIG (fig 6.1, fig 3.2) → here", "the run, loaded on start"],
    ["GROUP head, valid", "64, 1", "figure 6.3's GROUP queue → here", "R, start stream, t − 1, gap; valid = the entry is in"],
    ["STREAM head, valid", "64, 1", "figure 6.3's STREAM queue → here, fig 6.4", "count, d, mode, size, gap after, source, cm field"],
    ["pop_group, pop_stream", "1 each", "here → figure 6.3", "the visit used the entry: return its credit"],
    ["e, h, offer", "18, 1, 1", "here → figure 6.4", "element index (j or r), half, offer the request this cycle"],
    ["taken", "1", "SRAM read queue (fig 7.1) → here", "the request was accepted this cycle"],
    ["drift_set", "1", "here → TILE_CONDITIONS bit 0 (fig 2.4)", "group run & offer & ¬taken"],
    ["rd_running, rd_done", "1 each", "here → TILE_STATUS [1:0] (fig 3.2)", "panel B; bit 2 is spare"],
]
yG = table(30, GY + 12, [30, 240, 330, 620, 1270], rows, rh=21)

LY = yG + 30
f.line([(30, LY), (70, LY)], INK, 2, end=True)
f.text(78, LY + 4, "transitions and datapath", 11.5, "start", fill=MUTED)
f.line([(260, LY), (300, LY)], ORANGE, 1.6, end=True, dash=CTL)
f.text(308, LY + 4, "reset", 11.5, "start", fill=MUTED)
f.line([(380, LY), (420, LY)], RED, 1.6, end=True)
f.text(428, LY + 4, "condition", 11.5, "start", fill=MUTED)
f.text(530, LY + 4, "one state = one cycle; transitions at the end of the cycle; drawn where the spec is silent, as the README lists", 11.5, "start", fill=MUTED)

f.h = H = int(LY + 96)
f.caption(["Start loads the run and begins the read-ahead; the first request goes out when its entries are in; the run ends once its last entry is walked. Streams run one after another;",
           "a group run follows a fixed timetable, S_g = S_{g−1} + max(R·P, 1) + gap. A missing table entry holds the timetable on every tile alike; a request the SRAM",
           "read queue refuses holds it on this tile alone, which is drift."],
          y=H - 54, source="Source: spec/machine-spec.md v1.0, §4.2, §4.13, §8.5, §8.6; figures 6.1, 6.3, 6.4, 2.4 and 3.2.")
f.save("fig6_2_ag_control.svg")
