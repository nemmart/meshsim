# Parked for WORKER12 (from the integrator, 2026-11-08): send at its next stopping point

1. D30 (fold in with whatever else is sent next):

   A new rule from the owner, D30 in DECISIONS.md: very close is enough where exact would be costly. The RTL doesn't
   match ref cycle for cycle either, so don't rewrite or restructure large amounts of code to fix a difference of a cycle
   or so. Exactness stays the default check (a difference is a bug until explained), but if reproducing ref exactly would
   make your code markedly more complex, propose a deliberate difference to me with what it changes. Prefer differences no
   reference program reaches, so every check stays exact.
