# jolt-cpp: a rewound chunk header keeps the boundary's registers; correct only by accident of the expansions

**Component**: `src/tracer/trace_owner.cpp:705` (jolt-cpp 88dca85; meshsim `wip/planning/sessions/WORKER17-REPORT.md`,
"The rewound chunk header").

**What happens.** A compressed chunk boundary falling inside an expansion with non-zero scratch registers is rewound to
the expansion's start, but its header keeps the **boundary's** registers 0-47; the CPU and GPU expanders replay the
skipped rows from them. A replayed row would be wrong if it read a stateful register (32-47) that the expansion changed
before the boundary. Nothing checks or guarantees that; it holds only because no current expansion reads a stateful
register after writing it within the skipped part.

**Evidence**: both workloads hit the rewind (230 chunks on EVM Zone, up to 186 skipped rows; 48,081 on Cyclone, up to
1,066); a register-continuity check over every expanded row found no wrong row (CPU expander; the GPU expander uses the
same headers and replay, not run). meshsim's gold and a16z agree with jolt-cpp's rows on both workloads at 2^29.

**Impact**: none today; any future expansion (or a change to one) that reads a stateful register it wrote earlier in a
sequence would silently produce wrong rows in rewound chunks, and proofs that fail verification. **Expected**: store the
registers as of the sequence start in a rewound header, or assert the property when expansions are generated.
