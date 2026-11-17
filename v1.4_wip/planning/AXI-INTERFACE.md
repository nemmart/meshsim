# The path to LPDDR: a client's AXI port, the AXI arbiter and the LPDDR model

Written 2026-11-16 (`updater_wip`, WORKER5) and **agreed at the review with the owner the same day.** It is what
WORKER5 builds the arbiter and the LPDDR model to and WORKER6 the Updater's controller. A dagger (†) marks a choice that is proposed here and not yet
ruled on. Where a line says (owner), the design notes record it as the owner's ruling. The open questions are at the
end.

**What it is for.** With `CACHE-INTERFACE.md` and `UPDATER-INTERFACE.md`, this is all a worker needs to build a
client of LPDDR, the Updater's controller, so that it works with the cache's controller the first time they meet. The
two are built by two workers who share no code (owner); this document and its header are what they share.

**The C++ types** are `v1.37_wip/src/units/axi_port.h`: no logic, what both workers compile against.

**Where it comes from:** `DESIGN-NOTES-2026-11-15.md` sections 2 and 7 (the bus driver, which became the AXI
arbiter); `DESIGN-NOTES-2026-11-16.md` section 4 ("What the sequence rests on"); `RTL-CHANGE-REQUESTS.md` comments A
and B; v1.36's `lpddr` port (`v1.36/spec/machine-spec.md` 3, 17.7, 19) and its models (`v1.36/src/units/rw_engine.h`,
`v1.36/tests/axi4_slave.h`). In v1.35's names the arbiter was "the bus driver" and before that "the splitter".

```
 the cache's controller ──┐
 the Updater ─────────────┤  a port each:              ┌───────────────┐  one port:          ┌──────────────────┐
 the RW engine ───────────┼─ AXI4, no ID signals ─────►│ the AXI       │─ AXI4 with IDs ────►│ the LPDDR        │── LPDDR
 the Binder (later) ──────┘                            │ arbiter       │                     │ controller + PHY │
                                                       └───────────────┘                     └──────────────────┘
```

## Terms

| term | meaning |
|---|---|
| **a client** | a unit with an AXI port of its own to the arbiter: the cache, the Updater, the RW engine; the Binder later (owner, with Tony: every client of LPDDR talks AXI) |
| **the controller** | the LPDDR controller with its PHY. AXI is its own front: there is no AXI controller apart from it (owner) |
| **a beat** | 16 bytes: one transfer of the 128-bit data bus |
| **a burst** | one AXI transaction: an address, then its beats (and for a write, its response) |
| **a piece** | what the arbiter makes of a burst on its way to the controller: at most `TURN_BEATS` beats (section 3) |
| **outstanding** | a read burst from its address's handshake until its last beat; a write burst from its address's handshake until its response |

## 1. A client's port

**Each client has a port of its own** (owner). It is v1.36's `lpddr` port (machine-spec 3), with its rules unchanged:
AXI4 (ARM IHI 0022), the client the master, the arbiter the slave.

| channel | signal | bits | driven by | meaning |
|---|---|---|---|---|
| AR | `arvalid` | 1 | client | |
| | `arready` | 1 | arbiter | |
| | `araddr` | `LPDDR_ADDRESS_BITS`, 32 | client | the burst's first byte; a multiple of 16 |
| | `arlen` | 8 | client | the burst's beats less one, 0 to 255 |
| | `arsize` | 3 | constant 4 | 16 bytes a beat |
| | `arburst` | 2 | constant 1 | INCR |
| R | `rvalid` | 1 | arbiter | |
| | `rready` | 1 | constant 1 | the client takes every beat in the cycle it comes |
| | `rdata` | 128 | arbiter | the beat at byte address *a* holds LPDDR's bytes *a* to *a* + 15, byte *b* at bits [8*b* + 7 : 8*b*] |
| | `rresp` | 2 | arbiter | 0 `OKAY`, 1 `EXOKAY`, 2 `SLVERR`, 3 `DECERR` |
| | `rlast` | 1 | arbiter | the burst's last beat |
| AW | `awvalid`, `awready`, `awaddr`, `awlen`, `awsize`, `awburst` | as AR's | | a write burst's address |
| W | `wvalid` | 1 | client | |
| | `wready` | 1 | arbiter | |
| | `wdata` | 128 | client | as `rdata` |
| | `wstrb` | 16 | constant all ones | every byte written † |
| | `wlast` | 1 | client | the burst's last beat |
| B | `bvalid` | 1 | arbiter | |
| | `bready` | 1 | constant 1 | the client takes every response in the cycle it comes |
| | `bresp` | 2 | arbiter | as `rresp` |

