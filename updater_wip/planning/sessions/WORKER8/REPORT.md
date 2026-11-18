# WORKER8 report: HOST-LINK.md of 2026-11-17 night, read against the code that passes

**The protocol as HL now has it holds in everything run.** Since the report before: HL was brought up to the code;
the long search finished and holds; and the code was brought up to HL in the one place where HL had moved past
it, the sender's fast unit.

**What is new here:**

1. **HL read again, as a stranger would:** 6 places wrong, 9 unclear, 12 silent (section 1), and **the figures it
   quotes from me are of the slow sending unit:** section 2 has each beside the figure with the fast unit.
2. **Quest 231, the long search, is complete and holds:** 86,515,438 states (section 3).
3. **One thing HL says that the runs now contradict:** the error for a packet no longer kept "is raised only when
   there is real harm". With **two** forged headers it is raised and nothing was lost (section 4). With one it
   holds, as searched. It is far inside the limit already accepted, and it errs on the loud side.
4. **The random campaign on the runner is back, with each unit:** 7,519 runs with the fast unit and 6,071 with
   the slow, none failed (section 10).

Names: HL is `v1.4_wip/planning/HOST-LINK.md` at `870ed9d9`. The code is `updater_wip/link/`. A dagger (†) marks
what I chose or propose. A cycle is one byte on the link. "The fast unit" is HL's sending unit of 128 bits a
cycle; "the slow unit" is a byte a cycle, which the code had until this report. Every figure here was measured
on this branch on 2026-11-18, on the code as committed, and the command that gives it is named.

## 1. HL read against the code: what is still wrong, unclear or silent

**The worked examples agree, all of them:** the four example headers, the three mask examples with their header
CRCs, starting values and wire bytes, the restart pattern and its checksum 0xC8981D21, the two standing states
of the mask's register, and the check value 0x48674BC7. They are checked by `test_header` and again by a second
implementation in Python that read the hex strings out of HL itself.

**Wrong:**

| | where in HL | what |
|---|---|---|
| W1 | "Starting a link again": the error "is raised only when there is real harm"; "the owner's needs no watching and raises none" | true with one forged header. With two it is raised and nothing was lost: section 4 |
| W2 | "One run of numbers", "What it costs": "no more than 3 packets are ever kept" up to a delay of 512 | 3 with only acknowledgements coming back; 5 with full packets flooding the other way (`linkcost window`) |
| W3 | "The CRC as hardware": "A byte a cycle is the fit" | stands above the table that makes the sending units 128 bits a cycle. It is true of the receiving units only |
| W4 | "Holding the host back": "The wait no longer grows with the timer" | that was the slow unit. With the fast unit it grows again: 2,727, 3,380 and 8,356 cycles at 512, 2,048 and 8,192 |
| W5 | "The chain", "Measured (WORKER8, 240 random runs) ... with buffers of 512 words, 349" | the 349 was of 30 runs, not of the 240 |
| W6 | "Where this document stands", "What is still out"; "Open", "The long search" and "One run of numbers ... in hand" | the long search is complete and holds; the costs are measured and in the document |

**Unclear:**

| | where in HL | what a stranger would trip on |
|---|---|---|
| U1 | "When a packet is bad": "sets resend" | resend is set in **one** header, the next to go, and not in every header until the packet comes. Only the 2,048 cycles imply it. One who set it in every header would send the sender back over and over |
| U2 | "Starting a link again", the table's third row: "I have started again, and so have you" | it is said by an end that is starting again and has heard the other's "I have started again" |
| U3 | the same: "The handshake ends at a restart packet without lost, or at any ordinary packet" | that is when an end stops **answering**. An end stops saying "I have started again" when it hears a restart packet with recovery |
| U4 | the same: "An end that hears 'I have started again' drops the packets ... and raises an error" | only the first it hears. The packet comes again every round, and an end already answering, or starting itself, does neither again |
| U5 | "A packet made wrong": the flag "is cleared only when a packet is taken" | and when the link starts again |
| U6 | "Two streams": "The longest a foreground packet waits for the line is one background packet" | "for the line" leaves out the window and the packets sent again before it. In 60 runs a foreground packet stood ready up to 6,367 cycles in all (section 5) |
| U7 | "Recovery", the other side, step 2: "carries on from the packet after the acknowledged one" | beside "the answering end sends no data until its answer has been heard": it carries on only then |
| U8 | "The two checks": the data checksum is of the bytes "in the order they go on the link" | of the bytes as held, in that order. On the link they are masked. The table above it has it right |
| U9 | "The mask's limit": "the stream, the length, the number, the acknowledge field, hops and the stop bits" | and the four flags of the moment: "resend" is set whenever the sender owes one |

