# What v1.37 adds to v1.36

*For anyone who knows v1.36: what this folder has that v1.36 does not, and where each piece is specified.* In
progress; it grows with the work (`../updater_wip/planning/sessions/WORKER3/NOTES.md` has the plan).

**The specification is not changed** (`spec/` is v1.36's). What v1.37 adds is specified by the design documents of
`../v1.4_wip/planning/`: `INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`, `HOST-FRAMES.md` and `HOST-LINK.md`.

**v1.36's machine is unchanged.** The additions are a different master tile, built only when a machine asks for it
(`MachConfig::host_link`, off by default).

| what | where | specified by | status |
|---|---|---|---|
| the switch: a machine with the v1.37 master tile (`MachConfig::host_link`, setting `hostLink`; off by default; needs the special tiles and one domain) | `tests/mach.h`, `tests/mach.cpp`; `src/units/tile_ctl.h` (`G1Config::host_link`), `tile_ctl.cpp`; `src/units/periph_slot.h` (`PeriphSlotConfig::host_link`) | this file | built; with it off v1.36's machine is built as before, and every v1.36 test passes unchanged |
| Ingress's host link, a black box behind its registers: the link's packets read from a file and checked (header CRC, fields, data checksum, number), two streams of four packets, `STATUS` [3] and [4], `EVENTS` [8] and [9], `ERRORS` [1] to [3], words 80 and 81, the slot's DMA with `DMA_HDR.queue` | `src/units/host_link.h` (the reader and CRC-32C), `src/units/ingress_egress.h` | `INGRESS-INTERFACE.md`, `HOST-LINK.md` | built; `tests/test_ingress_link.cpp` (32 tests), `tests/test_packet_vectors.cpp` (WORKER2's vectors) |
| the master tile's DMA input on its router, for Ingress's DMA | `src/units/tile_ctl.cpp` (`peripheral_dmas` 1 on the master tile with the switch) | `INGRESS-INTERFACE.md` | built |
| the Updater, peripheral type 5, the master tile's peripheral 2, a black box behind its registers: the run's block 2, the segment queue and the free page queue with their credits, update words raw and LRU, copy-on-write, the Tracer's copy, the root made empty or copied, the errors, the statistics | `src/units/updater.h`, `updater.cpp`; `src/units/peripheral.h` (`PT_UPDATER`); `src/units/periph_slot.h`, `periph_slot.cpp` | `UPDATER-INTERFACE.md`; `HOST-FRAMES.md` (update words, the LRU code) | built; `tests/test_updater.cpp` (19 tests, three of them gold's frame files against gold's images), `tests/updater_frames_main.cpp` (a frame file against gold's `frames-images`) |
| the file store places a page where it is told: pages by address beside the pages it hands out, whole pages read and written | `src/units/file_store.h`, `file_store.cpp` | `UPDATER-INTERFACE.md` (the firmware owns the page map) | built; `tests/test_file_store.cpp` (7 tests); the old interface and its addresses unchanged |
| the Tracer and the Updater on one store: the tracer tile's memory system on the file store the Updater writes | `tests/mach.cpp` (with the switch) | `UPDATER-INTERFACE.md` | built |
| the frames read by the tests, and the proof's sequence of runs played on the Updater alone | `tests/frames.h`, `frames.cpp`; `tests/updater_bench.h`, `updater_bench.cpp` | `HOST-FRAMES.md`; `UPDATER-INTERFACE.md`, "The runs" | built |
| a provisional packet writer for the tests, and a tool around it (`packetize`); gold's packetizer (WORKER2) is the reference | `tests/packet_writer.h`, `tests/packetize_main.cpp` | `HOST-LINK.md` | built; its output equals gold's packet files byte for byte |
| the firmware's header for the host link and the Updater (hand-written beside the generated `mesh.h`, which stays v1.36's) | `fw/include/host.h` | `INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`, `HOST-FRAMES.md` | built; used from Stage 2 |
| the master tile's program: the page map, the frame loop, the proof's sequence, the hand-over to the tracer tile | `fw/cc/master.c` | `UPDATER-INTERFACE.md`, "The runs"; `HOST-FRAMES.md` | Stage 2 |
| the whole chip from a packet file | `tests/` | the brief's Stage 3 | Stage 3 |
