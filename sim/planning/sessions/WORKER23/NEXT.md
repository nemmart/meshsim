# Parked for WORKER23 (from the integrator, 2026-11-12): send at its next stopping point

1. Two more of W20's pieces are on main (5cefc440): request 11 (undefined behaviour stops the run with ref's exact
   message, e.g. "tile 9 ... misaligned 4-byte load", so CtrlTest's misaligned checks can pass), and Chip::ag_report()
   (ref's agReport, for CtrlTest's all-to-all). With the whole-chip reset (8fd281d7), nothing in your rows waits on W20
   any more, apart from S1's two harness items below.
2. W26's S1 is on main (ad1d0a96): measured as ref measures it, in the check builds, with units::s1_report giving ref's
   S1.report(K) line. W20 is adding MachConfig::measure_s1's wiring and Chip::s1_report() next, so S1Bench can port as
   soon as those land. Run it against v1.3's log then; the docs session asked for S1's result on v1.35.
3. CtrlTest's registers and invariants groups: tell me exactly which of the checker's messages their logs still need,
   for W26 (BusTest is its last item otherwise).
