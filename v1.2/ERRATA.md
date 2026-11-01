# v1.2 errata

Known errors and cleanups in **pure v1.2** (this folder; git tag `v1.2-tony`), with their corrections, all applied in
**`../v1.2.1/`**. The RTL engineer's copy is this one, so this list is the answer to "what's changed since my copy"; the full diff
is `v1.2.1/CHANGES-v1.2.1.md`.

## Fixed while the v1.2 simulator was built (before the v1.2.1 errata)

| # | the change | affects the RTL | commit |
|---|---|---|---|
| F1 | Load latencies fixed everywhere: 2 cycles for the register block and the peripheral windows, 1 for the table window and the memories; only `HIST` is loadable through the table window (DECISIONS-v1.2 section 45) | yes | `7f714c8` |
| F2 | One K for every destination, deliberately (the read-port search note in 4.8) | no (note) | `191ca33` |
| F3 | Peripheral types: 0 none, 1 Ingress/Egress, 2 Updater/Binder/RW, 3 Tracer, 4 the replayer (section 46), except 4.17.1 (E1) | yes | `1beb971`, `76c02f6` |
| F4 | K: meshsim's formula 2L + 4; K in hardware implementation-defined (section 49) | yes | `5fb4e44` |

## Errata

| # | where in `v1.2/` | the error | correct | found by, corrected in |
|---|---|---|---|---|
| E1 | `spec/machine-spec.md` 4.17.1, "Presence ... **1 the replayer**; the engines' types are assigned with their designs *(Open, item 82)*" | the replayer's peripheral type is given as 1, contradicting 4.17.4, 4.19 and 8.11, which say 4; the engines' types are called open | **0 none, 1 Ingress/Egress (with the UART and the chip-level lines, C&C 1's peripheral), 2 Updater/Binder/RW (C&C 2's), 3 Tracer (C&C 3's), 4 the replayer**; none are open (`DECISIONS-v1.2.md` section 46) | WORKER13; `wip/spec` 4.17.1 (WORKER10, `b7794e9`) |
| E2 | `spec/machine-spec.md` 8.1, the general rule "**Timing** (4.11): a load takes 1 or 2 cycles, fixed per register" | stale since Tony's latencies | **2 cycles for the register block and the peripheral windows; 1 cycle for the table window and the memories** (data, program, boot ROM), fixed on every access and unit (`DECISIONS-v1.2.md` section 45; 4.11 already says so) | WORKER13; `wip/spec` 8.1 (WORKER10, `b7794e9`) |
| E3 | `spec/machine-spec.md` item 111's text, "register-block and table-window loads take 1 or 2 cycles, implementation-defined" | the same stale rule, in the open items | as E2 (item 111 is superseded by section 45) | integrator |
| E4 | `spec/CHANGES-v1.2.md` | stops at the review's round 3 (decisions through section 44); **sections 45-49 have no entry**, though the spec includes them. Three change the hardware | **45:** load latencies fixed (E2). **46:** the peripheral types (E1). **47:** the waiting rules from the sort port (tables free per memory; polling a landed word only when the earlier packets also land in controller memory; a plain `wfi()` on counters done only when no other counter is armed). **48:** a peripheral without a DMA reads its words 8-9 as 0, a load of them not flagged; `mtime`'s rate implementation-defined. **49:** K in hardware is the implementation's; meshsim's formula is K ≥ max(2L + 4, L + 4, `cm_latency`, `rp_latency`) | WORKER13; covered in `wip/spec/CHANGES-to-v1.25.md` (WORKER13) |
| E5 | `spec/machine-spec.md` sections 11 and 12 (the controller's memories; the timing note) | the 32 KB memories' one-cycle access at 1 GHz is still called "with Tony, item 134" | **confirmed by Tony; item 134 is closed** (`DECISIONS-v1.2.md` section 45; 4.11 already says so) | WORKER13; `wip/spec` (integrator) |
| E6 | `spec/machine-spec.md` section 10, the **K** row: "(4.7; W4-08)" | cites 4.7 (output queue sets), not 4.8 (the traffic director, where K is defined) | **4.8** | WORKER13; `wip/spec` (integrator) |
| E7 | `spec/machine-spec.md` 4.19.10, the replayer's map | **a cleanup, not an error:** the queue ports' ranges are uneven (the two logs share part 2 at 512 KB each), making the decode two-level | one part per queue, each 1 MB: **1 code, 2 read log, 3 write log (0x300000), 4 expansion (0x400000)** | owner; `wip/planning/CLEANUP.md` CL1 |
| E8 | `spec/machine-spec.md` 4.19.10 and the parameters, "decode table 2K × 16 bits" | **a cleanup:** the organization is unstated, and 2K × 16 reads like a shape needing eight banks | an implementation note: built as 256 × 128, one row per packet | owner; `wip/planning/CLEANUP.md` CL2 |
| E9 | `spec/machine-spec.md` 4.17.4 (a peripheral's standard window) and 4.11 | **DECISIONS-v1.2 section 48 isn't in the spec:** words 8-9 of a peripheral without a DMA are called reserved, and `mtime`'s rate isn't stated | **words 8-9 (`DMA_HDR`, `DMA_ADDR`) read 0 on a peripheral without a DMA; a load of them is not flagged, a store is** (the RTL just returns 0, as for any reserved word; the change is meshsim's flag); **`mtime`'s rate is implementation-defined** | WORKER15; `wip/planning/CLEANUP.md` CL5 |

