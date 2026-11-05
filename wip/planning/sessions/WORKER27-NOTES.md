# WORKER27 notes

The pipeline: `wip/inputs/` on branch `inputs-pipeline` (README there). Plan agreed with the owner 2026-11-05: the pin is
jolt-asic-planning fa59d9a, proven by the 2²⁹ comparison (fallback 8d546e3 if it differs); the 2²⁹ rebuild goes to
`$DATA/w27-inputs` and is deleted once the comparison passes; the files are documented by their actual names.

## For WORKER22: your NOTES' file names are out of date

`WORKER22/NOTES.md` ("The host-side inputs") calls two of WORKER26's files `image.pages` and `seg/NNN.written`. jumpmap
writes them as **`initial.pages`** and **`seg/NNN.pages`** (same contents and layout), and that's what is on the runner
(`$DATA/w26-jumpmap`, Quest 109) and what `TracerT1 --w26` reads. Please rename them in your table.

## Findings

1. **ConvertDelivery's manifest wasn't reproducible.** Its `converted` object came from `Map.of`, whose iteration order is
   salted per JVM run, so one conversion had two SHA-256s: 105 and 106 logged different ones for the same EVM Zone
   delivery (`5bffa524…`, `e077ae1d…`), 105 and 107 for Cyclone. The records and every file hash were always the same.
   - Fixed in wip (`wip/meshsim/test/ConvertDelivery.java`, branch `inputs-pipeline`): a fixed order, `by` then
     `from_manifest_sha256`. ReplayerTest checks that two separate JVM runs and an in-process run give the same manifest
     (2187/0; v1.26.1's converter fails it in each of six tries).
   - v1.26.1 stays frozen (owner: an ERRATA note there). The pipeline converts with v1.26.1's converter, as Tony does, and
     rewrites the object in the fixed order, so its manifest equals wip's converter's output.
2. **Three tools write the paths they are given into their outputs:** `delivery_writer` (`program_dir`, `input_dir`),
   gold's `.json` sidecar (`program`, `input`), jumpmap's `proof.json` (`input`). 103/104 ran from the tracer directory with
   relative paths, so the pipeline does too: a build's files are then the same on every machine. 109 used its kit's
   absolute paths, so `proof.json` and `segments.seg.json` are compared with 109's without those fields.
3. **The tracer reserves 4 GB for the guest's RAM up front.** On a machine with `vm.overcommit_memory = 0` and a commit
   limit under about 5 GB (this container: 3 GB, no swap) it fails with "failed allocate riscv ram". `deps.sh` checks it;
   `sysctl vm.overcommit_memory=1` fixes it.
4. **`report.json` (jumpmap) records run times**, so it's left out of every checksum. `w9-selected-windows.tgz` and the
   delivery root `manifest.json` depend on which programs share the directory (103's and 104's differ for that reason), so
   they're left out too; the per-program manifests cover every file.
5. **gold and jumpmap moved on main while I worked** (WORKER18's v1.3 file now gold's default; jumpmap's K from gold's
   ProofParams). The pipeline pins them by meshsim commit and builds them from `git archive`: d97ecde for the v1.2 file and
   jumpmap (task 109's gold 7ef9039, jumpmap 4788e0e), eeb64b5 for the v1.3 file (`GOLD_FORMAT=v1.3`). Locally at 2²⁹ the
   v1.2 files equal 109's and the v1.3 files equal WORKER18's (`3cd32881…`, `e932baaf…`). Moving jumpmap to main's commit
   is a decision for whoever next rebuilds the Tracer's inputs: its proof.json's `gold_commit` changes with it.
6. **v1.26.1's top folder README is v1.1's** (inherited, as WORKER25 noted for v1.26): the pipeline's README points at the
   spec and `acceptance/README.md` directly.

## Log

- 2026-11-05: Quest 111, 112 claimed. Small runs built and checked locally (22 s and 15 s after a 3-minute tracer build on
  one CPU), reproduced by a second build and by a kit build with the libelf fallback; tasks 111/112 pushed.
