#!/usr/bin/env python3
"""B1 T1's summary (WORKER22): per workload, from TracerT1's per-segment CSVs: the shared-cache run (timing), the
separate-map-cache control, and the verdicts of every run. Usage: t1_summary.py RESDIR PROGRAM..."""
import csv, sys, statistics as st

def load(p):
    with open(p) as f: return list(csv.DictReader(f))

res, progs = sys.argv[1], sys.argv[2:]
out = []
for p in progs:
    try: rows, sep = load(f"{res}/{p}-raw.csv"), load(f"{res}/{p}-sep.csv")
    except FileNotFoundError as e: out.append(f"{p}: missing {e.filename}"); continue
    I = lambda r, k: int(r[k])
    real = [r for r in rows if I(r, 'rows') > I(r, 'padding_rows')]
    tot_rows = sum(I(r, 'rows') for r in rows); pad = sum(I(r, 'padding_rows') for r in rows)
    cyc = sum(I(r, 'cycles') for r in rows); rc = sum(I(r, 'cycles_real_rows') for r in rows)
    out.append(f"== {p}: {len(rows)} segments ({len(real)} with real rows), {tot_rows:,} rows ({tot_rows - pad:,} real), verdicts: "
               f"{sum(r['ok'] == 'OK' for r in rows)} OK of {len(rows)}")
    out.append(f"core: {rc / (tot_rows - pad):.4f} cycles per real row (target 4); {cyc / tot_rows:.4f} over every row, padding at 4")
    per = [I(r, 'cycles_real_rows') / (I(r, 'rows') - I(r, 'padding_rows')) for r in real]
    out.append(f"  per segment with real rows: min {min(per):.4f}, median {st.median(per):.4f}, max {max(per):.4f}")
    segc = [I(r, 'cycles') for r in rows]
    out.append(f"rows only, per segment: median {st.median(segc) / 1e6:.2f} ms, "
               f"max {max(segc) / 1e6:.2f} ms at 1 GHz, against 100 ms (segment {segc.index(max(segc))})")
    if 'drain_cycles' in rows[0]:
        allin = [I(r, 'cycles') + I(r, 'drain_cycles') for r in rows]; dr = [I(r, 'drain_cycles') for r in rows]
        out.append(f"the drain (an ideal controller, one packet a cycle): median {st.median(dr):,} cycles per segment, max {max(dr):,}; "
                   f"all-in with it: median {st.median(allin) / 1e6:.2f} ms, max {max(allin) / 1e6:.2f} ms (segment {allin.index(max(allin))}); "
                   f"scan stalls {sum(I(r, 'scan_stalls') for r in rows):,}")
    names = ['ifetch', 'dmiss', 'bus', 'probe', 'advice', 'newexp', 'exp']
    stall = {n: sum(I(r, n) for r in rows) for n in names}
    out.append("stalls, cycles per real row: " + ", ".join(f"{n} {stall[n] / (tot_rows - pad):.4f}" for n in names)
               + f"; rows alone {(rc - sum(stall.values())) / (tot_rows - pad):.4f}")
    for cls in ['mem_index', 'mem_data', 'map_index', 'map_data']:
        a, h = sum(I(r, cls + '_acc') for r in rows), sum(I(r, cls + '_hit') for r in rows)
        out.append(f"  shared cache, {cls.replace('_', ' ')} lines: {a:,} accesses, {100.0 * h / a if a else 0:.2f}% hits")
    walk, data = sum(I(r, 'walk_cycles') for r in rows), sum(I(r, 'data_cycles') for r in rows)
    out.append(f"the walk: {walk:,} cycles, {100.0 * walk / (walk + data):.1f}% of the memory system's cycles, {100.0 * walk / rc:.2f}% of the real rows' "
               f"cycles ({walk / (tot_rows - pad):.4f} per real row)")
    ee, rw, rwc = (sum(I(r, k) for r in rows) for k in ('early_evictions', 'read_waits', 'read_wait_cycles'))
    out.append(f"write buffer: {ee:,} early evictions, {rw:,} read waits ({rwc:,} cycles; worst segment {max(I(r, 'read_wait_cycles') for r in rows):,})")
    out.append(f"jumps: {sum(I(r, 'lru_hits') for r in rows):,} LRU hits, {sum(I(r, 'lru_misses') for r in rows):,} misses, {sum(I(r, 'probe_reads') for r in rows):,} map lines read")
    # the map's effect on the data: the control gives the map a shared cache of its own
    md = lambda rr: (sum(I(r, 'mem_data_hit') for r in rr), sum(I(r, 'mem_data_acc') for r in rr))
    (h1, a1), (h2, a2) = md(rows), md(sep)
    c1, c2 = rc, sum(I(r, 'cycles_real_rows') for r in sep)
    out.append(f"the map's effect on the data: data-line hits {100.0 * h1 / a1:.2f}% shared, {100.0 * h2 / a2:.2f}% with the map's own cache; "
               f"real-row cycles {c1:,} against {c2:,} ({100.0 * (c1 - c2) / c2:+.3f}%); the control's map LPDDR reads "
               f"{sum(I(r, 'map_sep_lpddr_reads') for r in sep):,}")
print("\n".join(out))
open(f"{res}/SUMMARY.txt", "w").write("\n".join(out) + "\n")
