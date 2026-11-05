# Parked for WORKER18 (send when its current work is done; remove once sent)

Item 1 (check.sh at 88dca85) is done (3f93276). Next, in order, plan first:

1. **Two of WORKER17's findings** (DECISIONS-v1.3 sections 17, 25): gold rejects `LR.D x0` / `LR.W x0` loading a nonzero word;
   gold flags a guest access outside `(address − lowest_address) < 8·K` (unsigned, full 64-bit) as the Tracer's comparator will;
   and a jolt-cpp bug report against its prover, which silently drops such an access (`jolt-utils/ram.hpp:16-30`; GPU RW
   checking drops cells >= K) and produces a proof that fails verification, noting the JIT's unchecked guest memory (each VM's
   4 GiB mapped with no guard pages or bounds checks; `sessions/WORKER17-REPORT.md`).
2. **The host's v1.3 rules, as the host tracer's reference** (spec 4.21; DECISIONS-v1.3 section 25): the end is any JAL with
   immediate 0, whatever rd; a JALR or branch to 0 is the map error; misalignment by a16z's floor rule on every memory row; and
   **stores into code rejected** by scanning the run's write list against the text sections (D1), once per segment.
3. **v1.3's host-side formats** (DECISIONS-v1.3 sections 16, 18): **frame 6 is the whole per-proof jump table** (u32 k, u32 p1,
   u32 p3, then the 2ᵏ lines), built after the run with `wip/jumpmap`'s `HashTable` (WORKER26, merged at 5e1f97c); **frame 7**,
   the proof parameters (log₂ K, `lowest_address`, a16z's `MemoryLayout`), sent first, layout yours to propose with WORKER16
   (WORKER22's `proof.json` is the stand-in); **frame 2** takes v1.26.1's record (stream word with advice, flags out of word 0).
   Coordinate with WORKER16's segment tools.
