# WORKER8 report: HOST-LINK.md as it stands on 2026-11-17, with the data masked

**The protocol as it now stands holds in everything run so far.** That is every search that fits this machine, on
one link and on chains of 1, 2 and 3; 18 planted bugs; and 1,380 random runs on chains of 1, 2, 4 and 16, 300 of
them under HL's rules and 1,080 under other settings.
**It is not yet the whole brief:** the runner did not run any of my six tasks (section 10), so the large random
campaign and the searches too large for this machine are still to come.

Three things are the owner's to weigh, all in `QUESTIONS.md`:

1. **The mask has a limit that is nearer than HL says.** Data made for the header it will travel under still gets
   a packet through, and it is solved for, not searched (section 2). I have treated it as the known limit HL's own
   sentence describes, and carried on.
2. **The pass over the data costs more than HL says** where the two streams meet, and on every idle link
   (section 5).
3. **The mask closes the false packet number as well,** for any packet with data (section 2).

Names: HL is `v1.4_wip/planning/HOST-LINK.md`. The code is `updater_wip/link/`. A dagger (†) marks what I chose or
propose. A cycle is one byte on the link. Every figure here was measured on this branch on 2026-11-17 and
2026-11-18, on the code as committed, and the command that gives it is named.

## 1. The mask, defined

**Each data byte goes on the wire XORed with the low byte of a 32-bit register. The register starts at the
packet's header CRC with bit 16 set, and takes eight shifts of the CRC-32C register, with nothing fed in, before
each byte.**

```
reg = header CRC  OR  0x00010000          the header CRC: the 32-bit number in header bytes 12 to 15, byte 12 lowest
for each data byte d, in order:
    repeat 8 times:  reg = (reg >> 1) XOR 0x82F63B78   if reg's bit 0 is 1
                     reg =  reg >> 1                    otherwise
    the byte on the wire is  d XOR (reg AND 0xFF)
```

- **It is HL's proposal with one bit added.** The eight shifts are the CRC unit's step for a byte of zero, so the
  mask is a second CRC-32C unit that is fed zeros. The receiver does the same, from the header it has just read.
- † **Why bit 16.** The register has two states that stand still: 0, whose mask is all zeros, and 0xFCA42DAF,
  whose mask is the byte 0xAF over and over. A header CRC equal to either would send its data in the clear, or
  as good as. With one bit of the starting value set, and 0xFCA42DAF has that bit clear, no header CRC starts it
  there. From every other state the register returns only after 2³¹ − 1 shifts. It is one constant
  (`header.h`, `MASK_SET`); question 1 of `QUESTIONS.md`.
- **What is held, and what the data checksum is over, is the data without the mask.** A forwarding node takes the
  mask off as the data arrives and puts the outgoing header's on as it leaves.
- † The restart packet's data is masked like any other.

**Worked examples** (`test_header`; the bytes agree with a second implementation, in Python):