**Silent:**

| | where it belongs | what the code does |
|---|---|---|
| S1 | "The CRC as hardware", the fast unit's header CRC: "the fixed value that those zeros bring to all ones" | the value is **0x641F6454**: 32 zero bits bring it to 0xFFFFFFFF (checked on HL's first example header) |
| S2 | the same | † whether the header's first bytes may leave while the pass still runs. The code holds the header until the pass is done, and counts no cycle for the header CRC |
| S3 | the same | † the restart packet has no pass: its checksum is a constant |
| S4 | "When a packet is bad" | what ends "resend owed": the packet taken, or a packet of length 0 whose number is the one expected (the sender has gone back) |
| S5 | "Starting a link again", the false acknowledgement | an acknowledge field **behind** the last one taken releases nothing and changes nothing. HL says it only of one beyond what was sent |
| S6 | "Recovery" | an end that is lost sends no data either, only its rounds. Step 4 implies it |
| S7 | "What a sender must keep" | a packet to be sent again goes before any new one of either stream. Go-back-N implies it |
| S8 | "Holding the host back", "When the status is sent" | † a packet of length 0 may go while a packet with data is still being passed over; the data follows it |
| S9 | "When the status is sent", step 1: "on a change" | "resend owed" and "a packet taken" are changes too, with the stop bits |
| S10 | "The link" | the wire's delay. The window of 8 is never full up to 512 cycles each way and fills at 2,048 (section 5) |
| S11 | "A packet made wrong": a packet of length 0 that is counted | it is also asked for again, like one with data, though there is nothing to send again |
| S12 | "The link", "When the status is sent" | a line that reads as zeros for long. It looks idle: the end that hears nothing stays in step and waits, its window full. In the runner's campaign a foreground packet stood ready 515,932 cycles that way (section 10). A live end sends a header every 512 cycles, so † an end that has heard none for some multiple of that could tell its owner the line is silent |

## 2. The figures HL quotes, against mine with the fast unit

HL's figures are mine, but most were measured with the slow unit. `linkcost`, `figures.sh` and `test_link` give
the right-hand column on the code as it now is.

| where in HL | HL has | with the fast unit |
|---|---|---|
| "Where this document stands" | 71.8 million states; 1,380 random runs | 36.9 million states here and 86.5 million on the runner; 1,140 runs here (section 3) |
| "Two streams": the longest wait for the line | 534 | 534 (60 runs) |
| "One run of numbers", a damaged background packet: a bit of its data | 1,071 at most, 553 on average | 1,056, 700 |
| the same: a bit of its header | 2,624, 1,677 | 2,144, 1,433 |
| the same: its first byte zero | 3,152, 1,709 | 2,672, 1,778 |
| the same: the whole packet gone | 1,598, 993 | 1,584, 1,067 |
| the same: at a delay of 2,048 a foreground packet waited | 1,404 | 849; 1,550 with full packets the other way |
| the same: the data rate by window 2, 4, 8, 16 | 0.4750, 0.5256, 0.5342, 0.5319 | 0.5803, 0.6252, 0.6389, 0.6392 |
| "The chain", the fullest forwarding buffer | 632; 349 with buffers of 512 words | 621 (60 runs); 361 (30 runs) |
| "The CRC as hardware", the table's empty cell: a full foreground packet on an idle link | | 561 on average, 574 at most |
| the same table's other rows | 0.5751; 0.8580; 534 | the same: they were already of the fast unit |
| "When a packet is bad", why 2,048: packets sent to a receiver that had them | 54 at 128 cycles, 18 at 512, none above | 232, 104, none |
| the same: "the random runs' 2 million packets" | 0 of 2 million | 0 of 385 million (Quest 237) |
| "A packet made wrong": the second comes after | 1,071 cycles; 128 words lost | 1,056; 128 |
| "Starting a link again", what these cost | 4,657; 1,554; 3,648 | the same |
| "Holding the host back", the timer's table: 512 | 0.4904, 8.79%, 2,548 | 0.5751, 7.64%, 2,727 |
| the same: 2,048 | 0.4908, 6.42%, 2,128 | 0.5756, 5.95%, 3,380 |
| the same: 8,192 | 0.4937, 6.20%, 1,822 | 0.5773, 5.76%, 8,356 |
| the same: 400 one-word packets from power-on | 31,023 without the acknowledgement at once, 17,489 with | the same |
| the same: packets of length 0 with and without it | 8.79%, 4.64% | 7.64%, 4.03% |
| the same: the host's window 8, 16, 64 | 0.5342, 0.5343, 0.5343 | 0.6389, 0.6392, 0.6392 |
| the same: the fullest queue | 630 | 629 here (60 runs); 953 in the runner's campaign, which also draws the host's window at 16 and 64. None refused |

