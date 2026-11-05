# Figure 9.1: the Tracer at block level: the RV64 core, its three local stores and the fill bus to the memory system on the
# left; the region, the register window and the output side toward C&C 3 on the right and along the bottom.
# Drawn from DECISIONS-v1.3 sections 1-25 (to 7374646); the review and the open points are in WORKER24/NOTES.md.
from common import *

W, H = 1300, 976
f = Fig(W, H, "Figure 9.1. The Tracer: the RV64 core, its memory interface, and its peripheral side toward C&C 3",
        "v1.3 (planning/DECISIONS-v1.3.md §1–25; DECISIONS-v1.26 items 2, 10, 12; spec §4.17) — block level; "
        "the core's inner split is for orientation only")

SW = 2.2
OPEN = DASH["open"]
RED = "#C0392B"


def rot_text(x, y, s, size=11, fill=INK, anchor="middle"):
    f.raw(f'<text transform="rotate(-90 {x:.1f} {y:.1f})" x="{x:.1f}" y="{y:.1f}" font-size="{size}" '
          f'text-anchor="{anchor}" fill="{fill}">{esc(s)}</text>')


def dot(x, y, c=INK):
    f.raw(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{c}"/>')


# ================= top row: the memory system (a black box) and C&C 3 =================================
MS = (40, 78, 540, 74)
f.rect(*MS, "#2C2C2A", "#2C2C2A", 1, rx=6)
f.text(MS[0] + 16, MS[1] + 24, "the memory system: a black box (§12, §20, §21)", 13, "start", "bold", fill="white")
f.text(MS[0] + 16, MS[1] + 43, "the translator: a TLB tagged (root, page) and the chip's one tree walker", 11, "start", fill="#E8E6DF")
f.text(MS[0] + 16, MS[1] + 60, "the shared 512 KB cache (4,096 lines of 128 B, 8-way), LPDDR behind it", 11, "start", fill="#E8E6DF")

CC = (830, 78, 442, 74)
f.box(*CC, "C&C 3 (RV32), the Tracer's controller, and its router",
      ["the bus: mode-0 writes into the region (0x04), via the director",
       "the register window: 32-bit loads and stores (0xFFFFFA00)"],
      fill=PURPLE_L, stroke=PURPLE, size=11, tsize=12.5)

# ================= left: the fill bus and the three local stores ======================================
TX = 60                      # the fill bus trunk (reads and fills)
WX = 76                      # the data cache's write-backs
f.line([(TX, 560), (TX, MS[1] + MS[3] + 2)], GREEN, SW + 0.6, end=True, start=True)
f.line([(WX, 372), (WX, MS[1] + MS[3] + 2)], BLUE, SW, end=True, dash=DASH["write"])
f.text(90, 168, "the fill bus: one 128 B line (1,024 bits) per transfer,", 11, "start", fill=GREEN)
f.text(90, 182, "a (root, depth, line) request: a program, data or mapping miss", 11, "start", fill=GREEN)

LX, LW = 100, 190
IC = (LX, 198, LW, 96)
DC = (LX, 322, LW, 106)
f.box(*IC, "instruction L1", ["16 KB: 128 lines of 128 B", "fully associative, single-cycle",
                              "tagged by its line in .pages", "read only"],
      fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12.5)
f.box(*DC, "data L1", ["16 KB: 128 lines of 128 B", "fully associative, single-cycle",
                       "write-back: a dirty victim by", "its line; dirty lines at a", "segment's end are discarded"],
      fill=GREEN_L, stroke=GREEN, size=10.5, tsize=12.5)
PR = (LX, 458, 86, 102)
JL = (LX + 98, 458, LW - 98, 102)
f.box(*PR, "hash probe", ["a multiply,", "a shift, one", "line read, 16", "comparators"],
      fill=GREEN_L, stroke=GREEN, size=10, tsize=11.5)
f.box(*JL, "jump LRU", ["64 entries,", "pc → base", "instr_idx;", "a hit is free"],
      fill=GREEN_L, stroke=GREEN, size=10, tsize=11.5)
f.line([(JL[0], 509), (PR[0] + PR[2] + 2, 509)], GREEN, 1.6, end=True)
# section 18's dedicated SRAM: open
SR = (LX, 578, LW, 44)
f.rect(*SR, "white", OPEN_C, 1.4, dash=OPEN, rx=5)
f.text(SR[0] + 10, SR[1] + 18, "the table in a dedicated 128 KB", 10.5, "start", fill=OPEN_C)
f.text(SR[0] + 10, SR[1] + 33, "SRAM (§18's proposal)", 10.5, "start", fill=OPEN_C)
f.tag(SR[0] + LW - 4, SR[1] + 34, "open", anchor="end")
# taps into the trunk
for y in (246, 375, 509):
    f.line([(LX, y), (TX + 2, y)], GREEN, SW, end=True, start=True)
f.line([(LX, 404), (WX + 2, 404)], BLUE, SW, end=False, dash=DASH["write"])
# the fill bus: arbitration etc. open
f.tag(TX - 18, 644, "open: arbitration, blocking, write buffer, error response", anchor="start")

# ================= centre: the RV64 core ================================================================
CO = (450, 180, 340, 420)
f.rect(*CO, BOX, INK, 1.8, rx=8)
f.text(CO[0] + CO[2] / 2, CO[1] + 22, "the RV64 core (§25): its own core", 14, "middle", "bold")
f.text(466, CO[1] + 40, "128 registers, one bank (§13 (b))", 11, "start", fill=MUTED)
IX, IW = 466, 308
f.box(IX, 232, IW, 58, "fetch and decode", ["RV64IMAC; a 4-byte instruction can straddle", "two lines and two pages: two lookups (§25)"],
      fill="white", stroke=EDGE, size=10.5, tsize=12)
f.box(IX, 300, IW, 74, "expansion sequencer", ["the decode table → the template's words, or", "controller-fed ones from the expansion queue;",
                                               "the next offset; stops after RUN_CYCLES rows"],
      fill="white", stroke=EDGE, size=10.5, tsize=12)
f.box(IX, 384, IW, 74, "row executor", ["Jolt's rows: ALU, PEXT (any mask, §9), memory", "rows; an advice row pops the read queue",
                                        "about 4 cycles per row [estimate] (§11)"],
      fill="white", stroke=EDGE, size=10.5, tsize=12)
f.box(IX, 468, IW, 92, "the working state: the state record", ["words 0 and 2–65 (v1.26 item 12): pc, instr_idx,", "the next offset, registers 0–127 × 64 bits;",
                                                     "written through the region, read out by the DMA"],
      fill=PURPLE_L, stroke=PURPLE, size=10.5, tsize=12)
f.rect(IX - 6, 226, IW + 12, 240, "none", OPEN_C, 1.2, dash=OPEN, rx=6)
f.tag(IX + IW, 220, "open: the split", anchor="end")

# buses to the left (Harvard)
f.line([(CO[0], 246), (IC[0] + IC[2] + 2, 246)], GREEN, SW, end=True, start=True)
f.text(370, 239, "instruction bus", 10.5, "middle", fill=GREEN)
f.text(370, 262, "fetch address / word", 9.5, "middle", fill=MUTED)
f.line([(CO[0], 375), (DC[0] + DC[2] + 2, 375)], GREEN, SW, end=True, start=True)
f.text(370, 368, "data bus", 10.5, "middle", fill=GREEN)
f.line([(CO[0], 509), (JL[0] + JL[2] + 2, 509)], GREEN, SW, end=True, start=True)
f.text(370, 502, "pc → instr_idx", 10.5, "middle", fill=GREEN)
f.text(370, 525, "jump targets only (§18)", 9.5, "middle", fill=MUTED)
# the range comparator, beside the data cache's lookup
RC = (304, 392, 132, 78)
f.box(*RC, "range comparator", [("(a − low) < 8·K", {"mono": True}), "64-bit, no cycle (§25)", "low, K: word 66"],
      fill="white", stroke=INK, size=10, tsize=11.5)
dot(370, 375, GREEN)
f.line([(370, 375), (370, RC[1] - 2)], GREEN, 1.4, end=True)
f.text(RC[0] + RC[2] / 2, RC[1] + RC[3] + 14, "fail → ERRORS", 10, "middle", fill=RED)

# ================= right: the bus from C&C 3, the queues and the expansion tables; the window; the DMA ===========
BT = 1196          # the bus from C&C 3 (128-bit region writes)
WT = 1228          # the window (control)
OT = 1256          # DMA outbound
RX, RW = 850, 300
boxes = [("advice queue", "4 KB (§13, §24)", 196, "advice pops", False),
         ("expansion queue", "4 KB: controller-fed expansions (§7)", 266, "controller-fed words", False),
         ("expansion lookup table", "4 KB: 2K × 16 bits (spec §4.19.5)", 336, "template index", True),
         ("expansion lines", "8 KB: the template ROM (spec §4.19.6)", 406, "template words", True)]
for title, sub, y, lab, startup in boxes:
    f.rect(RX, y, RW, 50, PURPLE_L, PURPLE, 1.4, rx=6)
    f.text(RX + RW / 2, y + 21, title, 12.5, "middle", "bold")
    f.text(RX + RW / 2, y + 38, sub, 10.5, "middle", fill=MUTED)
    cy = y + 25
    dot(BT, cy, PURPLE)
    f.line([(BT, cy), (RX + RW + 2, cy)], PURPLE, SW if not startup else 1.8, end=True,
           dash=DASH["startup"] if startup else None)
    f.line([(RX, cy), (CO[0] + CO[2] + 2, cy)], PURPLE, 1.6, end=True)
# the bus: from C&C 3, past the four, into the core (the state record, the working state)
f.line([(BT, CC[1] + CC[3]), (BT, 520), (CO[0] + CO[2] + 2, 520)], PURPLE, SW + 0.6, end=True)
f.text(RX + RW / 2, 500, "the bus from C&C 3: 128-bit writes into the region (§12)", 10.5, "middle", fill=PURPLE)
f.text(RX + RW / 2, 514, "into the core: the state record (§23)", 10.5, "middle", fill=PURPLE)
f.text(RX + RW / 2, 476, "dotted: the expansion tables, loaded once at startup", 10, "middle", fill=PURPLE, italic=True)
# the window: one control path into the core
f.line([(WT, CC[1] + CC[3]), (WT, 582), (CO[0] + CO[2] + 2, 582)], ORANGE, SW, end=True, start=True, dash=DASH["ctrl"])
f.text(RX + RW / 2, 572, "the register window: RUN_CYCLES, pulses, events, ERRORS (§24)", 10.5, "middle", fill=ORANGE)

# ================= bottom: the output side (figure 9.2 draws it) ================================================
BY = 662
OQ = (600, BY, 240, 62)
f.box(*OQ, "output LRUs and queues", ["see figure 9.2 for details"], fill=BLUE_L, stroke=BLUE, size=11, tsize=13)
f.line([(636, CO[1] + CO[3]), (636, BY - 2)], BLUE, SW, end=True, dash=DASH["write"])
f.line([(800, CO[1] + CO[3]), (800, BY - 2)], BLUE, SW, end=True, dash=DASH["write"])
f.text(630, 634, "code stream (32)", 10, "end", fill=BLUE)
f.text(806, 634, "log stream (64 + tag)", 10, "start", fill=BLUE)
# the state record, read in place by the DMA as output queue 0
f.line([(704, 560), (704, BY - 2)], PURPLE, SW, end=True)
f.text(710, 626, "the state record", 10, "start", fill=PURPLE)
f.text(710, 639, "(queue 0)", 9.5, "start", fill=PURPLE)
DM = (1196, BY, 76, 62)
f.box(*DM, "DMA", ["C&C 3's", "transfers"], fill="white", stroke=INK, size=10, tsize=12.5)
f.line([(OQ[0] + OQ[2], BY + 31), (DM[0] - 2, BY + 31)], BLUE, SW, end=True, dash=DASH["write"])
f.line([(OT, DM[1]), (OT, CC[1] + CC[3] + 2)], BLUE, SW + 0.4, end=True, dash=DASH["write"])
rot_text(OT + 12, 400, "DMA packets into a router input of its own → the tiles' memory (first implementation, §24)", 10.5, fill=BLUE)

# ================= lower left: the open points ========================================================
OB = (40, 668, 392, 128)
f.rect(*OB, "white", OPEN_C, 1.3, dash=OPEN, rx=6)
f.text(OB[0] + 12, OB[1] + 20, "Also open (WORKER24/NOTES.md, by number)", 12, "start", "bold", fill=OPEN_C)
f.lines(OB[0] + 12, OB[1] + 38, [
    "11 the fill bus: arbitration, blocking, write buffer, errors back",
    "12 the probe's line bound   13 the fetch path's offset subtract",
    "14 the per-stream compression enables   15 bit numbers",
    "17 queue depths, the drain's interleaving   18 bit positions",
    "21 the range check's top",
], 10.5, fill=INK, lh=15)

# ================= legend ===========================================================================
LY = 828
f.line([(40, LY), (76, LY)], GREEN, SW, end=True); f.text(84, LY + 4, "read: line fills", 11.5, "start", fill=MUTED)
f.line([(200, LY), (236, LY)], BLUE, SW, end=True, dash=DASH["write"]); f.text(244, LY + 4, "write: write-backs, streams, DMA packets", 11.5, "start", fill=MUTED)
f.line([(500, LY), (536, LY)], PURPLE, SW, end=True); f.text(544, LY + 4, "the bus from C&C 3", 11.5, "start", fill=MUTED)
f.line([(670, LY), (706, LY)], PURPLE, 1.8, end=True, dash=DASH["startup"]); f.text(714, LY + 4, "loaded once at startup", 11.5, "start", fill=MUTED)
f.line([(880, LY), (916, LY)], ORANGE, SW, end=True, dash=DASH["ctrl"]); f.text(924, LY + 4, "control: the window", 11.5, "start", fill=MUTED)
f.text(1100, LY + 4, "red: errors", 11.5, "start", fill=RED)
f.legend_status(40, LY + 28)

f.caption(["The Tracer runs the guest on its own RV64 core. Its instruction cache, data cache and jump LRU are its only local stores; each miss is one 128 B line of a file, (root, depth, line),",
           "on the fill bus to the memory system. One bus from C&C 3 fills the advice and expansion queues, loads the expansion tables once at startup, and writes the state into the core;",
           "the window runs it. At each done the DMA drains the output queues (figure 9.2): the state, read in place, and the three streams. Writing word 0 resets everything."],
          y=H - 66, source="Source: planning/DECISIONS-v1.3.md (to 7374646) §1–7, 12, 13, 18–25; DECISIONS-v1.26.md items 1, 2, 10, 12; spec/machine-spec.md §4.17, §4.19. Review: WORKER24/NOTES.md.")
f.save("fig9_1_tracer.svg")
