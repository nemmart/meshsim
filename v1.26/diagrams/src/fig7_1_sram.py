# Figure 7.1: the big SRAM — address split, areas and queues, read-queue priority, the disjoint-buffer rule.
from common import *

W, H = 1300, 1134
f = Fig(W, H, "Figure 7.1. The big SRAM: 8 areas, one access per area per cycle, no coherence",
        "v1.0 (spec/machine-spec.md §4.1, §4.5) — 8 areas, each one 128-bit row access a cycle")

# ---------------- A. the chunk address ----------------------------------------------
AX, AY, CW = 44, 120, 46
f.text(AX, AY - 30, "A. A 20-bit chunk address (1 chunk = 32 bits; 1,048,576 chunks = 4 MB)", 13.5, "start", "bold")
groups = [("area", 19, 17, SAND), ("row within the area", 16, 2, BOX), ("chunk", 1, 0, GREEN_L)]
for name, hi, lo, fill in groups:
    for b in range(hi, lo - 1, -1):
        x = AX + (19 - b) * CW
        f.rect(x, AY, CW, 34, fill, EDGE, 1, rx=0)
        f.text(x + CW / 2, AY - 6, str(b), 11, "middle", fill=MUTED)
    x0 = AX + (19 - hi) * CW
    x1 = AX + (20 - lo) * CW
    f.rect(x0, AY, x1 - x0, 34, "none", INK, 1.6, rx=0)
    f.text((x0 + x1) / 2, AY + 22, name, 12.5, "middle", "bold")
    f.path(f"M {x0 + 2} {AY + 42} L {x0 + 2} {AY + 48} L {x1 - 2} {AY + 48} L {x1 - 2} {AY + 42}", GREY, 1, end=False)
f.lines(AX, AY + 70, [("area, bits 19–17: which of the 8 areas; area a holds the 128-bit rows a·32K to (a + 1)·32K − 1", {}),
                      ("row, bits 16–2: one of the 32,768 rows of 128 bits in that area", {}),
                      ("chunk, bits 1–0: one of the 4 chunks in the row", {}),
                      ("example: chunk address 0x20006 → area 1, row 1, chunk 2", {"fill": MUTED})], 11.5, lh=16)
# notes right of the bar
nx = AX + 20 * CW + 24
f.lines(nx, AY + 4, ["The areas are contiguous blocks,", "not interleaved: a sequential", "stream stays in one area at", "4 chunks per access.",
                     "", "Addresses are 20 bits and wrap:", "the top of area 7 wraps to the", "bottom of area 0."], 11.5, lh=15)

# ---------------- B. rows and the eight areas ----------------------------------------
BY = 300
f.text(AX, BY, "B. The eight areas and what one row holds", 13.5, "start", "bold")
MX, MY, MW, MHh = AX, BY + 20, 200, 34
for a in range(8):
    y = MY + a * MHh
    f.rect(MX, y, MW, MHh, BOX if a % 2 == 0 else "#E9E7E0", EDGE, 1, rx=0)
    f.text(MX + 10, y + 22, f"area {a}", 12.5, "start", "bold")
    f.text(MX + MW - 8, y + 22, f"0x{a * 0x20000:05X}–0x{(a + 1) * 0x20000 - 1:05X}", 11, "end", mono=True)
f.text(MX, MY + 8 * MHh + 20, "each 128 bits × 32,768 rows = 512 KB;", 11.5, "start")
f.text(MX, MY + 8 * MHh + 36, "chunk ranges shown (4 MB device)", 11.5, "start", fill=MUTED)

# row detail
RX, RY, CHW = 300, BY + 40, 84
f.text(RX, RY - 6, "one row = 128 bits = 4 chunks", 12, "start", "bold")
for c in range(4):
    f.rect(RX + c * CHW, RY + 4, CHW, 34, GREEN_L, EDGE, 1, rx=0)
    f.text(RX + c * CHW + CHW / 2, RY + 26, f"chunk {c}", 11.5, "middle")
def brace(x0, x1, y, label, col=INK):
    f.path(f"M {x0 + 2} {y} L {x0 + 2} {y + 7} L {x1 - 2} {y + 7} L {x1 - 2} {y}", col, 1.2, end=False)
    f.text((x0 + x1) / 2, y + 22, label, 11.5, "middle", fill=col)
