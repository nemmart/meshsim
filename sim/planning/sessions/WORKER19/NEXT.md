# Parked for WORKER19 (from the integrator, 2026-11-11): send at its next stopping point

1. inputs/: yes, commit the two pinned source snapshots (d97ecde's gold and jumpmap for the v1.2 files, eeb64b5's gold
   for the v1.3 file) into pipeline/inputs/, with their origin commits and hashes recorded, and point lib/classes.sh at
   them instead of git archive. Check that the snapshot build equals the git-archive build. The ConvertDelivery piece
   waits for the owner's decision.

2. Step 4: go ahead now. Run step4.sh on main as it stands, rerun the moved checks, build without ref/ and oracle/,
   and push. W20's three new drivers (TSelf, MachSelf, NetSelf and their .sh scripts) are only on sim-w20, in a WIP
   commit with a failing test, so they aren't on main and won't move with yours; W20 moves them itself after merging
   main. Tell me when it's pushed.
