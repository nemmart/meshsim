# tests/routerconf/v1.36: the router's observations on v1.36's hardware

*For anyone changing the router: what these files are and when they may be rewritten.*

`tests/routerconf/*.script.gz` and `*.obs.gz` are v1.3's: seeded scripts of arrivals and what ref's `Router.java` did
with them, cycle by cycle. v1.36's router differs from ref's in two rules of the RTL's (machine-spec 16.2, 16.4):

- a packet for a reserved tile id, or for none, is dropped where it is routed, with no error (ref routes it as tile
  `dst mod 64` and counts it at the eject);
- v1.35's DMA source ids 72-74 do not exist: a special tile's DMA sends with the tile's own id (ref routes 72 + c as
  controller c, as a source and as a destination).

Ref's scripts draw ids at random, so each holds such packets, some 3 % of its events, the first within its first 50
cycles; from there on ref's observations are of another router.

| file | what | regenerated because |
|---|---|---|
| `<name>.obs.gz` (10) | `tests/test_router.cpp`'s observation lines of script `<name>` on v1.36's router, all 2,000 cycles | the two rules above |
| `tables.txt` | each tile's hash of the routing functions over every (src, dst) and (port, src) | ids 72-74 are no longer routed as the special tiles |

`test_router` compares each run with ref's observations for the cycles before the script's first such packet (and
prints how many), and the whole run with the file here. The three runs of a controller's router (`t64-s1`, `t65-s2`,
`t66-s3-dma`) have no file here: they drive v1.35's host port and a DMA without 16.4's flow control; the special tile's
router is tested from the specification in `tests/test_special_router.cpp`.

Rewrite these files only for a stated change of the router's hardware (`ROUTERCONF_WRITE=1 ./build/<cc>-release/test_router`),
and say so in the commit. They are the simulator's own output: they hold the router's cycles under regression, they do
not show them correct. Fixtures from the RTL's router (`rtl/blocks/router/test`) are what closes this
(`PENDING-v1.36.md`).
