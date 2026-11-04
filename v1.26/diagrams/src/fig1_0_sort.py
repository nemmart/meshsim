# Figure 1.0: the six-step sort these units serve (spec section 14, the baseline of 1 October).
from common import *

W, H = 1300, 984
f = Fig(W, H, "Figure 1.0. The sort these units serve: six steps (32-bit key, value of k records)",
        "v1.0 (spec/machine-spec.md §14) — context, not hardware specification")

CW = 400
CHS = [356, 236]
XS = [30, 450, 870]
YS = [96, 96 + 356 + 128]
LOCAL, MESH = (PURPLE, PURPLE_L), (ORANGE, ORANGE_L)

def card(col, row, n, title, kind, lines, tags=()):
    x, y = XS[col], YS[row]
    CH = CHS[row]
    c, cl = LOCAL if kind == "local" else MESH
    f.rect(x, y, CW, CH, "white", c, 1.4, rx=8)
    f.raw(f'<path d="M {x} {y + 8} Q {x} {y} {x + 8} {y} L {x + CW - 8} {y} Q {x + CW} {y} {x + CW} {y + 8} L {x + CW} {y + 44} L {x} {y + 44} Z" fill="{cl}" stroke="{c}" stroke-width="1.4"/>')
    f.rect(x + 12, y + 10, 26, 26, "white", c, 1.4, rx=13)
    f.text(x + 25, y + 28, str(n), 14, "middle", "bold", fill=c)
    f.text(x + 48, y + 28, title, 14, "start", "bold")
    f.text(x + CW - 12, y + 28, "local" if kind == "local" else "mesh", 11, "end", fill=c, italic=True)
    yy = y + 66
    for item in lines:
        if isinstance(item, tuple):
            s, kw = item
        else:
            s, kw = item, {}
        for i, ln in enumerate(wrap(s, CW - 34, 11.5)):
            f.text(x + 14 + (0 if i == 0 else 10), yy, ln, kw.get("size", 11.5), "start",
                   kw.get("weight", "normal"), fill=kw.get("fill", INK))
            yy += 15.5
        yy += 3
    for tx, ty, s in tags:
        f.tag(x + tx, y + ty, s)
    return x, y

B = {"weight": "bold"}
O = {"fill": OPEN_C}
M = {"fill": MUTED}
card(0, 0, 1, "Local sort of (key, index)", "local", [
    ("Augmenting pass:", B),
    "raw 32-bit keys stream to the tile itself: a mode-4 stream run, d = 0, read size 32, one key per packet and per cycle.",
    "PTR[me] = DMA_RECORD_BASE_ADDRESS = a new buffer: each key lands as (key, j), j = 0, 1, 2, … in bits 63–32.",
    ("Bucket passes:", B),
    "mode-3 stream runs to the tile itself; DMA_CONFIG's bucket digit picks the byte; count-on-arrival can count the next digit in the same pass.",
    "Passes ping-pong between two buffers (the disjoint-buffer rule).",
    ("Set aside: an arrival-order augmenting form of mode 3 saves nothing on its own, since the augmenting pass also counts digit 1 for the first bucket pass.", O),
], tags=[(14, 356 - 12, "open: §4.9, §13 item 19")])
card(1, 0, 2, "Local gather into key order", "local", [
    "Reorders the values by the sorted index stream: figure 7.4.",
    "A mode-1 stream run to the tile itself (d = 0, read size 64) streams the sorted (key, ordinal) words; the GATHER engine reads k records per ordinal at DMA_RECORD_BASE_ADDRESS + 2k·ordinal.",
    "The gather values (mode 2, dst me) land in key order at PTR[me].",
    "DMA_SRAM_REMAINING = n·k; the local wake ends the phase.",
    ("≈ k + 1 cycles per value: ≈ 196K per 64K values at k = 2 [estimate].", M),
])
card(2, 0, 3, "Split", "mesh", [
    "A bisection on key values finds, on every tile, each owner's range of records.",
    ("Exchanges:", B),
    "owners exchange probe keys (mode-7 words) and counts (mode-5 (lb, ub) pairs) from the send port.",
    ("Searches:", B),
    "each tile counts by binary searches of its own sorted run through the controller read port (§4.3): a step queues one read per active search, sets DMA_REPLY_REMAINING, and WFIs; converged searches drop out.",
    ("≈ 203K cycles on uniform, 246K mean over the 24 non-trivial sets, 367K worst; exact on all 25 [measured on a 64-tile RV32 ISS].", M),
])
card(0, 1, 4, "Two all-to-alls: keys mode 4, values mode 2", "mesh", [
    "Group runs on the 16-group schedule: groups of 4, T = 4 (T = 8 for 128-bit values), gap 0. Two-command phases, so every tile starts on the same cycle.",
    ("keys: mode 4 (augmented), count-on-arrival on at the receiver;", B),
    ("values: mode 2, count-on-arrival off.", B),
    "Each sender's run lands contiguously at PTR[src], which the receiver sets from its counts (prefix sums): senders need no destination addresses.",
    "Key i and value i correspond: both arrive in the same sender order, and the ordinal is a buffer position.",
])
card(1, 1, 5, "Local sort at the owner", "local", [
    "A local sort of the received (key, i) words: mode-3 bucket passes to the tile itself (§4.2).",
    "Its first digit is already counted: count-on-arrival ran during the keys all-to-all, saving that sort's first COUNT pass, ≈ 64K cycles per sort [estimate].",
])
card(2, 1, 6, "Local gather into final order", "local", [
    "The same operation as step 2, on the received values and the owner's sorted index.",
    "The output is one sequential run at PTR[me].",
])