- The data rates of the window and host-window rows are of 30 runs, the timer's of 60: different seeds, so 0.64
  and 0.58 are not to be set against each other.

## 3. What holds

**Quest 231: the long search is complete, and it holds.** `--packets 2 4 1 0 --queue 4 --damage 1 --hold 3`:
86,515,438 states, every one tried, depth-first, 2,813 seconds on the runner. From every state, with either
stream never taken again, the other finished. Breadth-first for its 1,400 seconds reached 39,859,919 states with
5,275,179 still waiting. It was run on the slow unit (`72f12b9a`); it is queued once more on the fast (Quest 242).

**The searches on this machine, with the fast unit: 36.9 million states, every search complete.**

| list (`searches.sh`) | what | searches | states | |
|---|---|---|---|---|
| `main` | one link, the list of the headers before | 24 | 4,493,052 | all hold |
| `held` | a stopped stream cannot block the other: each stream never taken again in turn | 7 | 4,873,086 | all hold; one more is HL as first written, and fails |
| `chain` | the host and 1, 2 or 3 nodes | 13 | 4,284,626 | all hold |
| `forged` | one forged header and one damaged packet, field by field | 24 | 5,126,743 | 23 hold; the 24th has two forged headers and fails: section 4 |
| `nested` | data that holds headers or packets, and a header that reads as zeros | 11 | 1,596,062 | 8 hold; 2 are without the mask and fail; 1 is the mask's limit and fails |
| `rules` | without "lost uses the acknowledge field", without "acknowledge at once", without the wait for 528 zeros, asking again after 24 and 400, the pass at a byte a cycle and with none | 21 | 1,654,818 | all hold |
| `window` | no rule to refuse a packet with no room | 4 | 1,294,654 | all hold |
| `starve` | the other side sends 5 or 8 packets back to back; no sender waits 150 cycles for an acknowledgement | 3 | 13,577,645 | all hold (run depth-first) |

**The state counts,** on the lines of `main` that can be compared:

| the search | 20 bytes, numbers for each stream | HL, the slow unit | HL, the fast unit |
|---|---|---|---|
| `--packets 2 0 2 0 --damage 1` | 414,807 | 995,260 | 447,722 |
| `--packets 1 0 1 0 --damage 2` | 85,494 | 118,412 | 87,090 |
| `--packets 2 2 0 0 --damage 1` | 141,320 | 467,453 | 191,428 |
| `--packets 1 1 1 0 --damage 1` | 144,825 | 296,820 | 160,908 |
| `--packets 7 0 0 0 --damage 1` | 342,638 | 759,530 | 375,382 |
| `--packets 2 0 1 0 --damage 1 --noise 1` | 598,558 | 975,013 | 627,659 |
| `--packets 2 0 2 0 --damage 0 --power 1` | 194,272 | 537,172 | 250,064 |
| `--packets 2 0 1 0 --damage 1 --flip 1` | 327,136 | 559,464 | 348,898 |
| `--packets 2 0 1 0 --damage 1 --wrong 1` | 41,187 | 120,530 | 85,840 |
| `--packets 1 0 1 0 --damage 1`, states kept whole, and by the hash | 22,343 and 14,434 | 36,931 and 20,402 | 25,244 and 14,661 |
| the 24 together | 4,110,540 | 7,684,999 | 4,493,052 |

