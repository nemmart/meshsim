# WORKER8 report: HOST-LINK.md as it stands on 2026-11-17, evening

**Stopped a second time, as NEXT.md orders. One more way the protocol is not sound: a header that reads as zeros,
in front of data that holds a packet.** The packet in the data is taken for the true one, a word is delivered
that is not the next, a true packet is lost, and no check fails. The cause is known and a shortest case is kept.
It is not mended. It is as old as the link's framing, not a result of today's changes.

**With the two rulings of this afternoon, everything else searched so far holds** (section 4). The rest of NEXT.md
is not done: section 9.

Names: HL is `v1.4_wip/planning/HOST-LINK.md`. A dagger (†) marks what I propose or chose. Every figure here was
measured on this branch on 2026-11-17, on the code as committed. A cycle is one byte on the link.

## 1. What fails

**The receiver finds a packet's start by the first byte that is not zero. If the line reads as zeros through a
header, it sees idle, and the first data byte that is not zero starts a "header".** What comes of it depends on
the data:

| the data behind the lost header | what the receiver does | how it ends |
|---|---|---|
| ordinary data | fails a header CRC, loses its place, "Recovery" | the packet is sent again. Holds: every search below has this damage among its fates |
| a header of length 0 that passes its CRC | believes it: its acknowledge field, its stop bits, its flags | a false acknowledgement. With a second packet damaged the sender is asked for a packet it no longer keeps: the error, and the link starts again. Loud |
| **a whole packet: a header with the number expected, and its data** | **takes it. Its data goes into the queue. The true packet is acknowledged and let go, or arrives later as "seen before"** | **a word delivered that was not the next, a true packet lost, no error. Silent** |

The case is `updater_wip/link/cases/packet-in-data.case`: 2 events on the shrunken model.

| cycle | what happens |
|---|---|
| 289 | end 0 lets out one foreground packet of two words. Its first word is a header: foreground, length 1, number 1, the checksum of the second word |
| 321 | end 0 sends it, as packet 1. Its 16 header bytes read as zeros on the wire; its 32 data bytes arrive whole |
| 369 | end 1 has seen idle, then a good header, number 1, the one it expects, then one word that passes its checksum: "foreground data, number 1: taken". Word 1 is delivered where word 0 was next |
| after | end 1 has taken number 1, and that is what it acknowledges: end 0's packet, with word 0 in it, is let go |

**Found by** `linksearch --rules hl,max_words=2,flush_zeros=48,zeros=64 --packets 1 0 0 0 --damage 1 --images3 --words 2`:
304 states before the failure. Two things are new in the search: a fate `head0` (the header reads as zeros, the
data is whole), now among the damage of every search; and data that holds a packet (`--images3`).

## 2. Why, and how likely

- **HL's rule:** "Idle is zero bytes, in any number: the first byte that is not zero starts a header." Nothing
  else marks a packet's start.
- **HL already closed this door once:** after a failed header CRC a receiver takes nothing until 528 zeros have
  gone by, because "it read the bad packet's data as headers, and one of them can pass". Here no CRC fails, so
  that rule never comes into play.
- **It needs three things at once:**

| what | how likely |
|---|---|
| the line reads as exact zeros for a stretch | a break or a squelched line, if that is how a dead line reads. I do not know the receivers. My random runs have had this kind of noise from the start |
| the stretch ends where a header's image starts in the data: on a word of the data | certain if the stretch is the header alone; about 1 in 16 if it ends anywhere |
| the data there holds a header that passes its CRC, and for the silent case a whole packet with the number expected | **by chance, 1 in 2³² a word:** the class of the forged header, which the owner has accepted. **Certain if the data is link packets:** a capture of this link sent as a frame, a file of test vectors of packets, or data made on purpose |

- **So against chance it is inside the limit already accepted. Against data that holds packets it is not.** The
  background stream carries whatever the host is given to send.
- **Not from today's changes.** Every form of the header had it. My searches before had no fate that zeroed a
  header and left its data.

## 3. What I would change

Nothing here is built or tested.

| | the change | what it costs | what it closes |
|---|---|---|---|
| 1 † | **Mask the data on the wire.** Each data byte is XORed with a sequence started from that packet's own header CRC. A receiver that read the header takes the mask off. One that did not sees bytes that whoever made the data could not choose | a 32-bit shift register a direction. No handshake, no new field. The data checksum stays over the data as held | chance, captures and test vectors. Data made on purpose would have to know the header's CRC, which holds the acknowledge field of the moment |
| 2 | **A value for each life of a link,** drawn at the restart and carried in the restart packet, as the header CRC's starting value | a register, a source of a random value, a field in the restart packet | the same, but a capture from the same life still passes |
| 3 | **Accept it as a known limit,** with a rule for the host: the data must never hold link packets | nothing in the chip | nothing. It moves the question to whoever makes frames |

