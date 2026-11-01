#!/usr/bin/env python3
"""make_jobs.py -- writes jobs.txt for run.sh: one line per run, "weight<TAB>group<TAB>id<TAB>seed<TAB>SortTest args".
weight: an estimate of the run's length (thousands of simulated cycles) so the longest go first. @KEYS@ is replaced
by run.sh with the key directory, @N@ with the keys per tile, @NH@ with half of it. Every run uses ready_latency 8 (lead, 3 Oct: the physical default, P4 and the sweep)."""
SETS = ["uniform", "zipf05", "zipfraw05", "zipf08", "zipfraw08", "zipf10", "zipfraw10"] + \
       [f"trace{h}-{f}" for h in (10, 100, 10000) for f in ("10", "30", "60")] + ["sorted", "reverse", "equal", "one99"]
BT = {"zipf10", "zipfraw10"} | {s for s in SETS if s.startswith("trace")}
def weight(s, fmt):
    k = fmt == "k128"
    if s in BT: return (2300 if k else 1450) if (k or s.startswith("trace")) else 1150   # zipf 1.0 in 32-bit stays direct
    if s == "reverse": return 1750 if k else 1150
    if s in ("zipf08", "zipfraw08", "one99"): return 1650 if k else 1150
    return 1550 if k else 1100
jobs = []
for s in SETS:
    for fmt in ("k128", "kv32"):
        jobs.append((weight(s, fmt), "p4", f"p4/{s}-{fmt}", -1, f"keys=@KEYS@/{s}.keys fmt={fmt} ready=8"))
# worker 6's built-in sets, kept as extra checks
jobs.append((2500, "p4", "p4/few5-k128", -1, "kind=few n=@N@ fmt=k128 ready=8"))
jobs.append((800, "p4", "p4/ragged-kv32", -1, "kind=ragged n=@NH@ fmt=kv32 ready=8"))
jobs.append((1300, "p4", "p4/shaped-k128", -1, "kind=ragged n=@NH@ fmt=k128 shaped ready=8"))
# shuffled child order on three full sets (lead, 3 Oct): direct, BT, TH
for s, fmt in (("uniform", "k128"), ("trace100-30", "k128"), ("reverse", "kv32")):
    jobs.append((weight(s, fmt), "shuffle", f"shuf/{s}-{fmt}", 4, f"keys=@KEYS@/{s}.keys fmt={fmt} ready=8"))
# the whole-sort sweep (M6's parameters and corner; ready_latency's base is 8 here, so its points are 1 and 4)
POINTS = [("default", "")] + \
    [(f"sram-read-{v}", f"cfg.sramReadLatency={v}") for v in (1, 3, 4)] + \
    [(f"area-queue-{v}", f"cfg.areaQueueDepth={v}") for v in (4, 16)] + \
    [(f"ts-latency-{v}", f"cfg.tsEngineLatency={v}") for v in (2, 3, 4)] + \
    [(f"ready-{v}", f"ready={v}") for v in (1, 4)] + \
    [(f"go-run-{v}", f"cfg.goLatency={v} cfg.runLatency={v}") for v in (4, 8)] + \
    [(f"link-{v}", f"cfg.linkLatency={v}") for v in (2, 4)] + \
    [("skew-off", "cfg.skew=false")] + [(f"skew-seed-{v}", f"cfg.skewSeed={v}") for v in (2, 3, 4, 5, 6)] + \
    [("cm-4", "cfg.cmLatency=4"),
     ("corner", "cfg.sramReadLatency=4 cfg.areaQueueDepth=4 cfg.tsEngineLatency=4 cfg.goLatency=8 cfg.runLatency=8 cfg.linkLatency=4 cfg.cmLatency=4")]
for s in ("uniform", "trace100-30"):
    for name, args in POINTS:
        ready = "" if args.startswith("ready=") else "ready=8 "
        jobs.append((weight(s, "k128") + (300 if name == "corner" else 0), "sweep", f"sweep/{s}-k128/{name}", -1,
                     f"keys=@KEYS@/{s}.keys fmt=k128 {ready}{args} cfg.checkDisjoint=false".strip()))
jobs.sort(key=lambda j: -j[0])
with open("jobs.txt", "w") as f:
    f.write("# weight\tgroup\tid\tseed\tSortTest arguments (tools/make_jobs.py; longest first)\n")
    for w, g, i, seed, args in jobs:
        f.write(f"{w}\t{g}\t{i}\t{seed}\t{args}\n")
print(f"jobs.txt: {len(jobs)} runs ({sum(1 for j in jobs if j[1] == 'p4')} P4, {sum(1 for j in jobs if j[1] == 'shuffle')} shuffled, {sum(1 for j in jobs if j[1] == 'sweep')} sweep)")
