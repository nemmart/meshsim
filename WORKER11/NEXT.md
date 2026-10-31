# Pending for WORKER11's next prompt (integrator, 2026-11-01)

> The v1.25 spec is done, on the branch v1.25 (WORKER10): machine-spec.md 4.19, replayer-opcodes.md at 821e1bd, and
> CHANGES-v1.25.md. Check your replayer model against it with the 10-point checklist in WORKER10/REPORT.md (on v1.25), and
> report any disagreement in QUESTIONS.md for WORKER10 to fix in the spec. The reference rows' sign flag is confirmed:
> imm_signed at word 0 bit [86], equal to the expansion word's sign_imm, as you proposed.

> Also, the acceptance plan changed (DECISIONS-v1.25 section 7, revised): WORKER9's delivery writer produces and proves a whole
> 2^29 program in about 17 minutes on one CPU, so one held writer task per program will produce its delivery on the runner box
> and keep it there; your kit's shards read their windows from it instead of generating their own. WORKER9 will tell you the
> path, manifest and checksums. The kit should take the delivery's location as input, check the manifest's checksums for the
> windows it reads, and not assume it can write next to the delivery.
