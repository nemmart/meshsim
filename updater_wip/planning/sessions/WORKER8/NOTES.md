# WORKER8 notes: the plan for the simulation of the host link's protocol

Status, 2026-11-17: **the plan, waiting for the owner's go-ahead. No code is written.** Branch `updater-w8`, from
main `641bc694`.

Names: HL is `v1.4_wip/planning/HOST-LINK.md`, II is `INGRESS-INTERFACE.md` beside it. A dagger (†) marks what I
propose where HL is silent; (owner) marks what the owner said. A cycle is one byte on the link, about a nanosecond:
1 ms is a million cycles, 300 ms is 3 × 10⁸.

**Read:** everything on the brief's list. **Checked by hand before planning** (a scratch computation, not kept):
HL's three example headers come out byte for byte from HL's own definition, with 0x48674BC7 over all 16 bytes of
each, and the three CRC values of its table.

## 1. What is built, and where

Everything is in `updater_wip/link/`. Nothing else is touched.

| file | what |
|---|---|
| `link_params.h` | the sizes (section 2), the rules taken where HL is silent, each a switch (section 4), the planted bugs |
| `crc32c.h`, `header.h` | CRC-32C a byte a cycle; the header word packed and unpacked; the numbers' arithmetic round the wrap |
| `link_end.h`, `.cpp` | **the link end:** its registers in one struct, `outputs(regs)`, `step(regs, inputs)` |
| `queues.h` | a word queue, a packet buffer: plain hardware FIFOs with a count |
| `node.h`, `.cpp` | the chain node: two link ends, the forwarding, the own queues, "stop" for each link |
| `host.h`, `.cpp` | the host end: a link end that keeps every packet, and the sorting of replies by hops |
| `wire.h`, `.cpp` | one direction of one link: a delay line, and the damage |
| `procs.h`, `.cpp` | the random processes: noise, aimed damage, consumers, traffic; the generator (its own, so a seed gives the same run under gcc and clang) |
| `monitor.h`, `.cpp` | the properties of section 8, checked every cycle |
| `cover.h`, `.cpp` | the counts of rare events (section 9) |
| `script.h`, `.cpp` | a run written down as a list of events, and played back: what a failing seed, a shrunk case and a search's counter-example all are |
| `linkrun.cpp` | the random runs: one seed, a range of seeds, `--replay`, `--shrink` |
| `linksearch.cpp` | the search of the shrunken model (section 10) |
| `linkplant.cpp` | each planted bug against the runs and the search (section 11) |
| `test_header.cpp` | HL's examples and CRC values; gold's `VECTORS.txt` when given its path (it is on `updater-w7`; I do not copy it) |
| `cases/` | the kept cases: each failure once found, and each flaw's case, as scripts |
| `Makefile`, `README.md` | gcc and clang, `-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror`; a debug build with the address and undefined-behaviour sanitizers; `make check` |

No simulator types and no `v1.37_wip/` code: the link end compiles alone, so WORKER10 can take it.

## 2. The sizes, all parameters

| parameter | default | in the shrunken model |
|---|---|---|
| the number's bits | 18: numbers 1 to 0x3FFFE | 3: numbers 1 to 6 |
| the most words in a packet | 32 | 1 |
| the packets kept a stream (the window) | 8; the host: all (HL) | 2 |
| a stream's queue, in words | 512 (8 KB) | 2 to 4 |
| "stop" at | half the queue | half |
| the zeros of a recovery | 1,024 bytes | 40 (longer than the longest packet, 32) |
| the status timer | 512 cycles | 48 |
| the timer for sending again unasked † | section 4, G1 | 0 (off) and 96 |
| a forwarding buffer † | 16 packets a stream, "stop" at half | 2 |
| the first number | 1; a test may set both ends of a link to any number, to start near the wrap | 1 |

**The random runs draw the number's bits too,** from 4 to 18, so that most runs wrap every few packets or few
hundred and some run the real width from just under the wrap.

## 3. The link end

One end of one link: a byte in and a byte out each cycle.

**Its ports.**

