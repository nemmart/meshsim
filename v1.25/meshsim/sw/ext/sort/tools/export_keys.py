#!/usr/bin/env python3
"""export_keys.py -- the sort's input key sets: worker 5's 20-set catalogue, generated with design v1's data.py.

    export_keys.py OUTDIR [--n PER_TILE] [--only SLUG[,SLUG...]] [--datapy DIR]

Writes OUTDIR/<slug>.keys for each set: the 64 tiles' input keys in GENERATION order (data.py's generators, cut into 64
consecutive blocks exactly as data.split_tiles cuts them, but not sorted: the sort's L1 does that). The stream matrix,
the split and every cycle count the planner sees depend only on each tile's key multiset, so these are the catalogue's
sets as worker 5 and worker 4 used them (data.datasets(64 * n), seed 0).

Format (little-endian uint32): magic 0x4B455953 ('SYEK'), 64 tile counts, then the keys tile by tile.
--datapy defaults to the package's design_v1/python/split-point-model (or $DATAPY); numpy is required.
"""
import argparse, os, struct, sys
import numpy as np

SLUGS = {   # data.datasets name -> file slug
    "uniform": "uniform",
    "zipf s=0.5": "zipf05", "zipfraw s=0.5": "zipfraw05", "zipf s=0.8": "zipf08", "zipfraw s=0.8": "zipfraw08",
    "zipf s=1.0": "zipf10", "zipfraw s=1.0": "zipfraw10",
    "sorted": "sorted", "reverse sorted": "reverse", "all equal": "equal", "99% one value": "one99",
}
for h in (10, 100, 10000):
    for f in (0.10, 0.30, 0.60):
        SLUGS[f"trace hot={h} frac={f:.2f}"] = f"trace{h}-{int(round(f * 100)):02d}"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--n", type=int, default=65536, help="keys per tile (default 65,536: N = 4M)")
    ap.add_argument("--only", default="", help="comma-separated slugs")
    ap.add_argument("--datapy", default=os.environ.get("DATAPY", os.path.join(here, "..", "..", "..", "..", "..", "..",
                                                                               "design_v1", "python", "split-point-model")))
    a = ap.parse_args()
    sys.path.insert(0, a.datapy)
    import data
    # generation order: the same cut as data.split_tiles, without its per-block sort
    def cut(keys, ntiles=64):
        n = len(keys); base, rem = divmod(n, ntiles); runs, s = [], 0
        for p in range(ntiles):
            e = s + base + (1 if p < rem else 0)
            runs.append(np.asarray(keys[s:e]).astype(np.uint32)); s = e
        return runs
    data.split_tiles = cut
    only = set(x for x in a.only.split(",") if x)
    os.makedirs(a.out, exist_ok=True)
    for name, grp, runs in data.datasets(64 * a.n):
        slug = SLUGS[name]
        if only and slug not in only:
            continue
        path = os.path.join(a.out, slug + ".keys")
        with open(path, "wb") as f:
            f.write(struct.pack("<I", 0x4B455953))
            f.write(struct.pack("<64I", *[len(r) for r in runs]))
            for r in runs:
                f.write(r.astype("<u4").tobytes())
        print(f"{path}: {name}, 64 x {len(runs[0])}", flush=True)


if __name__ == "__main__":
    main()
