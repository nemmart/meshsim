# spec: the machine specification

*For anyone reading the specification: which document holds what, and the order to read them in.*

**v1.37 is v1.36, the RTL's hardware, with the full system around it.** v1.36's platform and core are the
specifications of the RTL (Snarky One), taken with their chapter numbers, names and text; **the RTL is the truth about
the hardware it has built** (`GUIDELINES.md`, section 1), and a change to it is asked of the RTL, marked *Asked of the
RTL* where it is stated. v1.37 adds what the RTL pencilled in or left out, designed here: the host link and its
frames, Ingress/Egress's host side, the Updater, the memory system (the cache, the AXI arbiter, LPDDR's port), the
links between the special tiles, and the master tile's firmware as a contract. **For the cache's controller, the
Updater's controller and the link end, the code is the authority** (owner, 2026-11-18): their sections state what it
does. Where v1.37's simulator and firmware differ from this specification, `updater_wip/planning/sessions/SPEC/
FOR-THE-SIMULATOR.md` lists each difference.

1. `OVERVIEW.md`: what is being built and where it stands. Two pages.
2. `GUIDELINES.md`: the rules the design follows.
3. `machine-spec.md`: **the platform.** Chapters 1–19: the tiles, the packet, region 0xFF, one chapter per module
   (the core's place in the tile, the control, DMA, write, read, request, receive, gather and inject units, the
   scratchpad, the router and the line between the special tiles), the peripherals (17: Ingress/Egress 17.6, the RW
   engine 17.7, the Updater 17.10, the memory system 17.11), the nets and the reset (18) and the parameters (19).
   Chapters 20–27: the invariants the simulator checks (20), the timing summary (21), the rules and figures kept
   from v1.35 (22), the memory system (23: the files, the cache, the AXI arbiter and LPDDR), the budgets (24, 25),
   what is open (26) and the sort (27). Chapters 28–30 are stubs for the documents below. Appendix A: what the RTL's
   implementation holds where the contract leaves a choice.
4. `host-link.md`: **chapter 28, the host link:** the link packet, its checks and the mask, sending again, recovery,
   starting a link again, "stop", the chain of ASICs, and the link end as hardware.
5. `host-frames.md`: **chapter 29, the frames** the host sends, and the reply.
6. `master-firmware.md`: **chapter 30, the master tile's firmware,** as a contract.
7. `core.md`: **the RV32 core's contract**, a document of its own, as it is in the RTL; `machine-spec.md` chapter 6
   states what the machine requires of the core and cites it by section title.
8. `jolt/`: **the Jolt-specific peripherals**: the replayer (`jolt/replayer.md`, 17.8) and the Tracer
   (`jolt/tracer.md`, 17.9), and the replayer's opcode appendix (`jolt/replayer-opcodes.md`). Their section numbers
   are the platform's, and `machine-spec.md` keeps a stub under each.
9. `schedule.md`: the all-to-all's schedule.
10. `CHANGES-v1.36-to-v1.37.md`: **the guide from v1.36:** what changed, unit by unit, each item marked whether it
    touches the RTL; the requests to the RTL; the name table; and where each of v1.36's sections went.
11. `CHANGES-v1.35-to-v1.36.md`: the guide from v1.35, kept as v1.36 had it.

`history/` holds v1.35's specification as it was, what changed in each earlier version and the decisions behind it
(its `README.md`); a reference document never needs it. v1.36's own specification is the frozen `v1.36/spec/`.
