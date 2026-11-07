# Parked for WORKER3 (from the integrator, 2026-11-07): send at its next stopping point

W5's controller now matches all 67 goldens on the engine (merged into main). It found two things for you:

1. The debug options are accepted but ignored: shuffle seeds, instance-major order and step-all change nothing yet. W5 broke
   a unit on purpose and it passed all six option sets. That is exactly what the owner made a condition of explicit wakes
   (D20), so make these increment 5's first priority, ahead of the missed-wake detector. Until each option is implemented,
   setting it must be fatal ("option X not implemented"), never silently ignored. Apply that rule to any future option too.

2. run_until(t) stops before cycle t, which the headers don't say. Make it run through cycle t inclusive (so a halt at t is
   reached), document it in config.h, and add a test.

Also coming your way: W5 will profile the engine path on the runner. Its local figure, about 40 ns per unit step for three
small units, looks high, so expect the engine's part of the profile.
