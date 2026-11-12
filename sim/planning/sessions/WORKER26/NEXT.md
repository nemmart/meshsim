# Parked for WORKER26 (from the integrator, 2026-11-12): send at its next stopping point

1. The endpoint_checks call in Net's sends (Net::step, Net::send) is with W20; add the endpoint sends to CheckSelf when
   it lands.
2. W20 added TileCore::overwrites beside your track_port_b (8abebaed, on main): PhaseLog uses it to count the overwrites
   the cycle's port-B write will cause, on a copy of your tracking. It must stay in step with track_port_b, so if you
   change one, change the other.
3. F4, in your command-slot check (from W22): SearchBench reports 3 violations ref doesn't, "a command landed in command
   slot 0 before the core read the previous one", at cycle 139 on tile 0 and 143 on tiles 8 and 56, on the first
   broadcast command after the boot. Every measured figure matches ref, so the difference is in how the check sees the
   boot ROM's read of slot 0. Reproduce with build/clang-suite/suite_search (MODE=suite). It isn't specific to
   SearchBench: ReadPortGatherTest's "gather from C" group hits the same three (cycle 77 on tile 0, cycle 81 on tiles 8
   and 56). Fix it against ref with a CheckSelf case.
4. From ReadPortGatherTest's "read port from C": the 65th store into a full read-port queue should be reported as
   invariant 8 (ref does); nothing records it yet.
5. AgTest's 8 stream-entry checks: the AG's checkEntry kinds (your 1c).
Order: F4, then the read-port overflow, then the AG kinds, then 1d (once W20's new_phase hook exists).