| port | direction | what |
|---|---|---|
| `rx_byte`, `tx_byte` | in, out | the wire |
| a packet received | out, with a "taken" back | a packet that passed every test, offered once: stream, length, hops, data checksum, the words. Whoever owns the end takes it or has no room (G11) |
| a packet to send, for each stream | in, with a "taken" back | length, hops, data checksum, the words; taken when a place in the window is free |
| `say_stop`, for each stream | in | the level the stop bit should have |
| its state and counts | out | for the monitor and the coverage only |

The owner of an end is a test bench, the host, or a node. The queues sit outside the end, as hardware FIFOs.

**Its registers,** in one struct:

| part | registers |
|---|---|
| the receiver | its state, a byte count, the CRC unit, the header as latched, the packet buffer (one packet), a count of zeros in a row |
| each stream, receiving | the number expected; "resend" owed, and whether already asked; the stop bit last sent |
| the sender | its state, a byte count, the CRC unit, the header being sent |
| each stream, sending | the oldest number not acknowledged, the next to send, the next new number; the kept packets; the other side's stop bit; the timer of G1 |
| the link | its mode; the place in a recovery round; the status timer |

**The receiver's states.**

| state | what | leaves to |
|---|---|---|
| `HUNT` | idle: zeros pass | `HEADER` at a byte that is not zero |
| `HEADER` | 16 bytes through the CRC unit | the CRC fails: `FLUSH`, and the link's mode says "lost". It passes with a length that can be: `DATA`, or the packet's end if the length is 0. A field that cannot be: the data is passed over, then `HUNT`, "resend" owed (HL); a length over 32, G14 |
| `DATA` | 16 × length bytes through the CRC unit, set back to all ones | the packet's end, then `HUNT` |
| `FLUSH` † | waits for zeros in a row, longer than any run inside a packet (G10) | `HUNT` |

At a packet's end, in HL's order: the data checksum; the number (G6, G7); then, for a good packet, the
acknowledge field, the stop bit and the flags are used, and a packet with data and the expected number is offered.