- **AXI4's other signals are not on the port** and take their defaults, as on `lpddr`: no ID signals (below), no
  lock, cache, protection, QoS, region or user signals. So no access is exclusive, and an `EXOKAY` is an error.
- **Every output is a flip-flop or a constant,** the client's and the arbiter's (machine-spec 3's rule for `lpddr`).
  A `valid`/`ready` pair moves one item in the cycle both are 1; a sender raises `valid` without waiting for
  `ready`, and holds `valid` and the item until it moves (machine-spec 3, "Handshakes").
- **A client takes every beat and every response** (`rready` and `bready` 1, as on `lpddr`), so the arbiter never
  holds one back for a client.

### The address

- **A byte address** of `LPDDR_ADDRESS_BITS` bits (machine-spec 19: 24 to 36, 32 by default). † One address space:
  the controller drives both LPDDR4X channels as one memory (second note, section 2, proposed).
- **The other forms of an LPDDR address** in the design: a root, an index entry's page and a free page are in
  128-byte units (`UPDATER-INTERFACE.md`): the byte address is the unit times 128, and a page's is a multiple of 4096.
  The RW engine's registers hold beat addresses, the byte address over 16 (17.7).
- An address past LPDDR's end is answered `DECERR` by the model (section 5) †. It is a program error, as on `lpddr`.

### A burst's rules

| rule | |
|---|---|
| the type | INCR, 16-byte beats (`AxSIZE` 4) |
| the length | 1 to 256 beats (`AxLEN` 0 to 255) |
| the address | a multiple of 16 |
| **never across a 4 KB boundary** | AXI's rule (second note, section 2); a burst that crosses one is a client's error |
| a write's beats | exactly `AxLEN` + 1, `wlast` on the last and on no other; every byte enabled † |

### The sizes in use

The second note's three sizes are the clients' bursts (owner): on a 128-bit bus a line is 8 beats, a half page 128 and
a page 256.

| what | bytes | beats | `AxLEN` | aligned to | used by |
|---|---|---|---|---|---|
| **a line** | 128 | 8 | 7 | 128 † | the cache: a fetch, a dirty line's write-back, an update's write-through |
| **half a page** | 2 KB | 128 | 127 | 2 KB † | the Updater |
| **a page** | 4 KB | 256 | 255 | 4 KB, by the 4 KB rule | the Updater; the RW engine, the file system's page moves |
| any other | 16 *n* | *n* ≤ 256 | *n* − 1 | 16 | the RW engine's jobs: bursts to the end of an aligned group of 4 beats (17.7) |

Each size aligned to itself never crosses 4 KB.

### IDs

- **No ID signals on a client's port.** All of a client's transactions are one ID, as all of the RW engine's are on
  `lpddr` today.
- **The arbiter gives client *c*'s transactions the ID *c*** on its way to the controller, and steers each beat and
  response back by its ID (owner: "each client has its own ID, and the arbiter steers by it").

| ID | client |
|---|---|
| 0 | the cache |
| 1 | the Updater |
| 2 | the RW engine |
| 3 | the Binder, later; a test's client meanwhile |

† `ID_BITS` is 2.

## 2. What a client may count on

| a client may count on | a client may not count on |
|---|---|
| every burst answered, in bounded time: a read's every beat, a write's response | any latency |
| its reads answered in the order it raised them, and its writes likewise: they are one ID | any order between a read and a write, its own included, even of the same bytes |
| a write's response comes when LPDDR holds the write: a read raised after the response sees it, on any port | any order against another client's bursts |
| its read beats in address order, `rlast` on the burst's last | the beats of a burst back to back: other clients' pieces come between (section 3) |
| its ready rising again: the arbiter takes every burst in time | a ready in any given cycle |

- **A client that reads what it wrote waits for the write's response first** (second note, section 2: reads and
  writes are not ordered against each other). The same holds between clients: a read sees another client's write only
  if it was raised after that write's response, which is why the cache's `update` holds its reply until its write's
  response (`CACHE-INTERFACE.md`, section 3).
