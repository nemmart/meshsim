# Figure 7.4: the local gather — mode-1 requests to the tile itself, gather values to PTR[me].
from common import *

W, H = 1300, 1110
f = Fig(W, H, "Figure 7.4. The local gather: mode-1 requests to the tile itself, gather values to PTR[me]",
        "v1.0 (spec/machine-spec.md §4.4, §4.5, §4.9) — reorders values by a sorted index stream (the sort's steps 2 and 6); gathers are local only")

def step(x, y, n, c=INK):
    f.rect(x - 11, y - 11, 22, 22, "white", c, 1.4, rx=11)
    f.text(x, y + 5, str(n), 12, "middle", "bold", fill=c)

# ---------------- network row: injection -> loopback -> director --------------------
NY = 112
INJ = (300, NY, 200, 70)
LB = (560, NY, 200, 70)
TD = (820, NY, 220, 70)
f.box(*INJ, "injection point", ["read returns become packets"], fill=GREEN_L, stroke=GREEN, size=11)
f.box(*LB, "router: loopback", ["dst = me → eject"], fill=BOX, size=11)
f.box(*TD, "traffic director", ["routes by mode"], fill=BOX, stroke=INK, size=11)
f.line([(INJ[0] + INJ[2], NY + 35), (LB[0] - 2, NY + 35)], INK, 2, end=True)
f.line([(LB[0] + LB[2], NY + 35), (TD[0] - 2, NY + 35)], INK, 2, end=True)
f.text(INJ[0] - 14, NY + 22, "both loops pass here:", 11, "end", fill=MUTED)
f.text(INJ[0] - 14, NY + 38, "one injection point and", 11, "end", fill=MUTED)
f.text(INJ[0] - 14, NY + 54, "one eject per tile", 11, "end", fill=MUTED)

# ---------------- engines row --------------------------------------------------------
EY = 300
AG = (40, EY, 280, 96)
GA = (470, EY, 330, 120)
WE = (940, EY, 320, 96)
f.box(*AG, "address generator", ["a mode-1 stream run, d = 0, read size 64:", "one (key, ordinal) word per visit"], fill=GREEN_L, stroke=GREEN, size=11)
f.box(*GA, None, [], fill=GREEN_L, stroke=GREEN)
f.text(GA[0] + GA[2] / 2, EY + 22, "GATHER engine", 13, "middle", "bold")
for k in range(16):
    f.rect(GA[0] + 25 + k * 17.5, EY + 34, 15, 22, "white", GREEN, 0.8, rx=1)
f.text(GA[0] + GA[2] / 2, EY + 72, "16-entry request queue", 11, "middle")
f.text(GA[0] + GA[2] / 2, EY + 90, "per request: k reads of 64 bits at", 11, "middle")
f.text(GA[0] + GA[2] / 2, EY + 106, "DMA_RECORD_BASE_ADDRESS + 2k·ordinal + 2i,  i = 0 … k − 1", 11, "middle", "bold")
f.box(*WE, "write engine (mode 2)", ["each gather value at PTR[me],", "then PTR[me] += 2"], fill=BLUE_L, stroke=BLUE, size=11)

# ---------------- SRAM read queue -----------------------------------------------------------
RS = (40, 490, 760, 52)
f.rect(*RS, GREEN_L, GREEN, 1.4)
f.text(RS[0] + 14, RS[1] + 22, "SRAM read queue", 12.5, "start", "bold")
f.text(RS[0] + 14, RS[1] + 40, "1 gather value  >  2 address generator  >  3 read port (idle here)", 11, "start")

# ---------------- buffers ----------------------------------------------------------------
BY = 620
def buf(x, w, name, sub, c, cl, dash=None):
    f.rect(x, BY, w, 66, cl, c, 1.4, rx=4, dash=dash)
    f.text(x + w / 2, BY + 26, name, 13, "middle", "bold")
    f.text(x + w / 2, BY + 46, sub, 11, "middle", fill=c)
buf(40, 280, "index buffer", "area X — read", GREEN, GREEN_L)
buf(470, 330, "value buffer at DMA_RECORD_BASE_ADDRESS", "area Y — read", GREEN, GREEN_L)
buf(940, 320, "output buffer at PTR[me]", "area Z — written", BLUE, BLUE_L, DASH["write"])
f.text(W - 40, BY + 88, "index, values and output in three different areas keep every area within one access per cycle", 11, "end", fill=MUTED)

