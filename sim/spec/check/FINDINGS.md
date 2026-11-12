# Findings: what looks wrong, contradictory or unclear

*Found while mapping v1.3's spec for v1.35 (`MAP.md`). Nothing here is fixed in the spec; the docs session takes these
to the owner. One line each, with its section; line numbers are `v1.3/spec/machine-spec.md`'s.*

1. **Preamble (24) and section 9 (2942):** "meshsim stops with an error naming the access" was loose already in v1.3: its Java checker counted each violation and reported it, and the run went on (`v1.3/meshsim/src/Chip.java`, `checkInvariants`).
2. **Preamble (16):** `CONVENTIONS.md` and `RECIPES.md` are "being rewritten for v1.2", but both are still headed "v1.2 draft" in v1.3 and in v1.35 (`fw/`), whose content D38 keeps unchanged.
3. **4.19.1, 4.19.2, 4.19.8, 4.19.9 (1315, 1345, 1656, 1669, 1786):** `WORKER11/REPORT.md` and `WORKER20/REPORT.md` don't exist at those paths in v1.3; the reports are `planning/sessions/WORKER11-REPORT.md` and `WORKER20-REPORT.md` (MAP question 2).
4. **Section 10 (3016, 3018) and section 13:** the physical values of `go_latency` and `error_latency` are marked *(Open)*, but only `ready_latency`'s has an item (8).
5. **Item 62 (4.5, 4.7, 6.7):** it waits for "once meshsim models the request bus, the deliveries and the protected loopback", which meshsim has since v1.2; the record never closed it.
6. **8.11's heading (2821):** after the split it still reads "…and the replayer's"; rewording it isn't an allowed edit.
7. **Kept text names workers and session codes** (for example 4.21.10's "B1 Q…", 1291's "(WORKER18)", 2341, item 157's text in history): the docs proposal's rule 3 wants none in reference documents, and this job may not rewrite them.
8. **`fw/include/mesh.h` and `fw/REGISTERS.md`:** headed v1.2 and naming Java generators (`Registers.java`, `RegDoc.java`), though they cover v1.3's registers, the Tracer's included (outside the spec; for the regeneration).
