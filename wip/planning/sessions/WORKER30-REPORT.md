# WORKER30 report: v1.3's spec brought up to date for the RTL (the fold-in)

Branch **`v1.3-spec-final`** at bf36fef, from main a247a1f (seven commits); the integrator merges. Notes (the audit) in
`WORKER30/NOTES.md`. Plan agreed with the owner 2026-11-06, with its rulings on the counters, the latency lines, stage_delay,
item 158 and stage_delay's code constants (below).

## Delivered

1. **The spec** (`wip/spec/machine-spec.md`):
   - **B1's seventeen answers folded in**, cited **B1 Q*n*** (distinct from SP1's Q*n*):
     - 4.21.5: Q1 (ok only if the whole path is owned) and Q2 (beyond the depth's capacity), with item 148 as the default.
     - 4.21.8: Q6 (several errors in one row).
     - 4.21.6: Q7 (a zero-row run writes no code value).
     - 4.17.9: Q11 (routing by dst).
     - 4.21.9 and section 7: Q12 and Q13, the memory timing as the shared cache, meshsim's model of the black box.
     - 4.21.3's loop, 4.21.7 and invariant 16: Q14 (the dropped expansion).
     - 4.21.3's loop, 4.21.6 and the new 4.21.10: Q3 and Q4 (window 0's start, the last record forwarded) and Q15–Q17
       (the barriers, the arming, `stage_delay`, unexpected arrival, the per-window handshake, the final GO; Q16 reversed).
     - 6.3: a paragraph on a traced segment.
   - **The 11-05/11-06 decisions checked** (NOTES section 2):
     - Added: the stance, in the front matter and 4.21's opening; item 157's counters, in 4.21.3, 8.16 and section 13;
       D13 and D14, in 4.19.4.
     - Already in the spec: items 142, 148, 149, word 66, the end at any JAL with immediate 0, misalignment, the range
       comparator, stores into code and frame 7.
   - **Section 13:** items 148, 153 and 157 updated, 125 in the status table, and **158 new**: the tables to a16z's rule,
     decided for right after the freeze, no RTL effect.
2. **`CHANGES-v1.3.md`:** complete, every row citing its decision (V3-*n*, O, O6, B1 Q*n*); the latency lines excluded,
   as WORKER28's.
3. **The RTL guide**, `wip/spec/CHANGES-v1.26.1-to-v1.3.md`, unit by unit, each item marked RTL or not:
   - the Tracer: its interfaces I1–I5, blocks T1–T14, and the memory system's assumptions T15;
   - the DMA, D1–D4: 128-bit words, src 74 through the down port, routing by dst;
   - unchanged, N1–N6: the replayer, D13/D14, the broadcast, the ground truth, the latencies;
   - the firmware rules, and verification.
4. **The counters in meshsim** (d08a941):
   - **Model:** words 44–51 in `Tracer.java`, 32-bit and wrapping, cleared by the word-0 write and the reset, counting
     while `STATUS` [0] is 1. Each stall is counted in its own cycle, so a load mid-stall reads the cycles so far.
     `STAT_STALL_DATA` includes the write-back wait; `STAT_STALL_OUTPUT` counts once a cycle when either scan stalls;
     advice and expansion waits are in none of them.
   - **Tests:** TracerTest's new counters group (180 pass) checks each counter against the model's own statistics, a
     per-cycle read (never back, at most +1), accumulation across runs, clearing, output stalls under a slow controller,
     and stores as bus errors.
   - **Header and reference:** `TracerRegs` in mesh.h, both copies, regenerated with a `_Static_assert` on word 51;
     REGISTERS.md's Tracer line. RegDocTest 269 pass; PeripheralSlotTest and MemorySystemTest pass.
5. **stage_delay = 64** in Q17's text, WORKER22-REPORT and `tracer_layout.h`'s comment.

## Not run here

The full `run_tests.sh`: the box has no RISC-V toolchain.
- **The firmware is unchanged:** comments only, and mesh.h's new members sit inside the struct's padding, offsets as before.
- **The counters change no timing.**
- A Quest smoke run (TracerTest, a T1 window, one T2 segment) after the merge would confirm cycles are identical; I didn't queue
  it.

## For others

- **WORKER28:**
  - the counters tick every cycle while `STATUS` [0] is 1, so idle-skipping must catch them up (relayed by the owner);
  - `stage_delay`'s code constants are still 32 (`Config.stageDelay`, `rcp_cc1.c`, RecipeTest, RECIPES.md): yours, in the
    latency commit (owner);
  - the spec's latency lines are untouched here.
- **The tables (item 158), after the freeze:** rebuild `tables/replayer/` to a16z's rule. LR/SC get `rd0` = remap (they are 0
  today). Every NOP-policy template gets the non-virtual NOP (`rd0` 1 → 3; only ADDI has 3 today). Check gold's
  `rd0_add`, `lr_*_x0` and `sc_*_x0` guests through the replayer.
- **`tracer_ctl.c`'s header comment** still says C&C 3 waits for every tile's "booted" (superseded by the barriers; the code is
  right). This is a code file, so I left it.
- **Integrator:** `DECISIONS-v1.3.md` section 25 still marks [8] *provisional* (item 148 is now the default), and the
  8.16-era SP1 citation style (O) stays as it was.
