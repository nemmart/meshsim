# Figure 6.1: the address generator — STREAM, GROUP and DMA_CONFIG layouts, stream runs versus group runs.
from common import *

W, H = 1300, 1210
f = Fig(W, H, "Figure 6.1. The address generator: STREAM, GROUP and the run in DMA_CONFIG; stream runs and group runs",
        "v1.0 (spec/machine-spec.md §4.2, §4.10, §8.5, §8.6) — a state machine that walks the tables and offers one read-queue request per visit")

X0, S = 44, 29
DIM = "#E9E7E0"
def entry(y, title, where, low, high, notes_x=None):
    f.text(X0, y, title, 13.5, "start", "bold")
    f.text(X0 + 32 * S + 66, y, where, 11, "end", fill=MUTED)
    for k, (lab, flds) in enumerate([("high", high), ("low", low)]):
        yy = y + 14 + k * 54
        f.text(X0 + 18, yy + 26, lab, 11.5, "middle", fill=MUTED)
        bitbar(f, X0 + 44, yy, flds, S, h=40)
    return y + 14 + 2 * 54

# ---------------- STREAM ---------------------------------------------------------------
y = 104
y = entry(y, "STREAM[0..255]: one 64-bit entry per stream", "its own memory (§4.10); window 0x2800 + 8·i; two 32-bit stores, read as one access",
          low=[("source (chunk address)", 20, GREEN_L), ("controller-memory field", 12, PURPLE_L)],
          high=[("count (elements)", 18, BOX), ("d", 6, SAND), ("mode", 3, ORANGE_L), ("size", 2, GREEN_L), ("gap", 3, BOX)])
f.tag(X0 + 44 + 32 * S + 8, 143, "gap: likely to change")
f.lines(X0, y + 22, [
    "source: the run's first element, as a chunk address (no base register). It advances by the read size per element.",
    "controller-memory field: mode 5 only, the start word in controller memory, + 2 per 64-bit packet (words 0–0x7FF instruction memory, 0x800–0xFFF data memory).",
    "count: elements in the stream's run (0 = padding).   d: relative destination, (me + d) mod 64; d = 0 is the tile itself.   mode: 1–5, stamped on every packet (1 only with d = 0); codes 0, 6, 7 reserved.",
    "size: 00 reserved, an error even at count 0; 01 32 bits (low half of a payload); 10 64 bits; 11 128 bits (two packets on consecutive cycles).",
    "gap after (stream runs only): 0–7 idle cycles after the stream's run; group runs ignore it.   The low word holds what changes every round.",
], 11.5, lh=16)
f.text(X0 + 32 * S + 66, 104 + 14 + 110 + 0, "", 10)

# ---------------- GROUP and DMA_CONFIG ----------------------------------------------------
y = 356
y2 = entry(y, "GROUP[0..63]: one 64-bit entry per group", "its own memory (§4.10); window 0x3000 + 8·g",
           low=[("R: rotations (the group's global maximum count)", 18, BOX), ("spare", 14, DIM)],
           high=[("start stream", 8, SAND), ("t − 1", 4, SAND), ("gap", 8, BOX), ("spare", 12, DIM)])
f.lines(X0, y2 + 22, [
    "start stream, t − 1: the group's streams are start … start + t − 1 (1–16 streams). Groups name their own streams, so groups can share streams and several programs can sit",
    "in the tables at once. gap: idle cycles after the group. R: rotations; R = 0 sends nothing and takes 1 cycle plus its gap. Each pass rewrites only the low words: one store",
    "per group. All streams in a group have one read size.",
], 11.5, lh=16)

y = 584
f.text(X0, y, "DMA_CONFIG (register): the run, and the settings for the write engine and the GATHER engine", 13.5, "start", "bold")
bitbar(f, X0 + 44, y + 14, [("kind", 1, ORANGE_L), ("run start", 8, SAND), ("run count", 9, BOX), ("bucket", 3, BLUE_L), ("hist digit", 3, BLUE_L),
                            ("k − 1", 3, GREEN_L), ("spare", 5, DIM)], S, h=40)
f.text(X0 + 44 + 0.5 * S, y + 68, "kind", 10.5, "middle", fill=MUTED)
f.lines(X0, y + 90, [
    "kind: a stream run or a group run.   run start: the first stream (0–255) or GROUP entry (0–63).   run count: 0–256 streams or 0–64 groups (0: no entries).",
    "The bucket and histogram digits drive the write engine; k − 1 drives the GATHER engine (k = 1–8). Selectors 5–7 decode as none; numbers past a table's end wrap within it.",
    "DMA_PULSE acts on the run: [1] start; also [0] done; [3:2] reserved. In one store start acts before done.",
], 11.5, lh=16)
f.text(X0 + 32 * S + 66, y, "field order as listed in §8.5; bit positions are not assigned", 11, "end", fill=MUTED, italic=True)

# ---------------- lifecycle ----------------------------------------------------------------
y = 748
f.text(X0, y, "A run's life", 13.5, "start", "bold")
states = [("idle", "", "white"), ("running", "reads ahead, then issues", "#A8DCC8"), ("done", "rd_done", BOX)]
sx = [X0, X0 + 420, X0 + 900]
for (nm, sub, fill), x in zip(states, sx):
    f.rect(x, y + 14, 170, 46, fill, EDGE, 1.2, rx=23)
    f.text(x + 85, y + 34, nm, 12.5, "middle", "bold")
    if sub:
        f.text(x + 85, y + 50, sub, 10, "middle", fill=MUTED)
