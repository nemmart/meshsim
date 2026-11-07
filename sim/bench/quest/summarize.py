#!/usr/bin/env python3
"""Summarise a W6 task's raw.txt: group runs by (tag + the bench's configuration), median and spread of the timings.

usage: summarize.py raw.txt [key ...]   keys default to the configuration fields that identify a run."""
import re, statistics, sys
from collections import OrderedDict

TIMES = ("ns_per_mesh_cycle", "ns_per_hop", "thread_ns_per_mesh_cycle", "mesh_cycles_per_s", "latency_ns",
         "throughput_ns", "thread_ns_per_mul", "muls_per_s", "ns_per_unit_cycle", "slowest_mesh_cycles_per_s",
         "slowest_thread_rate", "core_cycles", "core_instr", "core_cmiss", "core_l1miss", "atom_cycles", "atom_instr",
         "atom_cmiss", "secs")
ID = ("lw", "dl", "width", "ready", "mix", "rate", "meshes", "threads", "form", "hold", "units", "n")

def parse(path):
    rows = []
    for line in open(path):
        tag, rest = line.split(" ", 1)
        kv = dict(re.findall(r"(\w+)=(\S*)", rest))
        kv["tag"] = tag
        rows.append(kv)
    return rows

def num(x):
    try: return float(x)
    except (TypeError, ValueError): return None

def groups(rows, ids=ID):
    g = OrderedDict()
    for r in rows:
        key = (r["tag"],) + tuple((k, r[k]) for k in ids if k in r)
        g.setdefault(key, []).append(r)
    return g

def stat(rs, field):
    v = [num(r.get(field)) for r in rs]
    v = [x for x in v if x is not None]
    if not v: return None, None, 0
    m = statistics.median(v)
    return m, (max(v) - min(v)) / m * 100 if m else 0.0, len(v)

if __name__ == "__main__":
    rows = parse(sys.argv[1])
    fields = sys.argv[2:] or ["ns_per_mesh_cycle", "ns_per_hop", "mesh_cycles_per_s"]
    for key, rs in groups(rows).items():
        cols = []
        for f in fields:
            m, sp, n = stat(rs, f)
            if m is not None: cols.append(f"{f}={m:.4g} (±{sp:.1f}% n={n})")
        print(key[0], " ".join(f"{k}={v}" for k, v in key[1:]), "|", " ".join(cols))
