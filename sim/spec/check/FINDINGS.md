# Findings: what looks wrong, contradictory or unclear

*Found while mapping v1.3's spec for v1.35 (`MAP.md`). Nothing here is fixed in the spec; the docs session takes these
to the owner. One line each, with its section; line numbers are `v1.3/spec/machine-spec.md`'s.*

1. **Preamble (24, 28) and section 9 (2942):** "meshsim stops" was loose already in v1.3: its Java simulator threw on undefined behaviour and counted the invariants (`checkInvariants` in `v1.3/meshsim/src/Chip.java`), the run going on (the owner, 2026-11-12). v1.35 has three mechanisms (`MECHANISMS.md`), which lines 28 and 2942 now state.
2. **Preamble (16):** `CONVENTIONS.md` and `RECIPES.md` are "being rewritten for v1.2", but both are still headed "v1.2 draft" in v1.3 and in v1.35 (`fw/`), whose content D38 keeps unchanged.
3. **4.19.1, 4.19.2, 4.19.8, 4.19.9 (1315, 1345, 1656, 1669, 1786):** `WORKER11/REPORT.md` and `WORKER20/REPORT.md` don't exist at those paths in v1.3; the reports are `planning/sessions/WORKER11-REPORT.md` and `WORKER20-REPORT.md` (MAP question 2).
4. **Section 10 (3016, 3018) and section 13:** the physical values of `go_latency` and `error_latency` are marked *(Open)*, but only `ready_latency`'s has an item (8).
5. **Item 62 (4.5, 4.7, 6.7):** it waits for "once meshsim models the request bus, the deliveries and the protected loopback", which meshsim has since v1.2; the record never closed it.
6. **8.11's heading (2821):** after the split it still reads "…and the replayer's"; rewording it isn't an allowed edit.
7. **Kept text names workers and session codes** (for example 4.21.10's "B1 Q…", 1291's "(WORKER18)", 2341, item 157's text in history): the docs proposal's rule 3 wants none in reference documents, and this job may not rewrite them.
8. **`fw/include/mesh.h` and `fw/REGISTERS.md`:** headed v1.2 and naming Java generators (`Registers.java`, `RegDoc.java`), though they cover v1.3's registers, the Tracer's included (outside the spec; for the regeneration).
9. **Size 11 (219, invariant 5):** the spec calls it undefined ("meshsim errors"); v1.35 counts it and records a fact, but doesn't make it fatal (`director.h:131`, `inject.cpp:49`): an exception to "undefined behaviour is fatal".
10. **4.2 (468, 473; invariant 17):** a misaligned stream source or destination, and a stream with *d* = 126 from a mesh tile ("errors loudly"), are only counted in v1.35, with no fact recorded (`ag.h:328–331`).
11. **Invariants 3 and 6:** counted (`rotations_late`, the `HIST` overflow link) but never recorded as facts: `ROTATION_LATE` and `HIST_OVERFLOW` exist in `check.h` with no record site. **Invariant 22** is the reverse: a fact in the debug and suite builds, not counted in release.
12. **Promised, not found in v1.35** (`MECHANISMS.md`): invariant 2's disjoint-buffer check (`check_disjoint` and `DISJOINT` exist, nothing records it); invariant 8's strict mode (`CMEM_REPLY_OVERWRITE` has no record site); 16's `DMA_HDR`/`DMA_ADDR` change mid-transfer (also 4.17.9, 1249); 17's 1, 3 and 1 requests outstanding, the tag windows and reads during a group run; 18 for modes 1–3 into big SRAM; 24's overtaking read (only the harness's S1 measurement).
13. **Not established in this pass** (`MECHANISMS.md`): the replayer's per-row flags (1433, 1602, 1631; appendix 10, 254, 325, 339), a pass with no state write since (1839), the Tracer's missing `EXPANSION_TEMPLATE` store (2183) and its waiting DMA (2270).
14. **8.6 (2762):** "`PULSE_DMA` while a run is running is ignored (meshsim flags it)", but invariant 26 makes it undefined and v1.35 makes it fatal (`ag.h:145`).
15. **`CHANGES-v1.3-to-v1.35.md`, the drivers' table:** five of the drivers the labels name are "not yet ported (2026-11-12)", a "to come" in a reference document, to settle by the freeze.
