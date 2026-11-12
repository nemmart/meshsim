# Parked for WORKER20 (from the integrator, 2026-11-11): send at its next stopping point

Wave 2 has started (W22 P4, W23 P5, W24 P6, W25 P7b). Their requests for the harness, for you to order with the
checker's remaining kinds (tell me the order):

From W25 (Stage B and the acceptance kit):
1. the replayer on tile 0 only, with every other tile's peripheral 0 (ref's testPeripheral);
2. C&C 1's expect = 1;
3. parked tiles and parkedUntouched;
4. a row hook on the replayer.

From W24 (the sort and its kit): PhaseLog (H3, with the phase namer and the peaks link/eject/weIn/gatherQ/area), the
Config fields Sweep and the kit's cfg.* set, and sim/run_tests.sh.

From W23 (the control suites and recipes):
5. loading and storing through a tile's core from a test;
6. test access to the request bus;
7. a queued soft reset on C&C;
8. wait_until, in the form W23 proposes: the test checks the condition between cycles, and C&C moves on in the next
   cycle. Approved; RecipeTest (W23) and SortTest (W24) both use it, so one form for both.

All four workers add steps to your runner, so landing sim/run_tests.sh early helps everyone. Each worker adds its own
steps and Makefile include line, telling me; I resolve any conflicts at merge.

One more, from W25 (F2): Stage B's detail lines and the kit's results.csv print "meshsim: violations ..." for the
replayer, which needs the replayer's violations recorded as chip violations through the checker. That's yours (the
replayer is an existing unit). W25 ports the replayer's own message texts and lists (its rp_results detail); you record
the chip-level facts. Agree the boundary with W25 through me if it's unclear.

From W22 (P4, NetTest; details in sim-w22's NOTES.md), six needs:
1. Cc::received on the coreless machine;
2. reset_stats;
3. a harness op to put into a tile's send-port hold;
4. the write engine's and GATHER queue's arrival logs;
5. Chip refusing a K below the formula at build, as ref does;
6. the checker kinds NetTest provokes: no-endpoint, stray, size 11, bad packets, invariants 10, 11, 19, 21 and 22.
NetTest is a long suite and the first wave-2 suite to hit the checker hard, so these are worth ordering early.