- **Bursts outstanding** †: a client may have up to `CLIENT_BURSTS`, 4, outstanding in each direction. Beyond that the
  arbiter keeps its `arready` or `awready` at 0 until one is answered, so a client needs no count of its own.
- **A write's beats back to back** †: once a client has raised a write's address, it offers the burst's beats a beat
  a cycle, `wvalid` 1 from the first to the last, waiting for nothing but `wready`. It may offer them before its
  address is taken, as AXI allows. The arbiter passes one write piece at a time (section 3), so a client that pauses
  inside a burst holds up every client's writes.
- **An error response** is `rresp` or `bresp` other than `OKAY` (`SLVERR`, `DECERR`, and `EXOKAY`, since no access is
  exclusive).
  - A read beat with an error carries undefined data, and the burst still runs to its `rlast`.
  - A write answered with an error leaves LPDDR's bytes under the burst undefined.
  - What the client does about it is its own: the RW engine sets `ERRORS.lpddr_error` (17.7), the Updater its
    `ERRORS.lpddr_error` (`UPDATER-INTERFACE.md`), the cache answers its request with `lpddr_error`
    (`CACHE-INTERFACE.md`).
- **The reset** empties every channel of every port and of the arbiter; nothing accepted before a reset is answered
  after it (machine-spec 3, `lpddr`). † The clients, the arbiter and the controller's port are reset together.

## 3. The AXI arbiter

The arbiter is the only thing on the controller's port (owner), and only arbitrates: it picks whose request goes to
the controller next and steers the data and the responses back (second note, section 7).

| | |
|---|---|
| **round robin** | (owner) a pointer: the turn goes to the first client after the last one served that has something waiting. First come, first served was the alternative |
| **reads and writes apart** | proposed and not objected to (second note, section 7): two arbiters, one for AR, one for AW with its W beats, as AXI's address channels are independent |
| **a turn is one piece, of at most `TURN_BEATS` beats** | proposed and not objected to: "say 32 beats (512 bytes), so that a cache line never waits behind a whole page". † `TURN_BEATS` is 32 |

### What a long burst becomes

- **A burst of at most `TURN_BEATS` beats** goes to the controller as it is: one piece. A line is one piece.
- **A longer burst is cut into pieces.** A burst of *n* beats at byte address *a* becomes ⌈*n* / 32⌉ pieces: piece *k*
  starts at *a* + 512 *k* and has min(32, *n* − 32 *k*) beats. A page is 8 pieces, a half page 4.
- **Every piece carries the client's ID.** None crosses 4 KB, because the burst does not.
- **A client's pieces go in order,** and another client's piece may go between two of them. So a client moving a page
  alone has eight turns in a row; with the cache asking too, the cache's line goes after at most one piece of each
  other client.

### What a client sees of it

| | |
|---|---|
| **a read** | its burst's beats in address order, each with the `rresp` the controller gave it, and `rlast` only on the burst's last beat: the arbiter drops the pieces' own `rlast` but the last. Between two pieces there may be other clients' beats, so gaps |
| **a write** | it sends the burst's *n* beats with `wlast` on the *n*th. The arbiter passes them on as the pieces' beats, with `wlast` on each piece's last. **It gets one response,** when every piece's has come: `OKAY` if every piece's was, else the first piece's that was not |

So a client sees its burst exactly as AXI says, as if it were alone on the controller, only slower: nothing in a
client depends on the cutting.

### Turns, exactly †

- **Reads.** The AR arbiter gives the turn to one piece a cycle at most. A piece's address goes to the controller as
  soon as it has the turn; the arbiter does not wait for the last piece's data.
- **Writes.** The AW arbiter passes **one write piece at a time:** the piece's address, then its beats on W, and only
  then the next turn. W beats leave in the order of their addresses, which AXI4 requires (it has no write
  interleaving), and a line's write waits behind at most one piece of each other client.
- **What it keeps for each client:** an AR slot and an AW slot, each one burst being cut, the slot's `ready` 1 while
  it is empty; and in each direction a list of the client's outstanding bursts and their pieces, so that it knows
  which beat is a burst's last and when a write's response is due. `CLIENT_BURSTS` bounds the lists.
- **Every output a flip-flop:** an address, a beat and a response each take one cycle through the arbiter. A client's
  address taken in cycle *t* is at the controller from *t* + 1 at the earliest; a beat or a response from the
  controller in *t* is at the client in *t* + 1.
