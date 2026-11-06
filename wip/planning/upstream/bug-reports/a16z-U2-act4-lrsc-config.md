# a16z/jolt: the ACT4 configuration lets SC.D succeed after LR.W; the tracer fails it

**Where**: `tests/arch-tests/jolt/jolt-rv64imac.yaml` (`LRSC_FAIL_ON_NON_EXACT_LRSC: false`) against
`tracer/src/emulator/cpu.rs:535-547` (`reservation_covers`: SC.D needs a doubleword reservation; LR.W makes a word one),
asserted by `reservation_covers_follows_the_lr_sc_width_rules` (`:1677-1695`).

**What happens.** ACT4 `Zalrsc-sc.d-00` check 241 ("cp_custom_sc_lrsc; bin: prev_lr_lr.w") runs `lr.w zero, (t4)` then
`sc.d t6, s2, (t4)` at the same address. Sail, configured as above, lets the SC.D succeed (expected rd = 0); the tracer
fails it (rd = 1), so the test fails. It passes in CI only because of U1.

**Suggested fix**: set `LRSC_FAIL_ON_NON_EXACT_LRSC: true` if the tracer's width rule is intended (it matches the
ratified A extension's "the reservation set contains the bytes being written"), or change the rule; then fix U1 so
CI would catch a recurrence.
