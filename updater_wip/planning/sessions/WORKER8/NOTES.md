# WORKER8 notes: the plan for the simulation of the host link's protocol

Status, 2026-11-17, later: **stopped on NEXT.md's order. HL as it now stands (one run of numbers) lets the foreground
stand still behind a stopped background: `REPORT.md` beside this file.** The code in `updater_wip/link/` is HL's
header of 16 bytes with one run of numbers; sections 16 and 17 below are results on the headers before it and are
history. Branch `updater-w8`. Sections 1 to 15 are the plan as agreed.

**The go-ahead (owner, 2026-11-17),** with the answers to section 15:

1. **Forged headers: yes,** as a form of damage, reported apart.
2. **One end from power-on: show the failure first,** then propose a rule and build it behind a switch like the
   others. A restart handshake is wanted; the header's reserved byte has room for a flag.
3. **A bit flipped in a node's buffer: in scope, at a low rate.** The requirement is that the link never hangs
   on it: it must end as a reported error.
4. **On suspicion 3 of section 5** (owner): a random header of length 0 must also carry a zero checksum field,
   so against made-up packets the check is still 64 bits. The weakness is wider than length 0: on any real
   packet the header's fields (acknowledge, flags, number) are covered by the header CRC alone. To measure: what
   a false acknowledgement costs on any packet, and whether the link can tell.
5. **Report after the search on one link,** before the chain is built.

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

## 16. What is built: the report after the search on one link (2026-11-17)

Steps 1 to 3 of section 13 are done. `updater_wip/link/README.md` says how to run everything. What the search found
is `QUESTIONS.md`. Nothing of the chain, the host end, the random campaign or the runner's tasks is built.

**What was built differently from the plan:**

| | the plan | as built |
|---|---|---|
| the small sizes | a status timer of 48 | 24: under the 32 zeros a lost receiver waits for, as 512 is under 528. With 48 the small model could find its place on an idle line, which the real sizes cannot |
| a case's damage | by cycle | by the packet's place on its wire ("the 5th packet end 0 sends"): a case then still means the same after a rule changes the timing before it |
| G1, the timer | on, at 2,048 | off: G7 does its work, and the searches hold with either |
| G17 | on | off: not needed |
| G4, G20, G21 | G4 only, as a case | the restart handshake, and two rules that end in it: acknowledgements that cannot be, and a kept packet that no longer matches its checksum |
| when "resend" is repeated | with each status | when the sender's status shows it has not gone back: the timer's repeat crossed the packet coming back every time |
| properties | section 8's | two more, watched from outside the link end: no data started on a stream the other end has stopped; and the link never starts again unless a forged header, a memory's flipped bit or a power-on excuses it |
| the search's states | a hash of every register | a hash of what can still matter (`LinkEnd::canon`): a header already judged, a slot already released and the like are left out. `--whole` keeps every register instead, to check it |

