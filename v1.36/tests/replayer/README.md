# The replayer's fixtures

What the replayer's tests read, and where each reference comes from. The model, `src/units/replayer.h` and
`replayer.cpp`, is the RTL's `replayer_unit` (Snarky One, `rtl/blocks/replayer_unit`), so its references are the
RTL's wherever a cycle is in them.

| File | What | Whose |
| --- | --- | --- |
| `alu-report.txt`, `alu-sweep.txt` | the opcode table's report and the ALU's sweep, per opcode a hash of 20,000 results (`test_replayer_alu`) | ref's (v1.3's Java model), unchanged: the opcodes' formulas did not change in v1.36 |
| `replayer.log` | `suite_replayer`'s output: the reports, the program pass's line, the checks' count | the suite's own, regenerated in v1.36 for the unit tests' port |
| `<name>.script.gz` | a conformance script (`tests/replayconf.h`): the replayer's inputs, a line a cycle | `replayconf --gen <name>`: the ideal controller's inputs on one of the unit tests' windows |
| `<name>.obs.gz` | the observations of that script, every cycle: `STATUS.busy`, `ERRORS`, `EVENTS`, the words 32–35, each row leaving the row port | **the RTL's**: `replayer_unit` under Verilator, below |
| `rtl/` | what makes the RTL's side | below |

## The conformance scripts

`test_replayconf` replays each script on the model and compares its observations with the RTL's, character for
character; it also checks each fixture's `ERRORS` and row count against the specification's rule for its scenario.
`test_replay_lists` checks the simulator's own account of each error (its lists) on the same scripts.

The scenarios (`replayconf --list`; `tests/replayconf_main.cpp` defines each): the hand-checked program raw, LRU-coded,
without the store log, twice, and cut short; passes of 0 rows; core-fed expansions, at a row offset, cut by the
pass's end, under each rd = x0 policy; a `VirtualPext` with a mask of several runs; a row that is not its expansion's
last; a read-log word after a control transfer, announced and not; each underflow, each leftover, each invalid
expansion; a pass right after an error stop; a queue overrun; a slow consumer on the row port (the output queue
full); a code entry, a read-log entry and an expansion word that come 0 to 19 cycles late, a pass for each delay; a
core that loads `EVENTS` in every cycle.

**To make them again** (after a change of the scenarios, or of the RTL):

1. The scripts, from this folder's version root: `for n in $(build/clang-release/replayconf --list); do
   build/clang-release/replayconf --gen $n > DIR/$n.script; done`.
2. The RTL's observations, in a scratch worktree of Snarky One (`mise run agent-new <name> shared`, then
   `mise run build`; never committed there): copy `rtl/replayer_script_test.rs` into
   `rtl/blocks/replayer_unit/test/src/`, apply `rtl/snarky-one.patch` (it adds the module to `lib.rs`, and the dump
   below to the acceptance), and run
   `REPLAY_SCRIPTS=DIR mise exec -- cargo --locked --offline test -p blocks_replayer_unit -- --ignored --nocapture replay_scripts`:
   it writes `DIR/<name>.obs` beside each script.
3. `gzip -n -9` each into this folder, and name a new scenario in `test_replayconf.cpp`'s lists.

A script made by the model's controller is only the stimulus: the reference is what the RTL does with it. A model
that differed from the RTL in a cycle of any port fails the comparison there.

## The acceptance's reference

`tests/acceptance/rtl/<program>.txt` holds, for every window of a delivery and each record variant, the RTL's pass:
its rows, its cycles and a hash of the cycle each row left the row port (`tests/stage_a.h`, `RtlReference`). It is
made from the RTL's own acceptance, `replayer_acceptance_test` (in no tier of Snarky One; its header says what it
needs), with `rtl/snarky-one.patch` applied:

    REPLAY_DUMP=DIR mise exec -- cargo --locked --offline test -p blocks_replayer_unit -- --ignored --nocapture evm_zone_23
    build/clang-release/rtl_left DIR 128 > tests/acceptance/rtl/evm-zone-23.txt

(`cyclone_22` and 64 for Cyclone.) The synthetic delivery's, `synthetic.txt`, is the same test pointed at
`calibrate.sh`'s delivery (`synthetic_delivery OUT --program synthetic --windows 128 --seed 11`): `MESHSIM_INPUTS`
naming a folder whose `v1261/evm-zone-23` is the delivery's `synthetic/` with the four table files of its `tables/`
beside the windows.
