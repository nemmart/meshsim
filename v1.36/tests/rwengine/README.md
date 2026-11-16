# tests/rwengine: the RW engine against the RTL

*For anyone who needs to trust or remake the RW engine's references: what each file is, and the RTL run that made it.*

The RW engine's model (`src/units/rw_engine.h`, in the peripheral slot) is checked against the RTL's unit, Snarky One's
`rtl/blocks/rw_engine_unit`, by what the RTL's own simulation did. (It was also checked against the RTL's chip under
the RTL's own firmware, every handshake on `lpddr` in its cycle, up to meshsim's commit 5a87d658, where this
folder holds that run's handshakes and the instrumentation that printed them; the chip now runs meshsim's firmware, whose
cycles are no longer compared: `tests/test_memory.cpp`.) Nothing here
was made by the simulator, and nothing here is regenerated from it.

| file | what | read by |
|---|---|---|
| `unit.fp` | for each bench of the RTL's unit test (`rw_engine_unit_test.rs`; a bench is named by its seed), a fingerprint of every output of the RTL's `rw_engine_unit` in every cycle: at the bench's end, with its cycles, and every 16,384 cycles | `test_rw_engine`: its bench is the RTL test's, ported with its random generator and seeds, and the model's outputs must give the same fingerprints |
| `rtl-instrument.patch` | the instrumentation of `rw_engine_unit_test.rs` that printed `unit.fp` | this page |

## What a fingerprint covers

In every cycle that is not a reset's, in this order, each a 64-bit word mixed in (`h = (h ^ x) * 0x100000001b3`, then
`h ^= h >> 29`, from `0xcbf29ce484222325`): a load's read data, if the register is defined; one word of `event_peripheral`,
`error_peripheral`, `packet_valid`, ARVALID, AWVALID and WVALID; the packet offered (dst, mode, counter, size, address,
payload; not its src, which the RTL's port does not carry: the router stamps it, 16.4); the AR channel while ARVALID
(address, length, size, burst); the AW channel while AWVALID; the W channel while WVALID (data, WLAST, WSTRB).
What the specification leaves undefined is left out, on both sides, by the test's reference model: a register never
written, the words of a write buffer row never written (on W, and in a packet whose beat LPDDR holds from such a row:
the RTL's flip-flops and SRAM rows start at random values, the simulator's at 0).

## How they were made

Snarky One at `076e647` (its `rtl/` is `370e3ac`'s), in a scratch worktree (`mise run agent-new <name> shared`), with
the patch applied (`git apply`), never committed:

```sh
mise run build && mise run sort-images
# unit.fp: the lines `RWFP <seed> end|at <cycle> <fingerprint>`
mise exec -- cargo test --quiet --locked --offline -p blocks_rw_engine_unit -- --nocapture --test-threads 4
```

The RTL's test passes with the instrumentation as without it: it prints, and keeps which data are defined.

## The figures

| run | the RTL | the simulator |
|---|---|---|
| the unit test's benches: registers (three configurations), read timing, read room, write timing, write port, reply, LPDDR error, reset | 59, 59, 59, 29, 608, 63, 67, 57, 81, 327 cycles | the same cycles, the same fingerprints |
| the unit test's random jobs: defaults, least, most | 281,503, 248,366 and 83,326 cycles | the same cycles, the same fingerprints at every checkpoint (17, 15, 5) and at the end |