- Against the header of 20 bytes there are 5 kinds of damage where there were 4, and one machine a direction
  where there were two. The slow unit's pass was a counter of up to 16 in the state; the fast unit's is one cycle
  at the small sizes.

**18 planted bugs, every one caught by the search** (`plants.sh`). Random runs alone catch 11 of them within 10
seeds (`plants_run.sh 10`).

**14 kept cases,** 28 runs, all as expected (`build/linkcase -v cases/*.case`), every one found again by the
search at the fast unit. New: `false-alarm`.

**The random runs on this machine, with the fast unit:** 1,140 runs under 28 settings (`figures.sh a` to `e`,
the same 60 or 30 seeds under each), on chains of 1, 2, 4 and 16. **Two failed, neither under HL's rules and
HL's noise:**

| the run | what failed | why |
|---|---|---|
| asking again only after 32,768 cycles (`--rules hl,reask=32768`, seed 18) | "a sender stood still: no acknowledgement in 20,737 cycles" | a request was lost, and the receiver would not ask again for 32,768 cycles. That is what the longer wait costs; the check's bound is 20,736. I have taken 32,768 out of the list |
| forged headers of every kind among the damage (`--forge`, seed 16) | "an error for a packet no longer kept, and the other end had taken every packet" | two forged headers: section 4 |

- With forged headers among the damage (`figures.sh e`, its first line, 30 runs, none failing): a packet made
  wrong asked for again 65 times, the error at the second 18 times, asked for a packet no longer kept 2 times, a
  link started again 40 times. The forging there is far more frequent than noise could make it.
- With bits flipped in senders' memory (30 runs): 72 bits flipped in kept packets, 7 wrong words delivered
  unseen, no error and no link standing still.

`make check` passes with the fast unit: the header with the mask's examples, the link's quick tests, the cases,
four small searches, 40 random runs, 18 planted bugs, under gcc with the sanitizers and under clang.

## 4. The error with nothing lost: two forged headers

**HL says the error for a packet no longer kept is raised only when there is real harm. That holds with one
forged header, and not with two.**

| cycle | what happens (`cases/false-alarm.case`, the shrunken model, a wire of 7 cycles) |
|---|---|
| 222 | end 1 sends a packet of length 0, acknowledge 0. **It is forged to acknowledge 1** |
| 239 | end 0 sends packet 1 |
| 244 | the forged header arrives. End 0 lets packet 1 go, while it is on its way |
| 261 | end 1 sends another packet of length 0, acknowledge 0: packet 1 has not landed. **It is forged to "resend"** |
| 277 | packet 1 lands whole and is taken |
| 283 | end 0 reads "resend" with an acknowledge field behind the one it took: it is asked for a packet it no longer keeps. It raises the error and the link starts again. Nothing was lost |

- **Found by a random run** with forged headers (seed 16 of `figures.sh e`), **then by the search:**
  `linksearch --packets 2 0 0 0 --damage 0 --forge 2 --fates forge-ack,forge-resend --delay 7`, 3 events.
- **Why my searches before missed it:** they forged two acknowledgements, or one header of any kind and one
  damaged packet, and on a wire of one cycle two headers do not fit while one packet is on its way.
- **How likely:** two headers forged by noise, each 1 in 2³² of the damaged ones, the second within a packet's
  flight of the first. Far inside what is accepted.
- **What it costs:** a link that starts again and says so, with nothing wrong. Loud, not silent.
- **A false "lost" does the same as a false "resend"** (the same search with `forge-lost`: the same failure).
- **I would change nothing but the sentence.** † If it mattered: a sender that is asked for a packet it no longer
  keeps could wait for the request to come a second time before raising the error; a true one always does.

## 5. What things cost, with the fast unit

