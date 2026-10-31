# Worker 10: the spec, v1.25

You are writing version 1.25 of the machine spec for the mesh, part of an ASIC for proving the Jolt zkVM. **v1.25 is a
milestone between v1.2 and v1.3: v1.2 with the replayer and its tables brought up to jolt-cpp's current expansions**
(commit `821e1bd`), so the hardware matches today's Jolt. The v1.2 spec, `wip/spec/machine-spec.md`, is complete; your
job is the changes v1.25 makes to it, and a delta file. The change is confined to the replayer (spec 4.19) and what
follows from it; the sort and the rest of the machine are unchanged.

REPOSITORY AND BRANCH
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- **Work on a branch `v1.25`, made from `main`.** `wip/` on `main` stays v1.2 until v1.2 is frozen; merge `main` into your
  branch when it moves (merge, don't rebase). **Don't touch `v1.2_wip/`** (in use outside this project).

INPUTS
- **`wip/planning/DECISIONS-v1.25.md`: the authority for every change**, all of it: scope (1), acceptance (2), deliveries
  (3), the expansion word's new bits and the tables (4), universal tables and the last bits (5), the definition of done.
- `wip/planning/DECISIONS-v1.3.md` sections 7-10, which define the Tracer-only fields of the expansion word (`untraced`,
  `advice_reg`) and PEXT; v1.25's spec describes the expansion word in full, so those fields are in it, though the Tracer
  itself is v1.3's.
- **WORKER9's work, the source for the details:** `WORKER9/REPORT.md` (the complete list of jolt-cpp's changes between
  `4f36b3d` and `821e1bd`, the signedness note, the tables' format change), `WORKER9/QUESTIONS.md`, `wip/tables/README.md`
  (the universal tables, the normalized template-index key), and `WORKER9/replayer-opcodes-821e1bd/` (the opcode appendix's
  harness outputs regenerated at `821e1bd`, with the changes listed for the spec editor).
- `wip/spec/machine-spec.md` and `wip/spec/replayer-opcodes.md` (v1.2), and `wip/spec/CHANGES-v1.2.md` as the model for the
  delta file.

WHAT TO WRITE
- **4.19 and everything that follows from it:**
  - the reference: jolt-cpp `821e1bd` (the standalone tracer, jolt-asic-planning PR #2), the workloads (Cyclone, and the new
    EVM Zone release `evm-zone-4e3451c1d8c5`);
  - **the expansion word**: `offset_imm` [63], `sign_imm` [62] (replacing 4.19.12's hard-wired list of signed-immediate
    opcodes), `advice_reg` narrowed to [61:56], `shift_offset`;
  - **the opcodes:** the six new ones (`VirtualLoadB/H/W`, `VirtualStoreB/H/W`), the changed ones (`ShiftData*`'s operands,
    the narrow memory rows' immediate, `VirtualHostIO`, the AMO\*.W bodies), the removed ones (`VirtualAlignAddr`, the
    alignment assertions), the renumbering; and **PEXT correct for any mask**, full speed promised only for contiguous masks,
    replacing v1.2's narrowing (invariant 16);
  - **the tables:** universal, not per program; the ROM's contents and fill; the decode classes, with invalid entries;
    the controller-fed set (the inlines and the system instructions, unless WORKER9 fits the latter into the ROM: follow
    `wip/tables/README.md` as delivered); **the normalized template-index key**; `seq_remaining` counting traced words
    only;
  - **`replayer-opcodes.md`** regenerated from WORKER9's harness outputs.
- **The parameters, the invariants and the open items** (sections 9, 10, 13): what v1.25 changes, closes or opens (spec
  item 113, the reference, among them).
- **`wip/spec/CHANGES-v1.25.md`**: every change from v1.2, section by section, each citing its decision, as
  `CHANGES-v1.2.md` does.

RULES
- **Don't design.** Where the log or WORKER9's work is silent, ambiguous or contradictory, record a question in
  `WORKER10/QUESTIONS.md` with your proposed answer, write the proposal into the spec marked *(Proposed)*, and carry on.
- **Coordinate with WORKER11**, who is updating meshsim's replayer to v1.25 on its own branch: it follows your spec, and its
  findings may come back as questions. Don't edit meshsim, the tables, or the decisions logs.
- This prompt is the go-ahead to change `wip/spec/` on the branch `v1.25` and to write in `WORKER10/`.
- **Keep the last few tool calls of every turn for committing and pushing**, whatever state the work is in. Keep
  `WORKER10/NOTES.md` current so the work survives a reset.

DELIVERABLES
- On `v1.25`: `wip/spec/machine-spec.md` and `replayer-opcodes.md` at v1.25, and `wip/spec/CHANGES-v1.25.md`.
- `WORKER10/QUESTIONS.md` and `WORKER10/REPORT.md` (what changed, what's left *(Proposed)*, what WORKER11 should check).
