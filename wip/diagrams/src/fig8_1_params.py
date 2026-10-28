# Figure 8.1: the parameters page — every width, depth and latency, with its default and where it is used. Item 17.
from rtl import *

W = 1300
f = Fig(W, 2000, "Figure 8.1. Parameters: every depth, width and latency, with its default",
        "v1.0 (spec/machine-spec.md §10) — [meas] measured in meshsim, [model] from the model, open: not fixed for the hardware")
r = R(f)
AY = 76
rows = [["parameter", "default", "status", "used in"],
        ["link_latency", "3 per link direction", "decided", "figure 4.3 (three link stages; a hop is 4 cycles)"],
        ["start skew between tiles", "0 or +1 cycle", "decided", "§3.2; figure 2.1 (c)"],
        ["queue_depth (each router FIFO)", "16 × 102 bits", "decided", "figure 4.3"],
        ["intra_tile_latency", "1 per stage", "decided", "every register stage drawn"],
        ["STREAM, GROUP", "256 and 64 entries × 64 bits", "decided", "figures 6.1, 6.3"],
        ["ts_engine_latency (L_ts)", "1 (1–4 measured)", "[meas]", "figures 5.2, 6.2, 6.3"],
        ["address-generator lead-in", "4·L_ts + 3 (group), 2·L_ts + 2 (stream)", "decided", "after the start store; one less from its effect (6.1–6.3)"],
        ["ag_lookahead", "2·L_ts + 2 entries per read-ahead queue (4)", "decided", "figure 6.3"],
        ["pointer stage depth", "2·L_ts + 2 (4)", "decided", "figure 5.2"],
        ["mem_chunks", "1,048,576: 8 areas × 128 bits × 32K rows", "decided", "figures 7.1, 7.2 (smaller devices defined)"],
        ["area_queue_depth", "8 in meshsim", "open", "figure 7.2"],
        ["area tie order", "writes first", "decided", "figure 7.2"],
        ["big-SRAM read latency", "queueing + the array's latency (4 unloaded)", "open; [meas] unloaded", "figures 7.2, 7.3"],
        ["token FIFO", "64 entries, 128-bit data slots", "decided", "figure 7.3"],
        ["GATHER request queue", "16 × 20 bits", "decided", "figure 7.5"],
        ["k (records per gather value)", "1–8", "decided", "figures 3.2, 7.5"],
        ["write engine input FIFO", "4 entries", "decided", "figures 5.1, 7.2"],
        ["read port's queue", "64 entries (at least 64)", "decided", "figure 3.3"],
        ["cm_latency (director to port B)", "2", "[built]", "figures 3.1, 5.1"],
        ["ready_latency", "1 in simulation; about 8 physically", "open for the hardware", "figure 2.2"],
        ["run_latency", "as GO", "decided", "figure 2.2"],
        ["load-use penalty (data memory, registers, table window)", "0", "the design target", "figure 3.1"],
        ["taken-branch penalty, multiply latency", "1, 3", "decided", "§4.11"],
        ["controller memories", "8 KB instruction + 8 KB data, two 1K × 32 banks each", "decided", "figure 3.1"],
        ["mailbox", "0x5C00–0x5FFF, 256 slots", "decided", "figures 3.1, 5.1"],
        ["peripheral registers", "13 at 0xFFFFF800 + 4·i", "v1.0", "figure 3.2"],
        ["conditions", "11, TILE_CONDITIONS [10:0]", "v1.0", "figure 2.4"]]
y = r.table(30, AY, [30, 380, 720, 900, 1270], rows, rh=21, size=10.5)
finish(f, r, y + 10, ["Every depth, width and latency the figures use, with its default. Open: the area queue depth, the array's read latency and the",
                      "physical ready_latency are still to be fixed for the hardware; the token FIFO's 64 entries must absorb the area queueing a phase's",
                      "placement allows (§4.2)."],
       "Source: spec/machine-spec.md v1.0.",
       legend=[("dot", INK, "status: decided, open, [meas] measured, [built] as built in meshsim")])
f.save("fig8_1_params.svg")
