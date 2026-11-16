# gold: Jolt's tracer in Java

> **updater_wip's copy (WORKER1):** v1.36's gold, unchanged in everything it did before, **plus the host's frames for
> v1.4** (`v1.4_wip/planning/HOST-FRAMES.md`): the `frames` command, the reader (`frames-read`) and the reference images
> (`frames-images`). The section "The host's frames" below says how to run them and what they are checked against;
> the plan and every choice where HOST-FRAMES.md is open are in `updater_wip/planning/sessions/WORKER1-NOTES.md`.

A standalone emulator of the Jolt zkVM's tracer (DECISIONS-v1.3 section 17, work item W4): it reads a guest's ELF, sets
up Jolt's memory, runs the program and produces **Jolt's trace rows exactly as jolt-cpp's tracer does** (jolt-cpp
`821e1bd`). Self-contained: plain Java 21, no dependencies, no native code, nothing imported from the rest of meshsim.

Status: **gold** (stage 2): it computes everything itself, the secp256k1 advice included, and writes section 16's
segment file **byte-identical to WORKER16's segment_writer** (jolt-asic-planning `worker16-w3` at `ca30b5e`) on both
workloads at 2^29 and on the hard-cut tests.

## Build and run

```sh
./build.sh                      # javac into build/, then the inline emit test (against test/inline-emit-digests.txt)
JOLT_CPP=<jolt-cpp> ./build.sh  # the same, but row for row against <jolt-cpp>/src/tracer/inlines/*.inc

java -cp build gold.Main segments <program dir> <input dir> <out file> [--cuts=0,a,...|--cuts-file=F]
     [--segment-rows=4194304] [--segments=128] [--compare=<reference file>]
java -cp build gold.Main compare <program dir> <input dir> <ref prefix> [--max-rows=N] [--computed-advice]
./check.sh <jolt-asic-planning tracer dir> <reference .seg dir>     # stage 2's acceptance, the four files below
```

`segments` writes section 16's file, by default **v1.3** (frame 7 the proof parameters, frame 1 the per-segment 4 KB
pages, frame 6 the proof's jump-target table, then per segment frame 2 as the 68-word record, frames 3 and 4 or 5;
`v1.3/planning/sessions/WORKER18-NOTES.md`, "The v1.3 segment file"), or with `--format=v1.2` the earlier file (no frames 6 and 7), with
`<out>.cuts` and `<out>.json`; with `--compare` it checks it byte for byte and names the first differing frame. The
format and every encoder choice it shares with WORKER16's tool are in `v1.3/planning/sessions/WORKER18-NOTES.md` ("Stage 2").

`<program dir>` holds `program.elf` and `program.json`; `<input dir>` holds `input.bin` and, if present,
`trusted_advice.bin` and `untrusted_advice.bin` (as jolt-cpp's tools take them). `<ref prefix>` is the output of
`row_dump` (jolt-asic-planning, branch `worker18-rows`, `tracer/rows/`): `compare` checks the layout and bytecode size,
then every row's record against `<ref>.rows.bin` when present (stopping at the first difference, with context), else
every 2^16-row window hash against `<ref>.hashes.csv`, then the row count, padded size and run hash. In stage 1 the
secp256k1 advice comes from `<ref>.advice.bin`; the division family computes its own.

## What it matches

| run | rows | checked | result |
|---|---|---|---|
| EVM Zone `trace-23-812ded164e49` | 5,833,069 | every record | match |
| Cyclone `input/22/input-default` | 2,705,819 | every record | match |
| EVM Zone `trace-29-4599adc50712` | 354,861,942 | every window hash, run hash | match |
| Cyclone `input/29/input-15961266` | 371,526,406 | every window hash, run hash | match |

Segment files (stage 2), byte for byte against segment_writer `ca30b5e`:

| file | rows | segments | bytes | SHA-256 | time |
|---|---|---|---|---|---|
| EVM Zone trace-29 | 354,861,942 | 128 | 1,747,828 | `208520ec…8aa17d18` | 13.5 s |
| Cyclone input/29 | 371,526,406 | 128 | 10,334,580 | `f45f71b6…8aa64070` | 15.5 s |
| EVM Zone trace-23, 93 hard cuts | 5,833,069 | 93 | 326,576 | `b33f9fc4…5b35c566` | 2.8 s |
| Cyclone input/22, 111 hard cuts | 2,705,819 | 111 | 339,884 | `a3858f5e…8cac81ec` | 1.6 s |

All 15 inlines match jolt-cpp's emit lists row for row (InlineEmitTest). Speed: about 16.5 M rows/s warm on one CPU
(a 2^29 run in 22 s); memory is sparse 16 KB pages, so a 2^29 run fits in a few hundred MB.

## The row record

Ten little-endian u64 words per row, jolt-cpp's `RiscCycle` (see `Row.java`):
pc; opcode | rd_reg << 8 | rs1_reg << 16 | rs2_reg << 24 | inline_sequence_remaining << 32 | is_compressed << 48 |
virt_flags << 56; imm; rd; rs1; rs2; rd_prev; mem_access_addr; mem_pre_value; mem_post_value. Hash: WORKER16's mix
over the ten words, folded per 2^16-row window, the windows folded into a run hash.

## The parts

| file | what |
|---|---|
| `Elf.java`, `Program.java`, `Layout.java`, `Memory.java`, `Json.java` | ELF64 by hand; Jolt's memory layout; loading inputs; sparse RAM |
| `Decoder.java`, `Instruction.java`, `HostIo.java` | RV64IMAC and Jolt's custom opcodes, compressed included, as jolt-cpp decodes them |
| `Expander.java`, `Bytecode.java`, `VInstr.java` | Jolt's expansions per instruction family (virtual.cpp), the expanded program |
| `Machine.java`, `Bits.java`, `Isa.java`, `Op.java` | the interpreter, building each row |
| `InlineAsm.java` and `*Inline.java` | the inlines as readable loops: secp256k1 (7), BLAKE2b, BLAKE3 (2), Keccak (2), SHA-256 (2), BIGINT256_MUL |
| `Row.java`, `AdviceFile.java`, `Main.java` | the record and hash, stage 1's advice, the drivers |
| `SecpCompute.java` | the secp256k1 advice in BigInteger (jolt-cpp's evaluate) |
| `SegmentWriter.java`, `FrameDiff.java`, `check.sh` | section 16's segment file; locating a difference; the acceptance run |
| `test/gold/InlineEmitTest.java` | each inline vs jolt-cpp's `.inc` emit list, as data |

Every rule cites jolt-cpp (file:line at `821e1bd`) or, for the inlines, upstream Jolt's builder (`f8fc214`, the
commit jolt-cpp pins); `v1.3/planning/sessions/WORKER18-NOTES.md` lists them.

## The host's frames (v1.4, WORKER1)

As `v1.4_wip/planning/HOST-FRAMES.md` stands after the owner's rulings of 2026-11-16.

```sh
java -cp build gold.Main frames <program dir> <input dir> <out file> [--cuts=0,a,...|--cuts-file=F] [--segment-rows=4194304]
     [--asics=64|128|256] [--base=none|static] [--compression=auto|none|lru] [--stem=P] [--workdir=P] [--base-path=P]
     [--verify]
java -cp build gold.Main frames-read <frame file> [--check] [--seg=<v1.3 segment file>] [--brief]
java -cp build gold.Main frames-images <frame file> <s> [--out=<prefix>]
```

- **`frames`** runs gold once and writes, in order: 1 and 2 the program image (header; update words in whole pages);
  with `--base=static`, the static targets' table established as a base table (frames 4 and 5 on their own); 3 the
  proof header; 4 and 5 the run's jump table (pages added to the base, or a full new table); 6 the data image (the
  least data); 7 the register states; 8 the advice logs; 9 the memory segments. *N* segments (`--asics`, default 128;
  in no frame). Frames 2, 5, 6 and 9 go in whichever form is smaller, the header's compression byte saying which;
  `--compression=none|lru` forces one, for tests. `<out>.json` beside it: every frame's bytes, both forms' sizes, the
  page counts, the file's SHA-256.
