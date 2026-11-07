# Parked for WORKER3 (from the integrator, 2026-11-07): send at its next stopping point

Three additions to the performance pass:

1. A new decision from the owner, D21 in sim/planning/DECISIONS.md: nothing machine-specific is baked in. Your runner
   tasks take their CPU lists as arguments or from W4's coming helper (sim/tools/machine.sh), and every before/after table
   records its machine: CPU model, core types, clocks, memory, compiler.

2. Inputs from W4's footprint breakdown of the framework mesh (228 KB per chip against W6's 127 KB hand-rolled; details in
   W4's NOTES.md):
   - Port headers are 96 B per Link (LinkPort 56 + Route 40): a target for slimmer headers.
   - A width-0 (event) Link stores a full ring of dummy words; a count would do.
   - Words held twice (a Link's consumer must take each word on arrival, so a router copies it into its own queue) is an
     M2 design item, noted in PLAN-M1.md; don't change it now.
   From W4's Quest 128: the framework mesh runs 5.6 µs per mesh-cycle on one P-core against W6's hand-rolled ~3.0 µs, so
   add W4's mesh to your before/after benchmarks beside W5's bench_controller.

3. Still hold on W4's digest: its E5 needs one more fix commit (port handles left out of the default digest). I'll tell
   you when to merge it.
