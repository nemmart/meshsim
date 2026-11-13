# WORKER26 report (P2's second half: the checker's remaining kinds, S1, BusTest)

Branch `sim-w26`. Everything below is on main except this report's own commit. Details, cases and decisions in
`NOTES.md`; the plan and every ruling are there too.

## What was delivered

**The checker's kinds** (`src/check.h`'s facts, recorded where ref counts each violation; their texts word for word in
`tests/checker.cpp`; each checked line for line against ref through `oracle/ref/CheckSelf.java`, fixture
`tests/mach/checkself.out` regenerated from ref, gcc and clang, debug and suite builds):
- the register-block slot: the core's terminal traps, mret, aliasing, the reserved first 16 bytes, the table window
  (HIST, PTR, the AG running; three SIM_CHECKS-only Wires, the core moved to level 3, accepted), the WFI woken in done,
  the full request push, the peripheral slot's checks (with a core's load of a reserved window word on the core's path),
  the Tracer's violations (ref's texts);
- invariant 8 and `unreadOverwrites` (`TileCore::track`, every build: PhaseLog's count), with the boot-ROM rule at GO
  (W22's F4: a GO Wire to the core);
- the request bus (the root's drops at a reserved or deferred target, W22-13; the receive queue's overflow), the
  endpoints' send checks (`units::endpoint_checks`), C&C's lost receive;
- the read port (its full queue, invariant 8; W22's ReadPortGatherTest), the AG's eight stream-entry checks (AgTest),
  invariants 3 (once a run) and 6 (once a tile);
- invariant 2, the disjoint buffers (1d): `BigSram::new_phase`, its marks and `disjoint_violations`, phases at the
  harness's `Chip::new_phase`, every GO and every counters-done latch (W22's F6; two when both share a cycle), big SRAM
  moved to level 2 in the check builds (accepted);
- the DMA_HDR/DMA_ADDR-mid-transfer case on W20's test peripheral.

**S1**, invariant 24's measurement as ref has it (`src/units/s1.h`): fixed per-tile structures, big SRAM's ordering
point and store, PTR and HIST write-backs, HIST loads, `S1_LATE`, `units::s1_report`; the write engine's puts staged
behind big SRAM's same-cycle removes (ref's child order). W20's overflow case runs with S1 on again.

**BusTest** (C2): `tests/suite_bus.cpp`, equal to v1.3's log ("BusTest: 154 passed, 0 failed"); the runner's step and
`make check` run its debug build (the framework's misuse checks are debug assertions).

**Found on the way**: the checker's within-cycle order (ref is tile-major); W22's F4 and F6; ref's endpoint-text rule;
ref's Net feeds `Net.at` only under `step` (for W20); two conflicts with ref brought back and ruled (D48: size 11 and
the stream cases stay counted; invariant 17's limits, 18 for modes 1-3 and 24's overtaking read aren't v1.3's).

**Every change to the units** records facts only (or carries a check's state under SIM_CHECKS): the protected tests
(test_chip, test_sort, test_t2_harness, test_cc_units, test_golden) unchanged in the suite and release builds at each
step; both level changes shown order-independent (the 16K sort, T2's boot, M1's goldens; plain, shuffle, step-all,
instance-major; both builds).

## What's left

- **Kinds recorded but no case reaches them yet** (NOTES.md keeps the list; check each when a suite does: W23's
  CtrlTest and RecipeTest, W22's RequestBusTest the likeliest): a port-B write as the core reads that word; a test's
  load clearing invariant 8's bit (`TileRef::load`); HIST/PTR table-window busy met by an instruction; a test's HIST
  load (no check, no S1 count); the slot's full post; the C&C 1 and stub region writes; the Tracer's violations; the
  full request push; the receive-queue overflow; the endpoints' sends (after W20's call in Net's sends); the read port's
  size 11 and misaligned destination (UB at the director); a late rotation; S1's HIST loads by instruction and its two
  same-cycle orders reached only by a we-in overflow.
- **With W20** (the harness): all four done (`check_disjoint`, `measure_s1`, `Chip::s1_report`, and
  `units::endpoint_checks` in Net's sends, dated at the next cycle as ref reports them; CheckSelf's endpoint case).
- **W23's CtrlTest** registers and invariants groups will name the messages their logs still need.
- **Side effect, kept noted**: `track.unread_commands` follows the boot-ROM rule only in the check builds.