**One run of numbers: a damaged background packet and the foreground behind it** (`build/linkcost setback`). A
background flood at the real sizes; one full foreground packet let out at 101 moments around the damaged packet;
cycles from let out to delivered, over the same moment with nothing damaged:

| the background packet's damage | at most | on average | packets sent again |
|---|---|---|---|
| a bit of its data | 1,056 more | 700 more | 2 |
| a bit of its header | 2,144 more | 1,433 more | 2 |
| its first byte zero | 2,672 more | 1,778 more | 3 |
| the whole packet gone | 1,584 more | 1,067 more | 3 |

**The shared window** (`build/linkcost window`). A background flood, a full foreground packet let out now and
then, by the wire's delay each way. The background's bytes of data a cycle, and the most packets kept:

| delay | window 2 | window 4 | window 8 | window 16 |
|---|---|---|---|---|
| 1 | 0.7556, kept 2 | 0.7556, kept 2 | 0.7556, kept 2 | 0.7556, kept 2 |
| 128 | 0.7636, kept 2 | 0.7636, kept 2 | 0.7636, kept 2 | 0.7636, kept 2 |
| 512 | 0.4679, kept 2 | 0.7901, kept 3 | 0.7901, kept 3 | 0.7901, kept 3 |
| 2,048 | 0.1236, kept 2 | 0.3319, kept 4 | 0.7592, kept 8 | 0.8519, kept 9 |

- **8 is still the right window.** Up to a delay of 512 cycles each way it is never full; with full packets
  flooding the other way as well, 5 are kept at most.
- At a delay of 2,048 it fills: a foreground packet then waited up to 849 cycles, 818 of them for the window
  (1,550 and 1,519 with full packets flooding the other way).
- In the random runs (`figures.sh d`, 30 runs each): 0.5803 bytes a cycle with a window of 2, 0.6252 with 4,
  0.6389 with 8, 0.6392 with 16.
- **A foreground packet's whole wait** in 60 random runs: ready at an end, not stopped, the link in step, until
  it starts: 192 cycles on average, 6,367 at most. The longest with the window full was 2,162; behind packets
  being sent again, 6,080. Those are with noise losing acknowledgements.

**The pass over the data** (`build/linkcost pass`, `figures.sh a`):

| | no pass | a byte a cycle | 128 bits a cycle |
|---|---|---|---|
| a one-word foreground packet on an idle link, let out to delivered | 33 on average, 48 at most | 49, 64 | 34, 48 |
| a full foreground packet on an idle link | 529, 544 | 1,041, 1,054 | 561, 574 |
| a full foreground packet beside a background flood, let out to its first byte | 272, 525 | 512, 525 | 271, 525 |
| the background's bytes a cycle in that flood | 0.7548 | 0.6824 | 0.7556 |
| 400 full packets back to back, link up to all acknowledged | 211,247 | 211,744 | 211,264 |
| the random runs, 60: the last node's bytes of data a cycle | 0.5703 | 0.4904 | 0.5751 |
| the same with every source saturated and the wires quiet | 0.8584 | 0.7138 | 0.8580 |
| the longest a foreground packet waited for the line with nothing else to wait for | 528 | 1,021 | 534 |

- **The fast unit gives back all that the slow one cost.**

**A packet made wrong, twice** (`build/linkcost wrong`; full packets back to back, the 10th made wrong): it comes
again 1,056 cycles later and the error is raised; the next word is delivered 3,712 cycles after that; 128 words
were lost with the restart.

**Coming up** (`build/linkcost start`): both ends from power-on, 4,657 cycles to a link in step. One end powering
on under a running link: the other raises its error 1,554 cycles later, and the link is up again after 3,648.

## 6. Figures for the numbers that were proposed, with the fast unit