# ---------------- flows ------------------------------------------------------------------------
# 1: AG -> SRAM read queue (prio 3) -> index buffer, return -> injection as mode 1
f.line([(AG[0] + 90, AG[1] + AG[3]), (AG[0] + 90, RS[1] - 2)], GREEN, 2, end=True)
f.text(AG[0] + 98, 452, "requests, priority 2", 11, "start", fill=GREEN)
f.line([(AG[0] + 90, RS[1] + RS[3]), (AG[0] + 90, BY - 2)], GREEN, 2, end=True, start=True)
step(AG[0] + 60, 452, 1, GREEN)
# returns up the left of the network row into the injection point
f.line([(RS[0] + 345, RS[1] - 2), (RS[0] + 345, 236), (INJ[0] + 60, 236), (INJ[0] + 60, INJ[1] + INJ[3] + 2)], GREEN, 2.4, end=True)
f.lines(INJ[0] - 52, 210, ["read returns become packets:", "(key, ordinal) words: mode 1, dst me", "gather values: mode 2, dst me"], 11, anchor="end", fill=GREEN, lh=14)
# 2: director -> GATHER queue (mode 1)
f.line([(TD[0] + 50, TD[1] + TD[3]), (TD[0] + 50, 262), (GA[0] + 250, 262), (GA[0] + 250, GA[1] - 2)], INK, 2, end=True)
f.text(GA[0] + 260, 256, "mode 1: gather requests", 11, "start")
step(GA[0] + 232, 280, 2)
# 3: GATHER -> SRAM read queue (prio 2) -> value buffer
f.line([(GA[0] + 160, GA[1] + GA[3]), (GA[0] + 160, RS[1] - 2)], GREEN, 2, end=True)
f.text(GA[0] + 168, 466, "k value reads, priority 1", 11, "start", fill=GREEN)
f.line([(GA[0] + 160, RS[1] + RS[3]), (GA[0] + 160, BY - 2)], GREEN, 2, end=True, start=True)
step(GA[0] + 130, 460, 3, GREEN)
# 4: director -> write engine (mode 2: gather values)
f.line([(TD[0] + 170, TD[1] + TD[3]), (TD[0] + 170, 262), (WE[0] + 160, 262), (WE[0] + 160, WE[1] - 2)], BLUE, 2, end=True, dash=DASH["write"])
f.text(WE[0] + 170, 256, "mode 2: gather values", 11, "start", fill=BLUE)
step(WE[0] + 140, 280, 4, BLUE)
# 5: write engine -> output buffer
f.line([(WE[0] + 160, WE[1] + WE[3]), (WE[0] + 160, BY - 2)], BLUE, 2, end=True, dash=DASH["write"])
f.text(WE[0] + 170, 520, "SRAM write queue", 11, "start", fill=BLUE)
step(WE[0] + 140, 520, 5, BLUE)

# ---------------- worked example ---------------------------------------------------------
EXY = 760
f.text(40, EXY, "Example, k = 2 (a 128-bit value is 2 records): value i sits at DMA_RECORD_BASE_ADDRESS + 4i, output position j at PTR[me] start + 4j", 12.5, "start", "bold")
cw = 58
def arr(x, y, cells, fill, title, hl=None):
    f.text(x, y - 8, title, 11.5, "start", fill=MUTED)
    for i, s in enumerate(cells):
        f.rect(x + i * cw, y, cw, 32, (GREEN_L if hl and i in hl else fill), EDGE, 1, rx=0)
        f.text(x + i * cw + cw / 2, y + 21, s, 11.5, "middle", mono=True)
arr(40, EXY + 34, ["a,5", "b,0", "c,3", "d,1", "…"], BOX, "index: (key, ordinal) words, keys a ≤ b ≤ c ≤ d")
arr(470, EXY + 34, ["V0", "V1", "V2", "V3", "V4", "V5", "…"], BOX, "value buffer: V0 at DMA_RECORD_BASE_ADDRESS, V1 at + 4, …", hl={0, 3, 5})
arr(940, EXY + 34, ["V5", "V0", "V3", "V1", "…"], BLUE_L, "output: one sequential run")
for (si, vi, oi) in [(0, 5, 0), (1, 0, 1), (2, 3, 2)]:
    f.path(f"M {40 + si * cw + cw / 2} {EXY + 66} C {40 + si * cw + cw / 2} {EXY + 110}, {470 + vi * cw + cw / 2} {EXY + 110}, {470 + vi * cw + cw / 2} {EXY + 68}",
           GREEN, 1.2, end=True)
    f.path(f"M {470 + vi * cw + cw / 2} {EXY + 66} C {470 + vi * cw + cw / 2} {EXY + 124}, {940 + oi * cw + cw / 2} {EXY + 124}, {940 + oi * cw + cw / 2} {EXY + 68}",
           BLUE, 1.2, end=True, dash="5 3")

# ---------------- notes --------------------------------------------------------------------
NY2 = EXY + 160
f.lines(40, NY2, [
    ("Counting.", {"weight": "bold"}),
    "Mode-1 requests are not counted; each gather value is a",
    "mode-2 arrival, so a gather of n values sets DMA_SRAM_REMAINING",
    "= n·k (a 20-bit counter). The local wake fires when the last",
    "value is written and mem_idle is set.",
], 11.5, lh=16)
f.lines(470, NY2, [
    ("Rate and back-pressure.", {"weight": "bold"}),
    "≈ k + 1 cycles per value through the one injection point and",
    "one eject: 3 at k = 2, ≈ 196K cycles per 64K values [estimate].",
    "Gather-value reads outrank the address generator, so its index",
    "reads stop while requests are queued; the queue then holds",
    "what was in flight, ≈ 10 [estimate; to be confirmed].",
], 11.5, lh=16)
f.lines(940, NY2, [
    ("Parameters.", {"weight": "bold"}),
    "DMA_RECORD_BASE_ADDRESS: value buffer start (20-bit chunk address).",
    "k: records per value (DMA_CONFIG)",
    "PTR[me]: the output start.",
    ("Open: the queue's in-flight bound under 16; the range of k;", {"fill": OPEN_C}),
    ("one 128-bit read per row-aligned value (halves the reads).", {"fill": OPEN_C}),
], 11.5, lh=16)

f.caption(["The address generator streams the sorted (key, ordinal) words to the tile itself as mode 1; the director hands each to the GATHER engine, whose",
           "value reads return as mode-2 gather values that land in order at PTR[me]. Mode 1 is only ever sent to the tile itself (invariant 10)."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §3.1, §4.2, §4.4, §4.5, §4.9, §4.12, §9, §13.")
f.save("fig7_4_gather.svg")
