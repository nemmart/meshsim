# WORKER4 questions for the integrator

Each item: what, where, my proposed answer (which the code follows, marked `W4-nn` in a comment), and its status.

## W4-01: the sort's suites while the simulator moves to v1.2

**What.** `run_tests.sh` builds and runs the sort (`sw/ext/sort/build.sh`, `tools/check_layout.sh`, SortTest and the
forced-BT runs). The sort is v1.1 code (modes, `PTR[64]` staging, `ebreak` exit, packed controller-memory
addresses) and is not in my scope (`wip/sort/`, `sw/ext/`), so it cannot run once the packet path changes.

**Proposed.** Take the sort's steps out of `run_tests.sh` from phase 1 on, leaving the files untouched, and list them in
REPORT.md under "what the sort port needs". The sort's port puts them back.

**Status:** proposed; the code follows it.

**Update (owner, 2026-10-30):** agreed; the sort's steps report "PENDING (sort port)".

## W4-02: "after boot" for data memory's first 16 bytes (2.2, invariant 22)

**What.** 2.2 and invariant 22 say meshsim flags any write to 0x00000000-0x0000000F "after boot". meshsim has no
natural "boot is over" moment per unit (a unit boots when its first command lands, 6.2), and nothing legitimate ever
writes there: overlays start at 0x10 (D37.4) and the boot ROM writes no data memory.

**Proposed.** Flag every packet write into those bytes, at any time (`TrafficDirector.checkCmem`). A core store there is
flagged in phase 2 the same way.

**Status:** proposed; the code follows it (W4-02 in TrafficDirector).

## W4-03: who may send uncounted writes (3.1, 6.2, invariant 22)

**What.** 3.1 says uncounted traffic is "commands and boot overlays into controller memory", and 4.12 says commands come
only from C&C 1-3. The spec doesn't say whether a unit may send uncounted writes to itself or to another tile outside
those two uses (for example a tile copying an overlay into its own program memory uncounted).

**Proposed.** meshsim flags any uncounted mode-0 write whose source isn't a controller (64-66), and any uncounted
write outside regions 0x00 and 0x01, whatever its source. A tile's own block copies are counted (6.5 says so).

**Status:** proposed; the code follows it (TrafficDirector.checkUncounted).

## W4-04: who owns `wip/sw/REGISTERS.md`

**What.** RegDoc.java generated the register tables in `wip/sw/REGISTERS.md`, which is still the v1.1 document and was
outside my scope.

**Answer (owner, 2026-10-30):** no session owns it now; it is in WORKER4's scope. RegDoc comes back and regenerates it
as the v1.2 document.

**Status:** agreed; done in phase 2.

## W4-05: C&C 1's peripheral's type

**What.** 4.17.1 leaves the engines' types Open (item 82); C&C 1's peripheral's `PARAMS` [7:0] must read something.

**Proposed.** meshsim reads 0 ("nothing to say", 4.17.4) until item 82 assigns it.

**Answer (owner, 2026-10-31; DECISIONS-v1.2 section 46, spec 4.17.1):** 0 none, 1 Ingress/Egress with the UART and the
chip lines (C&C 1's peripheral), 2 Updater/Binder/RW, 3 Tracer, 4 the replayer (type 1 in v1.1).

**Status:** answered; done (Replayer 4, Cnc1Peripheral 1, mesh.h's PTYPE_*); the draft now says 4 too, so the
generated header and the draft agree everywhere they overlap.