| the number | what was measured | |
|---|---|---|
| **the host's window of 8** | with nothing coming back, the host stops by itself at 8 or 16 packets with the queue at 256 or 512 words of 1,024; at 64 the queue fills and a packet is refused (`test_link`). In 30 random runs each the data rate is the same at 8, 16 and 64 (0.6389, 0.6392, 0.6392), and the host never kept more than 16 | 8 or 16 both hold; not 64 |
| **2,048 cycles before asking again** | 30 runs each (`figures.sh c`). Packets sent to a receiver that already had them: 232 at 128 cycles, 104 at 512, none at 2,048 and 8,192. The longest a sender waited for an acknowledgement: 1,920, 1,895, 2,728, 6,754. At 32,768 a run failed the check (section 3) | 2,048 is the least of these at which nothing was sent twice for nothing |
| **528 zeros before hunting** | without the mask, 16 fails and 17 holds at the small sizes: 497 at the real ones. With the mask the searches hold with none (4 searches, 445,179 states) | 528 has room; kept |
| **the forwarding buffers' 16 KB** | the fullest a buffer of 1,024 words got: 621 (`figures.sh a`, 60 runs). With 512 words 361, with 256 words 228, no packet refused; the data rate 0.6389, 0.6368, 0.6372 (`figures.sh d`, 30 runs each) | the rule asks for 512 words; the runs do not press it |
| **the second wrong packet before the error** | one wrong packet is asked for again and the link carries on. The second comes 1,056 cycles after the first for full packets and costs a restart, 128 words | two |
| **the status timer, 512** | 60 runs each at 512, 2,048 and 8,192: the data rate 0.5751, 0.5756, 0.5773; packets of length 0, 7.64%, 5.95% and 5.76% of the data's bytes; the longest a sender waited for an acknowledgement on a link in step 2,727, 3,380 and 8,356 | 512: the wait grows with the timer again |
| **an acknowledgement as soon as the line is free** | 400 one-word packets from power-on: 17,489 cycles with it, 31,023 without. In 60 runs, packets of length 0 are 7.64% of the data's bytes with it and 4.03% without; the data rate 0.5751 and 0.5705 | kept |

## 7. The state diagrams

**The receiver** (`RxState`):

| state | what it does | leaves |
|---|---|---|
| HUNT | between packets: zeros are idle | on a byte that is not zero, to HEADER |
| HEADER | collects 16 bytes through the CRC unit | on the 16th: the header CRC fails, to FLUSH, and the link is "lost"; a length over 32, to FLUSH likewise; a field that cannot be, to SKIP; otherwise to DATA. With length 0 the packet is judged at once, and to HUNT |
| DATA | collects 16 × length bytes, the mask taken off, through the CRC unit | on the last byte the packet is judged, and to HUNT |
| SKIP | the same for a packet with a field that cannot be: checked, and not kept | on the last byte the packet is judged, and to HUNT |
| FLUSH | takes nothing | after 528 zeros in a row, to HUNT |

**Judging a packet** whose header CRC passed, in HL's order:

| | what | then |
|---|---|---|
| 1 | a restart packet | acted on only if its length is 32, its checksum passes and its data is the pattern; else thrown away as damaged |
| 2 | the data checksum fails | thrown away. Resend is owed if its number is the one expected, at once, or ahead, after the 2,048 cycles |
| 3 | a field that cannot be, both checks passing | the number expected: the "made wrong" flag, and resend; the flag already set: the error, and the link starts again. Another number: thrown away |
| 4 | the acknowledge field, the stop bits, "resend" and "lost" | taken, in any mode but while this end is starting again. "Resend" or "lost" with an acknowledge field behind the last one taken: the error, and the link starts again |
| 5 | the number, of a packet with data | the one expected and room: taken, the flag cleared. No room: thrown away, resend. Ahead: thrown away, resend. Behind: thrown away, acknowledged |
| 6 | the number, of a packet of length 0 | ahead of the one expected: resend. The one expected: nothing is owed any more |

**The sender** (`TxState`): IDLE, HEADER (16 bytes), DATA (16 × length, the mask put on), ZEROS (1,024). From IDLE
it starts, in this order:

| | when | what |
|---|---|---|
| 1 | the link is not in step | a round: 1,024 zeros, then the round's packet (below) |
| 2 | a packet with data is next and its pass is done | that packet: one to be sent again before a new one; of new ones the foreground's before the background's; a new one only if its stream is not stopped and fewer than 8 are kept |
| 3 | a stop bit has changed, a resend is owed and not yet asked, a packet was taken since the last header, or 512 cycles since the last packet | a packet of length 0 |

