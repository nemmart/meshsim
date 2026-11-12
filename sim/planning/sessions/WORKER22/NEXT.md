# Parked for WORKER22 (from the integrator, 2026-11-12): send at its next stopping point

A measurement from the docs session, with the owner's agreement, to close spec items 62 and 70. Run the standard
full-size set, engine-full's four data sets (the all-to-alls and the radix sort), on the runner, and record per data
set:
1. the count of drift errors (ERRORS [3]) across all tiles, expected 0. Read it from the register blocks' ERRORS bit 3
   (set by the drift event), and the drift count if the unit keeps one. This doesn't need W26's invariant-3 reporting.
2. the cycles loopback reads spent waiting for tokens: the injection unit's token_wait_cycles (src/units/inject.h),
   summed over the tiles, with the run's length in cycles.
Item 70 asks for confirmation that group runs see no drift with the eject output serving its loopback last; item 62 asks
for the same plus the token-wait figure. Report the table (data set, drift count, token-wait cycles, run cycles) and the
machine (D21). If a drift count isn't 0, that's a finding to report with the tile, cycle and data set, not a closure.
Add it to Quest 184's follow-up or a new task (pull Quest and re-read NUMBERS.md immediately before claiming).
