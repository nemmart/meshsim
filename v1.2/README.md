# v1.2: the spec as the RTL engineer took it

**Pure v1.2**, exactly as handed over on 2026-10-30 (git tag `v1.2-tony`): the spec (`spec/`), the recipes and draft header
(`sw/`) and the decisions (`planning/`). Its spec is byte-for-byte the one the v1.2 simulator work started from. **There is no
simulator here**: meshsim was still v1.1 at that moment, and the simulator was built against this spec in the following hours,
with fixes landing as it went.

- **`ERRATA.md`** lists every known error and cleanup in this spec, with the correction, and whether it affects the RTL.
- **`../v1.2.1/`** is this spec with every erratum applied, plus the simulator and software in line with it, verified.
- To see everything that changed: `git diff v1.2-tony:v1.2_wip/spec/machine-spec.md <later tag>:v1.2.1/spec/machine-spec.md`
  (or between the folders on `main`); `v1.2.1/CHANGES-v1.2.1.md` describes each change.
