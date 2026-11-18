# For the simulator: where `main` differs from v1.37's specification

*The worklist for the session that brings v1.37's simulator and firmware to `v1.37_wip/spec/`. Each row: the
section, what the specification says, what `main` has. Kept as the specification is written (branch `v137-spec`);
"`main`" is the commit the row was checked against.*

## The memory system (machine-spec 17.11, 23)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.11, 23.1, 2.1 | the memory system (the cache, the AXI arbiter, the `lpddr` port) is the memory tile's peripheral 1, with a window whose `ERRORS` carries the cache's error | the cache, the arbiter and the LPDDR model are `CacheSystem` (`src/units/cache_system.h`), attached to the Tracer's fill port behind the setting `cache_ctl`, off by default; no window, and the cache's error flag reaches nothing |
| 17.7 | the RW engine is the memory tile's peripheral 2: window 0xFFFF_FC00, region 0x05 | peripheral 1: window 0xFFFF_FA00, region 0x04 (v1.36's) |
| 17.7, 23.10 | the RW engine is client 2 of the arbiter, on the one LPDDR | its own `lpddr` slave and memory (v1.36's); the adapter `RwEngineOnArbiter` exists and is used only by `tests/test_axi_arbiter.cpp` |
| 23.10 | the Updater is client 1 of the arbiter and the cache's look-up port 1 | the real controller runs against stand-ins of its own (WORKER9's second piece, the joint, not done) |

## The router (machine-spec 16)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 16.1, 16.2 | the line between the special tiles: master east ↔ memory west, memory east ↔ tracer west, mesh links with no flow control; a packet for another special tile goes along it by the ring's rule on the difference of the ids | none: a special tile has its attach port alone |
| 16.4 | a special tile's peripheral DMA enters its router on the south input | `chip.cpp`: `dma_inputs` bit 0, the west input, for the memory and tracer tiles; the master tile has no DMA |

## The Updater (machine-spec 17.10)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.10 | the Updater is its controller (`updater_ctl.h`), on the arbiter's client port 1 and the cache's look-up port 1 | the black box (`updater.h`) by default; the controller behind `MachConfig::updater_ctl`, against test pieces (`tests/ctl_bench.h`), not the arbiter and the cache: WORKER9's joint |
| 17.10.4, 23.6.8 | v1.37's master tile frees nothing; the cache needs no discard | as specified |
| 17.10.1 | `EVENTS` packed: `free_page_credit` at [2] | at [3], [2] reading 0 (`updater_top.h`, `updater.h`, the firmware's `host.h`) |
| 17.10.1, 17.10.11 | a bad root or free page address is UB: the simulator stops on it; `ERRORS.lpddr_error` is not set for it | the controller and the black box set `ERRORS.lpddr_error` and stop the run (WORKER6's C12, WORKER3's C10) |
