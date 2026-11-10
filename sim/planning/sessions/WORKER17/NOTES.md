# WORKER17 notes (v1.35: all 128 segments, back to back)

Branch `sim-w17`. Prompt: `PROMPT.md`. Plan agreed with the owner 2026-11-10 (go-ahead with Q1-Q3 answered, below).

## What carries between segments (from `ref`)

`TracerT2.main` carries the one memory by T1's path and runs each chosen segment's chip on `g.fs.copy()` (addresses
kept: `FileStore.copy` clones every page and `nextFree`); `M3Seg` takes W13's snapshot the same way. So:

- **Carried: the FileStore only** (the image, the map, every page created so far, `nextFree`).
- **At each segment's start** (T1's harness): `setOwnership(img, pages of the segment's update list)` on the carried
  store (frames 4/5 of `segments.seg`; T1 checks them equal to `seg/NNN.pages`). W13's snapshot is taken after it.
- **At each segment's end** (T1's harness): `writeBackDirtyUntimed()`. `ref`'s T2 chip never does it (its snapshot is
  thrown away), so the chain must.
- **Fresh every segment**: the chip (`new Mach`), its Tracer, a cold memory system over the store (`new
  MemorySystem(fs)`, `onStateWrite = cold`), the landings, the boot, CC_SEGMENT. T1's other edits (cache cold, stall
  counters cleared) are implied by the fresh Tracer and memory system.
- **The Tracer's record is not carried**: it is landed from frame 2 (with frame 3's advice), and C&C 3 checks its end
  state against the next segment's frame 2 (`tr_results` status 1). Segment 127 has none (`h[8] = 0`), as `ref`.

## The plan

