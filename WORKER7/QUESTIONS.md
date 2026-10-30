# Worker 7 questions

Each: what, where, my proposed answer. I carried on with the proposal in every case.

| # | question | where | proposed answer |
|---|---|---|---|
| Q1 | **Which jolt-cpp commit does the Tracer track?** jolt-cpp `main` (52fbbab3b) has diverged from the spec's reference copy (jolt-asic-planning 4f36b3d): new `VirtualLoad/Store B/H/W` opcodes, no `VirtualAlignAddr` or alignment assertions, rewritten sub-word loads and stores. The advice cases are byte-identical and the secp256k1 inverse changed implementation only, so **this work is unaffected**, but the replayer's opcode table and ROM templates are. | spec 4.19 header; `replayer-opcodes.md` header | Keep 4f36b3d as the reference for v1.3 (I did); open a separate item to re-derive the appendix against `main` with `wip/tools/replayer-opcodes/` |
| Q2 | **The template builder isn't anywhere I can see.** Not in jolt-asic-planning (either branch), jolt-cpp `main`, or meshsim. | prompt INPUTS; DECISIONS-v1.3 section 7 | Templates here are data plus a small generator (`wip/tracer/gen.py`); whoever owns the builder folds them in |
