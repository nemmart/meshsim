# WORKER15 notes (keep current: the work must survive a reset)

Branch `v1.2.1` from `main` @ 83c23cd. Owner's go-ahead 2026-10-31: the plan as proposed; `sw/mesh-v1.2-draft.h` updated
too (header_diff.py stays strict); one runner task (quick + full extras + full-size sort), split only if timing says so.

## Environment (a fresh sandbox)
- `apt-get update; apt-get install -y openjdk-21-jdk-headless gcc-riscv64-unknown-elf`.
- Quest repo cloned beside meshsim (`/home/claude/quest`); next free task number at start: 087.

## Progress
- [x] spec: E1-E3, E5-E8 in `v1.2/spec/machine-spec.md` (E1/E2/E5/E6 ported from WORKER10's 0b4ba20 / 2a107ab, only
      machine-spec.md's v1.2 hunks; E3 item 111 superseded by D45; E7 the map + "one part per queue"; E8 the note from a8df673
      and the 4.19.1 / parameters rows)
- [x] E4: `CHANGES-v1.2.md` round 4 (D45-D49). D48 isn't in the spec text (v1.2 or wip): QUESTIONS Q1
- [x] simulator/software CL1: Replayer.java decode (part = word addr >>> 18, 1-4 the queues, >= 5 unmapped, the old
      offset >= 0x400000 hack and the 0x1FFFFF mask gone), MeshHeader.java -> both mesh.h, mesh-v1.2-draft.h,
      ReplayDriver word addresses (wlog 0xC0000, exp 0x100000), ReplayerUnitTests (part 5 unmapped; one write per part).
      replay_ctl.c / rcp_replay.c use the macros: rebuilt only. Book unchanged (no addresses in it): check_book 46/0.
      Do NOT run gen_book.py: its template lacks section 14's measurements (Q2).
- Local baseline -> after: ReplayerTest 1188 -> 1195/0; RegDocTest 269/0 (draft strict); StageB synthetic 18/0;
  RecipeTest -1 2042/0. Mutation (controller on the old map): StageB passes 13 windows then hangs (no cycle cap).
- [ ] local: build, replayer tests, StageB synthetic, quick suite
- [ ] runner task (claim in Quest NUMBERS.md), kit smoke-tested locally, explicit `exit` before the kit
- [ ] CHANGES-v1.2.1.md, ERRATA (E1-E8 fixed in v1.2.1), REPORT

## 2026-11-01: the folders moved (main bd74b81)
- `v1.2/` is now Tony's copy (tag v1.2-tony) and only its ERRATA.md may change; **my work is in `v1.2.1/`**. Branch rebuilt by
  the integrator (de5c835, f1ed3c5 on v1.2.1/), reset to origin/v1.2.1, main merged (135da30).
- Q1-Q3 became CL5, CL3, CL4 (wip/planning/CLEANUP.md); E9 = CL5.
- [x] CL3: 23 template lines (section 14 and the two "Cycles" lines) restored from the committed book; gen_book now
      reproduces RECIPES.md byte-identically; check_book 46/0.
- [x] CL5 (E9): 4.17.4 (words 8-9 read 0 without a DMA, a load not flagged, a store flagged; word 4 stays reserved, as
      meshsim's PeripheralSlot.reserved does) and 4.11 (`mtime`'s rate implementation-defined); CHANGES-v1.2 D48 row.
- [x] CL4: StageB fails a window once a running pass has delivered no row for 50,000 cycles (both paths), with the
      queues' occupancy. Normal run 18/0; old-map controller: 10 of 18 windows FAIL "starved" in 25 s (was a hang).
- [x] the kit: v1.2.1/{meshsim (built --release 17, every image), sw, tools/replayer-opcodes/vec.tsv} + 086's sortrunner
      with a new jar (`jar cf` of build/) and HASHES (only the jar line changes: sort image, boot ROM, hops byte-identical
      to 077/086). kit.tgz 2.6 MB, sha256 5758d7d3... Smoke from a fresh unpack: run_quick bus/regdoc/replayer/reqbus/
      riscv/stageb 7/7 (39 s); run_p4 -n 2000 uniform-k128 ok (246,447 cycles, 23 s)
- [x] Quest 088 claimed (NUMBERS 146be82) and pushed (39ab0e9): tasks/088-w15-v121-verify.sh, branch at 090fc40
- [x] v1.2.1/CHANGES-v1.2.1.md (25 spec hunks by section, RTL yes/note/no, F/E/CL source; sim/sw table)
- [x] v1.2/ERRATA.md: E1-E9 marked fixed in v1.2.1 with commits; "Found after v1.2.1" section
- [x] 088 passed (quick 34/34, full 10/10, p4/uniform-k128 1,360,082 cycles = 086) -> REPORT
## 2026-11-01 (later): WORKER13's errata from main c2de4ae
- merged main (ERRATA conflict: took main's, re-applied the marks); E1 also 4.19's intro ("peripheral 1, type 4"), E10 4.12's
  EVENTS load 2 cycles, E11 4.19.11's duplicated sentence: 69570b6, wording from wip. ERRATA E1-E11 marked; changelog rows
  for 4.12, 4.19, 4.19.11. NEXT.md (the same request) removed. **Done: ready to merge.**
