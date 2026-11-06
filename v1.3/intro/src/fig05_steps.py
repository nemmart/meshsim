"""Figure 5: the six steps at full scale, and what each uses."""
from common import *

W, H = 940, 360
f = Fig(W, H, "Figure 5. The six steps of the sort",
        "Steps 1, 2, 5 and 6 run inside each tile as streams from the tile to itself; steps 3 and 4 use the mesh. With 32-bit values")
f.text(24, 74, "the value rides in the record's high word, and steps 2 and 6 (dashed) drop out.", 12.5, "start", fill=MUTED)
steps = [
    ("1. Local sort", ["keys in by augmenting", "(mode 4), then 4 bucket", "passes (mode 3)"], "HIST, PTR,", "write engine", False),
    ("2. Gather", ["values into key", "order by ordinal", "(mode 1)"], "GATHER", "engine", True),
    ("3. Split", ["bisection on key", "values: 63 searches", "in lockstep"], "controller", "read port", False),
    ("4. All-to-alls", ["keys (mode 4) and", "values (mode 2), on", "the 16-group schedule"], "group runs,", "PTR[src]", False),
    ("5. Local sort", ["4 bucket passes;", "digit 1 counted", "as keys arrived"], "HIST, PTR,", "write engine", False),
    ("6. Gather", ["values into final", "key order", "(mode 1)"], "GATHER", "engine", True),
]
BW, G, X0, Y0, BH = 136, 14.8, 24, 128, 96
for i, (t, body, h1, h2, dashed) in enumerate(steps):
    x = X0 + i * (BW + G)
    mesh = i in (2, 3)
    fill, st = (SAND, OPEN_C) if mesh else (BOX, EDGE)
    f.box(x, Y0, BW, BH, t, body, fill=fill, stroke=st, dash="6 4" if dashed else None, size=12, tsize=13.5,
          valign="top", pad=14)
    f.rect(x + 10, Y0 + BH + 18, BW - 20, 44, "white", PURPLE, 1, rx=4, dash="6 4" if dashed else None)
    f.text(x + BW / 2, Y0 + BH + 36, h1, 11.5, "middle", fill=PURPLE)
    f.text(x + BW / 2, Y0 + BH + 52, h2, 11.5, "middle", fill=PURPLE)
    f.line([(x + BW / 2, Y0 + BH), (x + BW / 2, Y0 + BH + 18)], PURPLE, 1, end=False)
    if i < 5:
        f.line([(x + BW + 2, Y0 + BH / 2), (x + BW + G - 2, Y0 + BH / 2)], INK, 1.6)
# bands
yb = Y0 - 24
f.rect(X0, yb - 12, 2 * BW + G, 18, "white", "white", 0)
f.text(X0 + (2 * BW + G) / 2, yb + 2, "inside each tile", 12, "middle", "bold", fill=MUTED)
f.text(X0 + 2 * (BW + G) + (2 * BW + G) / 2, yb + 2, "on the mesh", 12, "middle", "bold", fill=OPEN_C)
f.text(X0 + 4 * (BW + G) + (2 * BW + G) / 2, yb + 2, "inside each tile", 12, "middle", "bold", fill=MUTED)
yc = Y0 + BH + 92
f.text(24, yc, "Purple: the units each step relies on. Each step is one or more phases; a phase ends when all 64 tiles have pulsed READY", 12.5, "start")
f.text(24, yc + 18, "to C&C, which starts the next phase on every tile at once with GO (spec 6.3).", 12.5, "start")
f.save("fig05-six-steps.svg")
