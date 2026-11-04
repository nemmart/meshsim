"""Figure 7: one bucket pass on the tile (mode 3 with count-on-arrival), and the prefix sum before it."""
from common import *

W, H = 940, 470
f = Fig(W, H, "Figure 7. One bucket pass on the tile",
        "The records stream out of one buffer, loop back into the tile, and land in another, bucketed by digit d through PTR,")
f.text(24, 74, "while HIST counts digit d + 1 for the next pass. Before the pass, the controller turns the counts into bucket starts.",
       12.5, "start", fill=MUTED)

# big SRAM, two buffers
f.rect(24, 110, 190, 300, BOX, EDGE, 1, rx=8)
f.text(119, 132, "big SRAM", 13.5, "middle", "bold")
f.box(44, 150, 150, 92, "buffer A", ["records (key, ordinal),", "sorted by digits < d"], fill="white", size=12)
f.box(44, 290, 150, 100, "buffer B", ["256 buckets, filled", "at PTR[0] … PTR[255]", " "], fill="white", size=12)
for i in range(6):
    f.rect(56 + i * 21.5, 360, 18, 18, "white", BLUE, 0.9, rx=2)
f.text(119, 352, "", 1)

# read side
f.box(270, 150, 170, 92, "address generator", ["a stream run to the tile", "itself, mode 3, 64 bits"], fill=GREEN_L, stroke=GREEN, size=12)
f.line([(194, 196), (268, 196)], GREEN, 1.8)
f.text(231, 188, "reads", 11.5, "middle", fill=GREEN)
f.box(490, 150, 150, 92, "loopback", ["injection → router →", "traffic director"], fill=BOX, size=12)
f.line([(440, 196), (488, 196)], INK, 1.6)
f.text(464, 188, "pkt", 11.5, "middle", fill=MUTED)

# write engine
f.box(490, 290, 150, 100, "write engine", ["t = digit d of the key", "write at PTR[t]", "PTR[t] += 2"], fill=BLUE_L, stroke=BLUE, size=12)
f.line([(565, 242), (565, 288)], INK, 1.6)
f.line([(488, 340), (196, 340)], BLUE, 1.8)
f.text(330, 332, "writes the record (64 bits)", 11.5, "middle", fill=BLUE)

# tables
f.box(700, 270, 210, 50, "PTR[0..255]", ["bucket starts (chunk addresses)"], fill=PURPLE_L, stroke=PURPLE, size=11.5, tsize=12.5)
f.box(700, 350, 210, 50, "HIST[0..255]", ["counts of digit d + 1 (cleared first)"], fill=PURPLE_L, stroke=PURPLE, size=11.5, tsize=12.5)
f.line([(640, 305), (698, 298)], PURPLE, 1.4, start=True)
f.line([(640, 360), (698, 372)], PURPLE, 1.4)
f.text(668, 384, "+1", 12, "middle", "bold", fill=PURPLE)
f.text(668, 293, "+2", 12, "middle", "bold", fill=PURPLE)

# controller, between passes
f.rect(700, 110, 210, 132, PURPLE_L, PURPLE, 1)
f.text(805, 132, "controller, before the pass", 13, "middle", "bold")
f.text(714, 156, "for each digit value t = 0 … 255:", 12, "start")
f.text(728, 176, "PTR[t] = B + 2 × (records", 12, "start")
f.text(746, 194, "with digit d below t)", 12, "start")
f.text(728, 214, "HIST[t] = 0", 12, "start")
f.text(714, 234, "(the exclusive prefix sum)", 11.5, "start", fill=MUTED)
f.line([(805, 242), (805, 268)], PURPLE, 1.4, dash="4 3")
f.text(812, 260, "sets", 11, "start", fill=PURPLE)
f.path("M912,372 C936,350 936,270 912,236", PURPLE, 1.4, dash="4 3")

f.lines(24, H - 42, [
    "Both table updates take one cycle per record, so a pass moves about one record per cycle. DMA_CONFIG names the two digits:",
    "the bucket digit (d) and the histogram digit (d + 1; 0 on the last pass). Simplified; see spec 4.9, 4.10 and sw/RECIPES.md 4."],
    12.5, "start")
f.save("fig07-bucket-pass.svg")