**The link's mode,** four flags at each end:

| flag | set by | cleared by | while set, the round's packet |
|---|---|---|---|
| lost | a header CRC that fails; a length over 32 | a good packet with "recovery"; a good restart packet | "lost" |
| answer | a good packet with "lost" | a good packet without "lost" | "recovery" (with "lost" too, if this end is lost) |
| restart | power-on; a packet made wrong twice; asked for a packet no longer kept | a restart packet with "recovery" | the restart packet with "lost" (and "recovery", if answering) |
| restart answer | a restart packet with "lost". If this end was not starting itself: it drops what it kept, numbers from 1, and raises the error | a restart packet without "lost"; any ordinary good packet | the restart packet with "recovery"; "lost" first, if this end is lost |

- With no flag set the link is in step, and only then does data go.

**A direction's registers:** as a receiver, the last number taken, "resend owed", "asked", the cycles since it
asked, the "made wrong" flag, "acknowledgement owed", the stop bits last sent. As a sender, the last
acknowledgement taken, the next number to send, the next new number, 8 kept packets, the other end's two stop
bits, and the pass's counter. The mask is one more 32-bit register each way.

## 8. What changed in the code since the report before

- **The sender's pass is 128 bits a cycle by default** (`Params::pass` is 16 bytes a cycle). The slow unit and no
  pass stay as sizes (`,pass=1`, `,pass=0`) for the comparison.
- **Every case found again** by the search at the fast unit; `cases/false-alarm.case` is new.
- **`linksearch --deep`, `--seconds`, and the states still waiting at a stop** are what finished the long search.
- `figures.sh c` no longer has 32,768; `searches.sh forged` has the line with two forged headers.
- Nothing else in the link end is changed. The mask, the rules and HL's examples are as in the report before.

## 9. Ready to be built on (2026-11-18)

| | where |
|---|---|
| **the interface:** one header, `hostlink.h`, with a link end (`End`) and a chain node (`Node`): a byte in and a byte out a cycle for each link, words pushed in and taken out for each stream, the stop and error outputs, the pin | `updater_wip/link/hostlink.h`; `make lib` builds `build/libhostlink.a` |
| **how to use it:** two examples that build and run, what a user must promise, what it may rely on | `updater_wip/link/README.md`, "Using the link end"; `examples/` |
| **its test,** through the header alone: two ends and chains of 1 to 4 under noise, one ASIC that never takes its background, an end powered on under a running link, two ends in two processes | `test_hostlink.cpp` |
| **the link end as hardware:** every register with its flip-flops, every memory with its size and the reason, the CRC units and when each runs, the budget | `HARDWARE.md` beside this file; `build/linkhw` prints its tables |

- **The budget for one ASIC in the middle of a chain:** 1,424 flip-flops and 77,312 bytes of memory for the link,
  110,080 bytes with the chip's two queues of 16 KB. 85% of the link's memory is the four forwarding buffers.
- **`make check` runs all of it:** the examples are built against the library alone and must be in the README
  word for word; `HARDWARE.md` must hold the program's tables word for word; the list of registers must have a
  row for each of the model's 47.
- Nothing in the link end itself is changed by this.

## 10. The runner

| task | what | state |
|---|---|---|
| Quest 231 | the long search, the slow unit | **done: complete, holds** (section 3) |
| Quest 232 | the random campaign, the slow unit (`72f12b9a`) | **done: 6,071 runs, none failed** (below). It had started before I parked it |
| Quest 233 to 236 | the other campaigns, the searches and long noise at the slow unit | parked in `tasks/hold`, since HL no longer has that unit |
| Quest 237 | **the random campaign:** 16 slices of seeds for an hour each, chains of 1, 2, 4 and 16, the fast unit (`d2b298f7`) | **done: 7,519 runs, none failed** (below) |
| Quest 238 | the same with forged headers (8 slices) and with flipped bits (8) | parked by the owner until the night of 2026-11-18. A seed may fail there as seed 16 did here: section 4 |
| Quest 239, 240 | the 17 larger searches, depth-first, 55 minutes each | parked likewise |
| Quest 241 | long noise, bursts up to 300 ms | parked likewise |
| Quest 242 | the long search once more, the fast unit | parked likewise |