- **Weighting the turns toward the cache,** if the Tracer is found stalling behind background work (second note,
  section 7, proposed), is not built: the figures will say whether it is needed.

## 4. The arbiter's port to the controller

It is a client's port, AXI4, with ID signals added: the comment to Tony, `RTL-CHANGE-REQUESTS.md` A (owner: the port
to the memory controller supports multiple IDs, so that a cache line can come back ahead of a page requested
earlier).

| channel | added signal | bits | |
|---|---|---|---|
| AR | `arid` | `ID_BITS` | the client's number |
| R | `rid` | `ID_BITS` | the arbiter steers the beat by it |
| AW | `awid` | `ID_BITS` | |
| B | `bid` | `ID_BITS` | the arbiter steers the response by it |

- **No `wid`:** AXI4 has none; the W beats follow the order of the addresses.
- `rready` and `bready` are 1: the arbiter takes every beat and response and passes it to its client, which takes it.
- **The controller's rules,** `lpddr`'s (machine-spec 3), kept: it answers every burst in bounded time; it answers the
  reads of one ID in order and the writes of one ID in order; it sends no write response before both the write's
  address and its last beat have been taken; it answers nothing accepted before a reset after it.
- **Between IDs it may answer in any order,** and AXI4 lets it interleave the read beats of different IDs beat by
  beat. The arbiter steers every beat by its `rid`, so it needs nothing more for that.

## 5. The LPDDR model in the simulator

LPDDR and its controller are not chip hardware: the controller's port is the simulator's boundary, as `lpddr` is in
v1.36 (`rw_engine.h`, "THE LPDDR PORT"). The model stands behind the arbiter.

### One memory

- **The model's memory is the file store's pages** (`v1.37_wip/src/units/file_store.h`, pages by address), so the
  images the Updater builds and the lines the cache and the Tracer read are one memory. In v1.36 the RW engine's
  LPDDR and the file store's are two models (third note, section 14); with the setting on there is one.
- **A read of bytes nothing has written** returns zeros, and is counted as a fault of the run: a test fails on it †.
- **When the memory changes** †: a read beat takes its data from the memory in the cycle it is sent, and a write's
  beats reach the memory in the cycle its response is sent. So a read and a write of the same bytes that overlap may
  see either, and only the wait for the response orders them, as on a real controller.

### Its timing

| parameter | default † | meaning |
|---|---|---|
| `read_latency` | 70 | from a read's address handshake to its first beat, at the earliest |
| `write_latency` | 70 | from the later of a write's address handshake and its last beat's, to its response, at the earliest |
| a beat | one a cycle on R, one a cycle on W | 16 GB/s each way at 1 GHz |
| `max_reads`, `max_writes` | 16 each | bursts taken and not yet answered; beyond, `arready` or `awready` 0 |

- **The defaults make a line about 80 cycles,** the figure the notes use (second note, section 2): from a client's
  address handshake, a cycle through the arbiter, 70, the 8 beats, a cycle back.
- **R:** one beat a cycle, a burst's beats back to back. When a burst ends, the next is chosen round robin among the
  IDs whose oldest unanswered burst is due †; within an ID, in the order taken. So the cache's line waits for at most
  one piece of each other ID once it is due: what the IDs are for (owner).
- **W:** beats taken in the order of the addresses, a beat a cycle while `max_writes` is not reached.
- **B:** each response when it is due; within an ID in the order taken.

### Two modes

| mode | what |
|---|---|
| fixed (the default) | every ready 1 but at a limit; every latency as above |
| jitter (with a seed) † | each burst's latency drawn from 20 to 200; each ready 0 one cycle in four; the R channel's choice among the IDs that are due at random. Every AXI rule and the order within an ID are kept. For catching a client that counts on a latency, or a read that overtakes a write (second note, section 2: "a mode that varies the delays") |

### Errors: how a test makes it answer with one

| | |
|---|---|
| **a fault** | a test sets a byte range, a direction (reads, writes or both), a response (`SLVERR` or `DECERR`) and a number of bursts (0: every one). Every burst that touches the range answers with that response until the number is used: a read on every beat, its data `0xDEADBEEF` repeated; a write on its response, and it writes nothing † |
| **past LPDDR's end** | `DECERR`, as a fault: a read's beats `0xDEADBEEF`, a write writes nothing |
| **a burst that breaks a rule** | across 4 KB, an address not a multiple of 16, a size or a type not 4 and INCR, a `wlast` out of place, an ID that is no client's: the model stops the simulator, as a checker |

