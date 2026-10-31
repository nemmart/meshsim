# Worked example 5 (REGISTERS.md 7.5): a bucket pass with count-on-arrival (mode 3).
from examples_tile import *

H = 1310
f = Fig(W, H, "Example 5. A bucket pass with count-on-arrival: n records bucketed by digit d, counting digit next",
        "REGISTERS.md 7.5 — the prefix sum sets PTR from HIST; one stream, mode 3 to the tile itself; the write engine lands each record at PTR[digit d] and counts HIST[digit next]")
t = TileMap(f,
    active={"STREAM", "CFG_AG", "PULSE", "ag", "rq_ag", "mux", "rq", "ret", "inj", "inject", "router", "loopback", "eject", "dir",
            "m24", "we", "PTR", "HIST", "CFG_WE", "wq", "wake"},
    stores={"PTR": [1], "HIST": [2], "STREAM": [3], "CFG_AG": [4], "CFG_WE": [4], "dir": [5], "PULSE": [6]},
    steps={"STREAM": "a", "rq_ag": "b", "rq": "c", "ret": "d", "loopback": "e", "m24": "f", "PTR": "g", "HIST": "h", "wq": "i", "wake": "j"},
    buffers=[("source: n records", "src … src + 2n − 1", MX - 190, 380, "read"),
             ("destination: 256 buckets, in digit order", "PTR[0] … PTR[255] at the start", WE[0] + 20, 360, "write")])
t.draw(program("ex5"))                  # the program block in REGISTERS.md 7.5
LY = SR[1] + SR[3] + 34
t.legend(LY)
f.text(40, LY + 36, "What happens", 13, "start", "bold")
t.step_list(LY + 62, [
    ("a", PURPLE, "Before the run, the prefix sum (1, 2) has set PTR[t] to bucket t's start and cleared HIST. The start pulse begins the run: the address generator reads STREAM[0]."),
    ("b", GREEN, "It makes one read request per record into the SRAM read queue at priority 2."),
    ("c", GREEN, "The big SRAM reads the source records, in order."),
    ("d", GREEN, "Each return becomes a mode-3 packet with dst = me (d = 0)."),
    ("e", GREEN, "The router loops it back: injection straight to eject."),
    ("f", BLUE, "The traffic director routes mode 3 to the write engine and decrements DMA_SRAM_REMAINING."),
    ("g", PURPLE, "The write engine takes the key's digit d (DMA_CONFIG's bucket digit), lands the record at PTR[digit d], then adds 2: equal digits land in arrival order."),
    ("h", PURPLE, "Count-on-arrival: it adds 1 to HIST[digit next] (DMA_CONFIG's hist digit), ready for the next pass's prefix sum."),
    ("i", BLUE, "The write goes through the SRAM write queue into its bucket in the destination."),
    ("j", ORANGE, "The last record written: DMA_SRAM_REMAINING = 0 and mem_idle, so the local wake fires and WFI returns."),
])
f.caption(["A bucket pass is example 1's run with mode 3: the write engine picks each record's landing pointer by a digit of its key instead of by its source, and counts",
           "the next digit as it goes. The controller's part is the prefix sum through the table window, before the run, while no engine uses the tables. Four passes on",
           "digits 1–4 sort by the 32-bit key; PTR[64], C&C's staging pointer, is restored afterwards."],
          y=H - 54, source="Source: REGISTERS.md 2.3.1, 3.2, 3.3, 4.2; RECIPES.md 4 (bucket_pass()); spec/machine-spec.md v1.0, §4.9, §4.10, §4.15; the tile map is figure 1.3's.")
save(f, "ex5_bucket_pass.svg")
