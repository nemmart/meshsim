# Pending for WORKER10's next prompt (integrator, 2026-11-01)

> Merge main into v1.25 again: the v1.2 spec in wip/ changed under you. K's formula is now meshsim's, K >= max(2L + 4,
> L + 4, cm_latency, rp_latency) (W4-08: meshsim's big-SRAM write path measures 2L + 4), and K in hardware is
> implementation-defined (4.7, the summary table, the parameters, invariant 24; DECISIONS-v1.2 section 49). Keep that in
> v1.25 and list it in CHANGES-v1.25.md only if v1.25's text differs from v1.2's.
