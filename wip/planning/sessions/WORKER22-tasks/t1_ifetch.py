#!/usr/bin/env python3
"""B1 (WORKER22): the instruction-fetch stalls broken down, from TracerT1's CSVs: per workload the 128-line run (the
spec's 16 KB) split into cold and capacity misses, first halves and straddling second halves, with their cycles, and the
capacity sweep (256 and 1,024 lines). Usage: t1_ifetch.py RESDIR PROGRAM..."""
import csv, sys
res, progs = sys.argv[1], sys.argv[2:]
out = []
def load(p):
    try:
        with open(p) as f: return list(csv.DictReader(f))
    except FileNotFoundError: return None
for p in progs:
    base = load(f"{res}/{p}-ic128.csv")
    if base is None: out.append(f"{p}: missing ic128"); continue
    I = lambda r, k: int(r[k]); S = lambda rr, k: sum(I(r, k) for r in rr)
    real = S(base, 'rows') - S(base, 'padding_rows'); ok = sum(r['ok'] == 'OK' for r in base)
    out.append(f"== {p}: {real:,} real rows, {S(base, 'instrs'):,} instructions; verdicts {ok} OK of {len(base)}")
    tot = S(base, 'ifetch')
    out.append(f"instruction-fetch stalls: {tot:,} cycles, {tot / real:.4f} per real row; {S(base, 'imiss'):,} misses, "
               f"{S(base, 'i_miss_lpddr'):,} of them reaching LPDDR; {S(base, 'straddles'):,} straddling instructions "
               f"({100.0 * S(base, 'straddles') / max(1, S(base, 'instrs')):.2f}% of instructions)")
    for k, name in [('cold_first', 'cold, first half'), ('cold_second', 'cold, straddling second half'),
                    ('cap_first', 'capacity, first half'), ('cap_second', 'capacity, straddling second half')]:
        n, c = S(base, 'i_' + k), S(base, 'i_cyc_' + k)
        out.append(f"  {name:34s} {n:>10,} misses  {c:>13,} cycles  {c / real:.4f} per row  {100.0 * c / max(1, tot):5.1f}%  ({c / max(1, n):.1f} per miss)")
    c0 = S(base, 'cycles_real_rows') / real
    out.append(f"core cycles per real row: {c0:.4f} at 128 lines (16 KB)")
    for n in (256, 1024):
        r = load(f"{res}/{p}-ic{n}.csv")
        if r is None: continue
        rr = S(r, 'rows') - S(r, 'padding_rows')
        out.append(f"  {n:5d} lines ({n // 8} KB): {S(r, 'cycles_real_rows') / rr:.4f} per real row, instruction-fetch stalls "
                   f"{S(r, 'ifetch') / rr:.4f} per row (capacity misses {S(r, 'i_cap_first') + S(r, 'i_cap_second'):,}); verdicts "
                   f"{sum(x['ok'] == 'OK' for x in r)} OK")
print("\n".join(out)); open(f"{res}/IFETCH.txt", "w").write("\n".join(out) + "\n")
