# meshsim v4.0 M12: the verification against M11

Worker 13, 9 October 2026 (worker 12's M11 kit, adapted). M12 is the 9 October modes
(`design_v2/spec/PROPOSAL-mode-renumbering.md`, merged in spec 3.1, 4.8 and 4.9): the packet mode is 4 bits, augmenting
is mode 4 (the sender's choice per stream; `MASTER_CONFIG`'s attach flag is gone), and controller-memory writes,
mailbox writes and replies are modes 5, 6 and 7. The hardware change moves no packet and no cycle; the tile software
that drives it moved a few instructions (the key streams now choose mode 4 where the receiver used to set attach), and
two overlays changed by one 8-byte packet. So the sort's runs are compared **phase by phase**, not by their totals
(lead, 9 October). This kit runs the checks that are too long for the build box, on both versions, and compares them:

- **BT at full size, first** (lead, 9 October): `p4/trace10-10-k128` and `p4/trace10-10-kv32` at 64 × 65,536 on M11 and
  on M12. No run at 64 × 2,000 takes the BT path (DEV 115), and M12 changed BT's hop 2 (its key streams send mode 4
  with 128-bit values), so these are the runs that matter most. Each is a full-size sort: allow about as long as a
  split run or more, and 3 GB of heap.
- **the split:** `SplitRun` on all 25 of the split's data sets, on M11 and on M12. The split's image is
  instruction-identical on both (only its header constants differ), so everything must be identical.
- **the sort:** the sort kit's 92 runs (P4 43, shuffled 3, whole-sort sweep 46) at 64 × 2,000 keys, on M11 and on M12.

Java 17 or later and nothing else (no Python, no RISC-V toolchain). The lead's laptop: Windows with Cygwin, Java 20.

## What is in it

| path | what |
|---|---|
| `run.sh`, `tools/runjob.sh` | the runner and one run |
| `m11/` | the M11 sort kit **exactly as shipped** (`design_v2/sort/kit/mesh-sort-kit.tar.gz` of package 09e) |
| `m12/` | the M12 sort kit (the M12 simulator and the M12 sort images; its jar also has `PhaseNeutral`) |
| `m11/split/`, `m12/split/` | `split.jar` (that version's simulator with `SplitRun`; M12's also `SplitSets`, `VerifySummary`, `PhaseNeutral`), `hops.txt` and the split's tile image in `sw/build/` |
| `HASHES-SPLIT` | SHA-256 of both split directories' files and of the 25 split sets |

## Run

```
./run.sh -g bt                  # first: the two full-size BT runs on both versions (4 runs; set -j to what memory allows)
./run.sh                        # everything: 4 BT runs, 50 split runs and 184 kit runs, 12 at a time, 3 GB each, into results/
./run.sh -j 14 -m 3g
./run.sh -j 2 -f split/00 -g split      # a smoke test: uniform on both versions (a few minutes)
./run.sh -s                     # the summary only
```

Options: `-j` parallel runs (default 12), `-m` heap per JVM (default 3g), `-o` results directory (relative to this
one; default `results`), `-g` groups (`bt`, `split`, `kit`), `-i` versions (`m11`, `m12`), `-f` only ids containing a
string (`split/NN` or the kit's ids such as `p4/uniform-k128`), `-n` keys per tile for the kit runs (default 2,000),
`-s` the summary only.

- **Data.** The first run generates what it needs: the BT runs' key set at 64 × 65,536 per version (16 MB), the 25
  split sets into `sets-65536/` (`SplitSets`; 346 MB, a few minutes), and each kit's 20 key sets into `m11/keys-2000/`
  and `m12/keys-2000/` (`KeyGen`, seconds). All are checked against the hashes (`HASHES-SPLIT`, each kit's `HASHES`); a
  mismatch is a warning, and the summary flags the runs.
- **Order:** the BT runs first, then the split runs, M11 and M12 interleaved, then the kit runs longest first.
- **Restartable:** a finished run is skipped (a split log; a kit or BT CSV). Runs in progress write `.part` files.
- **Time** [estimate]: a split run takes about 2 to 4 minutes on one core, a kit run at 64 × 2,000 about 15 to 30
  seconds, a full-size BT run several times a split run. At `-j 12`: roughly 30 to 45 minutes in all.

## Results

In `results/` (upload the whole directory):
- `VERIFY.md`: the verdict; the BT runs and the kit run by run (checks, path, TOTAL and its change split into the
  slowest tiles' controller cycles, the overlay packets and the residual; invariant 8); the split set by set (exact, S
  on both, search steps and cycles per step, invariant 8, violations, the overlap check) and its inputs;
- `PHASES-bt.md`, `PHASES-kit.md`: every phase whose cycles changed, as dCycles = dInstr/64 + overlay packets (+ residual);
- `COMPARE-bt.md`, `COMPARE-kit.md`: `SortCompare`, M12 against M11 step by step;
- `m11/kit/SUMMARY.md`, `m12/kit/SUMMARY.md`: each kit's own summary (P4 table, the shuffled identity check, the sweep
  bands, the Inputs section);
- `m11/split/NN.log`, `m12/split/NN.log` and every kit and BT run's CSVs and log.

**Expected.**
- **BT and the kit, phase by phase** (`PhaseNeutral`): checks passing on both and the same path; in every phase the
  data plane identical (address-generator requests, stalls and runs, gather requests, read-port reads, injected and
  ejected packets, read latencies, send stalls, invariant 8, every peak), except that the phase that loads an overlay
  whose size changed (xfer +1 packet, local −1) moves agReq, injected, ejected and latNA together by
  exactly 64 per packet; and every phase's cycle change explained as the slowest tile's busy controller cycles plus the
  overlay packets, within 2 cycles, or, where that tile waited on others (BT's exchanges), within the range of every
  tile's change (`tile_busy`). `m11/meshsim.jar` is M11 with `m11-phaselog.patch`, an accounting-only patch adding
  those columns (DEV 125; every existing column proved identical to M11's). Totals move by tens of cycles
  (at 64 × 2,000 here: direct −6, LB 0, TH −56 and −114; BT at full size, run here with this kit: −882 and −295, M12
  faster); they are reported, not required equal.
- **The split:** exact on the 23 balanced sets on both (the two shaped sets, 21 and 23, run as balanced splits and are
  not exact on either version: DEV 116), with the same S, search steps, cycles per step and invariant-8 count, no
  violations and no read-port overlaps. Any difference is a finding.
