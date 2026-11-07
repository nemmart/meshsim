# Parked for WORKER9 (from the integrator, 2026-11-07): send at its next stopping point

Two items, the first from my last reply:

1. C&C stepping every cycle while it waits for the driver's release: that's polling, which D23 rules out, even though
   it's exact. In ref the release happens at a cycle the harness determines, so find which cycle that is.
   - If it's fixed by the run (e.g. the cycle the boot's last READY lands), C&C sleeps with a wake for that cycle, or
     wakes on the READY it already waits for.
   - If the driver decides it between cycles from state, ask W10 (through me) for a between-cycles poke: the driver wakes
     a unit for the next cycle, exactly.
   Either way, no per-cycle step while nothing happens. Also run the full make check (build by build) before I merge your
   next stage.

2. Your stage-0 headers are merged into main, so W8 builds against the same copy. Merge main into sim-w9.
   From W8:
   - It agrees ROUTER_OVF.
   - G1IO now has ControlIO's fields and add_g1 has ControlBuilder's signature; it renames to your names after merging
     main.
   - Its K-line port is now `kline`, since `line` is READY's name in ControlIO.
   - It adopts your net.h as the link word's definition; its own linkword.h moves to a separate namespace.
   - Send-busy: as you said, it ORs your Reg with the core's own put at t.
   wfi needs only G1 and G2, so wfi with every unit of tile 0 every cycle is your joint first full check. regs is checked
   through cycle 824 until G3 lands. Its mode-1 self-sends reach the write engine and big SRAM from 825; that's G3's first
   path.
