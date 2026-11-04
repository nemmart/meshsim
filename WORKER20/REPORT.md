# WORKER20 report: meshsim, the software and the acceptance at v1.26

Branch **`v1.26-meshsim`** (from `main`, main merged at c3f243f). Exactly the replayer changes of
`wip/planning/DECISIONS-v1.26.md` items 1-11, following WORKER19's `WORKER19/NOTES.md` (the stream word's layout and the
state word's [116]). Everything is in `wip/meshsim/` unless named.

## 1. The replayer model (`src/Replayer.java`)

- **The record, 68 words** (item 1): [0] state word, [1] stream word, [2-65] the image, [66] files, [67] the template
  bitmap. Part 0 holds 0-65; writes to 66-67 are accepted and ignored while stopped, UB while a pass runs (items 9, 11).
- **The stream word** (item 2, WORKER19's layout): words [15:0]/[31:16]/[47:32], compressed [48]-[50], values out
  [80:64]/[97:81]/[114:98]. One parser and one encoder (`parseStreamWord`, `streamWord`).
- **The state word** (item 8): [115] use the store log, [116] output rd_prev (0: every row's rd_prev is 0); [114:112]
  reserved.
- **4 KB queues** (item 3): 256 entries each, credits of 128. `-Dreplayer.logq=512` keeps v1.25's 8 KB logs for the control.
- **Part 0 is the working state** (item 12): the image is the register file and the state word's pc, `instr_idx` and offset
  are live, changed in place by a pass (no copy at `PULSE_RUN`: v1.25's 64-cycle image copy is gone); after a pass part 0
  holds the end state, so the controller reloads words 0-65 before every pass.
- **Setting the state resets everything** (items 4, 10): the word-0 write flushes the input queues, latches the flags and
  sizes, clears the counters, sets the credits to 2, resets the decompressors. `PULSE_RUN` only starts. Preloading kept
  (item 11).
- **Padding is anything** (item 5): the decoder stops at the values out.
- **Size checks** (items 6, 7): ERRORS [3] code, [4] read-log, [5] write-log underflow (a pop past the values or a decode past
  the words; the pass stops); [6] done with input left over (values not popped, a whole word after the last value, entries
  pushed past the words). The write log is exempt from [5] and [6] without the store log.

Model choices for WORKER19's text (in `REPLAYER-NOTES.md`, "v1.26"):
- the sizes are latched at the state word's write **and again at the stream word's**: state-first loading writes word 1
  after word 0, so a latch at word 0 alone would take the previous window's sizes;
- a pass started without a write to word 0 since the previous pass is flagged;
- the "push in flight at the state write" check can't be made inside the replayer (one in-order landing path); the
  controllers keep the rule by waiting for their refill counters.

## 2. The software and the harnesses

- **`sw/replay/replay_ctl.c`**: reads the record's 68 words for the bitmap (word 67) and the store-log flag; before every
  pass reloads words 0-65 into part 0 from the record in big SRAM (`load_part0`: one 66-word address-generator run, state
  word first; item 12); credits from `RP_HALF_LOG` (128). Layout version 3.
  `build.sh` also builds the 8 KB control (`build/log8k`, `-DHALF_LOG=256`).
- **`mesh.h`** (`src/MeshHeader.java`, both copies regenerated) and the draft header: `RP_HALF_LOG` 128.
- **Recipe 26** (`wip/sw/recipes/rcp_replay.c`) and its prose in `RECIPES.tmpl.md`: the record's words 0-65 are loaded
  before every pass, the state word's write setting the credits and flushing; `RECIPES.md` regenerated. `check_book.py` 47 blocks, 0 differ; `build.sh` all compiled.
- **The kit** (`acceptance/`): `run_shard.sh -q 512` for the control; per-window stall counts in `results.csv` and
  `SUMMARY.txt` (read-log and write-log waits per window); `ConvertDelivery` in the jar.
- **`ConvertDelivery`** (new): a v1.25 delivery to v1.26 in a new directory. The records are rewritten (bitmap to word 67,
  flags into the stream word, [116] set, files 0, words = ceil(bits / 128) checked against each stream file, values out
  from the raw record); every other file is a link; the manifest gets the new hashes.
- **`SyntheticDelivery`**: v1.26 records, random padding bits in every stream.
- **`ReplayDriver`, `ReplayDelivery`, `StageB`, `ReplayShard`, `ReplayWindow`**: 68-word records (end.bin too).

## 3. Tests

Locally: `ReplayerTest` 2034/0 (the v1.26 groups: the stream word, every underflow [3]-[5] by values and by words, [6] in
each form and its exemptions, random padding in all 8 compression combinations, the flush, preloading, a pass without the
flush, rd_prev off, words 66-67, the queue sizes, part 0 as the working state), `StageB synthetic` 19/0, `RecipeTest`
2056/0 (recipe 26's empty pass ends on [3]), `RegDocTest` 269/0, `PeripheralSlotTest` 93/0; the book 47 blocks, 0 differ.

**Quest 102b** (the branch at 0213eef, item 12 in; 102 passed the same before it): run_tests' quick suite **36 of 36 steps**
and its full-mode extras **11 of 11**, every log with 0 failed; **the full-size sort `p4/uniform-k128` 1,229,008 cycles,
unchanged** (DECISIONS-v1.26 s2).

## 4. The real deliveries and the acceptance (Quest)

**Converted, not regenerated** to v1.26 (work item 3). A stream's values out don't depend on its compression, so each
window's raw record gives them (code bits / 32, log bits / 64); its words are its bit length rounded up to 128. Every
acceptance task converts the v1.25 delivery into its own scratch (5.7 s and 2.5 s for the 32,768 records of each program).

**The runner's reboot (2026-11-04)** wiped `/tmp`: WORKER9's deliveries, the selected-windows tarball and the hand-copied
writer input. **Tasks 103, 104** regenerated them with WORKER9's `delivery_writer`, unchanged (jolt-asic-planning
`tracer-standalone-main` 8d546e3, whose tracer/src and tracer/delivery equal 5ba7f0c's: 079/080's 121c271 plus their
imm_signed patch), into **persistent data, `/home/questrunner/data`** (owner; `data_path.txt`), with the selected windows
and `w9-selected-windows.tgz` (SHA-256 `110ce227...`). **Both reproduce WORKER9's deliveries exactly**: per-program
manifests `7142e894...` (evm-zone-29, 8,192 windows, every one proven by the writer's replay, 469 s) and `f8a0622e...`
(cyclone-29, 8,192, 400 s), the root manifest `d80dac9a...`. Every task is safe to re-run after an interruption (staging
and markers for the data; scratch removed first; results only) and reads `$DATA` the same way (`WORKER20/tasks/`); Quest's
PIPELINE.md has a "Persistent data on the runner" section.

**DECISIONS-v1.26's main criterion is met: every window of both workloads at 2^29 matches in stages A, B and F, with the
4 KB queues.**

| task | what | result |
|---|---|---|
| 097b, 097c | smoke: windows 0-63 of both, A, B, F every 16th; B at 8 KB (097c on the item-12 model) | PASS |
| **098** | **evm-zone-29, all 8,192 windows, 4 KB: A, B, F every 64th** | **PASS**: A 8,192/8,192 (536,870,912 rows), B 8,192/8,192, F 128/128 with cycles equal to B; 28.8 min |
| **099** | **cyclone-29, the same** | **PASS**: A 8,192/8,192 (536,870,912 rows), B 8,192/8,192, F 128/128 with cycles equal to B; 28.0 min |
| 100 | evm-zone-29, all 8,192 in B at 8 KB (the control) | PASS, 8,192/8,192 |
| 101 | cyclone-29, the same | PASS, 8,192/8,192 |
| 102b | the suites and p4/uniform-k128 | PASS (section 3) |
| 103, 104 | the deliveries regenerated into persistent data | DONE, reproduced exactly |

Every row byte for byte, every stream's words and values consumed exactly (`consumedExactly`), the end states equal,
no underflow, leftover, violation or CORRUPT file. Stage B runs 3.2116 cycles per row on EVM Zone (v1.25: 3.213) and
3.1105 on Cyclone.

## 5. Stall counts, 4 KB against 8 KB (V3-12; spec item 143)

Stage B (the controller on tile 0, `replay_ctl.c`), every window of both workloads at 2^29; cycles per window, average
(max). 4 KB: tasks 098/099; 8 KB: the controls 100/101 (the same v1.26 model and controller with v1.25's 512-entry logs and
256-entry credits). v1.25: WORKER11's acceptance (Quest 083/084), 8 KB, with v1.25's 64-cycle image copy at the start.

| per window | EVM Zone 4 KB | EVM Zone 8 KB | EVM Zone v1.25 | Cyclone 4 KB | Cyclone 8 KB | Cyclone v1.25 |
|---|---|---|---|---|---|---|
| read-log wait | **39.2** (127) | 39.2 (127) | 59 | **43.5** (127) | 43.5 (127) | 63 |
| write-log wait | **100.0** (255) | 176.2 (383) | 267 | **84.5** (255) | 153.6 (383) | 222 |
| log waits, share of the pass | **0.066 %** | 0.102 % | ~0.15 % | **0.063 %** | 0.097 % | ~0.15 % |
| windows with a read / write wait | 2,716 / 4,752 | 2,716 / 4,984 | | 2,986 / 4,253 | 2,986 / 4,562 | |
| cycles per row | 3.2116 | 3.2128 | 3.213 | 3.1105 | 3.1120 | |
| total pass cycles | 1,724,207,328 | +645,070 (+0.037 %) | | 1,669,928,081 | +811,574 (+0.049 %) | |

(The other stalls are unchanged by the log size: code 160-165, new expansion 7.6 and 509-522, expansion 0-46 per window.)

**The 4 KB queues stall less than 8 KB, and the explanation holds on the full data.** The log waits are the pass's start,
not the logs' density: the controller's first refill run sends the code stream's half, then the read log's, then the
write log's, one 128-bit packet a cycle, so a row that needs a log word early waits for the run to reach it.
- **No wait exceeds the first run's reach,** in any of the 32,768 window-passes: read-log waits at most 127 at both sizes
  (behind one 128-entry code half), write-log waits at most 255 at 4 KB (a code half and a read-log half of 128) and 383 at
  8 KB (128 + 256). Cyclone's densest stretches (0.83 log words per row, spec 4.19.2) never stall past them.
- **Window by window** (`WORKER20/tools/pair.py`): the read-log wait is identical at both sizes in all 16,384 windows;
  the 8 KB write-log wait minus the 4 KB one lies in [0, 128] in every window, exactly 128 in 4,753 (EVM Zone) and 4,259
  (Cyclone) and 0 in 3,208 and 3,630 (windows whose first store comes after the first run has landed). The pass is as
  fast or faster at 4 KB in all 8,192 EVM Zone windows and 8,098 Cyclone windows; in the other 94 the 8 KB pass is faster
  by at most 794 cycles, all of it in new-expansion waits (the controller's timing shifted; their log waits are still
  higher at 8 KB).
- **Against v1.25:** v1.25's larger waits (59/63, 267/222) are the same start-up behind 8 KB halves, with its 64-cycle
  image copy overlapping part of the first refill. At v1.26 the image copy is gone (item 12) and the halves are 128.

So 4 KB queues cost nothing on either workload: the log waits are 0.06-0.07 % of a pass, all at its start; a half-queue
refill (128 entries) always arrives in time afterwards.

## 6. For the merge

- Branch `v1.26-meshsim` (main merged); the changes are in `wip/meshsim/` (model, controller, harnesses, kit, tests,
  `mesh.h`), `wip/sw/` (recipe 26, the book, both `mesh.h` copies and the draft header's `RP_HALF_LOG`) and `WORKER20/`.
- Model choices for the spec, in `REPLAYER-NOTES.md` ("v1.26"): the sizes latched at the stream word's write as well as the
  state word's (state-first loading writes word 1 after word 0); a pass without a state write since the last one is
  flagged; a push "in flight at the state write" can't be seen inside the replayer (one in-order landing path), so the
  controllers keep that rule by waiting for their refill counters.
- The kit (`acceptance/`) and `ConvertDelivery` read and write v1.26 records only; a v1.25 delivery is converted first.