**Quest 237, the random campaign with the fast unit** (`results/237-link-w8-campaign/campaign.txt` in the Quest
repository): 16 slices of seeds for an hour each, 2,000,000 cycles of traffic and noise a run, HL's rules. The host's
window is drawn for each run: 8 in three runs of five, 16 or 64 in the others.

| | |
|---|---|
| runs | 7,519, on chains of 1, 2, 4 and 16: 3,030, 2,264, 1,475 and 750. **None failed** |
| cycles; packets | 18,341,787,320; 385,299,289, of them 141,124,036 with data and 4,724,822 sent again |
| the noise | 16,339,577 bursts; 2,559,617 single bits flipped; 3,332,337 header CRCs failed; 1,150,633 data checksums failed |
| recoveries | 257,035: a link's last damaged byte to its next delivery 669 cycles on average, 5,187 at most |
| the packet number wrapped | 2,184,550 times on a new packet, 114,756 on a packet sent again |
| a sender went back | 1,387,089 times |
| the most kept | 8 packets at a node, 64 at the host; 953 words in a queue; 759 in a forwarding buffer, of 1,024 each |
| the longest wait for the line with nothing else to wait for | 573 cycles |
| the longest a sender waited for an acknowledgement, the link in step and its wires clean | 3,316 cycles |
| a foreground packet ready, not stopped, the link in step | 206 cycles on average; **384,187 at most** |
| never happened | a packet seen before; a packet with no room; a field that cannot be; an acknowledgement behind or beyond; a packet made wrong; a link started again but at power-on; asked for a packet no longer kept |

- **The 384,187 cycles are again a wire that read as zeros.** Seed 150255, a chain of 16: one wire was turned to
  zeros in bursts of up to 446,377 cycles. Run again here from the campaign's commit, the seed gives the same
  figure, all of it with the window full.
- **The fullest queue, 953 words of 1,024, is nearer the top than in the runs here** (629). No packet was refused.
  I have not traced which run it was; the campaign draws the host's window at 64 in one run of five, which HL
  rules out.

**Quest 232, the random campaign with the slow unit** (`results/232-link-w8-campaign/campaign.txt` in the Quest
repository): 16 slices of seeds for an hour each, 2,000,000 cycles of traffic and noise a run, HL's rules.

| | |
|---|---|
| runs | 6,071, on chains of 1, 2, 4 and 16: 2,457, 1,832, 1,158 and 624. **None failed** |
| cycles; packets | 15,493,691,454; 306,481,395, of them 105,527,464 with data and 3,145,772 sent again |
| the noise | 13,204,931 bursts; 2,168,526 single bits flipped; 2,807,798 header CRCs failed; 590,292 data checksums failed |
| recoveries | 195,516: a link's last damaged byte to its next delivery 688 cycles on average, 5,225 at most |
| the packet number wrapped | 1,495,705 times on a new packet, 77,294 on a packet sent again |
| a sender went back | 1,030,221 times |
| the most kept | 8 packets at a node, 64 at the host; 844 words in a queue; 749 in a forwarding buffer, of 1,024 each |
| the longest wait for the line with nothing else to wait for | 1,054 cycles (the slow unit) |
| the longest a sender waited for an acknowledgement, the link in step and its wires clean | 3,458 cycles |
| a foreground packet ready, not stopped, the link in step | 300 cycles on average; **515,932 at most** |
| never happened | a packet seen before; a packet with no room; a field that cannot be; an acknowledgement behind or beyond; a packet made wrong; a link started again but at power-on; asked for a packet no longer kept |

- **The 515,932 cycles are the wire, not the rules.** Seed 120095, a chain of 4: the wire from the first node to
  the host was turned to zeros in bursts of up to 373,729 cycles, 180 cycles apart on average. The host heard an
  idle line, stayed in step, and waited with its window full (501,922 of the cycles). Run again here from the
  campaign's commit, the seed gives the same figures. It is S12 of section 1.
- The events that never happened are the ones only a forged header or a sender's fault can make. Quest 238 runs
  them.
