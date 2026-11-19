# What is open in v1.37

*For anyone who needs to trust a result of v1.37, or to pick up its work: what is owed, where the simulator still
differs from the RTL or leaves a rule unchecked, and what this folder still reaches outside itself for. What v1.37
adds is `CHANGES-v1.37.md`; v1.36's open items, which all still hold here, are `OPEN-v1.36.md` (v1.36's page, kept as
it was). What is open in the design is the specification's chapter 26 (`spec/machine-spec.md`).*

## (a) Owed, not holding the freeze

| item | what | where |
|---|---|---|
| **WORKER8's long runs of the link** (Quests 238 to 242, parked) | a large campaign with the fast unit: forged headers, and bits flipped in a sender's memory, each ending in the link starting again and the chip stopping. The cases were searched and tested at small sizes; the evidence for what the link does in service is the searches, 13,000 random runs and 690 million packets, 1,380 resets in a running chain, the specification's review and the proof runs through the link. **The owner: they do not hold the freeze; owed for an idle night** (design notes 39) | spec item 192 (`spec/host-link.md`); `link/README.md`; `planning/updater/sessions/SPEC/FOR-THE-SIMULATOR.md` 28.9, 28.7 |

## (b) The firmware's choices where the specification was silent: now stated

The owner ruled on the build's notes 1 to 8 (design notes 39), and the specification now states each as built. The
last two, from the last record's rework (`planning/updater/sessions/INTEGRATOR/FOR-THE-SPEC.md` 7 and 8), are
in `spec/master-firmware.md` 30.6 and 30.7 and `spec/jolt/tracer.md` 17.9.10: **the master answers the tracer tile's
result only once the block has landed**, the tracer tile serving requests until then; and **the reply waits for
nothing further**, after which the master goes on serving its tiles, the tracer tile's `LOAD` among them. Nothing is
open here. (The rows traced, which the specification now gives as the sum of `RUN_CONFIG.rows` over the segment's
runs, are the firmware's windows run times the segment's one `RUN_CONFIG.rows`: the same number.) `SEND_END` is op
3, the firmware's (item 176).

## (c) Where the simulator differs from the RTL (the specification's own list, 21.5)

- **The RW engine's timing,** behind the AXI arbiter: its bursts reach `lpddr` a cycle after they leave its port, and
  the LPDDR model answers them, not the RTL test's slave (`chip_memory_test.rs`). To be reworked with a real RW engine.
- **A special tile's peripheral DMA** comes in on the router's south input, where the RTL has it on west for the
  memory and tracer tiles (16.4; RCR 3).
- The RTL's stream run (`test_rtl_stream`'s LVDS stream) is gone with v1.36's LVDS stream; the UART run's figures are
  each one cycle later than the RTL chip's (its program turns the UART on, one instruction more).

## (d) Rules with no check, and checks no test fires

From the INTEGRATOR's report, stage 7 (`planning/updater/sessions/INTEGRATOR/REPORT.md`):

| rule | as it stands |
|---|---|
| invariant 36: `WORD_COUNT` the frame's count, so that no run ends inside a code | not checked as such: a wrong count shows as `PAGE_COUNT` not 0 at `done`, or a wrong image (`fw_teeth.sh`'s counts one short and one long are caught so) |
| invariant 39: W beats back to back | not checked, as chapter 20 says |
| invariant 36: words 32–37 and `START` only while idle; `WRITE`s of other sizes to part 2; a free page off LPDDR | checked (FATAL); no test fires them |
| invariant 37: the stale-line check (23.6.11) | on in every run; no test fires it itself (the joint's checks are fired by `joined_teeth.sh`) |
| invariant 39: the arbiter's own burst checks | checked; no test fires them (the LPDDR model's are fired) |

**A test owed** (the owner, 2026-11-19): the master's wait for the block's counter (`wait_counter(CTR_TRACER)` in
`master.c`'s `reply`) stays in the firmware, and no test catches its removal: in every run here the block's one DMA
lands before Egress reads those rows. No test is to be added for it now.

**Planted bugs left unplanted,** because no run's timing can show them: a handshake sent early or not waited for
(`ARMED`, `STREAMS_ARMED`); the reply sent without waiting for the tracer tile's block (its one DMA lands before Egress
reads those rows) (`tests/fw_teeth.sh`). Two of the link's planted bugs do not show across processes and are caught by
`test_ingress_chain` alone (`tests/chain_proc_teeth.sh`'s header).

## (e) What a proof run still takes from its harness

The firmware images; the tracer tile's and the compute tiles' layouts and tables (the decode table, the ROM, the
templates); the Tracer's and the replayer's `RUN_CONFIG`; the windows a segment has and their rows
(`RUNNING-v1.37.md`, "What a run is"). No frame carries N or s: they are the run's arguments. The tracer tile's block
is put in the row before its own record (`tests/segment_bench.cpp` places the record; a `static_assert` keeps that row
free).

## (f) What this folder still reaches outside itself for

Nothing that `make`, `make check`, `./run_tests.sh` or `fw/build.sh` needs: the link's, the wire's and the frames'
gold's sources are copied in (`link/`, `wire/`, `pipeline/frames/gold/`) and held to their bytes at the copy
(`FROM-UPDATER.sha256`, checked by `tools/check_copies.sh`). What still points outside, all of it development
material of the copied code or documents:

- `tools/doccheck.sh` counts a document of `../v1.4_wip/planning/` (the design documents behind what v1.37 adds) as in
  the tree when a page here names it;
- gold's own development checks (`pipeline/frames/gold/build.sh`, with `pipeline/frames/gold/test/frames/guests.sh`
  and `pipeline/frames/gold/test/frames/old_unchanged.sh`) reach `../v1.36/` for v1.36's gold and jumpmap, and
  `pipeline/frames/gold/test/frames/workloads.sh` reaches `../v1.3/` for the published hashes and needs the
  workloads' data. They run in the copy (2026-11-19: the
  three scripts' paths, written for `updater_wip/pipeline/gold`, were brought to this folder when `updater_wip/` was
  removed, and `build.sh` passes here: 80 runs on the guests, 168 byte-identical to v1.36's gold, the packets). The
  simulator's checks of gold are its own (`tests/updater/make_fixtures.sh --check`, the packet vectors).

**The documents' history is in the folder:** the session reports, notes and briefs that the pages here cite as where
a figure or a decision came from are `planning/updater/sessions/`, moved from `updater_wip/planning/` when that folder
was removed (2026-11-19).