| | |
|---|---|
| **1.** HL's first example header: background, length 1, number 1, acknowledge 0 | `02 01 01 00 00 00 00 00 EA 9A 70 42 A0 8C 06 E1` |
| its header CRC, and the register's start | 0xE1068CA0, 0xE1078CA0 |
| the data as held | `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| the data on the wire | `2A 54 C0 74 C0 1C 58 2F A6 5D 27 37 39 C9 47 1F` |
| **2.** foreground, length 1, number 7, acknowledge 3 | `01 01 07 00 0C 00 00 00 EB 08 C9 D9 9E 94 2A 36` |
| its header CRC, and the register's start | 0x362A949E, 0x362B949E |
| the register after each of the first four bytes | 0x3EE668A4, 0x65EFC4D1, 0x21DD8D6C, 0x2CA4813F |
| the data as held | `00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F` |
| the data on the wire | `A4 D0 6E 3C 10 A4 2F 89 F5 74 21 9E DE 43 E4 C0` |
| **3.** the same data forwarded: hops 1, acknowledge 4 | `01 01 07 00 10 00 10 00 EB 08 C9 D9 A3 2B 76 11` |
| the data on the wire | `79 F1 F2 82 77 D9 B4 AF 91 8F 7C 00 39 96 A1 51` |

- In example 1 the data is zeros, so the wire shows the sequence itself.
- `test_link` checks the link end against this: every data byte of 12 packets on the wire, byte for byte.

## 2. What the mask closes, and what it does not

| the case | without the mask | with it |
|---|---|---|
| `packet-in-data`: a header reads as zeros, the data holds a packet | fails: word 1 delivered where word 0 was next | **holds** |
| `forged-number`: a header forged by noise with a false packet number | fails: a packet taken twice, the next lost | **holds** |
| `data-taken-for-a-packet`: a lost receiver hunting in data that is a header's image, with no wait for zeros | fails | **holds** |
| `data-made-on-purpose`: the data made for the header it travels under | fails | **fails** |

`build/linkcase -v cases/*.case` plays all four both ways: 27 runs over 13 cases, all as expected.

**What it closes besides the hole it was made for:**

- **The false packet number, for any packet with data.** A header that noise has forged carries another CRC, so
  the receiver takes the wrong mask off the data and the data checksum fails. The header's fields on a packet
  with data are now under both checks. HL's "A known limit, accepted" can go. What is left of forged headers is
  the packet of length 0: a false acknowledgement, false stop bits, a false "resend" or "lost".
- **The need for the 528 zeros.** With the mask the searches hold without that rule (`hl-flush`: 3 searches of
  the `rules` list and one of `nested`, 599,010 states). Without the mask it is needed, and by exactly one more zero than a packet's data can hold in
  front of a header's image: 16 fails and 17 holds at the small sizes, so 497 at the real ones. I would keep it:
  it spares a lost receiver a string of failed header CRCs, and it is the only guard left against data made on
  purpose.

**What it does not close: data made by one who knows the header.**

- HL: "whoever made the data would have to know the header's CRC, which covers the acknowledge field of that
  moment."
- **They need less.** The CRC is a function of the header's other fields and of the data. The CRC and the mask's
  sequence are both linear. So for a chosen stream, length, number, acknowledge field, hops and stop bits, **data
  can be solved for whose masked bytes are a whole packet:** 33 evaluations and 32 equations in 32 unknowns
  (`bench.cpp`, `made_on_purpose`).
- **Shown:** `cases/data-made-on-purpose.case`. One packet of two words made so, its header read as zeros, and
  the packet inside is taken: "a word delivered that was not sent". Two events, as before the mask.
- **How far those fields can be known.** On a quiet link, well: from the host hops is 0, the stop bits are 0, the
  acknowledge field stands still while nothing comes back, and the number counts from 1 after a restart. When
  foreground messages go between, the number and the acknowledge field move as the data's author cannot see.
- **It still needs the line to read as zeros through that one header.**
- **What would close it** is a value in the mask's start that the data's author cannot know: the value for each
  life of a link, which HL set aside. A sequence that is not linear would only turn the solving into a search of
  2³², which is minutes.

## 3. The two rulings of the afternoon, and the two seeds

- **"Stop" holds only packets not yet numbered.** `cases/stopped-blocks.case` fails under HL as first written and
  holds now. The searches with one stream never taken again are in section 4.
- **Asking again at any sign the sender is ahead.** `cases/seeds-227.case` is the cause of Quest 227's two failing
  seeds (200137, 140162): a "resend" was asked once and lost, and my rule then asked again only at a packet of
  length 0. Not the host's window alone: the window set how long the sender stood.

## 4. What holds

**The searches of the shrunken model: 71.8 million states in all, every search complete.** Each is every order of loss, damage, stall and delay within its budget,
and from every state the question "if nothing more goes wrong, does everything arrive?". `--hold` asks it also
with one stream never taken again. All on the code as committed, with the mask and 5 kinds of damage.

| list (`searches.sh`) | what | searches | states | |
|---|---|---|---|---|
| `main` | one link, the list of the headers before | 24 | 7,684,999 | all hold |
| `held` | a stopped stream cannot block the other: each stream never taken again in turn, queues of 4 words | 7 | 9,681,796 | all hold; one more is HL as first written, and fails |
| `chain` | the host and 1, 2 or 3 nodes | 13 | 7,271,470 | all hold |
| `forged` | one forged header and one damaged packet, field by field | 23 | 9,105,569 | all hold, the false packet number among them |
| `nested` | data that holds headers or packets, and a header that reads as zeros | 11 | 2,255,201 | 8 hold; 2 are without the mask and fail; 1 is the mask's limit and fails |
| `rules` | without "lost uses the acknowledge field", without "acknowledge at once", without the wait for 528 zeros, asking again after 24 and 400, the pass at 0 and 4 bytes a cycle | 21 | 2,059,605 | all hold |
| `window` | no rule to refuse a packet with no room | 4 | 2,448,190 | all hold |
| `starve` | the other side sends 5 or 8 packets back to back; no sender waits 150 cycles for an acknowledgement | 3 | 31,295,019 | all hold; the largest, 18,350,231 states, run depth-first to fit this machine |

**The state counts against the header of 20 bytes,** on the lines of `main` that can be compared:

| the search | 20 bytes, numbers for each stream | now |
|---|---|---|
| `--packets 2 0 2 0 --damage 1` | 414,807 | 995,260 |
| `--packets 1 0 1 0 --damage 2` | 85,494 | 118,412 |
| `--packets 2 2 0 0 --damage 1` | 141,320 | 467,453 |
| `--packets 1 1 1 0 --damage 1` | 144,825 | 296,820 |
| `--packets 7 0 0 0 --damage 1` | 342,638 | 759,530 |
| `--packets 2 0 1 0 --damage 1 --noise 1` | 598,558 | 975,013 |
| `--packets 2 0 2 0 --damage 0 --power 1` | 194,272 | 537,172 |
| `--packets 2 0 1 0 --damage 1 --flip 1` | 327,136 | 559,464 |
| `--packets 2 0 1 0 --damage 1 --wrong 1` | 41,187 | 120,530 |
| `--packets 1 0 1 0 --damage 1`, states kept whole, and by the hash | 22,343 and 14,434 | 36,931 and 20,402 |
| the 24 together | 4,110,540 | 7,684,999 |

- **Why more:** there are 5 kinds of damage where there were 4 (a header that reads as zeros is the fifth); the
  sender's pass over the data is a counter in the state; the restart packet is a full packet.
- **Why not more still:** one machine a direction, where there were two.

**What the searches show of the changed rules:**

- **A stopped stream cannot block the other** (change 1). In every state of the `held` list, with either stream
  never taken again, the other finishes.
- **A false acknowledgement** (change 5). With a forged acknowledge field and a damaged packet: where a packet
  was lost the error is always raised; in no state was the error raised and the packet then arrived after all.
  With the mask a false acknowledgement can only come on a packet of length 0.
- **A packet made wrong** (change 4). One forged header that makes a field wrong, or a length of 33: asked for
  again, and no error. A packet its sender made wrong comes a second time and ends as the error and a link
  that starts again.
- **A bit flipped in a sender's memory** (change 3) is delivered unseen and counted apart; the link does not hang.
- **A restart packet whose data is damaged, or a restart flag forged onto another packet,** changes nothing.

**18 planted bugs, every one caught by the search** (`plants.sh`). The five new ones: the "made wrong" flag
cleared by a packet thrown away; an error on an acknowledgement behind with nothing asked; a restart acted on
with its pattern wrong; a checksum not made afresh on a packet sent again; the mask of a packet's first sending
kept for its second. Random runs alone catch 10 of the 18 within 10 seeds (`plants_run.sh 10`); the other 8 need the search.

**A kept case for every rule:** 13 cases, the table in `QUESTIONS.md`.

**The random runs.** On this machine, since the runner's campaign did not run (section 10):

- **240 runs under HL's rules, none failing** (`build/linkrun --seeds 1000 1239 --cycles 300000`): chains of 1, 2, 4
  and 16 (95, 72, 44 and 29 runs), 190 million cycles, 4.8 million packets, 105,171 bursts of noise.
- **1,140 more runs under 28 settings, none failing** (`figures.sh a` to `e`, and two with the pass at 16 bytes a
  cycle): the same 60 or 30 seeds under each setting, 60 of them under HL's rules.

| in the 240 runs | times |
|---|---|
| a header CRC failed | 21,443 |
| a data checksum failed | 4,978 |
| "lost" sent; both ends lost at once | 35,220; 5,933 |
| a packet ahead of the one expected, or a number ahead | 3,955 |
| a sender went back; packets sent again | 8,296; 23,411 |
| the number wrapped on a new packet; on a packet sent again | 16,645; 699 |
| a packet seen before; a packet with no room; a link started again | never |

| with forged headers among the damage (`figures.sh e`, 30 runs, none failing) | times |
|---|---|
| a packet made wrong, the first time: asked for again | 59 |
| a packet made wrong a second time: the error | 20 |
| asked for a packet no longer kept: the error | 1 |
| an acknowledgement behind the last one taken; beyond what was sent | 8; 19 |
| a link started again | 42 |

- The forging there is far more frequent than noise could make it; the counts say the paths were walked, not how
  often they would be. A second wrong packet came 20 times for 59 firsts because a packet of length 0 counts
  (question 5 of `QUESTIONS.md`) and an idle link takes nothing that would clear the flag.
- **With bits flipped in senders' memory** (30 runs, none failing): 47 bits flipped in kept packets, 4 wrong words
  delivered unseen, no error and no link standing still.

**Two mistakes of my own, found and fixed.** On the second over-long packet running the receiver was left in the
middle of a header and wrote past its buffer: the sanitizer build found it. And the bench took a second life's
packet number for the first life's when it checked "an error with nothing lost".

## 5. What things cost

**One run of numbers: a damaged background packet and the foreground behind it** (`build/linkcost setback`). A
background flood at the real sizes; one full foreground packet let out at 101 moments around the damaged packet;
cycles from let out to delivered, over the same moment with nothing damaged:

| the background packet's damage | at most | on average | packets sent again |
|---|---|---|---|
| a bit of its data | 1,071 more | 553 more | 2 |
| a bit of its header | 2,624 more | 1,677 more | 2 |
| its first byte zero | 3,152 more | 1,709 more | 3 |
| the whole packet gone | 1,598 more | 993 more | 3 |

- With numbers for each stream the first row would be 0. The other three lose the receiver's place or a whole
  packet, and cost the link a recovery whatever the numbering.

**The shared window** (`build/linkcost window`). A background flood, a full foreground packet let out now and
then, by the wire's delay each way. The background's bytes of data a cycle, and the most packets kept:

| delay | window 2 | window 4 | window 8 | window 16 |
|---|---|---|---|---|
| 1 | 0.6806, kept 2 | 0.6824, kept 2 | 0.6824, kept 2 | 0.6824, kept 2 |
| 128 | 0.5464, kept 2 | 0.6921, kept 2 | 0.6921, kept 2 | 0.6921, kept 2 |
| 512 | 0.3178, kept 2 | 0.7336, kept 3 | 0.7336, kept 3 | 0.7336, kept 3 |
| 2,048 | 0.1024, kept 2 | 0.2931, kept 4 | 0.6709, kept 8 | 0.8095, kept 9 |

- **8 is still the right window.** Up to a delay of 512 cycles each way it is never full, and a foreground
  packet never waits for it; with full packets flooding the other way as well, 5 are kept at most at 512.
- At a delay of 2,048 the window of 8 fills: a foreground packet then waited up to 1,404 cycles, 893 of them for
  the window (1,689 and 1,178 with full packets flooding the other way).
- **In the random runs,** where acknowledgements are lost to noise: the last node's bytes of data a cycle were 0.4750 with a window of 2, 0.5256
  with 4, 0.5342 with 8 and 0.5319 with 16 (`figures.sh d`, 30 runs each). In the 240 runs a foreground packet
  stood ready 292 cycles on average; the longest it stood with the window full was 4,514 cycles, and behind
  packets being sent again 11,873.

**The pass over the data** (`build/linkcost pass`): a byte a cycle before the header leaves, against no pass.

| | no pass | with the pass |
|---|---|---|
| a one-word foreground packet on an idle link, let out to delivered | 33 on average, 48 at most | 49, 64 |
| a full foreground packet on an idle link | 529, 544 | 1,041, 1,054 |
| a full foreground packet beside a background flood, let out to its first byte | 272, 525 | 512, 525 |
| the background's bytes a cycle in that flood | 0.7548 | 0.6824 |
| 400 one-word packets back to back, link up to all acknowledged | 12,832 | 12,833 |
| 400 full packets back to back | 211,247 | 211,744 |
| the random runs: the last node's bytes of data a cycle (`figures.sh a`, 60 runs) | 0.5703 | 0.4904 |
| the random runs: the longest a foreground packet waited for the line with nothing else to wait for | 528 | 1,021 |
| the same runs with every source saturated and the wires quiet: the last node's bytes a cycle | 0.8584 | 0.7138 |

- **On a busy link of one stream the pass costs one packet's pass at the start and nothing after,** as HL says.
- **On an idle link it doubles a full packet's time,** and a chain pays it at every hop.
- **Where the streams meet it costs both.** † As I built it, a foreground packet that arrives takes the sending
  unit at once, the background packet whose pass was under way starts over, and the line stands idle until the
  foreground's pass is done. The flood lost a tenth of its rate with a foreground packet every 2,600 cycles or
  so. The other way to build it sends the ready background packet first, and the foreground waits up to two
  packets. Question 3 of `QUESTIONS.md`.
- **In the random runs the pass cost 14% of the data rate, and 17% with every source saturated:** on links that
  are idle between packets, and wherever a node's next packet changes under the pass, from the background to the
  foreground or from its own to one it forwards.
- **A unit of 128 bits a cycle gives it back** (`--rules hl,pass=16`, the same 60 runs): 0.5751 bytes a cycle,
  0.8580 saturated, and a foreground packet waits 534 cycles at most for the line.

**A packet made wrong, twice** (`build/linkcost wrong`; full packets back to back, the 10th made wrong): it comes
again 1,071 cycles later and the error is raised; the next word is delivered 4,192 cycles after that; 128 words
were lost with the restart.

**Coming up** (`build/linkcost start`): both ends from power-on, 4,657 cycles to a link in step. One end powering
on under a running link: the other raises its error 1,554 cycles later, and the link is up again after 3,648.

## 6. Figures for the numbers HL marks †

| the number | what was measured | |
|---|---|---|
| **the host's window of 8** | with nothing coming back, the host stops by itself at 8 or 16 packets with the queue at 256 or 512 words of 1,024; at 64 the queue fills and a packet is refused (`test_link`). In 30 random runs each the data rate is the same at 8, 16 and 64 (0.5342, 0.5343, 0.5343), and the host never kept more than 14 (`figures.sh b`) | 8 or 16 both hold; not 64 |
| **2,048 cycles before asking again** | 30 runs each (`figures.sh c`). Packets sent to a receiver that already had them: 54 at 128 cycles, 18 at 512, none at 2,048, 8,192 and 32,768. The longest a sender waited for an acknowledgement: 1,884, 1,915, 2,548, 3,197, 5,798. The data rate does not move (0.532 to 0.534) | 2,048 is the least of these at which nothing was sent twice for nothing; longer only lengthens the wait |
| **528 zeros before hunting** | without the mask, 16 fails and 17 holds at the small sizes: one more than a packet's data can hold in front of a header's image, 497 at the real sizes. With the mask the searches hold with none | 528 has room; keep it |
| **the forwarding buffers' 16 KB** | the fullest a buffer of 1,024 words got in the 240 runs: 632. With buffers of 512 words 349, of 256 words 228; no packet was refused, and the data rate was 0.5342, 0.5316 and 0.5327 (`figures.sh d`, 30 runs each) | the rule asks for twice the window, 512 words, and the runs do not press it; 16 KB is twice that |
| **the second wrong packet before the error** | one wrong packet is asked for again and the link carries on (forged headers, searched). The second time running is 1,071 cycles after the first for full packets, and costs a restart: 128 words lost | two is right: one would make every forged header on a packet of length 0 a restart |
| **the status timer, 512** | 60 runs each at 512, 2,048 and 8,192 (`figures.sh a`): the data rate 0.4904, 0.4908, 0.4937; the bytes in packets of length 0, 8.79%, 6.42% and 6.20% of the data's; the longest a sender waited for an acknowledgement on a link in step 2,548, 2,128 and 1,822. The wait no longer grows with the timer: the acknowledgement at once, and asking again, set it now | 512 is kept |
| **an acknowledgement as soon as the line is free** | 400 one-word packets from power-on to all acknowledged: 17,489 cycles with it, 31,023 without (`test_link`). In 60 runs it puts 8.79% of the data's bytes into packets of length 0, against 4.64% without, on lines that were idle; the data rate is 0.4904 against 0.4929 | kept |

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

## 8. What I chose where HL is silent (†)

| where | what I built |
|---|---|
| the mask | bit 16 of the starting value set; the restart packet's data masked too |
| the restart packet | stream field 1, number field 1, hops 0; its acknowledge field and stop bits not used by the receiver; sent in rounds after 1,024 zeros |
| both ends starting at once | restart with lost and recovery both set. The handshake ends at a restart packet without lost, or at any ordinary packet |
| an end that is starting again | takes nothing from ordinary packets until it has its answer |
| a restart packet whose pattern or checksum is wrong | a damaged packet like any other |
| "asks for a packet no longer kept" | resend, or "lost", with an acknowledge field behind the last one the sender took |
| a length over 32 | counted as "made wrong" only if its number field is the one expected, and not while the receiver is already lost |
| a packet made wrong whose number is not the one expected | thrown away as ahead or behind, and not counted |
| a packet of length 0 with a field that cannot be | counted, if its number field is the one expected (question 5) |
| asking again | a thrown-away packet with the number expected is asked for at once, every time; the 2,048 cycles hold back only the asking that a packet ahead sets off |
| hops 15 | tested in both directions at a node that would forward (question 4) |
| the pass over the data | one unit a direction. It starts on the packet that will go next, and starts over if that changes |

## 9. HL read against the code: what is wrong, unclear or silent

| where in HL | what |
|---|---|
| "Where this document stands" | the figures are of the headers before: 36 million states, 14 planted bugs, 300 runs. Section 4 has today's |
| "Two streams"; "The chain", "Measured" | "the longest a foreground packet waits is one background packet", and "never waited more than one packet's time for the line": not with the pass a byte a cycle. Up to 1,038 cycles in the 240 runs; 528 without the pass |
| "A packet", the table | "data checksum: over the data": over the data as held, before the mask |
| "One run of numbers", "What it costs" | "with 8 background packets not yet acknowledged, a foreground packet waits ... about a packet's time": it does not happen below a wire delay of some 2,000 cycles each way; section 5 has what it costs there |
| "One run of numbers", "Shown so far" | 604,477 states and a search cut at 2 million: section 4 |
| "One run of numbers", step 3 | "sent again at once": once it is asked for, and after its pass |
| "The chain": "From the host ... no value of it is an error" | against "a packet that arrives with hops 15 at an ASIC that would forward it is a bad packet": the forwarding node adds one |
| "The chain", "An ASIC that forwards a packet" | the five steps lack the mask: off as the packet is checked, on again from the new header's CRC |
| "The chain": "A hop holds a packet for its length" | and then, on an idle line, for its pass: twice its length |
| "The chain", "Measured" | 677 words: 632 of 1,024 in the 240 runs |
| "The two checks": "Together they are a 64-bit check" | true of the header's fields too, now, on a packet with data: the mask ties the data to the header CRC. A packet of length 0 has the header CRC alone |
| "The two checks": the data checksum "in the order they go on the link" | of the bytes as held; on the link they are masked |
| "The data is masked on the wire" | the sequence: section 1. "Would have to know the header's CRC": the header's other fields are enough, and the data is solved for; section 2 |
| "The CRC as hardware", "Four units an ASIC" | and four mask registers, one beside each |
| "The order a receiver tests in", step 2 | "the hops field is not tested": "When a packet is bad" tests hops 15. And a field's verdict is used only after step 3: "made wrong" needs the data checksum to have passed |
| "When a packet is bad", the table | silent on a data checksum that fails on a packet whose number is behind: nothing is owed. And on which packets are asked for at once (section 8) |
| "When a packet is bad": "in 5 million packets ... no receiver was sent a packet it had already taken" | none in the 240 runs' 2 million packets either. With asking again after 128 or 512 cycles, 54 and 18 in 30 runs |
| "A packet made wrong" | "it could be noise: a damaged header that happened to pass its CRC": with the mask, not on a packet with data. Silent on a wrong packet with another number, on a length over 32 with another number, and on a packet of length 0 |
| "Finding a packet's start" | "without this it read the bad packet's data as headers": with the mask that needs data made on purpose |
| "Recovery" | silent on what the "lost" packet is besides its flag: length 0, the next number in its number field, no "resend" |
| "Starting a link again", the flags' table | lacks restart with lost and recovery together, and how the handshake ends. Silent on the rest of section 8's restart rows |
| "Starting a link again", "A known limit, accepted" | the false packet number: closed by the mask for a packet with data. The two rates under it are of the header before |
| "Holding the host back", the timer's table | 2,548, 2,128 and 1,822 cycles now, and the wait no longer grows with the timer (section 6) |
| "Holding the host back": "29,703 cycles without it and 17,553 with" | 31,023 and 17,489, from power-on |
| "Holding the host back": "the fullest a queue ... got was 626" | 630 in the 240 runs; none refused |
| "What a sender must keep": "the searches hold with a timer, without one, or with only one" | the code has no timer that sends again unasked any more; the searches are without one |
| "Open", "the numbers marked †" | section 6 |

## 10. Not done, and why

| piece | state |
|---|---|
| **the search cut short** (`--packets 2 4 1 0 --queue 4 --damage 1 --hold 3`) | Quest 230: 71,162,948 states, no failure, stopped by its time. Queued again as Quest 231, to say how many states wait and then to go depth-first for 10,500 seconds. **It did not run:** below |
| **the random campaign, the forged and flipped campaigns, long noise** | Quest 232, 233, 236: did not run |
| **the searches too large for this machine** (`searches.sh big2` to `big18`) | Quest 234, 235: did not run |
| `rates.py` | still of the header before; the false packet number it rates is closed |

**Why the six tasks did not run.** The runner failed each three times before starting it:
`timeout: invalid time interval '3h30'`. Its new time limit is spelled as `timeout` does not take; it wants
`210m` or `12600`. It is set on the runner's box, not in the repository. The tasks are as queued
(`tasks/231` to `236`, at `72f12b9a`); with the limit mended, deleting `results/231-*` to `results/236-*` runs
them again (`PIPELINE.md`).

**If the long search still does not finish,** what splitting takes:

- **Depth-first is the first lever, and it is built** (`linksearch --deep`): the same states in another order,
  with a hundred or so waiting where breadth-first has millions. It needs about 100 bytes a state where
  breadth-first needed 480, so the runner's 62 GB hold some 350 million states, and time becomes the limit:
  about 30,000 states a second, so 10,500 seconds is some 320 million.
- **Then by the kind of damage.** The search has one damaged packet, of 5 kinds. `--fates gone`, `--fates first0`,
  `--fates header`, `--fates data` and `--fates head0` are five searches that together cover the same failures.
  Each repeats the states before any damage, and is otherwise a fifth of the whole. No new code: five lines of
  `searches.sh`, run at once on the runner's 16 cores.
- **Then by which packet is damaged,** which would need an option that is not built.

`make check` passes: the header with the mask's examples, the link's quick tests, 13 cases, four small searches,
40 random runs, 18 planted bugs, under gcc with the sanitizers and under clang.
