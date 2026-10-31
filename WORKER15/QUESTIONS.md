# WORKER15 questions

Things that look wrong beyond E1-E8, each with a proposed answer (candidates for E9 and later).

| # | where | what | proposed |
|---|---|---|---|
| Q1 | `spec/machine-spec.md` 4.17.4 ("A peripheral without a DMA leaves words 4, 8 and 9 unused (reserved)") and 4.11 | **D48 isn't in the spec text**, in `v1.2/` or `wip/` (wip's `CHANGES-to-v1.25.md` P7 and C1 say it is). As written, words 8-9 of a DMA-less peripheral are reserved, so a load of them is flagged (4.17.4's access rules), contradicting D48 (a load reads 0, unflagged; only a store flagged). `mtime`'s rate being implementation-defined isn't stated either. CHANGES-to-v1.25 P7 also says words **4**, 8 and 9, where D48 says 8 and 9 | E9: 4.17.4 "words 8 and 9 (`DMA_HDR`, `DMA_ADDR`) read 0 and a load of them is not flagged, only a store; word 4 and `STATUS` [1] as now" (confirm what meshsim does for word 4); 4.11 "`mtime` counts at an implementation-defined rate" (D48). RTL: yes (a load of words 8-9 must not raise anything). Fix in v1.2/ and wip/ together |
