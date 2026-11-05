# Worker 27: the input pipeline, so the simulation's files can be rebuilt from the repository

Tony (RTL) is building the replayer from v1.26.1 and will verify it against the same inputs meshsim's acceptance uses. Today
those inputs are scattered: jolt-cpp's rows and WORKER9's deliveries in the Quest runner's persistent data, built from
jolt-asic-planning; gold's segment files from wip/gold/; the jump map, initial image and page lists from wip/jumpmap/ (at 2²⁹ only
in the runner's data, Quest 109); the v1.25 to v1.26.1 conversion run inside Quest tasks. Your job is **one documented pipeline
in the repository that rebuilds all of them from the pinned sources on a fresh machine**, and proves the result identical to
what our acceptance runs used.

READ FIRST: the README's version table and v1.26.1/README.md; wip/planning/sessions/WORKER20-REPORT.md and WORKER25-REPORT.md
with their tools and tasks (how the deliveries were made, converted and accepted); WORKER22/tasks/ and NOTES.md (the Tracer's
inputs); sessions/WORKER26-REPORT.md and wip/jumpmap/README.md; wip/gold/'s README and check.sh; the Quest repository's
PIPELINE.md and the task scripts that wrote ~questrunner/data.

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: the pipeline in `wip/inputs/` on a branch, `inputs-pipeline`; notes and report
  in `WORKER27/` on main.
- **jolt-asic-planning**, https://github.com/Quarky93/jolt-asic-planning (Tony's): the guests, their inputs, jolt-cpp's tracer at
  88dca85, capture_writes and delivery_writer, on tracer-standalone-main (fa59d9a). The pipeline clones it at a pinned commit.
- **Quest**: the runner's copies, to check against.
- One token from your user preferences works for meshsim and jolt-asic-planning; never write a token into a file.

THE WORK
1. **Dependencies, documented and checked:** what a fresh Ubuntu 24 machine needs (JDK, RISC-V toolchain, compilers, Python),
   with a script that checks them.
2. **One command per workload and size**, for EVM Zone and Cyclone, at the small sizes (trace-23, input/22) in minutes and at 2²⁹
   in hours, building in order:
   - jolt-cpp's rows and WORKER9's deliveries (v1.25), and **their conversion to v1.26.1, the RTL's target: first, since Tony
     needs it now**;
   - gold's segment files (v1.2 today, v1.3 once WORKER18's formats land; leave the hook);
   - jumpmap's table, initial image and per-segment page lists, and proof.json.
3. **Checksums for every output**, in a manifest in the repository, **matched against the runner's copies**, so a build is known
   right. Where a file has no runner copy, the manifest records the first build's.
4. **How to use them**: a README for Tony saying what each file is, which spec section defines it, and how meshsim's acceptance
   (stages A, B, F for the replayer; T1 for the Tracer) runs against them.

RULES
- **Propose a plan first and wait for the owner's go-ahead.**
- Reuse the existing tools and scripts rather than rewriting them; where one exists only inside a Quest task, move it into
  wip/inputs/ and point the task at it.
- Long runs go to the runner; background jobs don't survive a turn. Claim Quest numbers in NUMBERS.md first.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean git status.

DELIVERABLES: wip/inputs/ on inputs-pipeline (scripts, manifest, README), the checksum comparison for both workloads at both
sizes, WORKER27/REPORT.md.
