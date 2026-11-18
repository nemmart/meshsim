# For the simulator: where `main` differs from v1.37's specification

*The worklist for the session that brings v1.37's simulator and firmware to `v1.37_wip/spec/`. Each row: the
section, what the specification says, what `main` has. Kept as the specification is written (branch `v137-spec`);
"`main`" is the commit the row was checked against.*

## First: a bug in the cache (WORKER5's review, 2026-11-18; owner: the specification stands)

| section | the specification | `main` (e7243dd) |
| --- | --- | --- |
| 3 (`fill`), 17.9.5, 23.6.4 | **a fill read of a line sees every fill write of it accepted before** (owner): `fill`'s read is not picked while `fill`'s write register holds a write accepted before it, including while that write waits for the miss handler or the slot | **not kept.** The front end's round robin (`cache_ctl.cpp:421`) takes source 4, `fill`'s read, before source 5, its write, when both wait and the pointer is below 4; and a fill write abandoned to wait for the miss handler (`wait_mh`) lets its read hit the old line. **Reproduction** (WORKER5): a look-up keeps the walker busy; a write of line 0 with data 0x2222 is taken, and a read of line 0 the next cycle; the read is answered 0x1111, the old data. No proof run has failed on it: the Tracer's write-backs seldom meet a read of the same line in the window. **The fix** (WORKER5): serve the write register before the read register whenever it is valid; or compare lines, and hold a read whose line equals a waiting write's. Keep the reproduction as a test |

