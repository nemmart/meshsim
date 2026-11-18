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

## The host link (chapter 28)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 28 | the link ends are WORKER8's model (`End`, `Node`), one run of numbers for a link, both stop bits in byte 7, the restart flag, the mask, the restart link packet, recovery, two link ends and four forwarding buffers on an ASIC, `chain_next` | Ingress's black box: 18-bit numbers for each stream, the stop bits as before, no mask, no restart; a bad link packet fatal (`crc_error`, `sequence_error`, `format_error`); no way back to the host; one ASIC. The link end is on `updater-w8`, not merged |
| 28.14 | a damaged link packet is no error; `link_error` only when a link starts again | the black box's `ERRORS` [1] to [3] for each kind of bad link packet |

## Ingress/Egress (machine-spec 17.6)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.6.1 | `PARAMS.version` 1 | 0 |
| 17.6.1 | the packed numbers: `STATUS` [2], [3]; `EVENTS` [6], [7], [8]; words 64 to 67 | `STATUS` [3], [4]; `EVENTS` [8], [9]; words 80, 81, 82; no word for `EGRESS_COUNT` |
| 17.6.1 | v1.36's host interface gone: words 64 to 72 as listed, `STATUS.stream_last`, `EVENTS.stream_in`, `stream_out`, and the parameters `STREAM_BYTES`, `STREAM_DEPTH` | still there, with v1.36's stream tests and the RTL images that use them |
| 17.6.9 | the UART redesigned: `UART_TX` 30, `UART_RX` 31, `UART_CONTROL` 42 (0 at the reset, the UART off), the levels and line errors in `STATUS` | v1.36's UART at words 42 to 45, running from the reset at 115,200 baud; every image that prints needs a store to `UART_CONTROL` |
| 17.6.3 | incoming queues of 16 KiB, 1,024 words, "stop" at half | 8 KiB, `QUEUE_WORDS` 512 |
| 17.6.8 | Egress: the outgoing queue in region 0x04 part 1, `EGRESS_COUNT`, `egress_credit`, `egress_error` | none: nothing goes back to the host |
| 17.6.4, 16.4 | Ingress/Egress's DMA on the master tile's south input | the black box's DMA (check its input) |
| 17.6.14 | the one reset empties the queues and restarts the link | the black box's queues: check what its reset does |

## The firmware (chapters 29, 30)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 30.1 | *N* and *s* are the master tile's image's arguments (S5, pending) | two words placed with the image (`system_config`) |
| 30.2 | the UART turned on by a store to `UART_CONTROL`, if the image prints | v1.36's UART runs from the reset; no store |
| 30.6 | the tracer tile's result carries its last record to the master tile (owner, MS-2) | `tracer_ctl.c` checks the end state itself and sends no record |
| 30.7, 29.14 | the reply frame, type 16, 72 words, through Egress | none: no Egress, no reply |
| 30.3 | an unknown frame type is dropped with one discard count | `master.c` ends in its handler (`MR_FRAME`) |
| 30.8 | an error stops the chip; nothing is recovered | as specified; the handler writes `master_results` for the harness |
