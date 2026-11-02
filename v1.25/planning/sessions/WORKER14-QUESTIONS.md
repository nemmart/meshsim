# WORKER14 questions

Each item: what, where, my proposed answer (which the code follows, marked `W14-nn` in a comment), and its status.

## W14-01: the controller's router and its link to tile c (4.16, item 118 deferred)

**What.** 4.16 says a controller is a tile instance with its own router, its link to tile *c*'s fifth port its only
link to the mesh, and defers which port the link enters and the routing inside it (item 118).

**Proposed.** The controller tile has the tile's router with the link on one port (meshsim uses the from-left input and
the to-right output). Routing inside it: a packet from its injection point for itself (its own loopback: read-port
results, mode-0 copies to itself) is ejected; any other packet from its injection point, broadcasts included, goes out
on the link (a controller's broadcast is rooted at tile *c* and never delivered to a controller, as today); every packet
from the link is for it and is ejected. The link is one cycle each way (`Config.ccLinkLatency`, default 1), matching
the Java model's holding register into tile *c*'s from-controller route block, so the two can be compared; neither
direction has back-pressure, as 4.16 says of the to-controller output (the from-controller route block takes one
packet a cycle).

**Status:** proposed; carried on with (integrator, 2026-10-31).

## W14-02: how the outside world talks to C&C 1 in meshsim (6.2 step 3; Ingress/Egress deferred)

**What.** 6.2 says the outside world lands C&C 1's overlays and first command, the host through Ingress/Egress on
silicon and the test harness in meshsim. The host interface's words are Deferred (8.14), so nothing says how a packet
from the host enters C&C 1.

**Proposed.** A meshsim-only host port: the harness hands C&C 1's traffic director uncounted mode-0 writes, one per
cycle, as if Ingress delivered them (src 64, so W4-03 holds), in order, so its overlays and then its command land
through port B and "command landed" fires as on hardware. Results go back by the harness reading C&C 1's data memory
(a mailbox, W14-04) and its UART TX, standing in for Egress.

**Status:** proposed; carried on with (integrator, 2026-10-31).

## W14-03: where the tiles' and C&C 2/3's images come from

**What.** C&C 1 sends C&C 2/3 and the tiles their overlays (6.2 steps 4-5), "streamed as broadcasts from C&C 1's big
SRAM" for the tiles; on silicon they come from the host. Big-SRAM data is always counted (6.2), which needs a host
protocol meshsim doesn't have.

**Proposed.** The harness preloads the images into C&C 1's big SRAM by back door before the boot (as if the host had
staged them; memories keep across the reset, 6.1), and passes their addresses and lengths in C&C 1's boot command. C&C 1
streams each with its address generator (mode 0, uncounted, the command as the last stream, recipe 22's way). Only
C&C 1's own overlay and first command go through the host port (W14-02).

**Status:** proposed; carried on with (integrator, 2026-10-31).

## W14-04: C&C 1's firmware and the harness's commands

**What.** C&C 1 has no GO and no READY (6.3), and the spec leaves its firmware's command protocol to the firmware.

**Proposed.** The harness lands commands in C&C 1's command block as C&C 1 does for tiles (arguments in the parity's
block, slot 0 last); C&C 1 waits for them on "command landed" (`EVENTS` [9] in its mask, as the ROM's controller branch
does), servicing requests meanwhile. Commands: boot (the images' places, W14-03), phase (a command word and arguments to
broadcast, `READY_EXPECT`, then GO when all are READY), and exit (`SIM_EXIT`). C&C 1 posts each command's completion
in a mailbox word in its data memory that the harness reads (W14-02).

**Status:** proposed.

## W14-05: a controller's relative d (4.2)

**What.** A stream's d is relative (0-63) or absolute (64-), and meshsim resolves a relative d from the unit's tile:
on a controller, its tile *c* (`AddressGenerator`: `tileOf(me) + d`). So on C&C 1 d = 0 names tile 0, not C&C 1 itself,
and a controller's own block copy (6.5's d = 0) must use its absolute id.

**Proposed.** Keep meshsim's reading (relative to tile *c*: C&C 1's d = 0 is tile 0); a controller copies into its own
memories with d = 64 + *c*. The firmware does so (6.5's sequence with that d). 4.2 could say it.

**Status:** accepted (integrator): relative to tile *c*, own memories by absolute id; 4.2 says so (CL6).

## W14-06: rcp_cc1.c's main deadlocks after the boot (recipe 27's phase contract)

**What.** Recipe 27's `phase()` assumes "the next command was sent right after the last GO": it waits for all READY
first, and READY needs both `done` and a landed command (6.3; the spec's READY line). But `cc1_boot` (recipe 22) ends
with `go(0)` for CMD_STAGE and the data stream, sending no next command, and `main` then enters
`for (;;) phase(1, ...)`. Run on C&C 1 as a core (RecipeTest's cc1 driver), every tile finishes CMD_STAGE and waits in
`wait_for_command()` with no command landed. No tile raises READY, and C&C 1 sleeps in `wait_all_ready()` forever.
`go(0)` turned the watchdog off, so nothing fires either. GOs stop at 2, the boot's and CMD_STAGE's.

**Proposed.** One line in `main` (outside every RECIPE region, so RECIPES.md's text is unchanged): send the first
phase's command before the loop.

```c
    boot_unit_fenced(DST_CC3, &b, 0x01000000);
    command(DST_BROADCAST, 1, 0, 0, 0);             /* the first phase's command: phase() sends each next one after GO */
    for (;;) phase(1, 0, 0, 0, 1000000);
```

Tried locally and reverted: RecipeTest passes, 2,056 checks, with eight phases after the boot. The alternative is to end
`cc1_boot` with the command (a RECIPE region, so the book's text changes).

**Status:** approved (integrator): the first phase's command is the application's choice, not the boot's, so it
belongs in `main`; the book stays as it is. Applied, with a comment that recipe 27's `phase()` expects a command to be
outstanding before it waits. RecipeTest passes, 2,056 checks.

## W14-07: meshsim's "overlay after the command" check on a running controller (6.2)

**What.** `TrafficDirector.checkUncounted` flags an uncounted overlay write that lands while the unit's "command
landed" is set: "after the unit's command and before its start". On a tile, GO clears "command landed". A controller has
no GO: it starts when its command lands (the ROM's jump, 6.2 step 4), and nothing clears the status afterwards. So
recipe fence's second boot of a running C&C 3 (`boot_unit_fenced`, run by rcp_cc1.c's main) was flagged on every
overlay packet, 80 violations.

**Proposed.** On a controller the check holds only until its image runs (`core.firstImageAt` set since the reset).
Done, marked W14-07 in `TrafficDirector`. It changes nothing for tiles.

**Status:** accepted (integrator); listed in REPORT.md's shared simulator changes.
