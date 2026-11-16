# What v1.37 adds to v1.36

*For anyone who knows v1.36: what this folder has that v1.36 does not, and where each piece is specified.* In
progress; it grows with the work (`../updater_wip/planning/sessions/WORKER3/NOTES.md` has the plan).

**The specification is not changed** (`spec/` is v1.36's). What v1.37 adds is specified by the design documents of
`../v1.4_wip/planning/`: `INGRESS-INTERFACE.md`, `UPDATER-INTERFACE.md`, `HOST-FRAMES.md` and `HOST-LINK.md`.

**v1.36's machine is unchanged.** The additions are a different master tile, built only when a machine asks for it
(`MachConfig::host_link`, off by default).

| what | where | specified by | status |
|---|---|---|---|