- **`frames-read`** prints every frame's fields; *N* is frame 7's length / 68. `--check`: the file against itself
  (every length, every count of frames 1, 3 and 4 against the data it describes, every page count rebuilt on
  copy-on-write trees for every *s*, the advice counts in frames 7 and 8, word 66 against frame 3). `--seg`: frames 7,
  8, 9 against the `segments` command's v1.3 file for the same run.
- **`frames-images`** plays a frame file into empty memory for segment *s* (`Reference`): the program image, the
  cycle-0 image, the start image and the Tracer's copy (equal), the final image and the run's table, with digests and
  the pages each run took; `--out` writes them. **An image** is a file's pages by offset: every page with a non-zero
  byte, ascending, each u64 offset then 4,096 bytes; its digest is the SHA-256 of that.
- **`--verify`** checks the file as written, read back from disk, against gold's own run: the reference's images equal
  gold's memory at cycle 0, at the cut before every *s* and at the end; the reference's page counts equal the headers';
  every jump target the run takes is found by the Tracer's probe in the run's table with gold's next `instr_idx`; both
  forms of every update-word frame decode (frame 6's two make the same image); with `auto`, the smaller form was sent.
- **Page counts** are the pages the Updater takes from the free page queue: data pages and the index pages above them,
  without the root (the firmware makes it); a page written many times counts once per file.

The reading side (`FrameFile`, `Reference`, `FrameReader`) shares no code with the writing side (`Frames`,
`FrameWriter`, `UpdateWords`, `FileTree`): in particular the page counts are computed twice, from page sets
(`FileTree`) and by building every file as a real tree of 1,024-entry index pages with owned bits and copy-on-write
(`Reference`).

**Tests**, run by `build.sh` (`FRAMES_TESTS=0` skips them):

| script | what |
|---|---|
| `test/frames/guests.sh` | every guest that runs to its end, jumpmap's `jumps` guest and keccak256 with three input files: frames written four times (no base, uncompressed, N 128; static base, LRU, N 64; static base, the smaller form, N 128; no base, the smaller form, N 256) with `--verify`, read back with `--check --seg`; `FramesTest`: the page counts two ways on synthetic writes (depths 1 to 3, pages rewritten across segments), broken files caught, the forced fall-back from a base to a full new table |
| `test/frames/old_unchanged.sh [<prog> <input> <cuts>]...` | the `segments` command byte-identical to v1.36 gold's (built in scratch) on every guest, in both formats, and on the runs given; `test/guests/check_guests.sh` |
| `test/frames/workloads.sh <tracer dir> <out> small\|29` | EVM Zone and Cyclone, small with the hard cuts or at 2^29: the frames three times (full table uncompressed; static base, the smaller form; static base, LRU, N 256) with `--verify`, `--check --seg`; at 2^29 the `segments` files against their published v1.3 SHA-256s |

Results: `updater_wip/planning/sessions/WORKER1-REPORT.md` (Quest 197; Quest 200 after the rulings).