The model also watches each channel's handshakes (`tests/axi4_slave.h`'s `Handshake`, reused): a request withdrawn
or changed while it waited stops the simulator.

## 6. What v1.36 has, and what changes

| in v1.36 | here |
|---|---|
| `lpddr` (machine-spec 3, 17.7): the RW engine's AXI4 port, 128-bit data, one ID, the controller straight behind it | **the RW engine's client port, unchanged.** The arbiter goes between it and the controller (`RTL-CHANGE-REQUESTS.md` B: "the RW engine plugs in unchanged"); the controller's port gains IDs (A) |
| `src/units/rw_engine.h`: `LpddrOut`, `LpddrIn`, `LpddrSlave`. AR and AW share one address and length register, since a job is a read or a write | **kept** for the RW engine. The new clients use `src/units/axi_port.h`'s types, whose AR and AW are apart, since the cache reads and writes at once. The RW engine joins the arbiter through an adapter that copies its one address and length to both channels |
| `tests/axi4_slave.h`: `UnitLpddr` and `ChipLpddr`, the RTL tests' controllers, each with a memory of its own; `Handshake` | **kept** for v1.36's tests, unchanged. The LPDDR model of section 5 is new, with IDs and on the file store. `Handshake` is reused |
| the RW engine's LPDDR and the file store, two memories | **one,** with the setting on (third note, section 14) |
| machine-spec 3's rules for the controller | kept, with the order within an ID added |

## 7. The C++ header

`v1.37_wip/src/units/axi_port.h`, types and constants only:

| in it | what |
|---|---|
| the bus's constants | the beat, `AxSIZE`, INCR, whole beats, 256 beats, 4 KB, `LPDDR_ADDRESS_BITS`, the three sizes, the 128-byte unit |
| `Resp` | `OKAY`, `EXOKAY`, `SLVERR`, `DECERR` |
| `Client`, `ID_BITS` | the IDs of section 1 |
| `MasterOut`, `SlaveOut` | a port's signals in one cycle, by who drives them; the ID fields are the controller's port's, and 0 on a client's |
| `ArbiterConfig`, `LpddrTiming`, `LpddrFault` | the parameters of sections 3 and 5 with their defaults, and a test's fault |

**A cycle, in the simulator:** each unit's outputs in cycle *t* are a function of its own registers alone (every
output a flip-flop); a simulator computes every unit's `MasterOut` and `SlaveOut` for *t* first, then steps each unit's
edge with the other side's outputs of *t*. A unit never reads another's insides.

## Open

1. **`TURN_BEATS`, 32** †. Smaller cuts a line's wait and costs the controller efficiency; the figures of Part B will
   show the cache's waits.
2. **`CLIENT_BURSTS`, 4** † a direction. The cache needs two writes at once (an update's and a write-back) and one
   read; the Updater one or two of each.
3. **One ID a client, `ID_BITS` 2** †. A client with reads that must pass each other would need two.
4. **One write piece at a time** †, against letting addresses run ahead of the W beats. The first keeps a line's write
   behind at most one piece of each client and asks clients to send W beats back to back.
5. **Weighting the turns toward the cache** (second note, section 7): not built; to decide on the figures.
6. **Whole beats only** †: no client writes part of a beat, so `wstrb` is a constant. A 32-bit write of an index entry
   is a line's write instead (`CACHE-INTERFACE.md`).
7. **A cycle each way in the arbiter** †: whether the RTL needs the registers on every output.
8. **The model's latencies, its round robin among IDs, and what it reads where nothing was written** are a model's: a
   real controller's policy and its figures come with the controller chosen. Its rules (section 4) are what a client
   relies on; its figures are not.
9. **`LPDDR_ADDRESS_BITS`:** 32 by default, so 4 GB, where the 31-bit stored form reaches 256 GB.
10. **The figure** (`diagrams/svg/fig_m1_lpddr_path.svg`) shows the buses rightly but still has the Updater by the RW
    engine; the arbiter, the ports and the IDs to be drawn when it is redone.
