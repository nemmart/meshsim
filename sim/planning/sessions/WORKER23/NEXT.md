# Parked for WORKER23 (from the integrator, 2026-11-12): send at its next stopping point

W20's whole-chip reset is on main (8fd281d7): Chip::reset(hard) (ref's doReset), Cc::reset(hard) (the queued form: two
waiting cycles, then READY_EXPECT for all 64 tiles), resets() and last_reset_at(), and the reset line in Chip::log().
It matches ref's StateFp across a reset at both seeds. RecipeTest's boot group, CondTest, and CtrlTest's faults are
unblocked; please port them. CondTest is the reset's best end-to-end check. W26's remaining checker messages (CtrlTest's
registers and invariants), W20's request 11 and the AG report still follow.
