# Pending for WORKER9's next prompt (integrator, 2026-11-01)

If WORKER9 hasn't yet had the part-2 decisions and the part-3 go-ahead (CSR option (a); Q8 resolved: keep the single Fermat loop (WORKER12's counts: EVM Zone's worst proof is about 15 ms against a 100 ms Tracer budget; the addition chain is candidate C4, DECISIONS-v1.3 section 11);
Q9 confirmed with the untraced LD reading through the D-cache; part 3 approved with the C++ functional replay), send those
first. Then:

> Two more for part 3 (DECISIONS-v1.25 section 7): (1) the write log's last-load entry (E[15]) is updated by every load
> row, the new VirtualLoad* rows included; make the encoder do the same. (2) The acceptance runs won't store full
> deliveries: each runner task will generate its shard of windows with your writer, replay them at once and discard them.
> So make the writer able to produce any range of windows on its own, quickly, without first producing the whole program.
> Also: the reference rows carry a sign flag, **`imm_signed` at word 0 bit [86]**, equal to the expansion word's
> `sign_imm` (sign_imm shapes Jolt's 128-bit imm()). WORKER10 has confirmed it in the v1.25 spec (branch v1.25,
> 4.19; its WORKER10/REPORT.md states the layout exactly); the writer must set it.
