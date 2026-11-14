# What is pending in v1.36: the replayer stage

*For whoever continues v1.36. This is the replayer stage's page (branch `v1.36-replayer`); the platform stage's
branch has a page of the same name with its own items, and the two are to be joined at the merge.* Nothing listed here
was weakened or deleted: a pending test is not built (`pending.mk`; `make pending` lists them), and its sources and
reference stay as they were. An item leaves this page in the commit that ports it.

The replayer's model is the RTL's `replayer_unit` at the block level: its rows, its cycles and every port's
observations equal the RTL's (`tests/replayer/README.md`, `tests/acceptance/README.md`).

| what | why | what closes it |
|---|---|---|
| `test_slotconf` (`tests/slotconf/seed1-5`) | the slot's conformance replays ref's seeded scripts on a slot with the replayer in it; its reference is ref's observations of v1.35's replayer under them (busy, errors, events and queue occupancies cycle by cycle), which the RTL's replayer does not give | the platform stage's slot (17.1-17.3): a conformance of the new slot, whose replayer side is `test_replayconf`'s |
| the replayer's window as the slot presents it: `START` at word 6, `EVENTS` at 3, `EVENTS_PEEK`, `EVENTS_MASK` (17.1) | the slot is the platform's; the model keeps v1.35's calls (`run`, `consume_events`, `events`, `write_word`, `word`) and its own words 32-35 | the merge with the platform stage: `Replayer::W_*` are the only word numbers here |
| the write port's latency in a chip: `accept(t)` presents the packet at `t + RP_LATENCY` | the director's and the slot's links are the platform's (`PERIPHERAL_LATENCY`, 17.3) | the platform stage; `Replayer::write(t, ...)` is the port itself |
| Stage B and Stage F's cycles (`tests/stageb/stageb.log`, the acceptance kit's B and F) | they run v1.35's `replay_ctl.c` on v1.35's tile with the new replayer in its slot: rows and end state pass; the cycles are a mixture | the platform and firmware stages (17.8.21, 17.8.22), then the RTL chip's figures (3.0875 and 3.1249 a row) |
| the row port's handshake | the model's consumer is its sink, ready one cycle in `sink_interval` (1: always, as the chip ties it); no unit reads `replay_row` | the materializer, *Open* (17.8.11) |
| `spec/CHANGES-v1.35-to-v1.36.md` 5.2.6 item 1: the timing rules the specification does not state | the model takes them from the RTL's sources: they are listed in `src/units/replayer.h`'s header and in the stage's report | a ruling: write them down (the guide suggests an appendix), or leave them to the RTL |
