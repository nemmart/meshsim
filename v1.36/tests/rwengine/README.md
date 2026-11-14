# tests/rwengine: the RW engine against the RTL

*For anyone who needs to trust or remake the RW engine's references: what each file is, and the RTL run that made it.*

The RW engine's model (`src/units/rw_engine.h`, in the peripheral slot) is checked against the RTL's unit, Snarky One's
`rtl/blocks/rw_engine_unit`, and against the RTL's chip, `rtl/chip`, by what the RTL's own simulation did. Nothing here
was made by the simulator, and nothing here is regenerated from it.

| file | what | read by |
|---|---|---|
| `unit.fp` | for each bench of the RTL's unit test (`rw_engine_unit_test.rs`; a bench is named by its seed), a fingerprint of every output of the RTL's `rw_engine_unit` in every cycle: at the bench's end, with its cycles, and every 16,384 cycles | `test_rw_engine`: its bench is the RTL test's, ported with its random generator and seeds, and the model's outputs must give the same fingerprints |
| `chip-memory-random.ev`, `chip-memory-fixed0.ev`, `chip-memory-fixed3.ev` | the RTL chip's `lpddr` port in `chip_memory_test`: every handshake (AR, AW, W, R, B) with its cycle and values, and the cycle count at which the master's done word is in its DMEM; with the test's slave as it is (random readies and latencies from its seed), and made deterministic (every ready 1, each latency 0, or 3) | `test_rtl_memory`: the same slave on the simulator's port, and every line must be the simulator's |
| `rtl-instrument.patch` | the instrumentation of `rw_engine_unit_test.rs` that printed `unit.fp` | this page |
| `rtl-chip-instrument.patch` | the instrumentation of `chip_memory_test.rs` that printed the `.ev` files | this page |

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

Snarky One at `076e647` (its `rtl/` is `370e3ac`'s, the commit `fw/rtl/` was copied from; the two images used,
`build/sort/memory/master.elf` and `build/sort/memory-tile/memory.elf`, have `fw/rtl/sort/`'s sha256), in a scratch
worktree (`mise run agent-new <name> shared`), with the two patches applied (`git apply`), never committed:

```sh
mise run build && mise run sort-images
# unit.fp: the lines `RWFP <seed> end|at <cycle> <fingerprint>`
mise exec -- cargo test --quiet --locked --offline -p blocks_rw_engine_unit -- --nocapture --test-threads 4
# chip-memory-random.ev: the lines `MEMEV <cycle> <channel> <a> <b>` and `MEMDONE <cycles>`
mise exec -- cargo test --quiet --locked --offline -p chip -- --nocapture --exact chip_memory_test::chip_memory_test
# chip-memory-fixed0.ev and chip-memory-fixed3.ev
MS136_FIXED=0 mise exec -- cargo test --quiet --locked --offline -p chip -- --nocapture --exact chip_memory_test::chip_memory_test
MS136_FIXED=3 mise exec -- cargo test --quiet --locked --offline -p chip -- --nocapture --exact chip_memory_test::chip_memory_test
```

The RTL's tests pass with the instrumentation as without it: it prints, and in the unit test keeps which data are
defined; the chip test's `MS136_FIXED` replaces its slave's draws only when set.

## The figures

| run | the RTL | the simulator |
|---|---|---|
| the unit test's benches: registers (three configurations), read timing, read room, write timing, write port, reply, LPDDR error, reset | 59, 59, 59, 29, 608, 63, 67, 57, 81, 327 cycles | the same cycles, the same fingerprints |
| the unit test's random jobs: defaults, least, most | 281,503, 248,366 and 83,326 cycles | the same cycles, the same fingerprints at every checkpoint (17, 15, 5) and at the end |
| `chip_memory_test`, the done word; the first and the last handshake on `lpddr` | after 35,665 cycles; in cycles 8,579 and 9,171 | the same, and every handshake between |
| the same, every ready 1 and latency 0 | 35,645; 8,579 and 9,151 | the same |
| the same, latency 3 | 35,654; 8,579 and 9,160 | the same |
