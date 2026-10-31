# Deliverables: part M (2026-10-27, CHANGES part v)

Archive: `mesh-sorter-v1.1-2026-10-27a.tgz`: every change from parts A–M. `part-m.diff` goes from 26r to 27a (`spec/machine-spec.md`); `part-l2.diff` from 26q to 26r; `part-l.diff` from 26p to 26q; `part-k2.diff` from 26o to 26p; `part-k.diff` (K10) from 26n to 26o; `part-j.diff` from 26m to 26n; `part-i.diff` from 26l to 26m; `part-h.diff` from 26k to 26l; `part-g.diff` from 26i to 26k; `part-f.diff` from 26h to 26i; `part-e.diff` from 26g to 26h; `part-d.diff` from 26c to 26g.

- `part-m.diff`: unified diff of `spec/machine-spec.md` against 26r; it reproduces it byte for byte.
- `part-l2.diff`: unified diff of `spec/machine-spec.md` against 26q (part u's corrected counts); it reproduces it byte for byte.
- `part-l.diff`: unified diff of `spec/machine-spec.md` against 26p; it reproduces it byte for byte.
- `part-k2.diff`: unified diff of `spec/machine-spec.md` and `sw/REGISTERS.md` against 26o; it reproduces both byte for byte.
- `part-k.diff`: unified diff of `spec/machine-spec.md` against 26n; it reproduces it byte for byte.
- `part-j.diff`: unified diff of `spec/machine-spec.md` and `sw/REGISTERS.md` against 26m; it reproduces both byte for byte.
- `part-i.diff`: unified diff of `spec/machine-spec.md` against 26l; it reproduces it byte for byte.
- `part-h.diff`: unified diff of `spec/machine-spec.md` and `spec/replayer-opcodes.md` against 26k; it reproduces both byte for byte.
- `part-g.diff`: unified diff of `spec/machine-spec.md`, `spec/replayer-opcodes.md` and `sw/REGISTERS.md` against 26i; it reproduces all three byte for byte.
- `part-f.diff`: unified diff of `spec/machine-spec.md` and `README.md` against 26h; it reproduces both byte for byte.
- `part-e.diff`: unified diff of `spec/machine-spec.md` and `sw/REGISTERS.md` against 26g (apply with `patch -p1`); it reproduces both files byte for byte.
- `part-d.diff`: unified diff of `spec/machine-spec.md` and `sw/REGISTERS.md` against 26c (apply with `patch -p1`).
- `CHANGES-v1.1.md` part l: rulings, sections changed, the simulator handoff, `mesh.h` constants, what is stale.
- Kept from earlier passes: `modes-counting.diff` (part k), `request-periph.diff` (part j), `wake-pass.diff`, and the notes files.

Changed files: `spec/machine-spec.md`, `sw/REGISTERS.md` (prose only; one new empty generated-block marker in 2.9), `CHANGES-v1.1.md`, this folder.

## Verification

Applying `part-d.diff` to a fresh copy of 26c reproduces both files byte for byte; every section reference in the spec resolves; every pre-existing generated block in `REGISTERS.md` is unchanged.

## The simulator session (2026-10-27, merged)

- `NOTES-sim-26n.md`: meshsim on spec 26n, commit by commit (A-M), with every old → new test expectation and the
  re-measured numbers; the summary is `../SIM-CHANGES-v1.1-26n.md`.
- `SPEC-OPEN-26n.md`: the spec items the simulator session raised (all taken into part M) and the six earlier ones
  fixed in 26r.
- `MERGE-2026-10-27.md`: how the spec session's archive (through part M) and the replayer-model session's cp14 were
  merged into this tree, with every conflict resolved.
