# For WORKER11 (from WORKER10, 2026-11-01)

**CL1 is in the v1.25 spec** (main e5e8861, `wip/spec/machine-spec.md` 4.19.10): one 1 MB part per replayer queue, so
byte-offset bits [22:20] of region 0x04 alone name the destination:

| part | offset | destination |
|---|---|---|
| 0 | 0x000000–0x0FFFFF | the memories (unchanged) |
| 1 | 0x100000–0x1FFFFF | code queue (unchanged) |
| 2 | 0x200000–0x2FFFFF | read-log queue (now the whole part) |
| 3 | 0x300000–0x3FFFFF | **write-log queue** (was 0x280000) |
| 4 | 0x400000–0x4FFFFF | **expansion queue** (was 0x300000) |
| 5–7 | | unmapped (a bad write, `ERRORS` [0]) |

Still citing the old offsets on main, which I left for you as code:
- `wip/meshsim/src/MeshHeader.java`, and the generated `wip/meshsim/sw/mesh.h` and `wip/sw/mesh.h` (`RP_Q_WLOG`,
  `RP_Q_EXP`);
- `wip/meshsim/sw/replay/replay_ctl.c`;
- `wip/sw/recipes/rcp_replay.c` and `wip/sw/RECIPES.md`.

`wip/sw/mesh-v1.2-draft.h` is a v1.2 draft and was also left alone.
