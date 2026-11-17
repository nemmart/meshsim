#!/usr/bin/env python3
"""sum_runs.py: adds up the totals of several linkrun outputs (the runner's slices): the runs, the failures, the
sums and the largest of each figure, and each rare event's count.      usage: sum_runs.py <file>..."""
import re, sys
runs = failed = cycles = packets = 0
chains = [0, 0, 0, 0]
events, biggest, fails = {}, {}, []
for path in sys.argv[1:]:
    for line in open(path, errors="replace"):
        if "FAILED:" in line: fails.append(line.strip()[:400]); continue
        m = re.match(r"runs (\d+) \(chains of 1, 2, 4, 16: (\d+), (\d+), (\d+), (\d+)\); FAILED (\d+)", line)
        if m:
            runs += int(m[1]); failed += int(m[6])
            for i in range(4): chains[i] += int(m[2 + i])
            continue
        m = re.match(r"cycles (\d+); packets (\d+)", line)
        if m: cycles += int(m[1]); packets += int(m[2]); continue
        if line.startswith(("the most", "the longest", "recoveries", "coming to rest")):
            key = re.sub(r"\d+", "#", line.strip())
            nums = [int(x) for x in re.findall(r"\d+", line)]
            old = biggest.get(key)
            biggest[key] = nums if old is None else [max(a, b) for a, b in zip(old, nums)]
            continue
        m = re.match(r"  (\S.*?)\s{2,}(\d[\d ]*?)(\s+<- never happened)?$", line.rstrip("\n"))
        if m:
            nums = [int(x) for x in m[2].split()]
            old = events.get(m[1])
            events[m[1]] = nums if old is None or len(old) != len(nums) else [a + b for a, b in zip(old, nums)]
print(f"runs {runs} (chains of 1, 2, 4, 16: {chains[0]}, {chains[1]}, {chains[2]}, {chains[3]}); FAILED {failed}")
print(f"cycles {cycles}; packets {packets}")
for f in fails: print("  " + f)
print("\nthe largest over the slices (a slice's average where the line says average):")
for key, nums in biggest.items():
    it = iter(nums); print("  " + re.sub("#", lambda _: str(next(it)), key))
print("\nrare events, summed:")
for name, nums in events.items():
    print(f"  {name:58s} " + " ".join(f"{n:13d}" for n in nums) + ("   <- never happened" if not any(nums) else ""))
sys.exit(1 if failed else 0)
