# spec: the machine specification

*For anyone reading the specification: which document holds what, and the order to read them in.*

1. `OVERVIEW.md`: what is being built and where it stands. Two pages.
2. `GUIDELINES.md`: the rules the design follows.
3. `machine-spec.md`: **the platform**: the tiles, the mesh, control, memory, the request bus, the peripheral slot,
   the registers, the invariants (section 9), the parameters (section 10), and what is open (section 13).
4. `jolt/`: **the Jolt-specific units**, plugged into the peripheral slot: the replayer (`jolt/replayer.md`, 4.19) and the
   Tracer (`jolt/tracer.md`, 4.21), and the replayer's opcode appendix. Their section numbers are the platform's;
   `machine-spec.md` keeps a stub under each.
5. `schedule.md`: the all-to-all's schedule.
6. `CHANGES-v1.3-to-v1.35.md`: v1.35 against v1.3: nothing for the RTL, where v1.3's text went, the simulator's
   deliberate differences, and the checks the spec promises that neither simulator has.

`history/` holds what changed in each earlier version and the decisions behind it (its `README.md`); a reference
document never needs it.
