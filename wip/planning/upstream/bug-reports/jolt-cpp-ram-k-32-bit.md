# jolt-cpp: K is capped at 2^30 by 32-bit fields; K = 2^32 would truncate to 0

**Component** (jolt-cpp 88dca85; meshsim `wip/planning/sessions/WORKER17-REPORT.md`, "K"): `SumcheckContext::ram_k` is
`uint32_t` (`sumcheck_context.hpp:36`), filled with `(u32)get_ram_k()` (`card_context.cu:356`), and every C++ sumcheck reads
it; the output check's `ram_k` is `uint32_t` (`output_check.cu:89`); GPU RW checking's `RW_MAX_ADDRESS_SPACE` is 2^30 (a
31-bit column with the write flag at bit 31, `rw_sparse.hpp:33-36`); `mem_waddr_p1` is a u32 of the absolute address >> 3,
plus one (addresses below about 2^35).

**What happens.** a16z's K is any power of two up to `compute_max_ram_k` (`heap_end − lowest_address` in words), which
reaches 2^32 for a 32 GiB layout. jolt-cpp proves K up to 2^30; K = 2^32 truncates to 0 in the context.

**Impact**: programs whose layout or accesses need K > 2^30 (over 8 GiB of RAM) can't be proven by jolt-cpp, and
nothing rejects them cleanly. EVM Zone (2^23) and Cyclone (2^24) are far below. Reproducer: none run (it needs a
> 8 GiB layout); the limit is in the types.

**Expected**: 64-bit K throughout, or an explicit rejection above 2^30.