- **With the mask, a packet's data still cannot hold 528 zeros** (it is at most 512 bytes), so the rule for a lost
  receiver stands.
- **I would take 1.** It is the only one of the three that needs nothing agreed between the ends.

## 4. What holds, with this afternoon's two rulings

The code is HL as it stands: the header of 16 bytes; one run of numbers; "stop" holding only packets not yet
numbered; asking again at any sign the sender is ahead; the restart packet with its pattern; both checks made
afresh with the pass over the data; the made-wrong flag; the error on a packet no longer kept. HL's four example
headers and the restart packet's checksum 0xC8981D21 come out byte for byte.

**The search you asked me to finish** (`--packets 2 4 1 0 --queue 4 --damage 1 --hold 3`) **is not finished here:**
12,000,000 states, no failure, and this machine's 7 GB ran out with 2,671,876 states still waiting. It is queued
for the runner (section 9).

**A stopped stream cannot block the other** (`searches.sh held`; `--hold 3` is each stream never taken again, in
turn; queues of 4 words so that "stop" comes after two packets):

| the search | states | |
|---|---|---|
| HL as first written (`+stop_holds_again`), `--packets 1 3 0 0 --queue 4 --damage 1 --hold 2` | 23,285 | **fails:** the case of the first report |
| `--packets 1 3 0 0 --queue 4 --damage 1 --hold 3` | 640,083 | holds |
| `--packets 3 1 0 0 --queue 4 --damage 1 --hold 3` | 637,003 | holds |
| `--packets 1 3 0 0 --queue 4 --damage 2 --hold 2` | 3,297,538 | holds |
| `--packets 1 2 1 0 --queue 4 --damage 1 --hold 3` | 2,346,739 | holds |
| `--packets 1 3 0 0 --queue 4 --damage 0 --noise 2 --hold 3` | 1,413,687 | holds |
| a chain of 1: `--chain 1 --cpackets 1 3 0 --queue 4 --damage 1 --hold 3` | 648,380 | holds |
| a chain of 1: `--chain 1 --cpackets 1 3 1 --queue 4 --damage 0 --hold 3` | 826,770 | holds |

- Two more outgrew this machine and gave no result: the same with a wire delay of 7, and a chain of 2. They are
  in the runner's list (`big17`, `big18`).

**One link, the list of the headers before** (`searches.sh main`): 24 searches, 7,785,529 states, every one
complete, every one holds. On the header of 20 bytes the same list was 4,110,540 states with 4 kinds of damage;
now there are 5 (`head0`), the sender's pass over the data is a counter in the state, and the restart packet is
long. Some of the lines:

| the search | 20 bytes | now |
|---|---|---|
| `--packets 2 0 2 0 --damage 1` | 414,807 | 1,001,592 |
| `--packets 1 0 1 0 --damage 2` | 85,494 | 120,914 |
| `--packets 2 2 0 0 --damage 1` | 141,320 | 470,724 |
| `--packets 1 1 1 0 --damage 1` | 144,825 | 299,770 |
| `--packets 7 0 0 0 --damage 1` | 342,638 | 759,705 |
| `--packets 2 0 1 0 --damage 1 --noise 1` | 598,558 | 1,024,849 |
| `--packets 2 0 2 0 --damage 0 --power 1` | 194,272 | 541,982 |
| `--packets 2 0 1 0 --damage 1 --flip 1` | 327,136 | 561,608 |
| `--packets 2 0 1 0 --damage 1 --wrong 1` | 41,187 | 120,921 |
| `--packets 1 0 1 0 --damage 1`, states kept whole, and by the hash | 22,343 and 14,434 | 34,935 and 20,779 |

**The other lists:**

| list | searches | states | |
|---|---|---|---|
| `forged`: one forged header and one damaged packet, field by field, the three new fields among them | 23 | 8,638,610 | 22 hold. The 23rd is the false packet number, the known limit, and fails as before |
| `rules`: without "lost uses the acknowledge field", without "acknowledge at once", asking again after 24 and 400, the pass at 0 and 4 bytes a cycle | 18 | 1,765,433 | all hold |
| `window`: no rule to refuse a packet with no room | 4 | 2,451,780 | all hold |
| `starve`: the other side sends 5 or 8 packets back to back; no sender waits 150 cycles for an acknowledgement | 3 | 31,295,019 | all hold |

