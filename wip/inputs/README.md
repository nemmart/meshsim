# The simulation's inputs, rebuilt from the pinned sources (WORKER27)

Everything meshsim's acceptance reads, for **EVM Zone** and **Cyclone**, at the small sizes (EVM Zone `trace-23`, Cyclone
`input/22`; minutes) and at **2²⁹** (hours on one CPU, about 20 minutes on the runner), built by one command per
workload and size, with every output's checksum in `manifest/`. At 2²⁹ the build is **identical to the files the
acceptance runs used** (the runner's persistent data, Quest 103–107 and 109; checked by Quest 111 and 112).

```sh
./deps.sh                                   # what's missing, and the apt line for it
export GH_TOKEN=...                         # only if jolt-asic-planning is private to you; never written to a file
./build.sh evm-zone small                   # or: cyclone small, evm-zone 29, cyclone 29
./check.sh evm-zone-23                      # the build against manifest/evm-zone-23.sha256: PASS or FAIL
```

`OUT` (default `~/meshsim-inputs`) holds `src/` (the clone), `build/`, `logs/` and the outputs, laid out like the runner's
persistent data, so every tool that reads that data reads a build unchanged. Steps run in order and can be run alone
(`./build.sh cyclone 29 convert`); each is staged, checked and marked `COMPLETE`, so a re-run resumes.

| step | what | the tool | time (small, 1 CPU / 2²⁹ on the runner) |
|---|---|---|---|
| `tools` | jolt-cpp's tracer and WORKER9's `delivery_writer` at the pin; gold, jumpmap and v1.26.1's `ConvertDelivery` compiled | cmake, javac | 3 min, once |
| `deliveries` | the replay deliveries, every window kept and self-proved; at 2²⁹ also the selected windows and `w9-selected-windows.tgz` | `delivery_writer --windows=all --keep=all` | 12 s / 8 min |
| `convert` | **the deliveries as v1.26.1 records, the RTL's target** | `ConvertDelivery` (v1.26.1) | 1 s / 6 s |
| `gold` | the segment file (section 16, frames 1–5) | `gold.Main segments` | 2 s / 15 s |
| `jumpmap` | the jump table, initial image, per-segment pages written, `proof.json` | `jumpmap.Main` | 7 s / 90 s |

Sizes: EVM Zone trace-23 about 700 MB, Cyclone input/22 about 350 MB; at 2²⁹ about 46 GB per workload.

## Pins

| source | pin |
|---|---|
| jolt-asic-planning (`tracer-standalone-main`) | `fa59d9a` (jolt-cpp `88dca85`): the guests and inputs, the tracer, `delivery_writer` |
| the universal replayer tables | `v1.26.1/tables/replayer/` (equal to `wip/tables/replayer/`, and to what 103/104 used) |
| `ConvertDelivery` and the acceptance kit | `v1.26.1/meshsim/` |
| gold, jumpmap | `wip/gold/` (`7ef9039`), `wip/jumpmap/` |

The runner's deliveries were written at `8d546e3` (jolt-cpp `821e1bd`); WORKER16 found the two tracers' output identical,
and Quest 111/112 check it file by file. (`JAP_COMMIT=8d546e3… ./build.sh …` builds at the old pin, should it ever be needed;
`pins.sh` keeps both.)

## The files, and where each is defined

Run names: `evm-zone-23`, `cyclone-22`, `evm-zone-29`, `cyclone-29`. Spec sections are `v1.26.1/spec/machine-spec.md`'s
unless marked wip (`wip/spec/machine-spec.md`, the v1.3 work); DECISIONS-v1.3 is `wip/planning/DECISIONS-v1.3.md`.

**`v1261/RUN/`: the replayer's input at v1.26.1 (what the RTL replays).** One directory per program, `RUN.manifest.json`
beside it (every window: its first row, rows, start state, counts, each file's size and SHA-256, the conversion's source).
Per window `window_NNNNN.`:

| file | contents | defined in |
|---|---|---|
| `state.bin`, `state-lru.bin`, `state-pick.bin` | the 68-word (1,088-byte) state record: state word, stream word, the image, word 66, the template bitmap; streams raw, all LRU-coded, and the Tracer's pick | 4.19.2 (the record, the unit-by-word table), 4.19.10 |
| `end.bin` | the state at the next window's start, same layout (zero stream word) | 4.19.2 |
| `code.bin`, `rlog.bin`, `wlog.bin` and `*-lru.bin` | the three streams, raw and LRU-coded, in 128-bit words | 4.19.2, 4.19.15 (decompression) |
| `rows.bin` | **the reference: jolt-cpp's rows** as 80-byte output rows; padding rows zero | 4.19.12 |
| `decode_table.bin`, `rom.bin`, `templates.bin`, `template_index.bin` | the universal tables (one copy per program directory): decode table, ROM, controller-fed templates and their index | 4.19.5, 4.19.6, 4.19.9 (templates and their numbers), 4.19.3 (the words) |

Only the records are new files; every other file is a symbolic link into `w9-deliveries/RUN/`, so keep both.
`RUN_CONFIG` (window word 32; 4.17.4, 4.19.9) is not in the files: the acceptance runs wrote **65,536 rows | store log |
rd_prev** (Quest 105–107). The tables' provenance: `v1.26.1/tables/manifest.json`; the input format's history:
`v1.26.1/meshsim/REPLAY-INPUT-REQUIREMENTS.md`.

**`w9-deliveries/RUN/`: the same windows at v1.25** (67-word records, flags in the state word; v1.25's 4.19.2), as
WORKER9's writer emits them; `RUN.manifest.json` beside it, `manifest.json` the root listing every program present.
**`w9-deliveries-selected/`** (2²⁹ only): 12 and 13 windows chosen by WORKER9 (start, boundaries, the last), as
`w9-selected-windows.tgz` too. The Tracer's acceptance (T1) reads these v1.25 deliveries.

**`gold/RUN/`: gold's segment file.** `segments.seg` (DECISIONS-v1.3 section 16: frame 1, then per segment frames 2, 3
and 4 or 5; byte-identical to WORKER16's `segment_writer`), `.cuts` (the segments' first rows), `.json` (counts).
v1.2 today: frames 6 and 7 come with WORKER18's formats (`GOLD_FORMAT=v1.3` is the hook in `build.sh`).

**`w26-jumpmap/RUN/`: the Tracer's host-side inputs** (WORKER26, `wip/jumpmap/README.md`):

| file | contents | defined in |
|---|---|---|
| `proof.json` | word 66's values (`log2_k`, `lowest_address`), the hash constants (`hash_k`, `hash_p1`, `hash_p3`), identities and SHA-256s | wip 4.21.2, 4.21.4; DECISIONS-v1.3 section 18 |
| `frame6.bin` | frame 6: u32 length, u32 6, u32 k, p₁, p₃, then the 2^k lines | wip 4.21.4; DECISIONS-v1.3 sections 16, 18 |
| `map.lines` | the same lines alone (16 entries of key u32, base u32; free all ones) | wip 4.21.4 |
| `initial.pages` | the cycle-0 image: per page u64 offset, u32 flags (0), u32 0, 4,096 bytes | wip 4.21.5; DECISIONS-v1.3 section 15 |
| `seg/NNN.pages`, `seg/NNN.json` | the pages segment NNN writes (u64 offsets, ascending); its first row, count, `gold_check` | wip 4.21.5, 4.21.7 |
| `segments.seg` | gold's segment file, as above | |
| `segments.csv`, `run_only.csv`, `report.json` | measurements (`report.json` holds run times: not checksummed) | `planning/sessions/WORKER26-REPORT.md` |

(WORKER22's NOTES call `initial.pages` and `seg/NNN.pages` `image.pages` and `seg/NNN.written`; these are the files'
actual names.)

## Running meshsim's acceptance against them

**The replayer, stages A, B and F** (v1.26.1's kit; `v1.26.1/meshsim/acceptance/README.md`). Build the kit from
`v1.26.1/meshsim` (the RISC-V gcc is needed for the controller firmware), then run a shard:

```sh
cd v1.26.1/meshsim && sw/build.sh && sw/replay/build.sh && ./acceptance/make_kit.sh /tmp/kit.tgz
mkdir /tmp/k && tar -xzf /tmp/kit.tgz -C /tmp/k && cd /tmp/k/w11-kit
./run_shard.sh -d $OUT/v1261 -p evm-zone-29 -M $OUT/v1261/evm-zone-29.manifest.json -f 0 -n 8192 -j 16 -x 64 results/
```

- **A**: the replayer model fed by an ideal controller; **B**: `replay_ctl.c` on tile 0 feeding the replayer; **F**: the
  full chip, every 64th window with `-x 64`, its cycles checked against B.
- Per window: every file against the manifest, every row byte for byte against `rows.bin`, each stream consumed exactly,
  the end state equal to `end.bin`, no condition or violation. `VERDICT.txt` says PASS only if all of that holds.
- The small runs: `-p evm-zone-23 -n 128`, `-p cyclone-22 -n 64` (`-x 16` for F). At 2²⁹ the references are Quest 106
  and 107: every window PASS in A and B, F on 128 of 128, 3.2116 and 3.1105 cycles per row in B.

**The Tracer, T1** (branch `v1.3-tracer`, `WORKER22/NOTES.md`; `WORKER22/tasks/make_t1_task.sh` for the 2²⁹ task):

```sh
java -Xmx5g -Dreplay.tables=v1.26.1/tables/replayer -cp build:gold TracerT1 $OUT/w9-deliveries evm-zone-29 --w26 $OUT/w26-jumpmap/evm-zone-29 [--compress]
```

It runs the Tracer model on every segment in order on one memory (`initial.pages`, `seg/NNN.pages`), with frame 6's map
and `proof.json`'s word 66, gold's frames 2 and 3 for each segment's record and frames 4/5 for the boundaries; per window
the start state, streams and bitmap against the v1.25 delivery, and with `--compress` Stage A on the Tracer's own record
and streams against `rows.bin`.

## Checksums

`manifest/RUN.sha256` lists, relative to `OUT`: the per-program delivery manifests (which hold every window file's
SHA-256: `check.sh` verifies each file against them), the tables beside the windows, the v1.26.1 manifest, gold's three
files, and every jumpmap file but `report.json`. Left out: `w9-selected-windows.tgz` and the root `manifest.json`, which
depend on which programs share the directory. The 2²⁹ manifests are the ones Quest 111/112 recorded after matching the
runner's copies; the small ones are the first build's, reproduced by a second build and by a kit build.

`ConvertDelivery` at v1.26.1 writes its manifest's `converted` object in an order that changes from one JVM run to the
next (v1.26.1/ERRATA.md; fixed in wip). `build.sh` rewrites it in the fixed order (`by` first), so the manifest's
SHA-256 is stable; the acceptance tasks' logged SHA-256s (105–107) are in one order or the other.

`compare_runner.sh RUN DATA` is the 2²⁹ comparison against the runner's persistent data; `manifest/runner-refs.txt` holds
the runner's checksums as their tasks recorded them. `quest/make_task.sh` makes the Quest task (the runner has no javac:
its kit carries compiled classes, the tracer at the pin and a libelf).

## Machine notes

- Ubuntu 24.04: `openjdk-21-jdk-headless cmake g++ libelf-dev python3 git curl gcc-riscv64-unknown-elf`. cmake fetches fmt,
  nlohmann_json and xbyak from GitHub at configure time.
- The tracer reserves the guest's 4 GB RAM up front. With `vm.overcommit_memory = 0` and a commit limit under about
  5 GB (a small VM or container) it fails with `failed allocate riscv ram`; `sysctl vm.overcommit_memory=1` fixes it.
- `delivery_writer`, gold and jumpmap record the paths they are given in their manifests, so `build.sh` runs them from the
  tracer directory with relative paths (as 103/104 did): the files are then the same on every machine.
