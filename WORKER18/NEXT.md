# Parked for WORKER18 (send when the current work is done; remove once sent)

After the rulings, the 7189216 re-check and the demo guests:

1. **Re-run `wip/gold/check.sh` at jolt-cpp 88dca85**, once WORKER16's `worker16-pin-88dca85` is merged into
   `tracer-standalone-main` (your REPORT's item 2): reference rows and segment files regenerated at the new pin, gold still
   byte-identical on both workloads at 2^29 and the hard-cut files.
2. **DECISIONS-v1.3 section 21(d):** replay both 2^29 workloads' address streams through gold with models of the Tracer's 16 KB
   instruction and data caches (128 lines of 128 B, fully associative), the memory system's TLB (proposed 64 entries) and
   upper-index cache (16-64), and the shared 512 KB cache (8-way, 512 sets, 128 B lines); report hit rates per segment, cold
   caches at each segment's start. Plan first.
3. **Two of WORKER17's findings** (DECISIONS-v1.3 sections 17, 25): gold rejects `LR.D x0` / `LR.W x0` loading a nonzero
   word; gold flags a guest access below `lowest_address` or above the layout's top as the Tracer's comparator will
   (`(address − lowest_address) < 8·K`, unsigned, on the full 64-bit address); and a jolt-cpp bug report against its prover, which silently drops such an access
   (`jolt-utils/ram.hpp:16-30`; GPU RW checking drops cells >= K) and produces a proof that fails verification
   (`WORKER17/REPORT.md`). Plan first.