- **A false acknowledgement** (change 5). With a forged acknowledge field and a damaged packet: where a packet was
  lost the error is always raised; in no state was the error raised and the packet then arrived after all. Two
  forged fields in a row hold too (102,235 states).
- **A packet made wrong** (change 4). One forged header that makes a field wrong, or a length of 33: asked for
  again, and no error. Two forged in one run hold too (632,713 states). A packet its sender made wrong comes a
  second time and ends as the error and a link that starts again (`--wrong 1`, 120,921 states).
- **A bit flipped in a sender's memory** is delivered unseen and counted apart; the link does not hang (561,608
  states).
- **17 planted bugs, every one caught by the search** (`plants.sh`), the four new ones among them: the flag
  cleared by a packet thrown away; an error on an acknowledgement behind with nothing asked; a restart acted on
  with its pattern wrong; a checksum not made afresh on a packet sent again.
- **A kept case for every rule** (`cases/`, 12 cases, 22 runs, all as expected): each fails without its rule and
  holds with it. New: `made-wrong`, `false-acknowledgement`, `stopped-blocks`, `seeds-227`, `packet-in-data`.

**The zeros a lost receiver waits for** (528 †): with data that is a word of zeros and then a header's image, and
packets of at most 32 data bytes, waiting for 16 zeros fails and 17 holds (245,243 states). So the number must be
more than the zeros a packet's data can hold in front of a header's image: 496 at the real sizes. 528 has room.

**A mistake of my own, found and fixed:** on the second over-long packet running the receiver was left in the
middle of a header and wrote past its buffer. The sanitizer build found it.

## 5. The two seeds that failed (Quest 227: 200137, 140162)

As the first report said, and now in HL: a "resend" was asked once and lost, and my rule asked again only at a
packet of length 0. Not the host's window alone. `cases/seeds-227.case` fails without the rule and holds with it.

## 6. What things cost, at the real sizes (`build/linkcost`)

**One run of numbers: a damaged background packet and the foreground behind it.** A background flood; one full
foreground packet let out at 101 different moments around the damaged packet; cycles from let out to delivered,
over the same moment with nothing damaged:

| the background packet's damage | at most | on average | packets sent again |
|---|---|---|---|
| a bit of its data | 1,071 more | 553 more | 2 |
| a bit of its header | 2,624 more | 1,677 more | 2 |
| its first byte zero | 3,152 more | 1,709 more | 3 |
| the whole packet gone | 1,598 more | 993 more | 3 |

- With numbers for each stream the first row would have been 0. The other three lose the receiver's place or a
  whole packet, and cost the link a recovery whatever the numbering.

**The shared window.** A background flood, a full foreground packet let out now and then, by the wire's delay
each way. The background's bytes of data a cycle, and the most packets kept:

| delay | window 2 | window 4 | window 8 | window 16 |
|---|---|---|---|---|
| 1 | 0.6806, kept 2 | 0.6824, kept 2 | 0.6824, kept 2 | 0.6824, kept 2 |
| 128 | 0.5464, kept 2 | 0.6921, kept 2 | 0.6921, kept 2 | 0.6921, kept 2 |
| 512 | 0.3178, kept 2 | 0.7336, kept 3 | 0.7336, kept 3 | 0.7336, kept 3 |
| 2,048 | 0.1024, kept 2 | 0.2931, kept 4 | 0.6709, kept 8 | 0.8095, kept 9 |

- **8 is still right.** Up to a delay of 512 cycles each way the window of 8 is never full and a foreground packet
  never waits for it. With full packets flooding the other way as well, 5 are kept at most at 512.
- At a delay of 2,048 the window of 8 fills: a foreground packet then waited up to 1,404 cycles, 893 of them
  for the window (1,689 and 1,178 with full packets flooding the other way).

**The pass over the data** (a byte a cycle, before the header leaves), against no pass:

| | no pass | with the pass |
|---|---|---|
| a one-word foreground packet on an idle link, let out to delivered | 33 on average, 48 at most | 49, 64 |
| a full foreground packet on an idle link | 529, 544 | 1,041, 1,054 |
| a full foreground packet beside a background flood, let out to its first byte | 272, 525 | 512, 525 |
| the background's bytes a cycle in that flood | 0.7548 | 0.6824 |
| 400 one-word packets back to back, link up to all acknowledged | 12,832 | 12,833 |
| 400 full packets back to back | 211,247 | 211,744 |

