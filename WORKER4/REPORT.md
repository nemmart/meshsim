# WORKER4 report: meshsim to v1.2

*In progress (phases 1-4 done; 5 and 6 to come). The final version lists what was built, every decision of my own,
the differences from the draft header, the measurements and what the sort and replayer ports need.*

## Known gaps at v1.2's freeze

- **The boot ROM's controller branch is untested.** The ROM (sw/bootrom.S, 6.2) branches on PARAMS' endpoint id; its
  tile branch (0-63) runs in every suite, but its branch for C&C 1-3 (64-66: wait for "command landed", restore the
  mask, jump) never runs in meshsim, because C&C 1-3 are behavioural Java models without cores (6.2's boot sequence,
  step 3: "In meshsim v1.2, C&C 1 stays a behavioural Java model").
- **C&C 2/3's "started" requests (6.2, step 4) aren't modelled**: their images and firmware don't exist yet, so C&C 1's
  model never waits for them. The request tree carries a controller's requests (RequestBus.controllerSend, tested in
  RequestBusTest), so the path is there when the firmware is.

## Stale, not ported

- `test/SplitRun.java` and `sw/ext/split-bisect/splitdrv.c` (WORKER8's R3, owner: leave both unported).
