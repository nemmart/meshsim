# Cleanups from review

Items found as people read the spec closely: things that **work but could be cleaner**. Each is fixed in the working line
(`wip/`: the spec, the simulator, the software), and, where frozen v1.2 has it, recorded in `v1.2/ERRATA.md` too. ("Frozen"
is loose here: v1.2 is a waypoint we stepped off from, not a sealed release.) Several are fixed together by one session where
possible.

| # | found | item | v1.2 has it | fixed in | status |
|---|---|---|---|---|---|
| CL1 | owner, 2026-11-01 | **The replayer's queue ports have uneven ranges** (4.19.10): the code queue takes 1 MB (0x100000-0x1FFFFF), the two logs 512 KB each within part 2 (0x200000-0x27FFFF, 0x280000-0x2FFFFF), the expansion queue 1 MB (0x300000-0x3FFFFF). The sizes mean nothing (a queue port is effectively one address; the range only lets an advancing stream keep hitting it), and the uneven split makes the decode a two-level one (bits [22:20], then bit 19 for the logs). **Fix: one part per queue, every part 1 MB, so bits [22:20] alone name the destination:** 0 the memories (unchanged), **1 code queue (0x100000), 2 read-log queue (0x200000), 3 write-log queue (0x300000, from 0x280000), 4 expansion queue (0x400000, from 0x300000)**, 5-7 unmapped | yes (E7) | spec 4.19.10; meshsim's replayer model; `replay_ctl.c`; recipe 26 and the book; then the replayer's tests and a short acceptance check | to do |
| CL2 | owner, 2026-11-01 | **The decode table's organization is unstated**, and the parameter row's "decode table 2K × 16 bits" reads like a physical shape that would need eight banks to take a 128-bit packet per cycle. **Fix: an implementation note**: built naturally as 256 × 128, one row per packet, a lookup reading row *e* >> 3 and selecting 16 bits by *e* & 7 | yes (E8) | spec 4.19.10 and the parameters | with WORKER10, if sent |
