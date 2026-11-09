# WORKER13 notes (M3, the reference side)

Branch `sim-w13`. Prompt: `PROMPT.md`. Plan agreed with the owner 2026-11-09 (go-ahead with four points, below).

## The plan

1. **Coverage.** A driver in `sim/tests/ref/` (`M3Seg.java`) that runs `TracerT2.chip`'s sequence exactly, with
   read-only hooks between cycles (as W7's `M2Sort`/`FpHooks`): which units step, bus and DMA traffic, registers
   touched, firmware functions retired (C&C 1, C&C 3, the tiles), the harness's stand-ins. Small runs locally (a few
   windows, the suites); the 64-window runs on the runner.
2. **`sim/tests/AUDIT-M3.md`.** W14's section (the Tracer, `RowCore`, `Rvc`, its caches and fill bus to
   `MemorySystem`/`BankedMemory`, C&C 3's slot) and W15's (the replayer, `ReplayerAlu`, `PeripheralSlot`, its DMA)
   published first, each as soon as it is solid; then the C&C cores, the DMA and request paths M2 never used, the order
   within `Chip`/`Tile` (D27), D23's events, the unit groups and their order.
3. **The fingerprint's `ref` side** for the new units (`StateFp`, the same canonical form), and its cost per unit.
4. **References** in `sim/tests/golden-m3/` with D12 headers: cycles, rows against `rows.bin`, fingerprints at
   intervals, per-unit dumps; `regen.sh` to regenerate and check; large files on the runner with their hashes.
5. **The baseline**: `ref` on the M3 run(s), one pinned JVM, setup and run separated, the machine recorded (D21), three
   runs.

## The owner's go-ahead (2026-11-09)

1. **The reference segment** is the one T2's own acceptance used, for continuity with the run `ref` was proven on. If
   coverage shows it misses Tracer or replay paths another segment reaches, add that one as a second run; don't switch.
2. **Java locally** (openjdk-21-jdk-headless) for small `ref` runs: a few windows, the suites, coverage on a short run.
   The runner for the 64-window runs, the baseline, and anything big.
3. **W14's and W15's sections early**, each as soon as it is solid, before the rest.
4. **ReplayBench**: confirm from coverage whether it is T2's replay phase only; if so, say so in the audit and give the
   bar's relation to T2's own numbers, so M3's timing compares like with like.

## Log

- 2026-11-09: T2's acceptance (`v1.3/planning/sessions/WORKER22-REPORT.md` section 3): **Quest 110u**, segment 10 of
  each workload (evm-zone-29 and cyclone-29), 64 windows, raw, C&C 1's `CC_SEGMENT` running the barriers: every unit's
  real firmware. `sim/ref`'s `TracerT2` is that code (`cc1Command(0x54, ...)`). Reference run: **evm-zone-29 segment
  10**; cyclone-29 segment 10 (110u's other run, the heaviest segment of either workload) is the candidate second run,
  added if coverage shows paths evm-zone 10 doesn't reach. Inputs on the runner: `$DATA/w9-deliveries`,
  `$DATA/w26-jumpmap` (hashes in `wip/planning/sessions/WORKER22-tasks/make_t1_task.sh`).
- ReplayBench's bar (`sim/tests/ref/baseline.sh`, D8) is **evm-zone-29 segment 0**, windows 0-63, preloaded: a different
  segment from the reference, and (from its source) the replay phase only. To confirm by coverage.
- T2 reaches segment 10 by carrying the one memory through segments 0-9 on T1's path (the model and the ideal
  controller): that is setup, kept apart from the run in the baseline.
