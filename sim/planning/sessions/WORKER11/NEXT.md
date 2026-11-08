# Parked for WORKER11 (from the integrator, 2026-11-08): send at its next stopping point

1. D30 (fold in with whatever else is sent next):

   A new rule from the owner, D30 in DECISIONS.md: very close is enough where exact would be costly. The RTL doesn't
   match ref cycle for cycle either, so don't rewrite or restructure large amounts of code to fix a difference of a cycle
   or so. Exactness stays the default check (a difference is a bug until explained), but if reproducing ref exactly would
   make your code markedly more complex, propose a deliberate difference to me with what it changes. Prefer differences no
   reference program reaches, so every check stays exact.

2. W12's g4 commit (dcc0b74) turns the AG and gather on in every tile: io.mem.gather and io.mem.ag set, its read Holds
   into your MemIO as clients 0 and 1, gather_srq_busy, the PTR view, and the STREAM/GROUP links. wfi and regs are
   unchanged everywhere, and EngConf now runs on your table store. W12 also extended your tests/mem_stand.h (the G1 benches
   give the new read Holds and gather_srq_busy an end; requests there are fatal), plus statefp_g1.*; look at both after
   merging. I merge it into main once W12 confirms its TSan run. Then run test_chip and test_mem on main as your
   cross-check, and a2a follows with your P2d.
