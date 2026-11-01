# Figure 2.2: the tile's edge toward C&C — RUN/HALT edges, GO, done, "command landed", READY and the error line, tile side
# only (C&C's internals are out of scope). Item 12.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 2.2. The tile's edge toward C&C: RUN/HALT, GO, done, command landed, READY and the error line",
        "v1.0 (spec/machine-spec.md §6.1, §6.3, §4.13) — the tile's side; C&C's internals are out of scope")
r = R(f)

AY = 76
r.frame(16, AY, W - 32, 520, "A. The tile's side of the five lines (§6.1, §6.3, §6.1, §6.3)")
# RUN edges
r.lbl(110, AY + 70, "RUN (from C&C)", c=ORANGE, weight="bold")
f.line([(114, AY + 70), (150, AY + 70)], ORANGE, 1.6, end=False, dash=CTL); r.dot(150, AY + 70, ORANGE)
r.reg(180, AY + 90, 80, 34, "run_q", [], fill=BOX, tsize=11)
f.line([(150, AY + 70), (150, AY + 107), (179, AY + 107)], ORANGE, 1.3, end=True, dash=CTL)
a1 = r.gate_and(320, AY + 70, 2, "HALT → RUN")
f.line([(150, AY + 70), (150, a1[0]), (319, a1[0])], ORANGE, 1.3, end=False, dash=CTL)
f.line([(260, AY + 107), (290, AY + 107), (290, a1[1]), (312, a1[1])], ORANGE, 1.3, end=False, dash=CTL); r.bubble(320, a1[1], ORANGE)
f.line([(356, AY + 70), (400, AY + 70)], ORANGE, 1.6, end=True, dash=CTL)
r.lbl(406, AY + 70, "h2r: core reset (PC 0x1F00, x1–x31 = 0); clears done and landed", "start", c=ORANGE)
a2 = r.gate_and(320, AY + 140, 2, "RUN → HALT")
f.line([(140, AY + 133), (312, AY + 133)], ORANGE, 1.3, end=False, dash=CTL); r.bubble(320, AY + 133, ORANGE)
r.lbl(136, AY + 133, "RUN", c=ORANGE)
f.line([(270, AY + 107), (270, a2[1]), (319, a2[1])], ORANGE, 1.3, end=False, dash=CTL)
f.line([(356, AY + 140), (400, AY + 140)], ORANGE, 1.6, end=True, dash=CTL)
r.lbl(406, AY + 140, "r2h: clears TILE_CONDITIONS (fig 2.4); halt = ¬RUN holds the engines in reset (§6.1, §6.3)", "start", c=ORANGE)
# done and landed
DN = (180, AY + 200, 230, 70)
r.reg(*DN, "done register", ["set: done_p (DMA_PULSE [0])", "clear: go_q, h2r"], fill=GREEN_L, stroke=GREEN)
LN = (180, AY + 300, 230, 70)
r.reg(*LN, "command landed register", ["set: a mode-6 write to slot 0 (fig 5.1)", "clear: go_q, h2r"], fill=GREEN_L, stroke=GREEN)
o1 = r.gate_or(470, AY + 235, 2, "done")
f.line([(410, AY + 228), (474, AY + 228)], GREEN, 1.4, end=False)
f.line([(440, AY + 250), (474, AY + 242)], ORANGE, 1.3, end=False, dash=CTL)
r.lbl(436, AY + 254, "halt", c=ORANGE)
f.line([(508, AY + 235), (560, AY + 235)], GREEN, 1.6, end=False); r.dot(560, AY + 235, GREEN)
r.lbl(566, AY + 222, "done → TILE_STATUS [7] (forced to 1 in HALT)", "start", c=GREEN)
# READY edge
x1 = r.gate_and(640, AY + 285, 2)
f.line([(560, AY + 235), (560, x1[0]), (639, x1[0])], GREEN, 1.4, end=False)
f.line([(410, AY + 335), (600, AY + 335), (600, x1[1]), (639, x1[1])], GREEN, 1.4, end=False)
f.line([(676, AY + 285), (700, AY + 285)], GREEN, 1.6, end=False); r.dot(700, AY + 285, GREEN)
r.reg(720, AY + 320, 90, 34, "both_q", [], fill=BOX, tsize=11)
f.line([(700, AY + 285), (700, AY + 337), (719, AY + 337)], GREEN, 1.3, end=True)
e1 = r.gate_and(860, AY + 292, 2, "rising edge")
f.line([(700, AY + 285), (859, e1[0])], GREEN, 1.4, end=False)
f.line([(810, AY + 337), (840, AY + 337), (840, e1[1]), (852, e1[1])], GREEN, 1.3, end=False); r.bubble(860, e1[1])
f.line([(896, AY + 292), (1000, AY + 292)], GREEN, 2, end=True)
r.lbl(1006, AY + 292, "READY: a one-cycle pulse", "start", c=GREEN, weight="bold")
r.lbl(1006, AY + 310, "own line, ready_latency to C&C", "start", c=GREEN)
# GO
r.lbl(110, AY + 420, "GO (global net)", c=ORANGE, weight="bold")
r.reg(180, AY + 403, 80, 34, "go_q", [], fill=BOX, tsize=11)
f.line([(114, AY + 420), (179, AY + 420)], ORANGE, 1.6, end=True, dash=CTL)
f.line([(260, AY + 420), (400, AY + 420)], ORANGE, 1.6, end=True, dash=CTL)
r.lbl(406, AY + 420, "go_q: wakes WFI (fig 2.3); clears done and landed; the done gate opens", "start", c=ORANGE)
# error line
r.lbl(30, AY + 470, "TILE_CONDITIONS Q[10:0]", "start", c=RED, weight="bold")
f.line([(196, AY + 470), (304, AY + 470)], RED, 2.4, end=False)
r.gate_or(300, AY + 470, 1, None, c=RED)
f.text(318, AY + 474, "≥1", 10, "middle", "bold", fill=RED)
f.text(319, AY + 500, "the OR of the registered bits (figure 2.4)", 10, "middle", fill=RED)
f.line([(338, AY + 470), (1000, AY + 470)], RED, 2, end=True)
r.lbl(1006, AY + 470, "error line → the OR tree (figure 1.2)", "start", c=RED, weight="bold")

