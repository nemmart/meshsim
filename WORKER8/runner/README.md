# The sort's full-size P4 runs on meshsim v1.2 (WORKER8)

`run_p4.sh` and `kit/` are v1.1's runner job 1 (`wip/runner/p4/`) with the v1.2 kit: the same 43 P4 runs
(`kit/jobs.txt`, unchanged), the same key generator and key hashes; a new `meshsim.jar` (branch `v1.2-sort`, Java 17
bytecode), the v1.2 sort image (`image/sort.code.bin`, `sort.data.bin`, `sort.sym`: no overlays) and meshsim's
interim loader (`image/loader.bin`, PHASE-2 INTERIM), and a regenerated `HASHES`. Quest tasks 070 and 071 carried it
embedded. **Stale since WORKER4's phase 4** (the boot ROM replaced the interim loader): to use it again, rebuild
`meshsim.jar` from the branch (`javac --release 17`), copy the image from `sw/ext/sort/build/`, add meshsim's
`sw/bootrom.hex` as `kit/sw/bootrom.hex` (Controller reads it from the working directory), drop `image/loader.bin`,
and regenerate `HASHES` (`java -cp meshsim.jar Inputs hash ...`).

    ./run_p4.sh -j 8 OUTDIR          # the 43 runs at 64 x 65,536; -n 2000 for a smoke test