1. **Inputs in C++** (`sim/tests/`, configuration code): `loadW26` (`proof.json`, `initial.pages`, `frame6.bin` and
   `map.lines`, `seg/NNN.pages`) into the Tracer's FileStore with `ref`'s allocation order, and `readSegments` (frames
   2-5, frame 5's LRU coding). The landings per segment as `TracerT2.chip`: C&C 3's `tracer_layout.h` (tables, index,
   templates, record, advice, expected end state, header) and every tile's `replay_layout.h`.
   **Check:** segment 10's landings equal W13's `*.sram.gz` byte for byte.
2. **The chain**: for segments a..b: ownership, build, land, boot, CC_SEGMENT, the checks, write back the dirty lines,
   carry the store on.
3. **Checks on the carry**:
   - every segment's start memory against `ref`'s T1 carry (Q1: a `ref` driver prints `filesScan` at every segment's
     start, after the ownership set, all 128, one runner task);
   - segment 10's start equal to W13's `*.files.gz` byte for byte;
   - at every segment's end, T1's own: every cell of the update list, the cumulative shadow, `seg/NNN.pages` equal to
     the update list's pages.
4. **T2's acceptance, everything `ref`'s T2 checks** (Q3): per tile the delivered state word and registers against
   `state.bin`, the delivered streams against `code.bin`, `rlog.bin`, `wlog.bin` (raw, value for value), every tile
   exits 1, rows equal `rows.bin` (count and hash), the replayer clean; C&C 3 exits, `tr_results` status 1 and its
   windows; no violations; plus `lag_cycles` 0. Reading the delivery and hashing happen outside the timed part.
5. **The driver**: `t2` extended (`--w26`, `--delivery`, `--chain a-b`): a line per segment (each check, C&C 3's exit
   and end cycles, setup / boot / segment / check times) and the totals; one thread for timing (D26).
6. **Runner tasks**: (a) `ref`'s T1-carry hashes, both workloads; (b) chain 0-10, both workloads: landings and
   snapshot compared, `--fp-points` at segment 10 against golden-m3 (Q2: the fingerprint only here); (c) the full
   0-127, the two workloads as independent chains side by side, CPUs from `machine.sh`, the machine recorded (D21).
7. **`make check`**: the parsers, the landing builder and the carry steps on synthetic inputs written in the formats
   (the runner's data isn't local).

## The owner's go-ahead (2026-11-10)

- Q1 **yes**: the `ref` driver for T1's carry hashes, all 128 segments, one task; the chain checked against it at every
  segment.
- Q2 **yes**: the fingerprint check in the separate 0-10 run; the full runs timed clean.
- Q3 **include** `ref`'s T2 checks of each tile's delivered state and streams, outside the timed part. "Every segment
  passes T2's acceptance check" means everything `ref`'s T2 checks.
- Commit after each piece that passes; pull Quest and re-read `NUMBERS.md` immediately before claiming each number.

## Log

- 2026-11-10: plan agreed; branch `sim-w17` from `main` at 1ebe9ab.
- 2026-11-10: **`T1Carry`** (`sim/tests/ref`; M3Inputs' dumps into any stream) and the ref task maker. **Quest 172**
  (eb09b0f, all PASS, one attempt): T1's carry through all 128 segments of both workloads (111 s / 100 s, not timing);
  T1's end state equal to every next frame 2 and `seg/NNN.pages` to every update list; **segment 10's start and
  landings equal W13's `*.files.gz` and `*.sram.gz` byte for byte** (so the oracle's form is W13's). Committed as
  `sim/tests/golden-chain/` (every start; the landings of 0, 1, 10, 126, 127). Figures: evm-zone-29 image 496 pages,
  map 2^13 lines, log2 K 23, nextFree 26,016 at 0 to 28,736 at 127; cyclone-29 1,519 pages, log2 K 24. Segment 127
  writes nothing (no updates, no owned pages) and has no next frame 2.
- 2026-11-10: owner: go ahead in this order (172 into notes, C++ inputs and landings with synthetic tests, the
  driver, the 0-10 run, the full runs); exit and end from `sim_exit_at`, confirmed against W16's segment 10 first;
  the store kept outside the Tracer (no unit change).
- 2026-11-10: **the chain's inputs and landings in C++** (`tests/chain.h/.cpp`, `tests/chain.mk`, one `include` line
  in `sim/Makefile`): a minimal JSON reader; `load_w26` (ref's allocation order: the image create(2) and its pages,
  then the map; loadW26's checks); `read_segments` (frames 2-5, frame 5's LRU coding) and a writer for tests; the
  landings (TracerT2.chip, as M3Inputs.sram's list); W13's two stream forms; the oracle's files; the manifest's
  windows with files; stream_values. `test_chain` (7 tests, synthetic inputs and the real replayer tables) passes on
  gcc and clang, debug and release. Equality with ref's landings and starts is for the runner (golden-chain).
- 2026-11-10: **the chain driver** (`tests/chain_main.cpp`, `build/<cc>-<mode>/chain`): per segment the ownership
  set, the start against the oracle, the landings (the tiles' and C&C 3's tables made once: setup 0.17 s), a fresh
  one-domain chip with C&C 3's Tracer on the carried store through a fresh MemSystem (the store stays outside the
  Tracer: no unit change), boot, CC_SEGMENT, the run in 250,000-cycle chunks under ref's stop rules, every check of
  TracerT2.chip (state.bin, the three streams, exits, rows, clean) plus violations and lag_cycles, then the write-back
  and T1's end checks; exit and end from `sim_exit_at`; `--dump S` writes W13's forms for the t2 tool. Built on clang
  and gcc; on synthetic inputs (2 windows) a whole segment runs (status 1), clean under ASan/UBSan. The C++ task maker
  (`tasks/mk_task.sh`, W16's with the replayer tables and golden-chain) and `chain10-body.sh`.
- 2026-11-10, **Quest 173** (sim-w17 85001fb; all 18 verdicts PASS, one attempt; not timing, the two chains in
  parallel): **the chain on segments 0-10 of both workloads, every segment OK**: every start memory equal to ref's
  (golden-chain), the landings of 0, 1, 10 equal to ref's; every TracerT2 check (states, streams, exits, rows 64 of 64,
  clean 64, status 1, no violation, lag 0) and T1's end checks. **Segment 10's start and landings equal W13's dumps
  byte for byte**, and **W16's t2 on the chain's own segment-10 inputs equals golden-m3 at every point** (evm 241 of
  241, cyclone 285 of 285; one domain and the 6-domain split; C&C 3's exit and the end at W16's labels). Segment 10's
  figures equal Quest 159's (ref): transfers 14,162 / 15,716, DMA packets, trace rows, drain, replay max.
  **The mapping:** a register block's `sim_exit_at` is W16's label + 1, for the exit and the end on both workloads.
  Indicative: 0.29-0.30 us/cycle, 6.2-7.8 s a segment (two chains on the machine at once).
- 2026-11-10: owner (merged to main, 59 binaries clean): go ahead with the full runs. The driver now prints C&C 3's
  exit and the end as W16's labels (`sim_exit_at - 1`) and the segment's cycles as W16's (end - boot end), timed
  against them; after C&C 3's exit it runs in 10,000-cycle chunks, so the timed run ends within 10,000 cycles of the
  end (it overshot by up to 250,000 idle cycles before). `chain128-body.sh`: both workloads side by side, each pinned to
  its own physical P core (machine.sh's P_ONE), segment 10 checked against 173's figures, a CSV per workload.
