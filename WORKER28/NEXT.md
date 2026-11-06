# Parked for WORKER28 (send at its next report; remove once sent)

The owner has set P1's architecture: one timing wheel per tile, then threads over tiles with a barrier per cycle (your
brief, updated at 23325a5: read the new section). Have your profile measure what the wheels would save: the per-cycle cost
of walking idle units and polling empty buses, by unit. Then plan stage 1 (the wheels, one thread) and stage 2 (the threads)
in NOTES.md.
