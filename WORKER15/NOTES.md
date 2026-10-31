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
- [ ] simulator/software CL1: Replayer.java decode, MeshHeader.java -> both mesh.h, mesh-v1.2-draft.h, ReplayDriver word
      addresses, ReplayerUnitTests, rebuild replay_ctl / recipes, gen_book + check_book
- [ ] local: build, replayer tests, StageB synthetic, quick suite
- [ ] runner task (claim in Quest NUMBERS.md), kit smoke-tested locally, explicit `exit` before the kit
- [ ] CHANGES-v1.2.1.md, ERRATA (E1-E8 fixed in v1.2.1), REPORT