**The sender's states:** `IDLE` (zeros), `HEADER` (16 bytes, the last four the CRC unit's), `DATA`, `ZEROS` (a
recovery's). What it starts next, when it is free, in this order:

1. in a recovery (the link's mode below): the round's zeros, then its packets. No data;
2. a status that is due on a stream: at a change of its stop bit, a "resend" owed, its timer. As a packet of
   length 0, unless that stream's own data is what goes next anyway;
3. foreground data, then background data (HL: the stream decides first): a packet sent again before a new one;
   new ones as the owner offers them, and a node offers a forwarded packet before its own.

A stream's data waits while the other side's stop bit for it is 1, or its window is full.

**The link's modes,** the same at both ends.

| mode | this end is | sends | leaves when |
|---|---|---|---|
| `RUN` | in step | data and status | a header CRC fails: `LOST`. A good packet with "lost": `ANSWER` |
| `LOST` | out of step | the packet in hand; then rounds of zeros and a "lost" packet for each stream (G8) | a good "recovery" packet: `RUN`. A good "lost" packet: `LOST_ANSWER` (G9) |
| `ANSWER` | answering | the packet in hand; then rounds of zeros and a "recovery" packet for each stream, having gone back to the number after the "lost" packet's acknowledge field | a good packet without "lost": `RUN`. A header CRC fails: `LOST_ANSWER` |
| `LOST_ANSWER` † | both | rounds of zeros and packets with both flags | a good "recovery": `ANSWER`, or `RUN` if that packet has no "lost". A good packet with neither flag: `LOST` |

The full diagrams, with every transition numbered for the coverage, go in this file when the end is built.

## 4. Where HL is silent: the rules taken †, each a switch

Each becomes an entry of `QUESTIONS.md` with the case that shows it, and each is a field of `Rules` in
`link_params.h`, so that the case fails with the rule off and passes with it on. "Known" is the brief's list.

| | where HL is silent | the rule taken † | also kept, as the switch's other setting |
|---|---|---|---|
| G1 | known: sending again with no "resend" asked | a timer for each stream, started again when an acknowledgement moves and at the end of each of the stream's packets; at 2,048 cycles the sender goes back to the oldest not acknowledged. Held while the stream is stopped | no timer. With G7 a receiver may always find a loss by itself: the search says whether the timer is needed |
| G2 | known: the recovery packet's number | the packet is of length 0, so its number field is already the number the sender restarts from. The receiver checks it and never takes its place from it | |
| G3 | known: an acknowledgement during a recovery | every good packet's acknowledge field releases what it covers, in any mode. The "lost" packet's is also where the sender goes back to. One that is behind is ignored; one beyond what was sent is ignored and counted | |
| G4 | known: one side from power-on | as written there is no way back: built as an event of its own runs, the case kept. A rule is proposed in `QUESTIONS.md` once the case is shown | |
| G5 | known: when a forwarding node acknowledges | when it has taken the packet into its own queue and its forwarding buffer, not when it has passed it on | when the next link has acknowledged it |
| G6 | a good packet with the wrong number: HL's table has no row | behind: thrown away, acknowledged as ever. Ahead: thrown away, "resend" owed, asked once and again with each status until the expected packet comes | asked at every such packet |
| G7 | what "checks" means for a packet of length 0 | its field is the number the sender puts on the wire next. Ahead of what is expected: a packet was missed, "resend" owed. Behind: nothing. Never an error | |
| G8 | "lost" and "recovery" hold one stream's numbers | a packet of length 0 for each stream in every round | one packet, the other stream left to G6 |
| G9 | both sides lost at once | a lost side answers a "lost" packet: both flags in one packet. Answering ends at any good packet without "lost" | as written: a lost side sends only "lost" |
| G10 | how a receiver that lost its place finds a packet's start | it waits for 528 zeros in a row, which no packet holds, then takes the first byte that is not zero | as written: the next byte that is not zero |
| G11 | a good packet and no room for it | thrown away and not acknowledged; the sender sends it again. The stop bit keeps this rare; it is not what prevents an overrun | |
| G12 | the header of a packet sent again | written afresh: the acknowledge field and the flags of now | the header as first sent |
| G13 | the status timer is one for a direction, and any packet starts it again | a timer for each stream, from that stream's last header; a status that is due goes before the other stream's data | as written |
| G14 | a length over 32 under a good header CRC is "still in step" | it cannot be: the receiver does not know how many bytes to pass over. Treated as a lost place | passing over 16 × length bytes |
| G15 | the last ASIC of a chain | a node is told that it has no neighbour down the chain | |
| G16 | where a forwarded packet waits, and when "stop" is said for it | a buffer for each stream and direction; a link's stop bit is up when the own queue or the forwarding buffer behind it is half full | |
| G17 | the first packet after "stop" is lifted | the oldest not acknowledged | whatever was next |
| G18 | when an acknowledgement is sent | as written: on whatever goes back, and with the status. An option sends one at once when the line is free; both are measured | |
| G19 | a good packet while lost | its data is not taken; its acknowledge field and stop bit are | ignored whole |

## 5. What I suspect from reading, to be shown by a case or withdrawn

None of these is shown yet. They say where I will look first.

1. **Both sides lost at once never recover** as written: each sends "lost" and waits for a "recovery" that the
   other, being lost, does not send (G9).
2. **One stream's acknowledgements and stop bit can be starved by the other stream's data** in the same
   direction: the status waits for 512 idle cycles, and replies from a whole chain leave none (G13).
3. **A packet of length 0 has one check, not two:** its data checksum is the constant 0. The packets that carry
   only an acknowledgement, a stop bit, "lost" or "recovery" are the ones with 32 bits. A false acknowledgement
   releases packets that never arrived.
4. **Numbers that disagree are never repaired:** after one side's power-on, or one false acknowledgement, every
   packet is thrown away for ever (G4).
5. **A packet that is bad where it is kept is sent again for ever:** hops 15 at a node that forwards, a field
   that cannot be, a bit flipped in a buffer under the end-to-end data checksum. "Resend" is the only answer HL
   has, and the same packet comes back.
6. **Nothing but the stop bit holds the host,** which keeps every packet: with the stop bit lost in the noise a
   queue overruns (G11).
7. **A burst of short foreground packets waits on the window:** 8 go out in 256 cycles, and the acknowledgement
   may be 512 cycles away (G18).

## 6. The chain node and the host end

**A node** has a link end up the chain and one down.

| path | what happens |
|---|---|
| from the host | a packet taken on the upper end goes, in one cycle, into the node's own queue for its stream (the words) and into the forwarding buffer down (the packet, hops plus one). No room in either: not taken (G11). The last node has no buffer down (G15) |
| towards the host | a packet taken on the lower end goes into the forwarding buffer up, hops plus one. A packet with hops 15 is bad (HL) |
| the upper end's sender | offered the forwarding buffer's packet first, then the node's own: its outgoing queue cut into packets of 32 words, or fewer at a frame's end, hops 0, the data checksum computed before sending |
| the lower end's sender | offered the forwarding buffer down |
| a forwarded packet | the stream, the length, the data and its checksum as they came; the number, the acknowledge field and the flags the outgoing end's; the header sealed again |
| "stop" | up the chain, a stream's bit is up when its own queue or its forwarding buffer down is half full; down the chain, when its forwarding buffer up is |

**The host end** is a link end with a window that never fills, sources that hold all the traffic, and a sink that
sorts by hops and rebuilds each node's replies.

**Chains of 1, 2, 4 and 16** are a host and that many nodes, built from a count.

## 7. The random runs

**A run is a seed.** From it: the chain's length, every size of section 2 (the defaults most often), and the
parameters of every process below. A quantity that spans orders of magnitude is drawn on a log scale; every
process can also be drawn "never". The processes do not look at the protocol's state, but the aimed damage.

**The wire,** each direction of each link on its own:

| process | parameters, drawn for the run | range |
|---|---|---|
| single flipped bits | the rate a bit | never; 10⁻⁹ to 10⁻³ |
| bursts | the gap between them | never; 10² to 10⁹ cycles |
| | how long one lasts, drawn again for each | 1 to 3 × 10⁸ cycles (300 ms), under a ceiling drawn for the run |
| | its density, a bit | 10⁻⁴ to 0.5 |
| | its kind | bits flipped; every byte zero; every byte random; zeros turned to bytes |
| aimed damage | the chance, for each kind of packet | never; 10⁻⁴ to 0.5 |
| | the kinds of packet | data first sent; sent again; a status; one with "resend"; "lost"; "recovery"; at the number's wrap; the first after a stop is lifted; forwarded |
| | the forms | the first byte to zero; a bit of the header; a bit of the data; the whole packet to zeros; its tail to zeros; bytes put into the idle before it; a header changed and sealed again so that it passes its CRC |
| the delay | constant for a direction | 1 to 256 cycles |

- The wire learns a packet's kind from a tap on the sender that the receiver never sees.
- **The last form is deliberate:** noise makes such a header once in 2³², so no run would meet one. It shows
  what one false header can do, and is reported apart from the noise.
- **Which wires are noisy is drawn too:** none, one direction, both, every link of the chain.

**The consumers,** one for each queue at each node and one at the host:

| parameter | range |
|---|---|
| how long it takes words before it stops | 1 to 10⁶ cycles; or never stops |
| how long it stops | 1 to 10⁶ cycles (1 ms) |
| its rate while taking | a word a cycle, down to one in 64 |

**The traffic:**

| source | parameters | range |
|---|---|---|
| the host, background | a frame's length | 1 to 10⁵ words |
| | the gap between frames | 0 to 10⁷ cycles; or none ever |
| the host, foreground | a message's length | 1 to 100 words |
| | the gap | 10 to 10⁷ cycles; or none ever |
| each node's replies | a reply's length | 1 to 2,000 words |
| | when | at random for each node; or every node at once, at a query |
| every sender | how it cuts packets | 32 words; or a length drawn for each packet |
| | what the words hold | counters; zeros; random; bytes that look like packets |

**How a run ends:** the traffic stops, the noise stops, the consumers run, and everything must arrive within a
bound. Then the monitor's totals and the coverage counts are written, one line a seed.

## 8. What must hold, and how each is checked

The monitor keeps, for each flow (a source, a stream, a sink), the words sent and not yet delivered. It sees every
word pushed in and every word taken out, and nothing of the protocol.

| property | how it is checked |
|---|---|
| every word once, in order | a word delivered must be the flow's oldest word not yet delivered, bit for bit. At the end nothing is left. From the host, every node is a sink of the same words |
| each node's replies whole and in order at the host | the host's sink for hops *h* is node *h*'s flow, and a frame's words come without another frame's among them |
| no queue overruns | a push into a full queue or buffer stops the run, anywhere |
| the foreground moves while the background is stopped | every foreground packet's wait is measured, a hop and end to end. With no noise on a link it must be under a bound: one full background packet, the foreground's own packets ahead of it, the window. The longest are reported with their runs |
| recovery after the noise | for each burst: from its end to both ends in `RUN` with a packet delivered in order. The times, the worst, and the run that had it |
| everything sent arrives | the end of every run, above |
| no state from which nothing moves | during a run: work waiting, quiet wires, and nothing delivered for a bound. At the end: every window empty, every end in `RUN` |
| the link end's own invariants, every cycle | no more unacknowledged than the window; a first byte never zero; a header never all zeros; no number 0 or all ones; no data started on a stopped stream; hops equal to the sender's distance |

**A failure stops the run** and prints the seed, the cycle and the property.

## 9. The rare events, counted

Counts are kept in every run and added over the campaign. The report has the table, and what is still 0.

| counted | |
|---|---|
| each form of damage at each kind of packet | the 7 forms by the 9 kinds, and whether the receiver saw it |
| each state entered, each transition taken | the receiver's, the sender's, the link's modes; by the numbers of the diagrams |
| things at once | a stop said or lifted during a recovery; a header CRC failing during a recovery; both ends lost; a recovery round itself damaged; a wrap during each of these, during a burst, and with the window full |
| what the rules of section 4 did | a packet with no room; a wrong number behind and ahead; the timer of G1 firing; an acknowledgement behind or beyond |
| the figures | the most packets ever unacknowledged; the fullest each queue and buffer got; the longest waits |

What stays at 0 is reached by widening a range, or is given to the search and said so.

## 10. The search that misses nothing

**The same code:** `LinkEnd::step`, the node and the host, with the small sizes of section 2. A packet is 16 or
32 bytes, a number wraps after 6, a window holds 2.

**What is tried.** The model runs cycle by cycle, and has a choice only at these moments:

| at | the choices |
|---|---|
| a packet's first byte entering a wire | whole; gone (all zeros); its first byte zero; a bit of its header; a bit of its data |
| an idle wire, when anything else happens | a byte of noise, or none |
| a source with packets left | offers its next now, or waits |
| a queue with words in it | its consumer takes, or stalls until the next moment |

- **A budget bounds a search:** so many packets from each source, so many damaged. Stalls and waits are not
  counted: they add no state that was not seen.
- **Every state reached is kept,** as a 128-bit hash of every register, every wire and the monitor; a state seen
  before is not tried again. A second mode keeps the states whole, to show the hash misses none.
- **Breadth first,** so the first failure found is the shortest.

**What is checked in every state reached:**

1. the properties of section 8 that hold at every cycle;
2. **from this state, if nothing more goes wrong, everything arrives:** the model is run on with quiet wires
   and consumers that take, and must come to rest with every word delivered, every window empty and every end
   in `RUN`, within a bound. This is "once the noise ends" and "no state from which nothing moves", for every
   state and not only at a run's end.

**The configurations,** each searched alone:

| configuration | what it is for |
|---|---|
| one link, one stream, data both ways | sending again, the wrap, both ends lost |
| one link, two streams, data one way | "stop" on one stream during a recovery; the status starved (G13) |
| a host and two nodes, one stream | forwarding, a node's acknowledgement (G5), "stop" backing up the chain |
| one end from power-on in the middle | G4 |

**What keeps it honest:** the code is the campaign's, so the sizes are the only difference, and the campaign
draws the small sizes too. A counter-example is a script, and plays back in `linkrun`.

**What I do not know yet is how many states there are.** It is measured on the first configuration before the
others are written. If one is too large for a day on the runner, the budget is cut and the report says exactly
what was covered: so many packets, so many damaged, these sizes.

## 11. How my own work is checked

- **The header:** HL's three examples and its CRC values in `test_header`; gold's 43 packets when `VECTORS.txt` is
  given.
- **Planted bugs,** one at a time, by a field of the parameters, compiled out of the kept build. The brief's
  eight: an acknowledgement one off; the window not kept; the wrap compared wrongly; the timer not started
  again; a duplicate delivered after sending again; a header not sealed again; a stop ignored; recovery answered
  once only. And mine: hops not counted; an own packet before a forwarded one; a packet of length 0 taking a
  number; the acknowledge field taken from a packet whose data failed; no zeros before "lost"; the data
  checksum computed again at a hop. For each: caught by the runs (in how many seeds of a fixed 1,000), by the
  search, or both. One caught by neither means a check is missing, and I add it.
- **Every proposed rule** of section 4 has its case in `cases/`, failing with the rule off.
- **Replay:** a run writes down what its processes did, as a script: each damaged byte, each stall, each
  packet offered. A failing seed is run again from the seed and from its script, and must fail at the same
  cycle both times.
- **Shrinking:** the script is cut down by removing events, shortening stalls, dropping traffic and nodes, as
  long as the same property fails. The shortest goes into `cases/` and into `make check`.
- **The builds:** gcc 13 and clang 18 here; the sanitizers on every test and on a part of the campaign.

## 12. How long the runs take

Estimates, replaced by measurements as soon as the link end steps. This machine has 2 cores.

| run | size | time |
|---|---|---|
| a link end's step | a byte, a table CRC | 20 to 40 ns guessed |
| `make check` | the header tests, 200 short seeds, the kept cases, the planted bugs on short runs, the smallest search | under 5 minutes here, sanitizers on |
| a run | 10⁷ cycles on a chain of 4: about 19,000 full packets a link | a few seconds |
| the campaign | 10,000 seeds: over 10⁸ packets, 10¹¹ cycles | hours on the runner's 16 threads |
| long noise | 300 ms on one link, a few hundred seeds; on a chain of 16, tens | minutes each |
| the search | section 10 | not known |

The campaign and the larger searches go to the runner as Quest tasks; I read `PIPELINE.md` in Quest when I get
there.

## 13. The order of the work

Each step ends with a commit, a push and a clean tree.

1. The header, the CRC, `test_header`.
2. The link end, two of them on a perfect wire, the monitor.
3. The wire's damage and the search on one link. **The protocol's flaws show here first,** so `QUESTIONS.md`
   starts here, and I report to the owner before going on.
4. The random processes, the coverage, the campaign on one link.
5. The node, the host, the chains; the search's third configuration.
6. The planted bugs, replay and shrinking, the kept cases.
7. The runner's tasks, the figures, `REPORT.md`.

## 14. Choices

| | choice | the alternative |
|---|---|---|
| C1 | the queues are outside the link end, which offers and is offered whole packets | the queues inside it |
| C2 | the rules of section 4 are switches in the kept code until the owner rules on each | built in |
| C3 | the planted bugs are compiled out of the kept build | always there |
| C4 | one engine: the random runs, the kept cases and the search's counter-examples are all scripts on it | a harness for each |
| C5 | the search is on the clocked code, with choices only at a packet's edge | a second model, a packet a step, that could drift from the code |
| C6 | the number's width is drawn for a run, so the wrap is common | the real width only, a wrap every 1.4 × 10⁸ cycles |
| C7 | a header forged to pass its CRC is a form of aimed damage, reported apart | left out, as a thing noise does not do |
| C8 | the monitor compares words with what was sent, so the data may be anything, packets' images included | words that name themselves |

## 15. Asked of the owner

1. **C7:** are forged headers wanted? They show what a false packet costs; a wire makes one in 2³².
2. **G4:** one end starting from power-on is run as an event, expecting the link not to come back. Is that what
   you want shown, or is a link's reset a design you would rather make first?
3. **Section 5, item 5:** a bit flipped in a node's buffer is not a wire's fault. In or out?
