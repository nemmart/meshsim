# Worked example 2 (REGISTERS.md 7.2): a block copy from big SRAM into controller memory (mode 5).
from examples_tile import *

H = 1250
f = Fig(W, H, "Example 2. A block copy into controller memory: n records from big SRAM to dst in instruction or data memory",
        "REGISTERS.md 7.2 — one stream, mode 5 to the tile itself (d = 0): read through the address generator, back through the loopback, written by port B")
t = TileMap(f,
    active={"router", "inject", "loopback", "eject", "dir", "inj", "wake", "cmem", "portB", "ag", "STREAM", "CFG_AG", "PULSE",
            "mux", "rq_ag", "rq", "ret"},
    stores={"STREAM": [1], "CFG_AG": [2], "dir": [3], "PULSE": [4]},
    steps={"STREAM": "a", "rq_ag": "b", "rq": "c", "ret": "d", "loopback": "e", "portB": "f", "wake": "g"},
    buffers=[("source: n records", "src … src + 2n − 1", MX - 190, 380, "read")],
    bodies={"cmem": ["destination: 2n words from dst"]})
t.draw(program("ex2"))                  # the program block in REGISTERS.md 7.2
LY = SR[1] + SR[3] + 34
t.legend(LY)
f.text(40, LY + 36, "What happens", 13, "start", "bold")
t.step_list(LY + 62, [
    ("a", PURPLE, "The start pulse begins the run: the address generator reads STREAM[0] across the table latency."),
    ("b", GREEN, "It makes one read request per 64-bit record, n in all, into the SRAM read queue at priority 2."),
    ("c", GREEN, "The big SRAM reads the source records, in order."),
    ("d", GREEN, "Each return becomes a mode-5 packet with dst = me and addr = the cm word, which advances by 2 per packet."),
    ("e", GREEN, "The router loops it back: dst = me goes straight from injection to eject, never onto the mesh."),
    ("f", PURPLE, "The traffic director routes mode 5 to port B and decrements DMA_CM_REMAINING; port B writes the packet's two words at its packed address."),
    ("g", ORANGE, "The last packet written: DMA_CM_REMAINING = 0 and cm_idle, so the local wake fires and WFI returns. The words are in memory."),
])
f.caption(["A copy into controller memory is example 1's run with mode 5: the same address generator, read queue and loopback, but the director hands the packets to",
           "port B instead of the write engine, and each packet carries its own destination address, from the STREAM entry's cm word. Nothing on the write side chooses",
           "addresses (REGISTERS.md 2.5). The program's code must lie outside the range it overwrites."],
          y=H - 54, source="Source: REGISTERS.md 2.3, 2.5, 3.4, 4.3; RECIPES.md 3 (cmem_copy()); spec/machine-spec.md v1.0, §4.2, §4.11, §4.12, §6.5; the tile map is figure 1.3's.")
save(f, "ex2_cm_copy.svg")
