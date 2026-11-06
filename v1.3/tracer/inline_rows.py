#!/usr/bin/env python3
"""Size the secp256k1 inlines' advice as untraced rows (WORKER7, REPORT step 3).

Builds, as straight-line rows on the Tracer's row ALU (the replayer's opcodes ADD, SUB, SLTU, XORI, OR, AND, MUL,
MULHU, ADDI with a 64-bit const; plus an untraced LD of an operand limb), the advice of the multiply and square
inlines: the quotient floor(a*b / m) for m = q (the field prime) and m = r (the group order), 4 words
(`inlines/secp256k1.cpp` multiply_field). Runs the rows in a small executor and checks the advice against
jolt-cpp's own evaluate(), compiled (harness/secp_advice.cpp, built at /tmp/secp_advice).

Method: P = a*b (schoolbook, product scanning). With m = 2^256 - c, write P = H*2^256 + L; then
P = H*m + (L + H*c), so the quotient gains H and the rest is L + H*c, which is far smaller. A fixed number of
rounds (2 for q, whose c has 33 bits; 3 for r, whose c has 129) leaves V < 2m, and one compare with m gives the
last quotient bit. No branches: everything is straight-line, as a template must be.

The division inlines (div_q, div_r: a modular inverse) and glv_r are sized from these building blocks; see main().
"""
import random
import subprocess
import sys

M64 = 2**64 - 1
Q = 2**256 - 0x1000003d1
R = 0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141


def limbs(x, n):
    return [(x >> (64 * i)) & M64 for i in range(n)]


class Prog:
    """Rows on symbolic registers; None stands for a value known to be 0 (no row needed)."""

    def __init__(self):
        self.rows = []
        self.n = 0
        self.live = set()
        self.peak = 0

    def reg(self):
        self.n += 1
        r = f'r{self.n}'
        self.live.add(r)
        self.peak = max(self.peak, len(self.live))
        return r

    def emit(self, op, a=None, b=None, k=0):
        d = self.reg()
        self.rows.append((op, d, a, b, k))
        return d

    def free(self, *rs):
        for r in rs:
            self.live.discard(r)

    # --- building blocks
    def const(self, v):
        return None if v == 0 else self.emit('ADDI', None, None, v)

    def add(self, x, y):
        """x + y -> (sum, carry). Zero-aware."""
        if x is None:
            return y, None
        if y is None:
            return x, None
        s = self.emit('ADD', x, y)
        c = self.emit('SLTU', s, x)
        return s, c

    def add3(self, x, y, cin):
        s, c1 = self.add(x, y)
        s, c2 = self.add(s, cin)
        if c1 is None:
            return s, c2
        if c2 is None:
            return s, c1
        return s, self.emit('OR', c1, c2)   # at most one of the two carries is set

    def add_limbs(self, A, B, n):
        """(A + B) mod 2^(64n), limb lists, zero-aware."""
        A = A + [None] * (n - len(A))
        B = B + [None] * (n - len(B))
        out, c = [], None
        for i in range(n):
            s, c = self.add3(A[i], B[i], c) if i < n - 1 else (self._add_nocarry(A[i], B[i], c), None)
            out.append(s)
        return out

    def _add_nocarry(self, x, y, c):
        s = x
        for t in (y, c):
            if s is None:
                s = t
            elif t is not None:
                s = self.emit('ADD', s, t)
        return s

    def mul(self, A, B, n):
        """(A * B) mod 2^(64n) by product scanning: a 3-word column accumulator."""
        out = []
        a0 = a1 = a2 = None
        for k in range(n):
            for i in range(len(A)):
                j = k - i
                if 0 <= j < len(B) and A[i] is not None and B[j] is not None:
                    lo = self.emit('MUL', A[i], B[j])
                    hi = self.emit('MULHU', A[i], B[j])
                    a0, c = self.add(a0, lo)
                    hi = self._add_nocarry(hi, c, None)  # hi <= 2^64 - 2, so hi + c never wraps
                    a1, c = self.add(a1, hi)
                    a2 = self._add_nocarry(a2, c, None)
            out.append(a0)
            a0, a1, a2 = a1, a2, None
        return out

    def geq_const(self, X, m):
        """1 if the limbs X >= the constant m, else 0: the borrow of X - m, inverted."""
        borrow = None
        for i, mi in enumerate(limbs(m, len(X))):
            k = self.const(mi)
            b1 = self.emit('SLTU', X[i], k) if k is not None else None
            d = self.emit('SUB', X[i], k) if k is not None else X[i]
            b2 = self.emit('SLTU', d, borrow) if borrow is not None else None
            borrow = b1 if b2 is None else (b2 if b1 is None else self.emit('OR', b1, b2))
        return self.emit('XORI', borrow, None, 1)


def multiply_advice(prog, A, B, m, rounds, hl):
    """floor(A*B / m) as 4 limbs. hl[i]: limbs of H after round i (from the bounds in the module doc)."""
    c = limbs(2**256 - m, 3 if m == R else 1)
    C = [prog.const(x) for x in c]
    P = prog.mul(A, B, 8)
    L, H = P[:4], P[4:]
    q = H
    for i in range(rounds):
        T = prog.mul(H, C, len(H) + len(C))
        V = prog.add_limbs(L, T, 4 + hl[i])
        L, H = V[:4], V[4:4 + hl[i]]
        if i < rounds - 1:
            q = prog.add_limbs(q, H, 4)
    # Now V = H * 2^256 + L < 2m with H in {0, 1}: one more quotient bit.
    corr = prog.emit('OR', H[0], prog.geq_const(L, m))
    return prog.add_limbs(q, [corr], 4)


