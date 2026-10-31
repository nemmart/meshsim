# Pending for WORKER8's next prompt (integrator, 2026-11-01)

Include in the next message to WORKER8:

> One more step before your branch is ready to merge: WORKER4 has removed the interim loader (v1.2-meshsim, phase 4);
> every core now boots through the boot ROM. Merge v1.2-meshsim and switch the sort to booting the way Mach.boot now
> does (WORKER4's NOTES.md has the steps: broadcast the image, send CMD_JUMP, then GO once every tile in READY_EXPECT is
> READY). Re-run the quick and shuffled suites after the switch, and say in REPORT.md whether the boot change moves any
> cycle counts.
