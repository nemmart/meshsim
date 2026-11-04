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

- **`sw/replay/replay_ctl.c`**: reads the record's 68 words, the bitmap from word 67, sends words 0-65 from offset 0,
  resends the state word before every pass after the first; credits from `RP_HALF_LOG` (128). Layout version 3.
  `build.sh` also builds the 8 KB control (`build/log8k`, `-DHALF_LOG=256`).
- **`mesh.h`** (`src/MeshHeader.java`, both copies regenerated) and the draft header: `RP_HALF_LOG` 128.
- **Recipe 26** (`wip/sw/recipes/rcp_replay.c`) and its prose in `RECIPES.tmpl.md`: the state word's write sets the credits
  and flushes; `RECIPES.md` regenerated. `check_book.py` 47 blocks, 0 differ; `build.sh` all compiled.
- **The kit** (`acceptance/`): `run_shard.sh -q 512` for the control; per-window stall counts in `results.csv` and
  `SUMMARY.txt` (read-log and write-log waits per window); `ConvertDelivery` in the jar.
- **`ConvertDelivery`** (new): a v1.25 delivery to v1.26 in a new directory. The records are rewritten (bitmap to word 67,
  flags into the stream word, [116] set, files 0, words = ceil(bits / 128) checked against each stream file, values out
  from the raw record); every other file is a link; the manifest gets the new hashes.
- **`SyntheticDelivery`**: v1.26 records, random padding bits in every stream.
- **`ReplayDriver`, `ReplayDelivery`, `StageB`, `ReplayShard`, `ReplayWindow`**: 68-word records (end.bin too).

## 3. Tests (local)

| suite | result |
|---|---|
| `ReplayerTest` | 2026 passed, 0 failed (v1.26 groups: the stream word, every underflow [3]-[5] by values and by words, [6] in each form and its exemptions, random padding in all 8 compression combinations, the flush, preloading, a pass without the flush, rd_prev off, words 66-67, the queue sizes) |
| `StageB synthetic` | 19 windows, 0 failed |
| `RecipeTest` | 2056 passed, 0 failed (recipe 26's empty pass now ends on [3], code underflow) |
| `RegDocTest` | 269 passed, 0 failed |
| a v1.25 synthetic delivery (built by main), converted | Stage A (all three variants) and Stage B: every window OK |

## 4. The real deliveries and the acceptance (Quest)

Conversion (item 3 of the work): **converted, not regenerated**. A stream's values out don't depend on its compression, so
each window's raw record gives them (code bits / 32, log bits / 64); its words are its bit length rounded up to 128.
Every task converts WORKER9's `/tmp/w9-deliveries` (manifests checked against 079's and 080's SHA-256) into its own
scratch in seconds, so no task depends on another's leftovers.

| task | what | result |
|---|---|---|
| 097 | smoke: windows 0-63 of both, A and B, F every 16th; B at 8 KB | pending |
| 098 | evm-zone-29: all 8,192 in A and B, F every 64th, 4 KB | pending |
| 099 | cyclone-29: the same | pending |
| 100 | evm-zone-29: all 8,192 in B at 8 KB (the control) | pending |
| 101 | cyclone-29: the same | pending |
| 102 | run_tests quick + full extras, p4/uniform-k128 (expected 1,229,008) | pending |

## 5. Stall counts, 4 KB against 8 KB

Pending (tasks 098-101).
