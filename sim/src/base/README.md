# src/base: the low-level structures

*For anyone reading or changing the framework's internals: what each structure is, and whether the cycle loop may use
it.* Unit authors don't use these directly: `src/sim.h` is their API. Each header states its own rules.

| file | what | where |
|---|---|---|
| `fatal.h`, `fatal.cpp` | `FATAL` and `DCHECK`, the only error handling: no exceptions; a message that doesn't allocate (D24) | everywhere |
| `alloc_guard.h`, `alloc_guard.cpp` | any allocation or deallocation in the cycle loop is fatal, in every build | the cycle loop's guard |
| `arena.h`, `arena.cpp` | permanent state, allocated once at configuration | configuration |
| `ring.h` | a FIFO of fixed-width slots of 64-bit words; full is fatal | the cycle loop |
| `bucket.h` | one delivery bucket: an append-only array of fixed-width entries | the cycle loop |
| `bitmap.h` | n bits in 64-bit words, n set at init | the cycle loop |
| `value_pool.h` | a thread's pool of fixed-size slots for payloads passed by reference, with its free indices | the cycle loop |
| `words.h` | the storage conventions the cycle-loop structures share | the cycle loop |