## The memory system (machine-spec 17.11, 23)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.11, 23.1, 2.1 | the memory system (the cache, the AXI arbiter, the `lpddr` port) is the memory tile's peripheral 1, with a window whose `ERRORS` carries the cache's error | the cache, the arbiter and the LPDDR model are `CacheSystem` (`src/units/cache_system.h`), attached to the Tracer's fill port behind the setting `cache_ctl`, off by default; no window, `PARAMS` or `ERRORS`; the cache's error flag, `CacheCtl::out().error`, reaches no tile's error line, and only the proof harness reads it at a run's end (`tests/proof_bench.cpp:210`) (WORKER5) |
| 23.6.10, 23.11, 17.11 | the chip's reset empties the cache and the arbiter | `CacheCtl::reset`, `AxiArbiter::reset` and `LpddrModel::reset` do what the specification says, but `CacheSystem` has no reset and the chip's reset calls none: each is cold once, when the machine is built (WORKER5) |
| 17.7 | the RW engine is the memory tile's peripheral 2: window 0xFFFF_FC00, region 0x05 | peripheral 1: window 0xFFFF_FA00, region 0x04 (v1.36's) |
| 17.7, 23.10 | the RW engine is client 2 of the arbiter, on the one LPDDR | its own `lpddr` slave and memory (v1.36's); the adapter `RwEngineOnArbiter` exists and is used only by `tests/test_axi_arbiter.cpp` |
| 23.10, 17.11 | the clients' ports wired: the Updater client 1 and look-up port 1, the RW engine client 2 | not wired: the Updater is the black box by default, with no AXI port and no look-up port, writing the store directly; its controller runs against stand-ins (the joint, not done); with `cache_ctl` on the arbiter's only client is the cache (`cache_system.h`, `tick`); the RW engine stays on its own slave, its adapter tested on the arbiter alone (WORKER5) |

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
| 17.10.1 | `EVENTS` packed: `free_page_credit` at [2] | at [3], [2] reading 0, in every place, which move together or the comparison with the black box breaks (WORKER6): `updater_top.h` 61 and 163; the black box, `updater.h`; the firmware's `host.h` 61 and `master.c`'s `UPD_EVENT_FREE_PAGE_CREDIT`; the benches, through `Updater::EV_FREE_PAGE_CREDIT` |
| 17.10.1, 17.10.11 | a bad root or free page address is UB: the simulator stops on it; `ERRORS.lpddr_error` is not set for it | the controller and the black box set `ERRORS.lpddr_error` and stop the run (WORKER6's C12, WORKER3's C10): `updater_ctl.h` 408 and 576 to stop the simulator instead |
| 17.10.2, 17.10.11 | a run of values past word index 2³² − 1, and a page at 2²³ or above, are UB: the simulator stops | the index wraps to 0 silently (`updater_ctl.h` 656), and at depth 3 there is no `beyond` check (447, `depth < 3`) (WORKER6) |
| 17.10.1 | the three roots held as 32 bits, bit 31 set UB | as specified; the check of bit 31 is not made |

## The host link (chapter 28)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 28 | the link ends are WORKER8's model (`End`, `Node`), one run of numbers for a link, both stop bits in byte 7, the restart flag, the mask, the restart link packet, recovery, two link ends and four forwarding buffers on an ASIC, `chain_next` | Ingress's black box: 18-bit numbers for each stream, the stop bits as before, no mask, no restart; a bad link packet fatal (`crc_error`, `sequence_error`, `format_error`); no way back to the host; one ASIC. The link end is on `main` since 2026-11-18 (`updater_wip/link/`, `hostlink.h`), with its counts (`End::counts()`, `Node::counts_above()`, `counts_below()`), the cause of an error beside `error` (`made_wrong`, the chip's `format_error`; `not_kept`; `other_end`) and the reset in a running chain in its `make check` (`test_reset.cpp`). It is not yet in the black box |
| 28.14 | a damaged link packet is no error; `link_error` only when a link starts again | the black box's `ERRORS` [1] to [3] for each kind of bad link packet |
| 28.13.2 | `error` sticky until the reset | `hostlink.h`'s `clear_error()` (80, 114) is the model's, not the chip's: the simulator's chip must not call it |
| 28.12, item 183 | a background link packet with data from below is forwarded | so the code does (`node.cpp` 72); no run sends one, as nothing makes one but a forged header |
| 28.9, 28.7 | a false acknowledgement; a link packet made wrong | not in the campaign of 7,519 runs (noise makes neither); covered by 60 random runs with forged headers and the searches; the runner's campaign (Quest 238) is parked |
| 28.13.6, 17.6.3 | two counts: words landed, and words landed and still to land, for the room and the stop bit | the model counts a link packet's words at once in the cycle it is taken (`hostlink.cpp` 38, 119; `node.cpp` 44–45): one count, which is the room's |
| 17.6.8 | Egress offers the link end a link packet of up to 32 words of a frame; the credit comes back as words leave | `Node::push()` takes one link packet of 1 to 32 words on the foreground, `hops` 0, offered only with no forwarded foreground link packet waiting (`hostlink.cpp` 74–86, 167; `node.cpp` 50). Its own queue, `Sizes::send_words`, is 1,024 by default, not 512 (`hostlink.h` 40), and there is no strobe for "taken": a word has left when `Node::room()` grows. The up to 31 words of a frame not yet a link packet are held above `push()` |

## Ingress/Egress (machine-spec 17.6)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.6.1 | `PARAMS.version` 1 | 0 |
| 17.6.1 | the packed numbers: `STATUS` [2], [3]; `EVENTS` [6], [7], [8]; words 64 to 67 | `STATUS` [3], [4] (and v1.36's [2] `stream_last`); `EVENTS` [8], [9]; `EVENTS_MASK` 10 bits, not 9; words 80, 81, 82; no word for `EGRESS_COUNT` |
| 17.6.1 | v1.36's host interface gone: words 64 to 72 as listed, `STATUS.stream_last`, `EVENTS.stream_in`, `stream_out`, and the parameters `STREAM_BYTES`, `STREAM_DEPTH` | still there, with v1.36's stream tests and the RTL images that use them |
| 17.6.9 | the UART redesigned: `UART_TX` 30, `UART_RX` 31, `UART_CONTROL` 42 (0 at the reset, the UART off), the levels and line errors in `STATUS` | v1.36's UART at words 42 to 45, running from the reset at 115,200 baud; every image that prints needs a store to `UART_CONTROL` |
| 17.6.3 | incoming queues of 16 KiB, 1,024 words, "stop" at half | 8 KiB, `QUEUE_WORDS` 512 |
| 17.6.8 | Egress: the outgoing queue in region 0x04 part 1, `EGRESS_COUNT`, `egress_credit`, `egress_error` | none: nothing goes back to the host |
| 17.6.4, 16.4 | Ingress/Egress's DMA on the master tile's south input | on the west input, `FROM_LEFT` (`chip.cpp` 113), as every special tile's DMA (WORKER7) |
| 17.6.14 | the one reset empties the queues and restarts the link | the black box's reset empties both queues, sets the count to 0 and the service levels low, ends "in flight", and rewinds the packet file, read again from its first byte (`link_reset`, `ingress_egress.h` 549); nothing goes to a host (WORKER7) |
| 17.6.3 | a taken link packet's words land a 128-bit word a cycle, each counted in `…_WORDS` as it lands | all of a link packet's words land at once, at the end of the cycle of its last byte (`ingress_egress.h` 616–621): `…_WORDS` jumps by its length, and "a word into an empty queue" comes with the whole link packet (WORKER7) |
| 17.6.15, 19 | `INGRESS_QUEUE_WORDS` a parameter | a constant, `IngressEgress::QUEUE_WORDS` (`ingress_egress.h` 149), not in `MachConfig` (WORKER7) |

## The firmware (chapters 29, 30)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 30.2 | the UART turned on by a store to `UART_CONTROL`, if the image prints | v1.36's UART runs from the reset; no store |
| 30.6 | the tracer tile's result carries its last record to the master tile (owner, MS-2) | `tracer_ctl.c` checks the end state itself and sends no record |
| 30.7, 29.14 | the reply frame, type 16, 72 words, through Egress | none: no Egress, no reply |
| 30.8 | the firmware's checks of a frame's contents, a stop with no `ERRORS` bit | as specified (`master.c` 440–567), its result in `master_results` for the harness |
| 30.8 | an error stops the chip; nothing is recovered | as specified; the handler writes `master_results` for the harness |

## The chapters that gather (2, 3, 17.1, 18, 19, 20, 21)

| section | the specification | `main` (be077a7d) |
| --- | --- | --- |
| 17.1, 17.11 | `PARAMS.type` 6 for the memory system, a window with `ERRORS` [0] `lpddr_error`, sticky, on the memory tile's error line | no such peripheral: `CacheSystem`'s error flag reaches nothing |
| 3, 18 | the chip's ports `link_up`, `link_down` (a byte each way a link cycle) and the pin `chain_next` | one input file of packets, read by the black box |
| 19 | the parameters `INGRESS_QUEUE_WORDS` to `CLIENT_BURSTS` | spread over `MachConfig` and the units' constants; `STREAM_BYTES`, `STREAM_DEPTH` and `UART_RESET_DIVISOR` still there |
| 20 | invariants 35 to 40, each a check: 35 and 36 for Ingress/Egress's and the Updater's UB, 37 by the cache's stale-line check, 38, 39 by the arbiter's and the LPDDR model's checks, 40 by the router's overflow | the Updater's and the arbiter's checks exist (behind their settings); Ingress/Egress's for the new words do not; 38 and 40 have no line or memory system to check |
| 21.5 | the new units' timing | `run_tests.sh`'s references are v1.36's and the proof runs'; none for the link or the chain |
| 18, 17.6.14 | the one reset reaches the link ends: a chip coming out of it says "I have started again" | the black box has no link to restart |
