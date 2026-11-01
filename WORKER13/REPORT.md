# WORKER13 report (draft; finished at the end)

## v1.2 errata

Errors in frozen v1.2 itself, beyond the unlisted changes the guide covers, for the integrator to add to
`v1.2/ERRATA.md` (after E1–E6). `v1.2/` was not edited.

| where in `v1.2/` | the error | correct |
|---|---|---|
| `spec/machine-spec.md` 4.12, "A wake consumes nothing": "The load costs 1 or 2 cycles, fixed for the register on every tile (4.11)" | the stale latency rule, as E2 | the load of `EVENTS` costs **2 cycles** (a register-block load; DECISIONS-v1.2 section 45), the same on every unit |
| `spec/machine-spec.md` 4.19 intro: "It is **peripheral 1, type 1**" | the replayer's type, as E1 (E1 names only 4.17.1) | **type 4** (DECISIONS-v1.2 section 46); corrected in `wip/` already (CHANGES-v1.25's 4.19 row) |
| `spec/machine-spec.md` 4.17.4, "A peripheral without a DMA leaves words 4, 8 and 9 unused (reserved)", with 8.1's "reserved and undefined words … read 0 and ignore stores (meshsim flags the access)" | contradicts DECISIONS-v1.2 section 48 (W4-06): words 8 and 9 read 0 and **a load of them is not flagged, only a store**; the handler's 24 reports read them on every window | words 8 and 9 of a peripheral without a DMA read 0; a store to them is flagged by meshsim, a load is not |
| `spec/machine-spec.md` 4.19.11, "The other two conditions leave the replayer running. The other two errors leave the replayer running." | duplicated sentence (editorial) | one sentence: "The other two errors leave the replayer running." (fixed in `wip/`) |