arrows = [("DMA_PULSE[1]", "start"), ("last entry", "walked")]
for i, (s1, s2) in enumerate(arrows):
    x0, x1 = sx[i] + 172, sx[i + 1] - 2
    f.line([(x0, y + 37), (x1, y + 37)], INK, 1.6, end=True)
    f.text((x0 + x1) / 2, y + 29, s1, 11, "middle")
    f.text((x0 + x1) / 2, y + 53, s2, 11, "middle")
f.text(sx[1] + 85, y + 78, "first request (S_0): 4·L_ts + 3 cycles after the start store (group run), 2·L_ts + 2 (stream run);", 10.5, "middle", fill=GREEN)
f.text(sx[1] + 85, y + 92, "4·L_ts + 2 and 2·L_ts + 1 after the cycle the start takes effect; the same on every tile", 10.5, "middle", fill=MUTED)
f.lines(sx[2] - 40, y + 78, ["GO does not start it; the core's start store does."], 10.5, fill=MUTED, lh=14)

# ---------------- stream run vs group run ------------------------------------------------------
y = 870
f.text(X0, y, "Stream runs: stream after stream", 13.5, "start", "bold")
f.text(X0 + 650, y, "Group runs: groups of streams, round-robin for R rotations", 13.5, "start", "bold")
# stream run strip
TX, TW = X0, 600
segs = [("stream s", 150, BOX), ("gap", 34, "white"), ("stream s+1", 110, BOX), ("gap", 34, "white"),
        ("stream s+2", 180, BOX), ("…", 92, "white")]
x = TX
for nm, w, fill in segs:
    f.rect(x, y + 16, w, 30, fill, EDGE if fill != "white" else GREY, 1, rx=0,
           dash=None if fill != "white" else "3 2")
    f.text(x + w / 2, y + 36, nm, 11, "middle", fill=INK if fill != "white" else MUTED)
    x += w
f.text(TX, y + 64, "each stream's whole run (count elements) is sent before the next stream; gap after, then the next", 11, "start", fill=MUTED)
f.lines(TX, y + 90, [
    "STREAM entries are read ahead, in stream order (figure 6.3); an element counter j walks each stream (figure 6.4).",
    "Uses: block copies (mode 5, d = 0); local passes (modes 2–4 to the tile itself);",
    "gather streams (mode 1, d = 0); one-word exchanges of big-SRAM data",
    "(63 streams of count 1, d in the 16-group offset order).",
    "Default: one-word exchanges carry gap after = 4 on every",
    "stream (§4.2, §13 item 7). Gapless: a later optimisation.",
    "A stream run reads big SRAM only; values in data memory go from the send port.",
], 11.5, lh=16)
f.tag(TX, y + 228, "open: whether gapless stream runs are safe (§13 item 7)")

# group run strip
CX = X0 + 650
P = 4
rot_w = 104
gx = CX
def group(gx, label, rots, streams, fill):
    for r in range(rots):
        for k in range(streams):
            xx = gx + r * rot_w + k * (rot_w / streams)
            f.rect(xx, y + 16, rot_w / streams, 30, fill, EDGE, 0.8, rx=0)
            f.text(xx + rot_w / streams / 2, y + 36, "abcd"[k], 10.5, "middle")
    f.text(gx + rots * rot_w / 2, y + 62, label, 11, "middle", fill=MUTED)
    return gx + rots * rot_w
gx = group(CX, "group g: R = 3 rotations of t = 4 streams", 3, 4, BOX)
f.rect(gx, y + 16, 40, 30, "white", GREY, 1, rx=0, dash="3 2"); f.text(gx + 20, y + 36, "gap", 10.5, "middle", fill=MUTED)
gx += 40
gx2 = group(gx, "group g + 1", 1, 4, GREEN_L)
f.text(gx2 + 10, y + 36, "…", 12, "start", fill=MUTED)
f.lines(CX, y + 90, [
    "A rotation visits the group's streams in order: 1 cycle a visit (2 for 128 bits), so",
    "the period is t or 2t; a stream with no elements left leaves its visit idle.",
    "Group g lasts max(R·P, 1) + gap: S(g+1) = S(g) + max(R(g)·P(g), 1) + gap(g).",
    "No per-stream state: every visit reads its STREAM entry (read ahead, figure 6.3), and one",
    "rotation counter r serves the group: element r if r < count (figure 6.4). Groups start",
    "one per cycle; a wait for read-ahead entries is the same on every tile, so it is part",
    "of the timetable. Only an SRAM read queue refusal makes a tile late:",
    "the drift condition (TILE_CONDITIONS bit 0).",
    "One read per element: each visit is one read-queue request.",
    "Use: the digit all-to-alls, 16 groups of 4 (T = 4; T = 8 for 128-bit), gap 0 (4 for TH, BT).",
], 11.5, lh=16)
f.tag(CX, y + 262, "open: link-queue peaks at group boundaries (§13 item 7)")

f.caption(["Every request goes to the SRAM read queue at priority 2, below gather values; a 64-entry token FIFO holds each request's token until its data returns, so packets",
           "trail the timetable by a constant while nothing stalls. A stream run walks streams one after another; a group run sends a fixed timetable of groups."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §4.2, §4.10, §8.5, §8.6, §9, §13.")
f.save("fig6_1_ag.svg")
