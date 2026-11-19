# Things we want to add

Started 2026-11-19 (owner: "Let's start a file with things we want to add"). Each entry is something the owner has
asked for that is not in v1.37: what it is, why, what was settled in the discussion, and what it would touch.
(owner) marks what the owner said; a dagger (†) marks what was proposed and not ruled on.

This is not the list of open questions. Those are chapter 26 of `v1.37_wip/spec/machine-spec.md`, and
`v1.37_wip/OPEN-v1.37.md` for the simulator's.

## 1. Statistics for each host link, and the names "parent" and "child" (2026-11-19)

**The names** (owner). An ASIC has two host links. **The parent link** is the one towards the FPGA; **the child
link** is the one to the next ASIC on the daisy chain. ("Server" and "client" were the owner's other thought.
† Parent and child are preferred: the two ends of a link are equals in the protocol, and "client" already names
the arbiter's clients and the simulator's `--client`.) v1.37's specification says "up" and "down" (`link_up`,
`link_down`), so this is a rename there.

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

**A command to read them.** † The counters are of use only if the host can ask for them: a frame on the
foreground stream that has every ASIC send its counters back, sorted by hops at the host. It is the case the
owner gave when the chain was designed (the FPGA runs a query on all the ASICs and puts the answers together by
the hop count). Nothing on the foreground is designed but the reply frame, so the counters and the command are
best done together.

**What it would touch:** the specification (17.6's words, chapter 28's names and a list of what a link end
counts, 29 for the command); the link's code (`hostlink.h` counts recovery rounds but not by direction, and not
stops); Ingress/Egress's model; the master tile's firmware for the command.

**When:** † the first change after v1.37 is frozen.