- **On a busy link of one stream the pass costs one packet's pass at the start and nothing after,** as HL says.
  **On an idle link it doubles a full packet's time.**
- **Where the streams meet it costs both.** † As I built it, a foreground packet that arrives takes the sending
  unit at once, the background packet whose pass was under way starts over later, and the line stands idle until
  the foreground's pass is done: the flood lost a tenth of its rate with a foreground packet every 2,600 cycles
  or so. The other way to build it lets the background packet go first, and the foreground then waits up to two
  packets. HL's "the longest a foreground packet waits is one background packet" holds only with a faster unit.

**A packet made wrong, twice** (full packets back to back, the 10th made wrong): it comes again 1,071 cycles
later and the error is raised; the next word is delivered 4,192 cycles after that; 128 words were lost with the
restart.

**Coming up:** both ends from power-on, 4,657 cycles to a link in step. One end powering on under a running
link: the other raises its error 1,554 cycles later, and the link is up again after 3,648.

## 7. What I chose where HL is silent (†)

| where | what I built |
|---|---|
| the restart packet | stream field 1, number field 1, acknowledge field and stop bits not used by the receiver; sent in rounds after 1,024 zeros, like "lost" |
| both ends starting at once | restart with lost and recovery both set, as "lost" has. The handshake ends at a restart packet without lost, or at any ordinary packet |
| an end that is starting again | takes nothing from ordinary packets until it has its answer |
| a restart packet whose pattern or checksum is wrong | a damaged packet like any other: thrown away |
| "asks for a packet no longer kept" | resend, or "lost", with an acknowledge field behind the last one the sender took |
| a length over 32 | counted as "made wrong" only if its number field is the one expected, and not while the receiver is already lost |
| a packet made wrong whose number is not the one expected | thrown away as ahead or behind, and not counted |
| asking again | a thrown-away packet that carries the number expected is asked for at once, whatever the 2,048 cycles; those are for signs that the sender is ahead |
| the pass over the data | one unit a direction; it starts on the packet that will go next, and starts over if that changes |

## 8. Corrections to HL, so far

Not the full reading NEXT.md asks for: these are the places I met while building.

| where | what |
|---|---|
| "Two streams" and "The chain" | "the longest a foreground packet waits is one background packet", and "a foreground packet never waited more than one packet's time for the line": not with the pass over the data. Section 6 |
| "The order a receiver tests in", step 2 | says hops is not tested; "When a packet is bad" tests hops 15 on a packet to forward. And a field that fails is judged only after the data: the packet's data must be passed over and checked first, since "made wrong" needs both checks to pass |
| "Starting a link again" | silent on everything in section 7's first four rows |
| "A packet made wrong" | silent on a wrong packet with another number, and on a length over 32 whose number is not the one expected |
| "The size of a packet", "A hop holds a packet for its length" | a hop holds it for its length and then, on an idle line, for the pass: twice |
| "Where this document stands", "Holding the host back", "The chain", "When a packet is bad" | figures of the headers before: 36 million states, 14 planted bugs, 626 and 677 words, the timer's table, 29,703 and 17,553 cycles, 5 million packets |
| "Finding a packet's start" | section 1 of this report |

## 9. Not done

| piece of NEXT.md | state |
|---|---|
| the search cut short | 12,000,000 states here, no failure. Queued for the runner as Quest 230, alone with the memory the task has. On the code as it is now it has `head0` among its damage as well |
| the chain's searches (1, 2, 3 nodes) | not run on this code, but for the two chains of 1 in the held list above |
| the random campaign, chains of 1, 2, 4 and 16 | 40 runs in `make check`, none failing. No campaign, no rare-event table |
| Quest tasks for the campaign, long noise and the larger searches | not queued. 227 to 229 stay in `tasks/hold`. The generator has the new single-search item only |
| figures for 2,048 cycles, the host's window, the forwarding buffers (`figures.sh`) | written, not run |
| HL read as a stranger would | section 8 only |
| `QUESTIONS.md`, `NOTES.md`, the state diagrams | not brought up to date; `NOTES.md` has a status line |

`make check` passes: the header, the link's quick tests, 12 cases, four small searches, 40 random runs, 17 planted
bugs, under gcc with the sanitizers and under clang.
