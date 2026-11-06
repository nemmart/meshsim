#!/usr/bin/env python3
"""B1's T2 summary (WORKER22): per workload and run, TracerT2's per-segment CSV: C&C 3's cycles for the segment (the trace
and the drain through tracer_ctl.c and the DMA), the tiles' replay, the wall time per segment. Usage: t2_summary.py RESDIR PROGRAM..."""
import csv, glob, os, sys
res, progs = sys.argv[1], sys.argv[2:]
out = []
for p in progs:
    for f in sorted(glob.glob(f"{res}/{p}-*.csv")):
        for r in csv.DictReader(open(f)):
            c = int(r['cc3_cycles'])
            out.append(f"{p} segment {r['segment']} ({os.path.basename(f)[len(p) + 1:-4]}, {r['windows']} windows): {r['ok']}; C&C 3 {c:,} cycles = "
                       f"{c / 1e6:.2f} ms at 1 GHz (Tracer rows {int(r['trace_rows_cycles']):,}, drain {int(r['drain_cycles']):,}); "
                       f"{r['transfers']} transfers, {int(r['dma_packets']):,} packets; tiles' replay max {int(r['replay_cycles_max']):,} cycles; "
                       f"{int(r['rows_checked']):,} rows checked; wall {float(r['wall_s']):.0f} s")
print("\n".join(out)); open(f"{res}/T2.txt", "w").write("\n".join(out) + "\n")