- 2026-11-09, **Quest 159** (`sim/ref` precompiled, kit at 58aefa4; the runner, i5-14400F): `ref`'s `TracerT2` on 110u's
  two runs, in parallel. **Both OK** (every delivery and all 4,194,304 rows checked), and every figure equals 110u's
  (Tracer rows, drain, transfers, DMA packets, replay max) **except C&C 3's span, +26 cycles in both workloads**:
  evm-zone-29 23,693,532 (110u 23,693,506), cyclone-29 28,105,968 (110u 28,105,942). **Explained:** after a326fbd,
  `Config`'s defaults changed (owner P1, 2026-11-06): `goLatency` 1 -> 8, `readyLatency` 1 -> 8, `stageDelay` 32 -> 64;
  C&C 1's `CC_SEGMENT` barriers fall inside C&C 3's span, hence one constant shift. To confirm directly with the
  coverage driver (the old latencies set by argument should give 110u's figures). The goldens' D12 header carries
  GO 8, READY 8, error 8 (READY's), stage_delay 64.
  - **Wall: 46 s (evm-zone) and 50 s (cyclone)** per segment, against 110u's 1106 s and 1338 s: WORKER28's idle-skip
    (on by default in `sim/ref`). The baseline is `ref` as it runs: idle-skip on.
  - My verdict compared exactly against 110u, so the task FAILED and the runner retried it twice (ATTEMPTS 3).
    Later tasks judge PASS on `ref`'s own checks, and report differences from a reference as data.

## The runner's data (Quest 159's listing; `$DATA` = `/home/questrunner/data`)

| path | size | what | made by | hash |
|---|---|---|---|---|
| `w9-deliveries/evm-zone-29/`, `evm-zone-29.manifest.json`, `.COMPLETE` | 90 GB both | WORKER9's 2^29 delivery, v1.25 records (`window_NNNNN.{state,code,rlog,wlog,rows}.bin`); T2 reads it as is | Quest 103 | manifest sha256 `7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f` (= `.COMPLETE`) |
| `w9-deliveries/cyclone-29/`, `cyclone-29.manifest.json`, `.COMPLETE` | | the same for cyclone-29 | Quest 104 | `f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982` |
| `w9-deliveries/manifest.json` | | the combined manifest | 103/104 | |
| `w9-deliveries-selected/{evm-zone-29,cyclone-29}/`, their manifests | 138 MB | selected windows (evm-zone-29: 0, 1, 2, 7, 12, 21, 22, 34, 35, 5414, 5415, 8191; cyclone-29: 104's list) | 103/104 | |
| `w9-selected-windows.tgz` | 12 MB | the selected windows as one tarball | 103/104 | |
| `w26-jumpmap/{evm-zone-29,cyclone-29}/`, `manifest.sha256`, `.COMPLETE` | 26 MB | WORKER26's Tracer inputs: initial image, map (map.lines, frame6.bin), per-segment pages written, gold's `segments.seg`, proof.json (T1's and T2's) | Quest 109 | `manifest.sha256`'s sha256: evm-zone-29 `57dc0ffe7574ab3e11f79f8414501bd1ceb2fd75faab0b03f3b4a6c2c61cad2d`, cyclone-29 `ace78f61efbb9c0e418a0b1d7f54a79f6e299b7ff45ad7f09537b11961b95fba` (= `.COMPLETE`); every file checked by `sha256sum -c` |
| `w7-golden-m2/` | 75 MB | M2's large references (W7) | | |

Checks as 110u (`tasks/mk_task.sh`'s `checkin`): the manifest's sha256 equals `.COMPLETE` and the value above;
`w26-jumpmap/P/manifest.sha256` likewise, then `sha256sum -c` inside it. Stage A on a v1.26.1 replayer reads v1.26.1
records: Quests 105-107 converted the v1.25 deliveries in scratch with `ConvertDelivery` (never stored). `TracerT1`/T2
read the v1.25 delivery directly (`state.bin` in v1.25's order).
- 2026-11-09: **`AUDIT-M3.md` sections 3.1 (the slot) and 3.2 (the replayer) published for W15**, to the approved
  structure (the peripheral a member of the slot unit, a switch on type). The slot's ports: the director's region write
  a `Link(3)`, the core's stores and consuming loads a `Link(1)`, all applied at level 0; the window a `RegFile` written
  only at level 0 (the end of t − 1); the work after. Level 0's order: the consume and mask first, then the landings,
  then the core's inputs (a state-word landing's credits otherwise differ). Findings 9.1, 9.2. Next: W14's 3.3 and
  section 6's Tracer streams (owner, 2026-11-09: registers, the caches' tags and lines, FIFOs, output queues, with the
  cost per call).
- 2026-11-09: **3.3 (the Tracer, its memory system and memory) and 6.6 (the Tracer's fingerprint streams) published
  for W14.** The memory system and `FileStore` are members of the Tracer (synchronous timing model, no port). 3.1's
  level 0 gains step 0 (the slot's store effects, `outStart` included) and finding 9.3 (`outStart` doesn't settle:
  differs only in UB cases; counted in the coverage run). Section 6's M3 rules: cycle-valued fields as max(v, c);
  pending inputs as (effect cycle, kind, words), never settled by the reader; large memories summed over 64-bit words.
  Next: the `ref` side of 6.6 in `StateFp` and the coverage driver, then one runner task (coverage, the +26 check, the
  fingerprint's cost, the counts for 9.2 and 9.3).
- 2026-11-09, **Quest 160** (`M3Seg`, five JVMs, all PASS): M3Seg = TracerT2 (159's figures); the hooks neutral (figures
  and fingerprints identical with coverage on or off); the Tracer's fingerprints deterministic (240 points), about
  0.5 ms a call. **9.1 confirmed** (110u's latencies give 23,693,506). **9.3: 0** of 14,162 / 15,716 PULSE_DMAs.
  **9.2: one lost depth-1 post per workload** (to identify: unit, bit, cycle). **Coverage: evm-zone 10 never reaches
  controller-fed expansions or the memory system's early evictions and read waits; cyclone-29 10 does** (30 new
  expansions; 48,501 early evictions; 16,321 read waits), and reaches 151 more `replay_ctl` and 116 more `tracer_ctl`
  instructions (evm-zone reaches 22 `tracer_ctl` ones cyclone doesn't): **cyclone-29 segment 10 is the second M3 run**
  (the owner's rule). AUDIT-M3 section 1 written; `sim/tests/golden-m3/cover/` holds the counters and pc histograms.
  ReplayBench: T2's replay phase only, on segment 0, preloaded: its numbers compare with T2's tiles' replay, not T2's run.
- 2026-11-09: AUDIT-M3 2.2, 3.4, 4, 5 (G7: the C&C cores and the chip's integration, the next worker), 6.7-6.9, 9.4
  (W14's write buffer). **Quest 161** (all PASS): the references (`sim/tests/golden-m3/`: chip lines with D12 headers,
  RUNS, HASHES of the large files in `$DATA/w13-golden-m3`: every unit's lines, the memory snapshot, the SRAM landings);
  the Tracer's lines equal Quest 160's (1,195 points); **9.4: the write buffer holds at most 3 lines (cyclone), never
  used in evm-zone; 9.2: none** (160's one per workload was the counter's artefact at cycle 0, fixed in M3Seg).
  **Baseline** (one JVM pinned to CPUs 0,1): evm-zone 10 segment 46.23 s (3 runs, 45.87-46.93), setup 10.5 s, boot
  0.29 s; cyclone 10 50.90 s (50.28-51.38), setup 8.8 s. AUDIT-M3 sections 7, 8 written. Next: regen.sh proven on the
  runner (Quest 162), then REPORT.md.
