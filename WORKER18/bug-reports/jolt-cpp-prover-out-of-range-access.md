# jolt-cpp: an access outside the provable range is silently dropped by the prover; the JIT's guest memory is unchecked

**Components**: the prover's RAM remapping, `src/jolt-core/jolt-utils/ram.hpp:16-30` (`new_remap_address`), and its GPU
RW checking, `src/jolt-core/zkvm/ram/gpu/rw_sparse_init_kernels.cu:93-95`; the tracer's VM memory, `src/tracer/vm.cpp:13-30`
(and `bump_ptr.hpp:24-36`, `compressed_trace_gen.cpp:46`, `utils/mem.hpp:26, 33`, per meshsim `sessions/WORKER17-REPORT.md`).
jolt-cpp 88dca85 (unchanged since 821e1bd).

**1. The prover drops out-of-range accesses.** A guest load or store whose address is below `lowest_address`
(min of the trusted and untrusted advice starts) can't be proven: R1CS ties the row's RAM address to rs1 + imm, and RAF's
Σ ra·(8k + lowest) can't reach it. a16z's prover rejects such a run ("RAM address precedes the memory layout",
`crates/jolt-prover/src/config.rs:165-174`), and an address above the layout makes K exceed `compute_max_ram_k`, which
a16z's verifier rejects (`crates/jolt-verifier/src/verifier.rs:473-490`). jolt-cpp instead:
- `new_remap_address` returns false for an address below `lowest_address`, and the caller treats the access as absent;
- the GPU kernel keeps an access only if `addr >= lowest_address && col < K`, dropping cells at or above K; `col` is
  `(addr - lowest_address) >> 3` truncated to **u32**, so an address 2^35 or more above `lowest_address` aliases into a
  low cell instead of being dropped.
Either way it **produces a proof**, which then fails verification, with no error before.

**2. The JIT's guest memory is unchecked.** Each of the 33 VMs gets 4 GiB of read-write RAM, guest address = offset
from its base, with its `RiscV` state struct directly after it and no guard pages between VMs. So an access below
`lowest_address` or above the layout's top (under 4 GiB) silently reads or writes mapped, zeroed RAM; from 4 GiB it
corrupts the VM's own state and then the next VM's RAM.

**Reproducers** (meshsim `wip/gold/test/guests/`, `make_guests.py`): `range_below_lowest` (`ld a0, 0(0x7000_0000)`):
jolt-cpp's tracer traces it (4 rows, `39d98dd243dbb5f1`), as a16z's tracer does; a16z's prover would reject it.
`range_above_top` (`ld` from 0x8040_0000, past `heap_end` 0x80200090): jolt-cpp's tracer traces it (`71b3a07dc3ef6505`);
a16z's tracer panics (`tracer/src/emulator/memory.rs:36`). meshsim's gold rejects both, as the Tracer's comparator will:
"(address − lowest_address) < 8·K" unsigned on the full 64-bit address, and K ≤ max_ram_k.

**Expected**: the host tracer (or at least the prover) rejects a run with an access outside [lowest_address,
lowest_address + 8·K) and a K above `compute_max_ram_k`, as a16z does, instead of proving it. A per-access check in the
JIT would cost speed; guard pages (or mapping only the layout's regions) would make it free.
