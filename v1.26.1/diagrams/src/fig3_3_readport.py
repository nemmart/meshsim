# Figure 3.3: the controller read port — SRAM_RD_ADDR_QUEUE and SRAM_RD_DEST_ADDR, the read port's queue, the in-flight
# count, the free count, the overflow condition, and the request to the SRAM read queue. Item 9.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 3.3. The controller read port: the read port's queue, the in-flight count and the free count",
        "v1.0 (spec/machine-spec.md §4.3, §4.13) — the controller reads big SRAM without stalling")
r = R(f)

AY = 76
r.frame(16, AY, W - 32, 420, "A. Datapath (§4.3, §4.3)")
# stores
r.lbl(30, AY + 64, "st_rdq: a store to SRAM_RD_ADDR_QUEUE, wdata[19:0] = the chunk", "start", c=ORANGE, weight="bold")
r.lbl(30, AY + 82, "st_rdd: a store to SRAM_RD_DEST_ADDR, wdata[11:0]", "start", c=ORANGE, weight="bold")
DR = (60, AY + 120, 190, 58)
r.reg(*DR, "SRAM_RD_DEST_ADDR", ["12 bits: a packed data-memory word"])
f.text(DR[0] + 95, DR[1] + DR[3] + 16, "+1 on every st_rdq, a dropped read included;", 10, "middle", fill=MUTED)
f.text(DR[0] + 95, DR[1] + DR[3] + 30, "wraps in 12 bits; a store to it loads it", 10, "middle", fill=MUTED)
# queue
Q = (330, AY + 100, 200, 120)
f.box(*Q, "the read port's queue", ["64 × 32 bits:", "{chunk 20, dest 12}", "FIFO; pop on taken_3"], fill=BOX, stroke=INK, sw=1.5, size=10.5, tsize=11.5)
f.line([(DR[0] + DR[2], DR[1] + 29), (Q[0] - 1, DR[1] + 29)], INK, 1.6, end=True)
f.text(DR[0] + DR[2] + 6, DR[1] + 22, "dest", 10, "start", fill=MUTED)
f.line([(300, AY + 100), (300, Q[1] + 30), (Q[0] - 1, Q[1] + 30)], INK, 1.6, end=True)
f.text(294, AY + 112, "chunk", 10, "end", fill=MUTED)
# overflow
r.gate_and(330, AY + 270, 2, "overflow")
f.text(250, AY + 266, "st_rdq", 10, "end", fill=ORANGE); f.text(250, AY + 280, "free = 0", 10, "end")
f.line([(254, AY + 263), (329, AY + 263)], ORANGE, 1.3, end=False, dash=CTL)
f.line([(254, AY + 277), (329, AY + 277)], INK, 1.3, end=False)
f.line([(366, AY + 270), (420, AY + 270)], RED, 1.6, end=True)
r.lbl(426, AY + 270, "set[3]: read-port overflow; the read is dropped", "start", c=RED, weight="bold")
f.text(30, AY + 310, "push = st_rdq & free > 0: the entry is {wdata[19:0], SRAM_RD_DEST_ADDR}. SRAM_RD_DEST_ADDR += 1 on every st_rdq, ungated: a dropped read advances it too (§4.3).", 10.5, "start", fill=MUTED)
# request to the SRAM read queue
RQ = (600, AY + 100, 300, 120)
f.line([(Q[0] + Q[2], Q[1] + 60), (RQ[0] - 1, Q[1] + 60)], PURPLE, 2, end=True)
f.text(Q[0] + Q[2] + 6, Q[1] + 52, "head", 10, "start", fill=PURPLE)
f.box(*RQ, "client 3 request (figure 7.2)", ["valid_3 = queue not empty", "chunk = head.chunk; width 32", "mode 7, dst = me, dest addr = head.dest",
                                            "token rp = 1; lowest priority"], fill=PURPLE_L, stroke=PURPLE, sw=1.4, size=10, tsize=11.5)
f.line([(RQ[0] + 150, RQ[1] + RQ[3]), (RQ[0] + 150, AY + 260)], ORANGE, 1.4, end=False, dash=CTL)
f.text(RQ[0] + 156, AY + 252, "taken_3: pop; in-flight + 1", 10, "start", fill=ORANGE)
# in-flight and free
IF = (960, AY + 100, 290, 120)
r.reg(*IF, "in-flight counter, 7 bits", ["+1 on taken_3, −1 on rp_inj (figure 7.3)", "both in one cycle: unchanged", "", "free = 64 − (queue count + in flight)"],
      fill=GREEN_L, stroke=GREEN)
f.line([(IF[0] + 145, IF[1] + IF[3]), (IF[0] + 145, AY + 300)], GREEN, 1.6, end=True)
r.lbl(IF[0] + 145, AY + 314, "a load of SRAM_RD_ADDR_QUEUE returns free (0–64)", "middle", c=GREEN)
f.lines(600, AY + 350, ["An entry stays counted until its result is injected (§4.3): 64 free means every return has left;",
                        "only DMA_REPLY_REMAINING = 0 says every result has landed in data memory (§4.3).",
                        "In HALT the queue and the in-flight count are held at 0 (§4.3)."], 10.5, lh=15, fill=MUTED)

# B. interface
BY = AY + 440
f.text(30, BY, "B. Interface", 14, "start", "bold")
rows = [["signal", "width", "from → to", "meaning"],
        ["st_rdq, st_rdd, wdata", "1, 1, 32", "register decode (fig 3.2) → here", "stores to SRAM_RD_ADDR_QUEUE and SRAM_RD_DEST_ADDR"],
        ["free", "7", "here → register reads", "a load of SRAM_RD_ADDR_QUEUE: free entries"],
        ["valid_3, request", "1, —", "here → SRAM read queue (fig 7.2)", "the head: chunk, width 32, mode 7, dst me, dest addr"],
        ["taken_3", "1", "SRAM read queue → here", "pop the head; in flight + 1"],
        ["rp_inj", "1", "token FIFO (fig 7.3) → here", "a read-port result was injected: in flight − 1"],
        ["set[3]", "1", "here → TILE_CONDITIONS (fig 2.4)", "a store with no free entry: the read is dropped"]]
y = r.table(30, BY + 12, [30, 230, 330, 620, 1270], rows)
y = r.notes(y + 26, ["The result is a mode-7 packet to the tile itself: addr[11:0] = dest, addr[19:12] = 0, payload[31:0] = the chunk, [63:32] = 0 (§4.3). "
                     "It loops back through the router and lands through port B, counted by DMA_REPLY_REMAINING (figure 5.1)."])
finish(f, r, y, ["A store to SRAM_RD_ADDR_QUEUE queues a 32-bit read with the current SRAM_RD_DEST_ADDR, which then advances. The queue drains into the SRAM",
                 "read queue at the lowest priority. An entry counts against the 64 until its result has been injected, so the free count a program reads",
                 "never overstates the room; a store with nothing free is dropped and sets condition 3."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("line", PURPLE, "the controller's reads"), ("line", GREEN, "counts"), ("ctrl", ORANGE, "control"), ("line", RED, "condition")])
f.save("fig3_3_readport.svg")