# arrows between cards
def harrow(c0, row):
    x0 = XS[c0] + CW + 2
    x1 = XS[c0 + 1] - 2
    y = YS[row] + CHS[row] / 2
    f.line([(x0, y), (x1, y)], INK, 2.2, end=True)
harrow(0, 0); harrow(1, 0); harrow(0, 1); harrow(1, 1)
yb = YS[0] + CHS[0]
f.line([(XS[2] + CW / 2, yb + 2), (XS[2] + CW / 2, yb + 28), (XS[0] + CW / 2, yb + 28), (XS[0] + CW / 2, YS[1] - 2)], INK, 2.2, end=True)

# global totals note between rows, and footer
f.text(XS[1] + CW / 2, yb + 54, "global totals (the R fields' group maxima, any global sum): the mesh reduction", 11.5, "middle", fill=MUTED)
f.text(XS[1] + CW / 2, yb + 70, "rows collect to column 0, column 0 to tile 0, tile 0 broadcasts: ≈ 8 controller turnarounds instead of 63;", 11.5, "middle", fill=MUTED)
f.text(XS[1] + CW / 2, yb + 86, "one-word totals move as mode-7 replies (DMA_REPLY_REMAINING = 2 at a collector, 1 mid-chain);", 11.5, "middle", fill=MUTED)
f.text(XS[1] + CW / 2, yb + 102, "vectors, such as the 16 group maxima, as eight 64-bit mode-5 packets counted by DMA_CM_REMAINING", 11.5, "middle", fill=MUTED)
# white halo under the note crossing the arrow
fy = YS[1] + CHS[1] + 34
f.lines(30, fy, [
    ("The values cross the network once, and each value is read twice locally, by the two gathers.", {}),
    ("32-bit values: key and value fit in one 64-bit record, so steps 2 and 6 disappear.", {}),
    ("The phase-by-phase plan with cycles is sort/sort-walkthrough.md: ≈ 1.37M cycles at k = 2, uniform, before its optimisations.", {}),
], 12, lh=17)
f.rect(XS[2] + 210, fy - 14, 190, 20, "white", "white", 0)
for i, (kind, s) in enumerate([("local", "local: inside one tile, one-command phases"), ("mesh", "mesh: tiles exchange packets")]):
    c, cl = LOCAL if kind == "local" else MESH
    f.rect(XS[2] + 40, fy - 13 + i * 20, 22, 14, cl, c, 1.2, rx=3)
    f.text(XS[2] + 70, fy - 2 + i * 20, s, 11.5, "start", fill=MUTED)

f.caption(["Steps 1, 2, 5 and 6 run inside each tile as loopback streams; steps 3 and 4 use the mesh. The gather (figure 7.4) serves steps 2 and 6, augmenting and",
           "count-on-arrival (figure 1.3's write engine) serve steps 1, 4 and 5, and the controller read port serves step 3."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §14, with §4.2–§4.4, §4.9.")
f.save("fig1_0_sort.svg")
