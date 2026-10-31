# Pending for WORKER9's next prompt (integrator, 2026-11-01)

Include in the next message to WORKER9 (it replaces point 2 of the message it already has, about the atomics):

> The goal: every expansion except the inlines and the advice generators fits in the ROM, the atomics (every AMO .W and
> .D, LR, SC) and the system instructions (ECALL and the like) included; the controller feeds only the inlines and the
> advice generators. Report the ROM fill against v1.1's ~508. If it doesn't fit, report how many words short it is and
> which expansions are largest, and the owner will decide between a larger ROM and what to feed. (DECISIONS-v1.25
> section 5.)