def peak_live(prog, outs):
    """The most values live at once (a value lives from its row to its last use): bank-1 registers needed."""
    last = {}
    for i, (op, d, a, b, k) in enumerate(prog.rows):
        for r in (a, b):
            if r is not None and r.startswith('r'):
                last[r] = i
    for r in outs:
        if r is not None:
            last[r] = len(prog.rows)
    live, peak = 0, 0
    ends = {}
    for i, (op, d, a, b, k) in enumerate(prog.rows):
        live -= ends.pop(i, 0)
        if d in last:
            live += 1
            ends[last[d] + 1] = ends.get(last[d] + 1, 0) + 1
        peak = max(peak, live)
    return peak


def run(prog, inputs):
    v = dict(inputs)
    get = lambda r: 0 if r is None else v[r]  # noqa: E731
    for op, d, a, b, k in prog.rows:
        x, y = get(a), get(b)
        v[d] = {'ADDI': lambda: (x + k) & M64, 'ADD': lambda: (x + y) & M64, 'SUB': lambda: (x - y) & M64,
                'SLTU': lambda: int(x < y), 'OR': lambda: x | y, 'XORI': lambda: x ^ k,
                'MUL': lambda: (x * y) & M64, 'MULHU': lambda: (x * y) >> 64, 'LD': lambda: x}[op]()
    return v


def build(op):
    p = Prog()
    square = op in ('square_q', 'square_r')
    A = [p.emit('LD', f'a{i}') for i in range(4)]
    B = A if square else [p.emit('LD', f'b{i}') for i in range(4)]
    m, rounds, hl = (Q, 2, [1, 1]) if op.endswith('_q') else (R, 3, [3, 1, 1])
    out = multiply_advice(p, A, B, m, rounds, hl)
    return p, out


OPS = {'mul_q': 1, 'square_q': 2, 'mul_r': 4, 'square_r': 5}
TRACED = {'mul_q': 186, 'square_q': 167, 'mul_r': 232, 'square_r': 213, 'div_q': 194, 'div_r': 240, 'glv_r': 13}


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    rng = random.Random(7)
    print('# secp256k1 multiply/square advice as untraced rows, checked against jolt-cpp evaluate()')
    print('# inline\tuntraced rows\tof which MUL/MULHU\tpeak live registers\ttraced rows (shapes)\tcases\tmismatches')
    sizes = {}
    for op, code in OPS.items():
        p, out = build(op)
        m = Q if op.endswith('_q') else R
        lines, cases = [], []
        while len(cases) < n:
            kind = rng.randrange(4)
            a = rng.randrange(m) if kind < 3 else rng.randrange(2**256)
            b = (rng.randrange(m) if kind < 3 else rng.randrange(2**256)) if 'mul' in op else a
            if kind == 1:
                a, b = m - 1 - rng.randrange(4), (m - 1 - rng.randrange(4)) if 'mul' in op else a
            if (a * b) // m >= 2**256:
                continue            # jolt-cpp rejects these (quotient_overflow): not a valid trace
            cases.append((a, b))
            lines.append(f'{code} ' + ' '.join(f'{x:x}' for x in limbs(a, 4) + limbs(b, 4)))
        res = subprocess.run(['/tmp/secp_advice'], input='\n'.join(lines) + '\n', capture_output=True,
                             text=True).stdout.split('\n')
        bad = 0
        for (a, b), line in zip(cases, res):
            f = line.split()
            want = [int(x, 16) for x in f[1:5]]
            env = {f'a{i}': x for i, x in enumerate(limbs(a, 4))} | {f'b{i}': x for i, x in enumerate(limbs(b, 4))}
            v = run(p, env)
            got = [0 if r is None else v[r] for r in out]
            bad += f[0] != '0' or got != want
        muls = sum(r[0] in ('MUL', 'MULHU') for r in p.rows)
        sizes[op] = len(p.rows)
        print(f'{op}\t{len(p.rows)}\t{muls}\t{peak_live(p, out)}\t{TRACED[op]}\t{len(cases)}\t{bad}')
    # Building blocks for the rest (sized, not built):
    pm = Prog()
    X = [pm.reg() for _ in range(4)]
    pm.mul(X, [pm.reg() for _ in range(4)], 8)
    prod = len(pm.rows)
    print(f'# building block: 256 x 256 -> 512-bit product: {prod} rows')
    print(f'# a modular multiply mod q is ~{sizes["mul_q"] - 8} rows, mod r ~{sizes["mul_r"] - 8} rows (no loads)')
    fq = 255 * (sizes['square_q'] - 4) + 15 * (sizes['mul_q'] - 8)
    fr = 255 * (sizes['square_r'] - 4) + 40 * (sizes['mul_r'] - 8)
    print(f'# div_q by Fermat (a^(q-2), ~255 squarings + ~15 multiplies, addition chain): ~{fq} rows')
    print(f'# div_r by Fermat (~255 squarings + ~40 multiplies): ~{fr} rows')
    return 0


if __name__ == '__main__':
    sys.exit(main())
