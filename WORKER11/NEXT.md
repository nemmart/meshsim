# Pending for WORKER11's next prompt (integrator, 2026-11-01)

> W10's spec for the rest of section 8 is on main (d590722; DECISIONS-v1.25 8e). Merge it into s8. These meshsim flags become
> UndefinedBehavior stops: the replayer's bad write (an unmapped offset, a wrong size, a single-ported memory written during a
> pass; ERRORS [0] is now reserved), reserved digit values 5–7, stream or group numbers past the table's end, reserved modes 6–7,
> and the misuses that were "ignored" (a start while the engine runs, PULSE_RUN during a run, PULSE_DMA during a transfer, a
> transfer naming a missing output queue). The bad write needs the most care, since meshsim used to drop it, count it and set [0].
> The queue overrun stays checked. Do these on s8 with the rest. And if not already done: the ROM's two reports use sequence
> numbers 24 and 25 (spec 4.13), not 0 and 1.