brace(RX, RX + 2 * CHW, RY + 44, "a record (64 bits)")
brace(RX + 2 * CHW, RX + 4 * CHW, RY + 44, "a record (64 bits)")
brace(RX, RX + 4 * CHW, RY + 74, "a 128-bit value, or 4 keys")
f.lines(RX, RY + 132, [
    "• An area does one 128-bit read or write per cycle.",
    "• A 64-bit write fills half a row:",
    "   per-chunk write enables make it one access, no read-modify-write.",
    "• Write combining (two 64-bit writes to one row in one access).",
    "• No alignment: an access may start at any chunk. One that",
    "   crosses a row boundary costs two accesses (figure 7.2).",
], 11.5, lh=16)
f.tag(RX + 384, RY + 132 + 48, "open", "end")
f.lines(RX, RY + 250, ["A smaller device (defined, not expected): 1 to 7 whole", "areas; any chunk at or above the end reads 0 and drops", "writes, with no condition."], 11, lh=14, fill=MUTED)

# ---------------- C. clients, queues, priority ----------------------------------------
CX0 = 740
f.text(CX0, BY, "C. Who reaches an area, and in what order", 13.5, "start", "bold")
# read clients and priority mux
cy = BY + 26
clients = ["1  GATHER engine: gather values", "2  address generator: its visits", "3  controller read port (§4.3)"]
for i, s_ in enumerate(clients):
    f.rect(CX0, cy + i * 34, 236, 28, GREEN_L, GREEN, 1, rx=4)
    f.text(CX0 + 10, cy + i * 34 + 19, s_, 11.5, "start")
MXR = CX0 + 252
for i in range(3):
    f.line([(CX0 + 236, cy + 14 + i * 34), (MXR, cy + 14 + i * 34)], GREEN, 1.4, end=False)
f.line([(MXR, cy + 14), (MXR, cy + 82)], GREEN, 1.4, end=False)
f.line([(MXR, cy + 48), (MXR + 12, cy + 48)], GREEN, 1.4, end=True)
RSX = MXR + 14
f.rect(RSX, cy + 28, 120, 40, GREEN_L, GREEN, 1.4, rx=4)
f.text(RSX + 60, cy + 45, "SRAM read queue", 12, "middle", "bold")
f.text(RSX + 60, cy + 60, "priority 1 > 2 > 3", 10.5, "middle")
# SRAM write queue
f.rect(RSX, cy + 106, 120, 40, BLUE_L, BLUE, 1.4, rx=4)
f.text(RSX + 60, cy + 123, "SRAM write queue", 12, "middle", "bold")
f.text(RSX + 60, cy + 138, "write engine only", 10.5, "middle")
f.text(CX0, cy + 130, "modes 2–4 (incl. gather values)", 11.5, "start", fill=BLUE)
f.line([(CX0 + 206, cy + 126), (RSX - 2, cy + 126)], BLUE, 1.8, end=True, dash=DASH["write"])
# queue and area
QX, QY = RSX + 150, cy + 70
f.line([(RSX + 120, cy + 48), (QX - 2, QY + 14)], GREEN, 2, end=True)
f.line([(RSX + 120, cy + 126), (QX - 2, QY + 30)], BLUE, 2, end=True, dash=DASH["write"])
for k in range(4):
    f.rect(QX + k * 15, QY + 8, 13, 30, "white", OPEN_C, 1, rx=1, dash=DASH["open"])
f.text(QX + 29, QY - 2, "queue", 11, "middle", fill=OPEN_C)
f.line([(QX + 60, QY + 23), (QX + 72, QY + 23)], INK, 1.4, end=True)
f.rect(QX + 74, QY - 6, 66, 58, BOX, INK, 1.4, rx=4)
f.text(QX + 107, QY + 16, "area a", 12, "middle", "bold")
f.text(QX + 107, QY + 32, "1 access", 10.5, "middle")
f.text(QX + 107, QY + 44, "per cycle", 10.5, "middle")
f.line([(QX + 107, QY + 52), (QX + 107, QY + 92)], GREEN, 2, end=True)
f.text(W - 24, QY + 108, "every read return → injection point", 11, "end", fill=GREEN)
f.lines(CX0, cy + 196, [
    "• The read clients merge into one SRAM read queue at a fixed priority; the",
    "   address generator advances only when the queue takes its request.",
    "• Every request queues at the area it addresses. The area serves its",
    "   queue in arrival order; on a same-cycle tie, writes go first, since",
    "   writes can't wait and reads can.",
    "• The two queues meet only at the areas; the controller reads only through the read port.",
    "• A read takes 4 cycles unloaded [meas: meshsim], longer when it",
    "   waits in its area's queue. Returns leave in request order (§4.5).",
    "• The traffic director never stalls: overloading one area overflows the",
    "   write engine's 4-entry input FIFO: condition 2.",
], 11.5, lh=16)
f.lines(CX0, cy + 370, [
    ("Open (§4.1, §13 items 1–4):", {"weight": "bold", "fill": OPEN_C}),
    ("the queue depth for the hardware (a parameter: 8 in meshsim);", {"fill": OPEN_C}),
    ("the array's read latency; queueing against the address", {"fill": OPEN_C}),
    ("generator's timetable. What a full area does: figure 7.2.", {"fill": OPEN_C}),
], 11.5, lh=16)

