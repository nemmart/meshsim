# Pending for WORKER9's next prompt (integrator, 2026-11-01)

If WORKER9 hasn't yet had the part-2 decisions and the part-3 go-ahead (CSR option (a); Q8 on hold for WORKER12's counts;
Q9 confirmed with the untraced LD reading through the D-cache; part 3 approved with the C++ functional replay), send those
first. Then:

> Two more for part 3 (DECISIONS-v1.25 section 7): (1) the write log's last-load entry (E[15]) is updated by every load
> row, the new VirtualLoad* rows included; make the encoder do the same. (2) The acceptance runs won't store full
> deliveries: each runner task will generate its shard of windows with your writer, replay them at once and discard them.
> So make the writer able to produce any range of windows on its own, quickly, without first producing the whole program.
> Also: the reference rows carry a sign flag (sign_imm shapes Jolt's 128-bit imm()), proposed at word 0 bit [86]; follow
> the spec when WORKER10 confirms the position.
