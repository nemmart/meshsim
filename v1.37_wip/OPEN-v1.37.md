# What is open in v1.37

*For anyone who needs to trust a result of v1.37, or to pick up its work: what is owed, where the simulator still
differs from the RTL or leaves a rule unchecked, and what this folder still reaches outside itself for. What v1.37
adds is `CHANGES-v1.37.md`; v1.36's open items, which all still hold here, are `OPEN-v1.36.md` (v1.36's page, kept as
it was). What is open in the design is the specification's chapter 26 (`spec/machine-spec.md`).*

## (a) Owed, not holding the freeze

| item | what | where |
|---|---|---|
| **WORKER8's long runs of the link** (Quests 238 to 242, parked) | a large campaign with the fast unit: forged headers, and bits flipped in a sender's memory, each ending in the link starting again and the chip stopping. The cases were searched and tested at small sizes; the evidence for what the link does in service is the searches, 13,000 random runs and 690 million packets, 1,380 resets in a running chain, the specification's review and the proof runs through the link. **The owner: they do not hold the freeze; owed for an idle night** (design notes 39) | spec item 192 (`spec/host-link.md`); `link/README.md`; `../updater_wip/planning/sessions/SPEC/FOR-THE-SIMULATOR.md` 28.9, 28.7 |

## (b) Where the specification leaves the firmware a choice

The specification's session and the owner have ruled on the build's notes 1 to 6 (design notes 39). Two remain, from
the last record's rework (`../updater_wip/planning/sessions/INTEGRATOR/FOR-THE-SPEC.md` 7 and 8):

| | what the specification says | what the firmware does |
|---|---|---|
| 30.6 | the tracer tile sends its result when its block is ready, before `SEND_END`; it does not say when the master answers it. The answer ends the tracer tile's image (6.5), after which nothing serves `SEND_END` | `master.c` holds the answer until the block has landed; `tracer_ctl.c` serves `SEND_END` while it awaits it |
| 30.7 | "then it is done", once `EGRESS_COUNT` is 0 | the master also takes the tracer tile's `LOAD`, which follows that answer, before it parks, so its count of `LOAD`s is whole (130) for the harness. The reply is not changed by it. `SEND_END` is op 3, the firmware's (item 176) |

## (c) Where the simulator differs from the RTL (the specification's own list, 21.5)

- **The RW engine's timing,** behind the AXI arbiter: its bursts reach `lpddr` a cycle after they leave its port, and
  the LPDDR model answers them, not the RTL test's slave (`chip_memory_test.rs`). To be reworked with a real RW engine.
- **A special tile's peripheral DMA** comes in on the router's south input, where the RTL has it on west for the
  memory and tracer tiles (16.4; RCR 3).
- The RTL's stream run (`test_rtl_stream`'s LVDS stream) is gone with v1.36's LVDS stream; the UART run's figures are
  each one cycle later than the RTL chip's (its program turns the UART on, one instruction more).

## (d) Rules with no check, and checks no test fires

From the INTEGRATOR's report, stage 7 (`../updater_wip/planning/sessions/INTEGRATOR/REPORT.md`):

| rule | as it stands |
|---|---|
| invariant 36: `WORD_COUNT` the frame's count, so that no run ends inside a code | not checked as such: a wrong count shows as `PAGE_COUNT` not 0 at `done`, or a wrong image (`fw_teeth.sh`'s counts one short and one long are caught so) |
| invariant 39: W beats back to back | not checked, as chapter 20 says |
| invariant 36: words 32–37 and `START` only while idle; `WRITE`s of other sizes to part 2; a free page off LPDDR | checked (FATAL); no test fires them |
| invariant 37: the stale-line check (23.6.11) | on in every run; no test fires it itself (the joint's checks are fired by `joined_teeth.sh`) |
| invariant 39: the arbiter's own burst checks | checked; no test fires them (the LPDDR model's are fired) |

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
- gold's own development checks (`pipeline/frames/gold/test/frames/old_unchanged.sh`, `guests.sh`,
  `workloads.sh`) compare it with `../v1.36/` and `../v1.3/`; the simulator's checks of gold are its own
  (`tests/updater/make_fixtures.sh --check`, the packet vectors);
- the link's `Makefile` names WORKER8's notes (`../planning/sessions/WORKER8/HARDWARE.md`) for its documents' target;
- the documents' history: the session reports and briefs of `../updater_wip/planning/sessions/` that the pages here
  cite as where a figure or a decision came from.
