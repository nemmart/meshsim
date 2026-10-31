# Pending for WORKER9's next prompt (integrator, 2026-11-01)

> Outstanding work: a full 2^29 program proved end to end in 17 minutes. First, commit and push the writer changes, and end
> every turn with a clean git status, reserving the calls for it.
>
> 1. The rows' signed immediate is decided (DECISIONS-v1.25 section 7, confirmed in WORKER10's v1.25 spec on branch v1.25):
>    a flag, **imm_signed at word 0 bit [86]**, equal to the expansion word's sign_imm; the 64-bit immediate stays raw. Set it
>    in the reference rows and check it in the functional replay.
> 2. Your plan for the runner is right, and the log now says so (section 7, revised): one held writer task per program
>    produces its delivery on the runner box and keeps it there for WORKER11's acceptance shards, then it's discarded. Check the
>    box's free disk for two programs at about 31 GB each, and tell WORKER11's kit where the deliveries live (path, manifest,
>    checksums).
> 3. Q8 is open again (DECISIONS-v1.3 section 11, T7): at about 4 cycles per row, the single Fermat loop puts EVM Zone's first
>    proof at about 145 ms against a 100 ms Tracer budget. Keep the loop as built; the owner and Tony are deciding between an
>    addition chain and an inverse unit.
>
> Then your list as planned: Cyclone at 2^29, the selected windows, the held runner task, and the part 3 sections of REPORT and
> the README.
