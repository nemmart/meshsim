"""The toy example shared by the figures: 4 tiles x 8 keys, balanced (8 per tile).

Records are ordered by (key, tile, position); tile q receives ranks 8q .. 8q + 7.
"""
INP = [[57, 12, 89, 42, 33, 5, 15, 42],
       [64, 42, 18, 90, 27, 51, 8, 76],
       [39, 83, 42, 71, 60, 94, 22, 47],
       [70, 31, 3, 86, 55, 42, 99, 26]]
P, n = 4, 8
N = P * n
R = [n * q for q in range(P + 1)]                      # rank boundaries 0, 8, 16, 24, 32

# a record is (key, tile, position); its value is named by tile letter and position
recs = [(k, t, j) for t in range(P) for j, k in enumerate(INP[t])]
rank = {r: i for i, r in enumerate(sorted(recs))}
dest = lambda r: rank[r] // n
value = lambda r: "abcd"[r[1]] + str(r[2])

local = [sorted(r for r in recs if r[1] == t) for t in range(P)]                 # step 1 (stable)
inbox = [[r for p in range(P) for r in local[p] if dest(r) == q] for q in range(P)]  # step 4, source by source
final = [sorted(inbox[q], key=lambda r: r[0]) for q in range(P)]                  # step 5 (stable)
slices = [[sum(dest(r) == q for r in local[p]) for q in range(P)] for p in range(P)]

# ordinals: step 1 stamps the input position; the keys all-to-all stamps the receive-buffer position
ord_in = {r: r[2] for r in recs}
ord_rx = {r: i for q in range(P) for i, r in enumerate(inbox[q])}

assert all(rank[r] // n == q for q in range(P) for r in final[q])
assert [r for q in range(P) for r in final[q]] == sorted(recs)


# ---- the split, as sort/split/split.c runs it, scaled to 4 tiles -------------------
def lb(run, v): return sum(k < v for k in run)
def ub(run, v): return sum(k <= v for k in run)


def split():
    """Per owner q (1..3): the seed samples and bracket, then the rounds.
    A round is dict(lo, hi, v, less[], eq[], L, U, act) with act in
    'lower' (hi = v - 1), 'higher' (lo = v + 1), 'equal' (L = R: done), 'tie' (done inside a run of v),
    and 'known' when lo = hi after both ends were probed (K* = lo without a probe)."""
    runs = [[r[0] for r in local[t]] for t in range(P)]
    out = {}
    for q in range(1, P):
        Rq = R[q]
        samples = [runs[p][(q * n) // P] for p in range(P)]     # j_p = q n_p / P (split.c: >> 6 for 64 tiles)
        lo, hi = min(samples), max(samples)
        lo_known = hi_known = False
        ubA, lessB = [0] * P, [n] * P
        rounds, cuts = [], None
        while cuts is None:
            v = lo + ((hi - lo) >> 1)
            less = [lb(x, v) for x in runs]; leq = [ub(x, v) for x in runs]
            L, U = sum(less), sum(leq)
            rd = dict(lo=lo, hi=hi, v=v, less=less, eq=[b - a for a, b in zip(less, leq)], L=L, U=U)
            if L == Rq:
                rd["act"] = "equal"; cuts = less
            elif L > Rq:
                rd["act"] = "lower"; hi = v - 1; hi_known = True; lessB = less
            elif Rq < U:
                rd["act"] = "tie"; cuts = tie(less, leq, Rq)
            else:
                rd["act"] = "higher"; lo = v + 1; lo_known = True; ubA = leq
            rounds.append(rd)
            if cuts is None and lo == hi and lo_known and hi_known:
                rd["known"] = lo
                cuts = tie(ubA, lessB, Rq)
        out[q] = dict(samples=samples, bracket=(min(samples), max(samples)), rounds=rounds, cuts=cuts)
    return out


def tie(less, leq, Rq):
    """Equal keys at the boundary go below it in tile order until the rank is reached."""
    need, cuts = Rq - sum(less), []
    for a, b in zip(less, leq):
        take = min(b - a, need); need -= take; cuts.append(a + take)
    return cuts


SPLIT = split()
for q in range(1, P):
    assert SPLIT[q]["cuts"] == [sum(1 for r in local[p] if dest(r) < q) for p in range(P)]

if __name__ == "__main__":
    for t in range(P): print("T%d" % t, [r[0] for r in local[t]], "inbox", [(r[0], r[1]) for r in inbox[t]])
    print("slices", slices)
    for q, s in SPLIT.items(): print(q, s["bracket"], [(r["v"], r["L"], r["U"], r["act"]) for r in s["rounds"]], s["cuts"])
