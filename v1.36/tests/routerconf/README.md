# tests/routerconf: the router's conformance fixtures

*For anyone changing the router (`src/units/router.h`): what each file here is, whose it is, and how the RTL's are made
again.* `tests/test_router.cpp` reads them.

| Files | What | Whose |
| --- | --- | --- |
| `rtl/<name>.script.gz` | a script of arrivals at one router, a line a cycle (the format is in `rtl/router_script_test.rs`'s header) | `test_router`'s generator, `ROUTERCONF_GEN=DIR` |
| `rtl/<name>.obs.gz` | what the router did with that script, a line a cycle: each output's register, the eject's loopback mark, the overflow pulse, each DMA input offering and not ready | **the RTL's**: Snarky One's `router` (`rtl/blocks/router`) under Verilator, below |
| `rtl/tables.txt` | every router's routes, tile 0 to 66, as a hash: each input, each src 0 to 66, each dst 0 to 127, the outputs the packet goes to | **the RTL's**, below |
| `rtl/router_script_test.rs`, `rtl/snarky-one.patch` | what makes the RTL's side | below |

## The RTL's fixtures

`test_router` replays each script on the simulator's router and compares its lines with the RTL's, character for
character (`router_conformance_with_the_rtl`, and under the framework's debug options), and sends the same packets as
the RTL's recorder through each of the 67 routers and compares the hashes of their routes (`routes_equal_the_rtls`).

The scripts: ten, of 3,200 cycles each, on a compute tile's router with no attach port (tile 27 twice, tile 8 and tile
63, at the ring's row ends), on each attach tile's (0, 7, 15) and on each special tile's (64 with DMAs on its four
link inputs, 65 on west and north, 66 on east and south: the RTL's models of `rtl/blocks/router/partition.toml`).
Each is random many-to-one load: phases with a packet at each input in 20 %, 50 % and 95 % of the cycles, a lull,
then every input sending to the tile itself in every cycle, then cycles to drain; 45 % of the packets are for the tile
itself, the others broadcasts, reserved ids, special tiles and any compute tile, from any source id. So queues fill and
overflow, the arbiters choose among several waiting queues in most cycles, the loopback queue waits, and a special
tile's DMA inputs are held back (`test_router` prints each script's counts and requires each kind).

A script is only the stimulus: the reference is what the RTL does with it. The random ids put packets at tiles their
routes do not pass, which no link delivers; the RTL's route functions decide those too, and the simulator's follow
them (`src/units/router.cpp`).

**To make them again** (after a change of the scripts, or of the RTL's router):

1. The scripts, from this version's root: `ROUTERCONF_GEN=DIR build/clang-release/test_router` writes
   `DIR/<name>.script`.
2. The RTL's observations, in a scratch worktree of Snarky One (`mise run agent-new <name> shared`, then
   `mise run build`; never committed there): copy `rtl/router_script_test.rs` into `rtl/blocks/router/test/src/`,
   apply `rtl/snarky-one.patch` (it adds the module to `lib.rs`), and run
   `ROUTER_SCRIPTS=DIR mise exec -- cargo --locked --offline test -p blocks_router -- --ignored --nocapture router_scripts`
   (it writes `DIR/<name>.obs` beside each script) and
   `ROUTER_TABLES=DIR/tables.txt mise exec -- cargo --locked --offline test -p blocks_router -- --ignored --nocapture router_tables`.
3. `gzip -n -9` each script and each `.obs` into `rtl/`, copy `tables.txt` there, and name a new script in
   `test_router.cpp`'s `kRtlFixtures`.

These were made on 2026-11-15 from Snarky One at 076e647 (`rtl/blocks/router` as of that commit).
