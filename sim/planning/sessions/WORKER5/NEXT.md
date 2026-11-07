# Parked for WORKER5 (from the integrator, 2026-11-07): send at its next stopping point

A new decision from the owner, D21 in sim/planning/DECISIONS.md: nothing machine-specific may be baked in. Your tools
already take --cpus, which is right. Two things:
1. Your Quest task scripts: no hardcoded CPU lists. Take them as arguments or detect them; W4 is writing a shared helper
   (sim/tools/machine.sh), so use it once it's on main.
2. Every speed result records its machine: CPU model, core types and counts, clocks, memory, compiler and versions. Put it
   at the top of each result in REPORT.md and NOTES.md, including the ref comparison.
