# Things we want to add

Started 2026-11-19 (owner: "Let's start a file with things we want to add"). Each entry is something the owner has
asked for that is not in v1.37: what it is, why, what was settled in the discussion, and what it would touch.
(owner) marks what the owner said; a dagger (†) marks what was proposed and not ruled on.

This is not the list of open questions. Those are chapter 26 of `v1.37/spec/machine-spec.md`, and
`v1.37/OPEN-v1.37.md` for the simulator's.

## 1. Statistics for each host link, and the names "parent" and "child" (2026-11-19)

**The names** (owner). An ASIC has two host links. **The parent link** is the one towards the FPGA; **the child
link** is the one to the next ASIC on the daisy chain. ("Server" and "client" were the owner's other thought.
† Parent and child are preferred: the two ends of a link are equals in the protocol, and "client" already names
the arbiter's clients and the simulator's `--client`.) v1.37's specification says "up" and "down" (`link_up`,
`link_down`), so this is a rename there.

**The pins' names** (owner, 2026-11-19, going through the list for Tony): **`lvds_parent`** and
**`lvds_child`** for the two links, in place of `link_up` and `link_down`; and **`chain_end`** in place of
`chain_next`, with the opposite sense: 1 says this ASIC is the last on the daisy chain, so its child link is
unused and nothing is forwarded down, where `chain_next` 1 said another ASIC follows. v1.37 is frozen with the
old names; Tony is given the new ones (`v1.37/RELEASE-NOTE-v1.37.md`), and the specification takes them in v1.4.

**Why statistics.** The simulator's link end counts every kind of damage and recovery. The chip as specified
shows the core none of it: Ingress/Egress has no statistics words, and the only thing a core sees of a link is
`link_error` when it starts again. On real hardware nothing would say how noisy a link is. The owner wants an
overview of the kind "the link was working right 95% of the time".

**What the FPGA gives without the chip** (owner: "the FPGA will be able to give us a lot of this data itself"):

- everything about its own link to the first ASIC: its end is a link end like the chip's, and it can be
  reprogrammed, so richer figures (as `nvidia-smi` has for a bus) can be added there at any time;
- every link's use of its bandwidth: down the chain each link carries what the FPGA sent, and up the chain
  what the ASICs below it replied.

**What only the chips can give:** the health of the links between ASICs. That is what the counters are for, and
it cannot be added once the chip is made.

**The counters** †, twelve for each link, 32 bits each, 24 registers and about 768 flip-flops in all. They wrap,
and software takes the difference of two readings.

**Where they are** (owner): plain registers in Ingress/Egress's window, read by the core like any other.
**The parent link's at words 96 to 107, the child link's at words 112 to 123,** a block of 16 each, the same
counter at the same place in both. (The owner's first thought was 64 and 96; words 64 to 67 are the four host
words.)

| counter | counts |
|---|---|
| good received | link packets that passed both checks |
| bad headers | headers that failed their CRC |
| bad data | data checksums that failed |
| sent | link packets with data, sent the first time |
| sent again | link packets sent again |
| **lost sent** | every "lost" this end sent (owner: recovery in each direction) |
| **lost received** | every "lost" heard from the neighbour |
| restarts | times the link started again |
| foreground stops sent | times this end asked its neighbour to stop the foreground (owner: stop/start pairs) |
| background stops sent | the same for the background |
| foreground stops received | times the neighbour asked this end to stop the foreground |
| background stops received | the same for the background |

- **"Lost" is counted for each announcement, not for each time a link loses its place.** A lost end announces
  once a round, and a round is a fixed length (1,024 zeros and a link packet, about 1,040 cycles), so the count
  is a measure of time. A count of episodes would not give the owner's figure.
- **A stop is counted when it begins,** so one count is one stop/start pair. They are counted for each stream
  because they mean different things: a background stop is ordinary (the Updater slower than the link), a
  foreground stop is not.
- "Lost sent" against "lost received", and "stops sent" against "stops received", say which direction of a link,
  and which of two chips, is the one in trouble.

**What software works out from them:**

| figure | from |
|---|---|
| the share of time a link was working | 1 − (lost sent + lost received) × 1,040 ÷ the cycles elapsed |
| the link packets' error rate | (bad headers + bad data) ÷ (good received + bad headers + bad data) |
| what sending again cost | sent again ÷ sent |

**No firmware goes with them** (owner: "don't worry about the firmware support. These should just be mapped
into the registers"). How the host comes to read them, a command on the foreground stream or anything else, is
not part of this.

**What it would touch:** the specification (17.6's words, chapter 28's names and a list of what a link end
counts); the link's code (`hostlink.h` counts recovery rounds but not by direction, and not stops);
Ingress/Egress's model.

**When:** † the first change after v1.37 is frozen.

## 2. A countdown on Ingress/Egress that raises an error when it runs out (2026-11-19)

**What** (owner): "a 32-bit register that's RW on Ingress/Egress that's a master clock. It gets decremented by 1
every cycle. If it transitions from 1 to 0, it raises an error condition. If we write 0, it disables it."

Every row below is ruled: the first four are the owner's words, and the rest were proposed by the design
session and accepted (owner, 2026-11-19: "all your rulings are right").

| | |
|---|---|
| the register | 32 bits, read and written by the core like any other (owner) |
| counting | while it is not 0 it goes down by 1 every cycle, whatever the core is doing (owner) |
| running out | the step from 1 to 0 raises an error (owner) |
| a store of 0 | turns it off: no count and no error (owner) |
| a store of anything else | loads it and starts it; storing again before it runs out starts it afresh |
| a load | what it holds now |
| at the reset | 0, off |
| after it runs out | it holds 0 and is off until it is stored again |
| a store in the cycle it would step from 1 to 0 | the store wins, and there is no error |
| the error | a sticky bit of Ingress/Egress's `ERRORS`, the next free one, [4]; so its tile's error, which stops the chip, as every peripheral's error does |
| its name and place | `WATCHDOG`, at word 68, beside the four host words |

**What it is for:** a limit on how long something may take. Software sets it before a piece of work and sets
it again, or to 0, when the work is done; if the work hangs, the chip stops with an error and the host sees it,
where today it would sit for ever. At a count a cycle, 32 bits is 4.29 seconds at 1 GHz: "plenty. 32 bits is the right size" (owner).

**What it would touch:** the specification (17.6's words and its `ERRORS`); Ingress/Egress's model. About 32
flip-flops and a decrementer. No firmware is asked for with it.

## 3. The next project: the RW peripheral, and the mesh's two regimes (2026-11-19)

**The owner:** "The next project for us is the RW peripheral. With it we're going to add the regimes to the
compute tile for router queue full = error versus router queue full = flow control."

- **The RW peripheral.** In v1.37 it is v1.36's RW engine, unchanged but for its place: the memory tile's
  peripheral 2 and the arbiter's client 2. Nothing in v1.37's proof runs uses it. The owner, of its figures in
  the specification: "All that stuff will get reworked once we have a real RW engine anyway."
- **The two regimes.** Today a full router queue is always an error: the links have no flow control, the program
  paces its traffic, and a packet pushed into a full queue sets `router_overflow` (chapter 16). The owner's
  standing view: "There are regimes on the mesh. Some need flow control, some don't." With the RW peripheral the
  compute tile gets both: one in which a full queue is an error, and one in which a full queue holds the sender
  back.
- **What is already waiting on it:** flow control on the line between the special tiles, the specification's
  open item 182, left out of v1.37 because nothing there needed it.

Nothing of this is designed. It is recorded here so that it is the starting point.