**The search, as built** (`linksearch.cpp`'s heading has it in full):

- The model stops wherever a packet starts or ends on a wire, a packet is judged, a header fails or a mode changes.
  At a stop it tries every fate of a packet just started (whole; gone; first byte zero; a header bit; a data bit),
  each source letting its next packet out or not, each queue with words losing none, one or all, and a byte of
  noise on an idle wire. Budgets bound the damage; waits and stalls are free.
- **In every state:** the bench's properties; and a run on from there with nothing more going wrong, which must
  come to rest with everything delivered and acknowledged and both ends in step.
- **What it does not cover:** a choice between two stops (`--tick 8` adds stops and found nothing more); wire
  delays other than those run (1, 7, 20); more packets or more damage than the budgets.

**The protocol as written** (`--rules as_written`) fails the first search, with one packet damaged: `QUESTIONS.md`.

**The proposed rules,** every search complete (every state within its budget tried). `--packets a b c d` is each
source's packets: end 0's foreground and background, end 1's. 28,261,334 states in all:

| the search | states | |
|---|---|---|
| `--packets 2 0 2 0 --damage 1` | 4,587,428 | holds |
| `--packets 1 0 1 0 --damage 2` | 287,937 | holds |
| `--packets 1 0 0 0 --damage 3` | 134,110 | holds |
| `--packets 2 0 0 0 --damage 2` | 268,905 | holds |
| `--packets 1 1 0 0 --damage 2` | 334,025 | holds |
| `--packets 2 2 0 0 --damage 1` | 4,730,180 | holds |
| `--packets 1 1 1 0 --damage 1` | 476,036 | holds |
| `--packets 3 0 0 0 --damage 1 --queue 2` | 248,921 | holds |
| `--packets 2 1 0 0 --damage 1 --queue 2` | 1,576,865 | holds |
| `--packets 7 0 0 0 --damage 1` | 4,006,208 | holds |
| `--packets 2 0 1 0 --damage 0 --noise 2` | 771,052 | holds |
| `--packets 2 0 1 0 --damage 1 --noise 1` | 3,105,248 | holds |
| `--packets 2 0 1 0 --damage 1 --delay 7` | 558,787 | holds |
| `--packets 2 0 1 0 --damage 1 --delay 20` | 1,319,931 | holds |
| `--packets 2 0 1 0 --damage 1 --tick 8` | 573,004 | holds |
| `--packets 1 0 1 0 --damage 2 --images` | 287,629 | holds |
| `--packets 2 0 2 0 --damage 0 --power 1` | 1,336,881 | holds |
| `--packets 1 0 1 0 --damage 1 --power 1` | 317,583 | holds |
| `--packets 2 0 1 0 --damage 1 --flip 1` | 1,608,584 | holds |
| `--packets 5 0 0 0 --damage 1 --queue 4` | 1,651,092 | holds |
| `--packets 1 0 1 0 --damage 1 --whole` | 47,478 | holds |
| `--packets 1 0 1 0 --damage 1` | 33,450 | holds |

- **One search larger than this machine was started and not finished:** `--packets 2 0 2 0 --damage 2`, two
  packets each way and two damaged. 30,000,000 states tried, no failure, about 750,000 still waiting when it
  was stopped for memory (4 GB of 7). It is first on `searches.sh big`.
- The last two are the same search with the states kept whole and by the hash: the same verdict; 47,478 whole
  states are 33,450 by what can still matter.
- The longest any state took to come to rest was 567 cycles of the small sizes.

**Each rule the search has not shown to be needed, taken away or put in** (7,788,289 states):

| the search | states | |
|---|---|---|
| `proposed-lost_each_stream` `--packets 1 0 1 0 --damage 2` | 301,642 | holds |
| `proposed-lost_each_stream` `--packets 1 1 0 0 --damage 2` | 364,562 | holds |
| `proposed-lost_each_stream` `--packets 3 0 0 0 --damage 1 --queue 2` | 268,528 | holds |
| `proposed-lost_uses_ack` `--packets 1 0 1 0 --damage 2` | 287,997 | holds |
| `proposed-lost_uses_ack` `--packets 1 1 0 0 --damage 2` | 334,025 | holds |
| `proposed-lost_uses_ack` `--packets 3 0 0 0 --damage 1 --queue 2` | 248,921 | holds |
| `proposed-long_is_lost` `--packets 1 0 1 0 --damage 2` | 287,937 | holds |
| `proposed-long_is_lost` `--packets 1 1 0 0 --damage 2` | 334,025 | holds |
| `proposed-long_is_lost` `--packets 3 0 0 0 --damage 1 --queue 2` | 248,921 | holds |
| `proposed-status_each` `--packets 1 0 1 0 --damage 2` | 191,936 | holds |
| `proposed-status_each` `--packets 1 1 0 0 --damage 2` | 216,828 | holds |
| `proposed-status_each` `--packets 3 0 0 0 --damage 1 --queue 2` | 118,916 | holds |
| `proposed+stop_lift_back` `--packets 1 0 1 0 --damage 2` | 287,937 | holds |
| `proposed+stop_lift_back` `--packets 1 1 0 0 --damage 2` | 334,025 | holds |
| `proposed+stop_lift_back` `--packets 3 0 0 0 --damage 1 --queue 2` | 305,767 | holds |
| `proposed+ack_now` `--packets 1 0 1 0 --damage 2` | 332,293 | holds |
| `proposed+ack_now` `--packets 1 1 0 0 --damage 2` | 335,217 | holds |
| `proposed+ack_now` `--packets 3 0 0 0 --damage 1 --queue 2` | 166,058 | holds |
| `proposed+ask_every` `--packets 1 0 1 0 --damage 2` | 287,937 | holds |
| `proposed+ask_every` `--packets 1 1 0 0 --damage 2` | 334,025 | holds |
| `proposed+ask_every` `--packets 3 0 0 0 --damage 1 --queue 2` | 248,906 | holds |
| `proposed,resend_timer=96` `--packets 1 0 1 0 --damage 2` | 311,920 | holds |
| `proposed,resend_timer=96` `--packets 1 1 0 0 --damage 2` | 355,253 | holds |
| `proposed,resend_timer=96` `--packets 3 0 0 0 --damage 1 --queue 2` | 260,900 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 1 0 1 0 --damage 2` | 354,860 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 1 1 0 0 --damage 2` | 400,431 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 3 0 0 0 --damage 1 --queue 2` | 268,522 | holds |

**G11 without its rule, the window half the queue** (9,764,896 states):

| the search | states | |
|---|---|---|
| `proposed-refuse` `--packets 4 0 0 0 --damage 2 --queue 4` | 5,084,696 | holds |
| `proposed-refuse` `--packets 5 0 0 0 --damage 1 --queue 4` | 1,638,697 | holds |
| `proposed-refuse` `--packets 3 1 0 0 --damage 1 --queue 4` | 3,041,503 | holds |

**One forged header and one damaged packet, field by field** (3,207,515 states; `…` is the four ordinary fates):

| the search | states | |
|---|---|---|
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-ack` | 244,563 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-stop` | 225,323 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-lost` | 287,900 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-recovery` | 133,650 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-resend` | 161,963 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-number` | 292,056 | **fails:** 25,136 states, word 0 delivered where word 1 was next |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-ack-data` | 104,117 | holds |
| `--packets 3 0 0 0 --damage 1 --forge 1 --fates …,forge-stop --queue 2` | 1,757,943 | holds |

**The planted bugs,** each against the search (`plants.sh`): every one caught.

| planted | caught by the search | as |
|---|---|---|
| `ACK_ONE_OFF` | `--damage 1 --packets 2 0 1 0` | the link started again, and nothing had happened that could excuse it |
| `WINDOW_NOT_KEPT` | `--damage 1 --packets 3 0 0 0 --queue 2` | the link started again, and nothing had happened that could excuse it |
| `WRAP_WRONG` | `--damage 0 --packets 7 0 0 0` | nothing more goes wrong, and it does not finish |
| `TIMER_NOT_RESTARTED` | `--damage 1 --packets 2 0 1 0` | nothing more goes wrong, and it does not finish |
| `DUPLICATE_TAKEN` | `--damage 1 --packets 2 0 1 0` | word 0 delivered where word 1 was next |
| `STOP_IGNORED` | `--damage 1 --packets 3 0 0 0 --queue 2` | a data packet started on a stream the other end has stopped |
| `RECOVERY_ONCE` | `--damage 2 --packets 1 0 1 0` | nothing more goes wrong, and it does not finish |
| `ZERO_TAKES_NUMBER` | `--damage 1 --packets 2 0 1 0` | more kept than the window |
| `ACK_FROM_BAD_DATA` | `--damage 1 --forge 1 --fates gone,forge-ack-data --packets 1 0 1 0` | the link started again, and nothing had happened that could excuse it |
| `NO_ZEROS` | `--damage 1 --packets 2 0 1 0` | nothing more goes wrong, and it does not finish |

**What the checks caught in my own code** (not the protocol's flaws):

1. The per-stream status timer did not run under the other stream's data: seen in `test_link`'s figure on a whole
   wire.
2. The restart handshake hung when the answering end lost its place: the search, one damaged packet.
3. The handshake made a fresh end start again a second time, with an error: the property "never starts again
   without an excuse", which I added because two planted bugs were being hidden by the restart.
4. A budget of one damaged packet let two be damaged in one cycle: the first search's output.

**How long it takes here** (2 cores): about 60,000 states a second; the whole of the tables above in about 15
minutes; `make check` in about 2 minutes from a clean tree.

**Next,** after the owner's word: the random processes and the campaign on one link (step 4), then the chain
(step 5). The larger searches (`searches.sh big`: two packets each way with two damaged, and the like) want the
runner, and less memory a state: its unit allows 16 GB and 45 minutes a task, about 100 million states as the
search stands.

## 17. The header of 20 bytes, the chain and the first runs (2026-11-17, later)

The owner's go-ahead of that morning: the rules G4, G6, G7, G9, G10, G11, G20 and G21 accepted to carry on with;
the header of 20 bytes as the baseline; no second check for the false number; an acknowledgement as soon as the
line is free; queues of 16 KB. `QUESTIONS.md` is written again for all of it. What is built since section 16:

| piece | file |
|---|---|
| the header of 20 bytes; every header speaks for both streams; one timer a direction; one packet a recovery round | `header.h`, `link_end.cpp` |
| G22, a packet its own sender made wrong; when "resend" is asked again (`reask`) | `link_end.cpp` |
| the chain node: two link ends and the forwarding buffers | `node.h`, `.cpp` |
| the chain on the bench: the host, nodes, wires, sources, queues, the monitor | `chain.h`, `.cpp` |
| the random runs | `linkrun.cpp`; `figures.sh`, `plants_run.sh`, `sum_runs.py` |
| the search on either bench (`--chain N`) | `linksearch.cpp` |
| the rates of damage passing a CRC unseen | `rates.py` |
| the kept cases, found again by a script | `make_cases.sh`, `cases/` |

**Properties added,** each watched from outside the link end, in both benches:

- **A sender is acknowledged before long:** with packets kept, all of them sent, on a link in step with whole
  wires, an acknowledgement moves within a bound (8 status periods and recoveries by default). This is what
  would have caught the starvation of the header of 16 bytes in a search.
- **A foreground packet's turn:** with nothing but the line to wait for, it starts within one packet.
- **A node's own packet never goes before one it is forwarding.**
- **At the host,** a packet's hops name a node of the chain, and its words are the next of that node's.

**What was built differently from the plan:**

| the plan | as built |
|---|---|
| a failing seed cut down event by event | not built. A failing seed fails again from its seed alone; the search gives the shortest case of a flaw. Cutting a random run down is still to do if a seed fails that the search cannot reach |
| every transition numbered and counted | the events of the table below are counted; the state diagrams with numbered transitions are not drawn |
| "a header not sealed again" planted in the node | planted in the link end: a node cannot pass a header through, since the outgoing end writes and seals every header |
| the wire's forms "its tail to zeros" and "bytes before it" | not among the aimed forms; the bursts make both |

### The searches on the new header: every one complete

**One link, the proposed rules** (4,110,540 states; the same list as section 16, and each is about a tenth the size):

| the search | states | |
|---|---|---|
| `--packets 2 0 2 0 --damage 1` | 414,807 | holds |
| `--packets 1 0 1 0 --damage 2` | 85,494 | holds |
| `--packets 1 0 0 0 --damage 3` | 28,359 | holds |
| `--packets 2 0 0 0 --damage 2` | 33,440 | holds |
| `--packets 1 1 0 0 --damage 2` | 66,914 | holds |
| `--packets 2 2 0 0 --damage 1` | 141,320 | holds |
| `--packets 1 1 1 0 --damage 1` | 144,825 | holds |
| `--packets 3 0 0 0 --damage 1 --queue 4` | 34,595 | holds |
| `--packets 3 0 0 0 --damage 1 --queue 2` | 51,990 | holds |
| `--packets 2 1 0 0 --damage 1 --queue 4` | 68,344 | holds |
| `--packets 7 0 0 0 --damage 1` | 342,638 | holds |
| `--packets 2 0 1 0 --damage 0 --noise 2` | 218,028 | holds |
| `--packets 2 0 1 0 --damage 1 --noise 1` | 598,558 | holds |
| `--packets 2 0 1 0 --damage 1 --delay 7` | 144,755 | holds |
| `--packets 2 0 1 0 --damage 1 --delay 20` | 282,193 | holds |
| `--packets 2 0 1 0 --damage 1 --tick 8` | 368,642 | holds |
| `--packets 1 0 1 0 --damage 2 --images --words 2` | 170,678 | holds |
| `--packets 2 0 2 0 --damage 0 --power 1` | 194,272 | holds |
| `--packets 1 0 1 0 --damage 1 --power 1` | 122,072 | holds |
| `--packets 2 0 1 0 --damage 1 --flip 1` | 327,136 | holds |
| `--packets 2 0 1 0 --damage 1 --wrong 1` | 41,187 | holds |
| `--packets 5 0 0 0 --damage 1 --queue 4 --window0 4` | 193,516 | holds |
| `--packets 1 0 1 0 --damage 1 --whole` | 22,343 | holds |
| `--packets 1 0 1 0 --damage 1` | 14,434 | holds |
| `--packets 2 0 2 0 --damage 2` | 2,593,263 | holds |
| `--packets 2 0 1 0 --damage 3` | 1,687,356 | holds |
| `--packets 2 2 0 0 --damage 2` | 961,887 | holds |
| `--packets 3 0 3 0 --damage 1` | 4,290,326 | holds |

- **Two packets each way with two damaged is complete** (2,593,263 states): on the header of 16 bytes it was
  stopped at 30 million.

**The starvation** (13,714,770 states): the other end sends 5 or 8 packets back to back; no sender waits 150 cycles.

| the search | states | |
|---|---|---|
| `as_written` `--packets 0 3 5 0 --damage 0 --still 150` | 4,066,995 | holds |
| `proposed-ack_now` `--packets 0 3 5 0 --damage 0 --still 150` | 5,199,100 | holds |
| `proposed` `--packets 0 3 5 0 --damage 0 --still 150` | 1,922,830 | holds |
| `proposed` `--packets 0 2 8 0 --damage 0 --still 150` | 2,525,845 | holds |

**Each rule not shown to be needed, either way, and `reask` at 24 and 400** (1,908,572 states):

| the search | states | |
|---|---|---|
| `proposed-lost_uses_ack` `--packets 1 0 1 0 --damage 2` | 85,544 | holds |
| `proposed-lost_uses_ack` `--packets 1 1 0 0 --damage 2` | 66,986 | holds |
| `proposed-lost_uses_ack` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,595 | holds |
| `proposed-long_is_lost` `--packets 1 0 1 0 --damage 2` | 85,494 | holds |
| `proposed-long_is_lost` `--packets 1 1 0 0 --damage 2` | 66,914 | holds |
| `proposed-long_is_lost` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,595 | holds |
| `proposed+stop_lift_back` `--packets 1 0 1 0 --damage 2` | 85,494 | holds |
| `proposed+stop_lift_back` `--packets 1 1 0 0 --damage 2` | 66,914 | holds |
| `proposed+stop_lift_back` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,713 | holds |
| `proposed-ack_now` `--packets 1 0 1 0 --damage 2` | 110,015 | holds |
| `proposed-ack_now` `--packets 1 1 0 0 --damage 2` | 102,244 | holds |
| `proposed-ack_now` `--packets 3 0 0 0 --damage 1 --queue 4` | 66,689 | holds |
| `proposed+ask_every` `--packets 1 0 1 0 --damage 2` | 85,494 | holds |
| `proposed+ask_every` `--packets 1 1 0 0 --damage 2` | 66,914 | holds |
| `proposed+ask_every` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,690 | holds |
| `proposed,resend_timer=96` `--packets 1 0 1 0 --damage 2` | 98,094 | holds |
| `proposed,resend_timer=96` `--packets 1 1 0 0 --damage 2` | 74,359 | holds |
| `proposed,resend_timer=96` `--packets 3 0 0 0 --damage 1 --queue 4` | 35,325 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 1 0 1 0 --damage 2` | 125,160 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 1 1 0 0 --damage 2` | 97,212 | holds |
| `proposed-zero_number,resend_timer=96` `--packets 3 0 0 0 --damage 1 --queue 4` | 37,793 | holds |
| `proposed,reask=24` `--packets 1 0 1 0 --damage 2` | 88,943 | holds |
| `proposed,reask=24` `--packets 1 1 0 0 --damage 2` | 64,229 | holds |
| `proposed,reask=24` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,812 | holds |
| `proposed,reask=400` `--packets 1 0 1 0 --damage 2` | 106,033 | holds |
| `proposed,reask=400` `--packets 1 1 0 0 --damage 2` | 84,722 | holds |
| `proposed,reask=400` `--packets 3 0 0 0 --damage 1 --queue 4` | 34,595 | holds |

**G11 without its rule,** the window a quarter of the queue (the default) and half of it (1,174,097 states):

| the search | states | |
|---|---|---|
| `proposed-refuse` `--packets 4 0 0 0 --damage 2` | 185,501 | holds |
| `proposed-refuse` `--packets 4 0 0 0 --damage 2 --queue 4` | 530,874 | holds |
| `proposed-refuse` `--packets 5 0 0 0 --damage 1 --queue 4` | 189,730 | holds |
| `proposed-refuse` `--packets 3 1 0 0 --damage 1 --queue 4` | 267,992 | holds |

**One forged header and one damaged packet** (1,638,717 states):

| the search | states | |
|---|---|---|
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-ack` | 71,423 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-stop` | 60,883 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-lost` | 61,462 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-recovery` | 45,871 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-resend` | 49,802 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-number` | 83,670 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-number-back` | 50,724 | holds |
| `--packets 1 0 1 0 --damage 1 --forge 1 --fates …,forge-ack-data` | 39,431 | holds |
| `--packets 3 0 0 0 --damage 1 --forge 1 --fates …,forge-stop --queue 2` | 231,220 | holds |
| `--packets 2 0 1 0 --damage 1 --forge 1 --fates …,forge-number` | 463,828 | holds |
| `--packets 1 0 1 0 --damage 2 --forge 1 --fates …,forge-number` | 460,971 | holds |
| `--packets 2 0 0 0 --damage 1 --forge 1 --fates …,forge-number-back` | 19,432 | **fails:** 18 states, word 1 delivered where word 0 was next |

**The chain:** the host and 1, 2 or 3 nodes; `--cpackets a b c` is the host's foreground and background packets
and each node's replies (4,419,487 states):

| the search | states | |
|---|---|---|
| `--chain 1 --cpackets 1 1 1 --damage 1` | 150,077 | holds |
| `--chain 1 --cpackets 2 0 1 --damage 1` | 78,081 | holds |
| `--chain 1 --cpackets 1 0 1 --damage 2` | 87,330 | holds |
| `--chain 1 --cpackets 0 2 1 --damage 1 --queue 2` | 367,667 | holds |
| `--chain 2 --cpackets 1 0 0 --damage 2` | 526,336 | holds |
| `--chain 2 --cpackets 0 1 0 --damage 2` | 515,314 | holds |
| `--chain 2 --cpackets 0 0 1 --damage 1` | 235,937 | holds |
| `--chain 2 --cpackets 1 0 1 --damage 0` | 323,954 | holds |
| `--chain 2 --cpackets 0 2 0 --damage 0 --queue 2 --fwd 2` | 569,452 | holds |
| `--chain 2 --cpackets 0 1 0 --damage 1 --queue 2 --fwd 2` | 342,495 | holds |
| `--chain 2 --cpackets 0 0 2 --damage 0 --fwd 2` | 900,676 | holds |
| `--chain 3 --cpackets 1 0 0 --damage 1` | 80,251 | holds |
| `--chain 3 --cpackets 0 0 1 --damage 0` | 241,917 | holds |

- **Larger than this machine:** a chain of two with a packet on each stream and a reply from each node, one
  damaged (`searches.sh big5`), was killed for memory here. It and three more are queued for the runner.

### The planted bugs: 14, every one caught

| planted | by the search (`plants.sh`) | by the random runs (`plants_run.sh`: seeds that caught it) |
|---|---|---|
| `ACK_ONE_OFF` | `--damage 1 --packets 2 0 1 0`: the link started again, and nothing had happened that could excuse it | 20 of 20 |
| `WINDOW_NOT_KEPT` | `--damage 1 --packets 3 0 0 0 --queue 2`: the link started again, and nothing had happened that could excuse it | 4 of 20 |
| `WRAP_WRONG` | `--damage 0 --packets 7 0 0 0`: nothing more goes wrong, and it does not finish | 13 of 20 |
| `TIMER_NOT_RESTARTED` | `--damage 1 --packets 2 0 1 0`: a sender stood still | 0 of 20 |
| `DUPLICATE_TAKEN` | `--damage 1 --packets 2 0 1 0`: word 0 delivered where word 1 was next | 0 of 20 |
| `STOP_IGNORED` | `--damage 1 --packets 3 0 0 0 --queue 2`: a data packet started on a stream the other end has stopped | 11 of 20 |
| `RECOVERY_ONCE` | `--damage 2 --packets 1 0 1 0`: nothing more goes wrong, and it does not finish | 7 of 20 |
| `ZERO_TAKES_NUMBER` | `--damage 1 --packets 2 0 1 0`: more kept than the window | 20 of 20 |
| `ACK_FROM_BAD_DATA` | `--damage 1 --forge 1 --fates gone,forge-ack-data --packets 1 0 1 0`: the link started again, and nothing had happened that could excuse it | 0 of 20 |
| `NO_ZEROS` | `--damage 1 --packets 2 0 1 0`: nothing more goes wrong, and it does not finish | 12 of 20 |
| `OTHER_STREAM_STALE` | `--damage 0 --packets 0 3 5 0 --still 150`: a sender stood still | 5 of 20 |
| `HOPS_NOT_COUNTED` | `--damage 0 --chain 2 --cpackets 0 0 2`: at the host, under hops 0, a word that node did not send | 13 of 20 |
| `NOT_SEALED_AGAIN` | `--damage 0 --chain 2 --cpackets 0 0 2`: nothing more goes wrong, and it does not finish | 13 of 20 |
| `OWN_BEFORE_FORWARDED` | `--damage 0 --chain 2 --cpackets 0 0 2`: a node sent a packet of its own before one it was forwarding | 10 of 20 |

- **Three are caught by the search only.** `TIMER_NOT_RESTARTED`: with an acknowledgement for every packet the
  status timer is seldom what a run waits on. `DUPLICATE_TAKEN`: no packet arrives twice in the runs (below).
  `ACK_FROM_BAD_DATA` needs a forged header.

### The first random runs, here: 300 seeds

`build/linkrun --seeds 1 300 --cycles 300000`: chains of 1, 2, 4 and 16, every process drawn from its seed.

```
runs 300 (chains of 1, 2, 4, 16: 114, 86, 66, 34); FAILED 0
cycles 202073653; packets 5080194; words delivered: 25689841 from the host (every node counted), 1323997 to it
coming to rest after the noise: 373578 cycles on average, 2038152 at most
bytes sent: 579899700 in packets with data, 53616460 in packets of length 0 in step (9.25% of the data's), 1835560 in recovery and restart rounds
the most: packets a node kept on a stream 8, the host 18; words in a queue 626; words in a forwarding buffer 677
the longest: a foreground packet waiting for the line with nothing else to wait for 532 cycles; a sender without an acknowledgement on a link in step 2736
recoveries (a link's last damaged byte to its next delivery, or to nothing owed): 20269, 736 cycles on average, 24051 at most (seed 47)
```

**Rare events, counted.** What never happened is marked, and is covered by the search: a packet seen before
(`plants.sh`, `reask=1`), a packet with no room (`g11-overrun.case`), the three that end in a restart (the `g20`,
`g21` and `g22` cases), and a field that cannot be (`g22`).

```
rare events, counted:
  damage aimed at             data    sent again        status          lost      recovery lost+recovery       restart
  gone                         442           262          1389           125           176            27            65
  first0                       445           281          1354           118           179            27            83
  header                       432           284          1338           123           206            22            68
  data                         411           257             0             0             0             0             0
  packets sent: data                                                2275977
  sent again                                                          31616
  status (length 0, in step)                                        2680823
  "lost"                                                              42640
  "recovery"                                                          23443
  "lost" and "recovery" together (both ends lost)                      4459
  restart                                                             21236
  header CRC failed                                                   22392
  data checksum failed                                                 6805
  a field that cannot be                                                  0   <- never happened
  a header CRC failed during a recovery                               21828
  a stop bit taken during a recovery                                  36909
  a packet ahead of the one expected, or a number ahead                7227
  a packet seen before                                                    0   <- never happened
  a packet with no room (G11)                                             0   <- never happened
  a sender went back                                                   9812
  the number wrapped on a new packet                                  18816
  the number wrapped on a packet sent again                            1172
  bursts of noise                                                    117119
  cycles inside a burst                                            18743601
  single bits flipped                                                 36598
  consumer stalls                                                   2811660
  an acknowledgement behind (G20)                                         0   <- never happened
  an acknowledgement beyond what was sent (G20)                           0   <- never happened
  a link started again                                                    0   <- never happened
  a kept packet found bad (G21)                                           0   <- never happened
  a packet made wrong (G22)                                               0   <- never happened
```

- **The window of 8 and the queues:** a node never kept more than 8; **the fullest a queue got is 626 words of
  1,024, a forwarding buffer 677.** No packet was ever refused.
- **Recovery:** from a link's last damaged byte to its next delivery (or to nothing owed), 736 cycles on
  average and 24,051 at most, over 20,269 recoveries.
- **How long it takes here:** about half a million cycles a second on one core, the chains mixed; 300 seeds in
  7 minutes.

### Queued for the runner (Quest attached to the session; `tasks/make_task.sh` writes the tasks)

| Quest | what | |
|---|---|---|
| 227 `link-w8-campaign` | the random runs: 16 slices of seeds at once for 30 minutes, 2 million cycles of traffic and noise a seed, chains of 1, 2, 4 and 16; the slices added up (`sum_runs.py`) | queued |
| 228 `link-w8-long-noise` | 16 runs with bursts up to 300 ms (3 × 10⁸ cycles): 10 on a chain of 1 for 1.5 × 10⁹ cycles, 6 on a chain of 2 for 7 × 10⁸ | queued |
| 229 `link-w8-searches` | six searches larger than this machine, 20 million states each at most, three at a time: a chain of 2 with a packet on each stream and a reply from each node, one damaged; a chain of 2 with two damaged; a chain of 2 with small queues and buffers; a chain of 3; one link with three packets each way and two damaged; one link with three sources | queued |

All three are of `updater_wip/link` at `ac4d3078`. Their results go into `REPORT.md`.

**Still to do after them:** `REPORT.md`; the state diagrams with their transitions numbered; cutting a failing
random run down, if one ever fails; the list of changes for `HOST-LINK.md`, which is `QUESTIONS.md`'s rules once
the campaign has confirmed them.
