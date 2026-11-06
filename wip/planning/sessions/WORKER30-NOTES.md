# WORKER30 notes: the audit (v1.3's spec fold-in)

Read at a247a1f. Branch `v1.3-spec-final` from main. Plan agreed with the owner 2026-11-06, with these rulings:
`STAT_STALL_OUTPUT` counts cycles in which either drain scan is stalled on a full output queue; `STAT_STALL_DATA` includes the
wait for the write-back buffer; the spec says advice and expansion waits are in none of the eight (both defaults, the RTL may
refine them); the latency lines (`go_latency`, `ready_latency`, the error line) are WORKER28's, relayed by the owner;
`stage_delay` is 64 everywhere, Q17's text included.

## 1. Q1-Q17 (WORKER22-SPEC-QUESTIONS.md) against the spec

| Q | answer | where it belongs | in the spec at a247a1f? |
|---|---|---|---|
| Q1 | ok only if every entry on the path from the root is owned | 4.21.5 status table | **no** (leaf's owned bit only) |
| Q2 | status 3 = line beyond the depth's capacity, line >= 2^(5 + 10 depth) | 4.21.5, 4.21.8 [9] | **no** ("beyond the file's length") |
| Q3 | C&C 3 sends window 0's start to tile 0 from its copy | 4.21.3 loop; new 4.21.10 | **no** |
| Q4 | C&C 3 forwards window 63's words 1, 67 to tile 63 | 4.21.3 loop, 4.21.6; 4.21.10 | **no** (all 68 to its own memory only) |
| Q5 | src 74, the down port, sources only | 4.17.9, section 2 | yes (4.17.9) |
| Q6 | misaligned and out of range: [10] and [12] together; [8], [9] only if the access goes ahead | 4.21.8 | **no** |
| Q7 | a zero-row run inside an expansion writes no code value | 4.21.6 | **no** (implied by 4.21.3 only) |
| Q8 | moot (the shared cache) | - | - |
| Q9 | 8 K (item 149, decided 11-06) | 4.21.5, 13 | yes; CHANGES-v1.3 says Open |
| Q10 | frame 6's row fixed | DECISIONS only | - |
| timing | the shared cache: 512 KB, 8-way x 512 sets, LRU, write-back, by LPDDR address, data and index lines; no TLB; hit 6, miss 80; one request outstanding; cold at a segment's start | 4.21.9, 7 | **no** (4.21.9's "requested since the word-0 write") |
| Q11 | routing by dst; dst = the router's own unit ejected there | 4.17.9 | **no** |
| Q12 | next victim kept free or clean; 4-line write buffer, 1 line / 80; a read miss waits for the buffer to empty | 7 (the model's black box), 4.21.9 | **no** |
| Q13 | a write-back missing the shared cache allocates without an LPDDR read | 7, 4.21.9 | **no** |
| Q14 | on loading a record C&C 3 drops any expansion it was feeding | 4.21.3 loop, 4.21.7 | **no** |
| Q15 | the record's pieces armed at boot (66 state, 2 totals); per-window handshake (totals, "armed", streams); segment barriers | 4.21.6, 4.21.10, 6.3 | **no** |
| Q16 | reversed: all tiles replay together at the final GO | 4.21.10, 6.3 | **no** |
| Q17 | READY unarmed; GO; arm on waking; nothing before stage_delay; READY at counters 0; final GO | 4.21.10, 6.3 | **no** |
| CC_LINK_OUT | the down port | meshsim only | - |

## 2. The 11-05/11-06 decisions that concern the Tracer

| decision | in the spec? | to do |
|---|---|---|
| the stance (11-06): defaults, observable behaviour | no | front matter and 4.21's opening |
| 142 region decode: Tony's | section 13 yes | nothing in 4.21 |
| 148 statuses frozen as the default | section 13 yes; 4.21.5, 4.21.8, 25-ref "provisional" | provisional -> default; status 3 per Q2 |
| 149 8 K | yes | CHANGES row |
| 157 words 44-51 | 13, 8.16 yes; **4.21.3 says reserved, open** | 4.21.3 table and text; mesh.h; model |
| stage_delay 64 | 6.2, 10 yes | Q17's text, WORKER22-REPORT, tracer_layout.h comment (below) |
| word 66's I/O-region field | 4.21.2, 4.19.2, 10 yes | CHANGES row (says `lowest_address` / 4 KB) |
| end at any JAL imm 0 | yes | - |
| misalignment (150) | yes | - |
| range comparator | yes | - |
| stores into code | yes | - |
| D13, D14 | **no**; and **the universal tables still follow jolt-cpp**: LR/SC rd0 = 0 (exempt), every NOP-policy template rd0 = 1 (virtual), only ADDI's 3 (decoded from rom.bin) | a gap: Proposed, section 13 (below) |
| frame 7 (155) | yes | - |
| item 152, 145-147, 151, 156 | yes | - |

## 3. stage_delay = 32 still written (outside frozen folders)

Current documents fixed here: WORKER22-SPEC-QUESTIONS.md Q17, WORKER22-REPORT.md (line 139), `meshsim/sw/tracer/tracer_layout.h`'s
comment. Historical records (CHANGES-v1.1, DECISIONS-v1.2's history, planning notes, reviews, old session reports, deliverables)
are left as history. **Code constants at 32, not mine to change (asked):** `Config.stageDelay` (meshsim), `rcp_cc1.c`'s
`STAGE_DELAY`, RecipeTest's `waitCycles(32)`, RECIPES.md's copy.

## 4. Gaps found (to propose)

- **G1, D13/D14 and the tables.** The ruling follows a16z; gold does; the universal tables (`wip/tables/replayer/`, from
  jolt-cpp 821e1bd) don't, so the Tracer and the replayer would produce jolt-cpp's rows for an rd = x0 rewrite (virtual) and
  LR/SC with rd = x0 (unremapped). Neither workload has either. Proposed: regenerate the tables to a16z's rule (LR/SC remap;
  the rewrite row not virtual for every NOP-policy template); no hardware change.
- **G2, the counters while stopped.** Item 157 counts while `STATUS` [0] is 1; after a stopping error it stays 1, so
  `STAT_CYCLES` keeps counting. Taken as written.

## 5. Done (see REPORT.md)

Every row of sections 1-2 above marked "no" is now in the spec (branch v1.3-spec-final, bf36fef). Owner 2026-11-06: item 158
decided (tables after the freeze, no RTL effect); stage_delay's code constants are WORKER28's; the counter catch-up note relayed.
