# spec: the machine specification

*For anyone reading the specification: which document holds what, and the order to read them in.*

**v1.36 is the RTL's hardware, in the RTL's terms.** The platform and the core are the specifications of the RTL
(Snarky One), taken with their chapter numbers, names and text; what meshsim's specification has beyond them (the
invariants, the timing summary, the budgets, the open items, the sort, the rules and figures only v1.35 stated) is
kept, in the same terms.

1. `OVERVIEW.md`: what is being built and where it stands. Two pages.
2. `GUIDELINES.md`: the rules the design follows.
3. `machine-spec.md`: **the platform.** Chapters 1–19: the tiles, the packet, region 0xFF, one chapter per module
   (the core's place in the tile, the control, DMA, write, read, request, receive, gather and inject units, the
   scratchpad, the router), the peripherals (17), the nets and the reset (18) and the parameters (19). Chapters
   20–27: the invariants the simulator checks (20), the timing summary (21), the rules and figures kept from v1.35
   (22), the deferred memory system (23), the budgets (24, 25), what is open (26) and the sort (27). Appendix A:
   what the RTL's implementation holds where the contract leaves a choice.
4. `core.md`: **the RV32 core's contract**, a document of its own, as it is in the RTL; `machine-spec.md` chapter 6
   states what the machine requires of the core and cites it by section title.
5. `jolt/`: **the Jolt-specific peripherals**: the replayer (`jolt/replayer.md`, 17.8) and the Tracer
   (`jolt/tracer.md`, 17.9), and the replayer's opcode appendix (`jolt/replayer-opcodes.md`). The split of v1.35 is
   kept: their section numbers are the platform's, and `machine-spec.md` keeps a stub under each.
6. `schedule.md`: the all-to-all's schedule.
7. `CHANGES-v1.35-to-v1.36.md`: **the guide from v1.35**: what to change in the simulator and the firmware, unit by
   unit; the timing table; three reversals of recorded rulings, for the owner; the errors found in v1.35; the name
   table; and where each of v1.35's sections went.

`history/` holds v1.35's specification as it was, what changed in each earlier version and the decisions behind it
(its `README.md`); a reference document never needs it.