# ---------------- D. the disjoint-buffer rule -----------------------------------------
DY = 790
f.text(AX, DY, "D. The disjoint-buffer rule: within one engine run, no buffer is both read and written", 13.5, "start", "bold")
def buf(x, y, name, area, mode):
    c, cl = (GREEN, GREEN_L) if mode == "r" else (BLUE, BLUE_L)
    f.rect(x, y, 150, 50, cl, c, 1.4, rx=4, dash=None if mode == "r" else DASH["write"])
    f.text(x + 75, y + 21, f"buffer {name}", 12.5, "middle", "bold")
    f.text(x + 75, y + 38, f"area {area} — {'read' if mode == 'r' else 'written'}", 11, "middle", fill=c)
py = DY + 32
for i, (rd, wr, ttl) in enumerate([("A", "B", "run n: a local-sort pass"), ("B", "A", "run n + 1: the next pass, maybe in the same phase")]):
    x = AX + i * 470
    f.rect(x, py, 440, 120, "white", GREY, 1, rx=8)
    f.text(x + 14, py + 22, ttl, 12, "start", "bold")
    buf(x + 20, py + 46, rd, 2 if rd == "A" else 3, "r")
    buf(x + 270, py + 46, wr, 3 if wr == "B" else 2, "w")
    f.line([(x + 172, py + 71), (x + 268, py + 71)], INK, 1.8, end=True)
    f.text(x + 220, py + 64, "reorder", 10.5, "middle", fill=MUTED)
# boundary
bx = AX + 448
f.line([(bx, py - 4), (bx, py + 126)], ORANGE, 2.4, end=False, dash=DASH["ctrl"])
f.lines(bx, py + 142, ["between runs: the next starts only when DMA_SRAM_REMAINING = 0", "and mem_idle (the writes are in the array); at a phase",
                       "boundary the tile reports done only then, and reads after GO"], 11, anchor="middle", fill=ORANGE, lh=14)
# not allowed
nx0 = AX + 950
f.rect(nx0, py, 262, 120, "white", "#C0392B", 1.2, rx=8, dash="5 3")
f.text(nx0 + 14, py + 22, "not allowed", 12, "start", "bold", fill="#C0392B")
f.rect(nx0 + 56, py + 46, 150, 50, BOX, "#C0392B", 1.2, rx=4)
f.text(nx0 + 131, py + 67, "buffer C", 12.5, "middle", "bold")
f.text(nx0 + 131, py + 84, "read and written", 11, "middle", fill="#C0392B")
f.lines(nx0, py + 142, ["no ordering between a run's reads", "and writes: the result is undefined"], 11, fill="#C0392B", lh=14)

f.lines(AX, py + 196, [
    "Already follows the rule: the local sort's passes (ping-pong between two buffers); the gathers (index and values in, output out);",
    "the all-to-alls (send buffer out, receive buffer in); the split's searches through the read port (they read sorted keys that nothing writes during the split).",
    "Throughput comes from placement: buffers that one run uses together sit in different areas, so up to 8 accesses happen per cycle in total.",
], 11.5, lh=16)

f.caption(["Each of the 8 contiguous areas serves one 128-bit access per cycle from its own queue, in arrival order. There is no coherence: correctness",
           "comes from never reading and writing one buffer in one engine run; ordering between runs comes from waiting for the writes (DMA_SRAM_REMAINING and mem_idle)."],
          y=H - 38, source="Source: spec/machine-spec.md v1.0, §1, §4.1, §4.5, §4.13, §4.14, §13.")
f.save("fig7_1_sram.svg")
