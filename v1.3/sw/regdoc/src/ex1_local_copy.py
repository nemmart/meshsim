# Worked example 1 (REGISTERS.md 7.1): a local copy of n records inside the tile.
from examples_tile import *

H = 1280
f = Fig(W, H, "Example 1. A local copy: n records from src to dst in the tile's own big SRAM",
        "REGISTERS.md 7.1 — one stream, mode 2 to the tile itself (d = 0): read through the address generator, back through the loopback, landing at PTR[me]")
t = TileMap(f,
    active={"router", "eject", "inject", "loopback", "dir", "inj", "wake", "we", "ag", "PTR", "STREAM", "CFG_AG", "PULSE",
            "m24", "mux", "rq_ag", "rq", "wq", "ret"},
    stores={"PTR": [1], "STREAM": [2], "CFG_AG": [3], "dir": [4], "PULSE": [5]},
    steps={"STREAM": "a", "rq_ag": "b", "rq": "c", "ret": "d", "loopback": "e", "m24": "f", "PTR": "g", "wq": "h", "wake": "i"},
    buffers=[("source: n records", "src … src + 2n − 1", MX - 190, 380, "read"),
             ("destination", "dst … dst + 2n − 1", WE[0] + 30, 340, "write")])
t.draw(program("ex1"))                  # the program block in REGISTERS.md 7.1
LY = SR[1] + SR[3] + 34
t.legend(LY)
f.text(40, LY + 36, "What happens", 13, "start", "bold")
t.step_list(LY + 62, [
    ("a", PURPLE, "The start pulse begins the run: the address generator reads STREAM[0] across the table latency."),
    ("b", GREEN, "It makes one read request per record, n in all, into the SRAM read queue at priority 2."),
    ("c", GREEN, "The big SRAM reads the source records, in order."),
    ("d", GREEN, "Each return becomes a mode-2 packet with dst = me (d = 0) and goes to the injection point."),
    ("e", GREEN, "The router loops it back: dst = me goes straight from injection to eject, never onto the mesh."),
    ("f", BLUE, "The traffic director routes mode 2 to the write engine and decrements DMA_SRAM_REMAINING."),
    ("g", PURPLE, "The write engine lands the record at PTR[src] = PTR[me], then adds 2: the destination fills in order."),
    ("h", BLUE, "The write goes through the SRAM write queue into the destination buffer."),
    ("i", ORANGE, "The last record written: DMA_SRAM_REMAINING = 0 and mem_idle, so the local wake fires and WFI returns."),
])
f.caption(["A local copy is one stream run: the address generator reads, every read return becomes a packet to the tile itself, and the write engine lands it through",
           "PTR[me]. The program only describes the run (1–3), says how many packets to expect (4), starts it (5) and waits (6). Source and destination must not overlap:",
           "one engine run never reads and writes the same buffer."],
          y=H - 54, source="Source: REGISTERS.md 2.3, 3.3, 3.4; RECIPES.md 0 and 2 (local_run(), local_copy()); spec/machine-spec.md v1.0, §4.1, §4.2, §4.9, §4.12; the tile map is figure 1.3's.")
save(f, "ex1_local_copy.svg")