# B. the lines
BY = AY + 540
f.text(30, BY, "B. The five lines", 14, "start", "bold")
rows = [["line", "direction", "latency", "the tile side"],
        ["RUN/HALT", "C&C → every tile", "run_latency, fixed (as GO)", "run_q gives the two edges; chip reset holds the tile in HALT"],
        ["GO", "C&C → every tile", "balanced: the same cycle on every tile", "go_q: a pulse at t wakes a waiting WFI at t + 1"],
        ["READY", "each tile → C&C", "ready_latency, equal on every line", "a pulse on the rising edge of done & landed; sets C&C's bit"],
        ["error", "each tile → C&C", "no equal-latency requirement", "the OR of the tile's conditions, into an OR tree and C&C's sticky bit"],
        ["the fifth port", "tile 0 and C&C, both ways", "a router port (figure 4.3)", "packets: C&C's commands in; errors and results out"]]
y = r.table(30, BY + 12, [30, 170, 330, 620, 1270], rows)
y = r.notes(y + 26, ["In HALT done reads 1, so the boot marker (mode 6 to slot 0) raises READY at once (§6.2, figure 2.1 (a)). "
                     "GO cannot arrive before the core is in WFI: it needs the READY round trip (§6.3). REPORT is a C&C and software protocol (§6.6), not drawn."])
finish(f, r, y, ["RUN's two edges come from one register: HALT → RUN resets the core and clears done and command landed; RUN → HALT clears the conditions.",
                 "READY is its own line, a one-cycle pulse when done and command landed are first both 1; done reads 1 throughout HALT. GO enters through go_q.",
                 "The error line is the OR of the tile's conditions."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("ctrl", ORANGE, "lines from C&C"), ("line", GREEN, "done, landed, READY"), ("line", RED, "error"), ("dot", INK, "junction; wedge: clocked register")])
f.save("fig2_2_tile_edge.svg")
