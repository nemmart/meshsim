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
- [ ] the kit (086's layout: v1.2.1/meshsim built --release 17 with images; sortrunner with a rebuilt jar and HASHES),
      smoke test, claim a number after a fresh pull (087 is WORKER11's), push the task
- [ ] CHANGES-v1.2.1.md (F1-F4, E1-E9, CL1-CL5, sim/sw); v1.2/ERRATA.md E1-E9 fixed in v1.2.1; REPORT
