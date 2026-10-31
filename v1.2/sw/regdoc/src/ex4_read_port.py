# Worked example 4 (REGISTERS.md 7.4): reading big SRAM through the read port.
from examples_tile import *

H = 1250
f = Fig(W, H, "Example 4. Reading big SRAM through the read port: na scattered chunks into data memory",
        "REGISTERS.md 7.4 — stores queue the reads; each result returns as a mode-7 packet to the tile itself, through the loopback, written by port B")
t = TileMap(f,
    active={"rp", "RDQ", "rq_rp", "mux", "rq", "ret", "inj", "inject", "router", "loopback", "eject", "dir", "portB", "cmem", "wake"},
    stores={"dir": [1], "RDQ": [2, 3]},
    steps={"rq_rp": "a", "rq": "b", "ret": "c", "loopback": "d", "portB": "e", "wake": "f"},
    buffers=[("na chunks, anywhere", "chunk[0] … chunk[na − 1]", MX - 190, 380, "read")],
    bodies={"cmem": ["res[0 … na − 1]: one result per read"]})
t.draw(program("ex4"))                  # the program block in REGISTERS.md 7.4
LY = SR[1] + SR[3] + 34
t.legend(LY)
f.text(40, LY + 36, "What happens", 13, "start", "bold")
t.step_list(LY + 62, [
    ("a", GREEN, "Each store to SRAM_RD_ADDR_QUEUE queues one read, with the current SRAM_RD_DEST_ADDR as its result's word, which then advances by 1. It never stalls; the read port feeds the SRAM read queue at priority 3, the lowest."),
    ("b", GREEN, "The big SRAM reads each 32-bit chunk, wherever it is."),
    ("c", GREEN, "Each return becomes a mode-7 packet to the tile itself: addr = the entry's result word, payload = the chunk. Returns go first at the injection point."),
    ("d", GREEN, "The router loops it back: dst = me goes straight from injection to eject."),
    ("e", PURPLE, "The traffic director routes mode 7 to port B and decrements DMA_REPLY_REMAINING; port B writes the result at res[a], in the order the reads were issued."),
    ("f", ORANGE, "The last result in: DMA_REPLY_REMAINING = 0 and cm_idle, so the local wake fires and WFI returns."),
])
f.caption(["The read port lets the program read big SRAM without waiting on each read: the stores only queue them, and the results come back the way every controller-memory",
           "write does, as packets that carry their own address (REGISTERS.md 2.5), here the result word chosen by SRAM_RD_DEST_ADDR when the read was queued. They share the",
           "loopback and eject with arrivals, so read steps never overlap incoming traffic."],
          y=H - 54, source="Source: REGISTERS.md 2.3.3, 2.4, 2.5, 4.3; RECIPES.md 11 (sram_read32(), search()); spec/machine-spec.md v1.0, §4.3, §4.12; the tile map is figure 1.3's.")
save(f, "ex4_read_port.svg")
