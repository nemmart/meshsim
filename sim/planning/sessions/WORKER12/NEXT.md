# Parked for WORKER12 (from the integrator, 2026-11-08): send at its next stopping point

1. Commit and push first next turn, before running anything: the merge, the gather engine with test_gather, and the
   address generator and register-block work behind the g4 flag (off by default, so main's behaviour is unchanged), as a
   work-in-progress commit ("unchecked: gcc debug only"). Then test_chip, the other builds, and fix forward. Commit after
   each piece compiles and passes its own test.

2. The PTR view: W11 has built it on sim-w11 (1c11c2f): TableStore::ptr_view, 256 registers, PTR as it stood at the end
   of t - 1 (before the table store's write-back of t), present only when the AG's ports are. Its test checks the
   same-cycle cases against ref's order. It reaches main with W11's next make-check-clean commit (P2a); I'll tell you
   then. Meanwhile, build the AG's tests against that contract. W11's P2a turns on the gather and AG clients of the read
   queue only once your units have their read ports, so agree with W11 when to switch g4 on.

Then the AG's tests and EngConf.java, as planned.
