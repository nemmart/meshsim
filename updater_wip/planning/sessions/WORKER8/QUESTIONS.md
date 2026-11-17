# WORKER8 questions: where HOST-LINK.md is silent or wrong

2026-11-17, second issue: on the header of 20 bytes (HL of that morning), after the searches were run again, the
chain was built, and the first random runs. The first issue, on the header of 16 bytes, is in git at `80c4d227`.

HL is `v1.4_wip/planning/HOST-LINK.md`. The code is `updater_wip/link/`. "The search" is `linksearch`: the link
end's own code at small sizes, every order of loss, damage, stall and delay within a budget, and in every state
the question "if nothing more goes wrong from here, does everything arrive?". "The runs" are `linkrun`: random
processes on chains of 1, 2, 4 and 16. `NOTES.md`, sections 16 and 17, have the tables.

**How to read an entry.** Each flaw has a case in `updater_wip/link/cases/`, found by the search, the shortest
there is: `build/linkcase -t cases/<name>.case` shows it line by line. It fails under the rules its first `run`
line names and holds under the fix, and `make check` plays every one both ways. Each rule is a switch in
`link_params.h` (`Rules`), on in `proposed` and off in `as_written`. A dagger (†) marks a number I chose.

## Where each stands

| | what | as HL has it | with the rule | the owner |
|---|---|---|---|---|
| G6 | a good packet with the wrong number | the link stops for ever after one lost packet | holds | accepted |
| G7 | the last packet of a burst lost | stops for ever | holds; no timer at the sender | accepted |
| G9 | both ends lost at once | stops for ever | holds | accepted |
| G10 | a lost receiver hunts in the data | data is taken for a packet; a packet is lost for ever | holds | accepted |
| G11 | the stop bit lost, the window over half a queue | a queue overruns | holds | accepted; and the queues are 16 KB |
| G4 | one end starts again from power-on | stops for ever | the link starts again and says so | accepted |
| G21 | a bit flipped in a kept packet | the same packet sent again for ever | the link starts again and says so | accepted |
| G20 | a false acknowledgement (a forged header) | a packet released that never arrived | the link tells, starts again and says so | accepted |
| **G22** | **a packet its own sender made wrong** | asked for again for ever | **new:** the link starts again and says so | to rule |
| F1 | a false packet number (a forged header) | a packet taken for another, silently | not fixed | a known limit (ruled) |
| ~~G13~~ | one stream's status behind the other's data | 102,447 cycles' stall on the header of 16 bytes | **gone with the header of 20 bytes,** with one timer and no rule | closed |
| ~~G8~~ | "lost" and "recovery" for each stream | | **gone:** one packet speaks for both | closed |

## What the header of 20 bytes changed

1. **The starvation is gone, with one timer a direction and no rule for it.**
   - On a whole wire at the real sizes (`test_link`): under 200 foreground packets coming back, the background
     stands still for 532 cycles at most, one packet, as HL is written. It was 102,447.
   - **By the search** (`searches.sh starve`): the other end sends 5 or 8 foreground packets back to back; in no
     state does a sender wait 150 cycles for an acknowledgement. As written, 4,066,995 states; with the proposed
     rules 1,922,830 and 2,525,845. The same search catches a header made not to speak for the other stream
     (`plants.sh`, `OTHER_STREAM_STALE`), so it would have seen the starvation.
2. **A search is about a tenth the size it was.** One timer, one packet a recovery round, and fewer packets of
   length 0. Two packets each way with two damaged, which did not finish in 30 million states, is now complete
   in 2,593,263.
3. **A sender goes back exactly as far as it must.** Every header carries both streams' acknowledgements, so a
   "resend" or a "lost" always has the latest. In 300 random runs, 5 million packets, **no receiver was ever sent
   a packet it had already taken.**
4. **"Resend" needed one more rule.** Every packet now shows both streams' next numbers, so a receiver that has
   asked sees "still ahead" in every packet the sender had on its way, and asking again each time made the sender
   go back again each time. **It asks again only after 2,048 cycles † (`reask`)** if the sender still shows
   itself ahead. The value is for speed only: the searches hold with 24 and with 400 at the small sizes.
5. **The cases are the same, and found again:** as HL is written the link still stops for ever after one lost
   packet (G6), and each of the others fails as before.
6. **A forged number one more is now harmless;** one less is not (F1).

## The rules, each with its case

The first eight are as in the first issue, in short. G22 is new.

- **G6** (`resend_ahead`; `g6-wrong-number.case`). HL's table has no row for a good packet with the wrong number. One
  ahead of the expected is thrown away and "resend" is owed; one behind is thrown away and acknowledged. Ahead and
  behind are within half the numbers, so a window stays under half the numbers.
- **G7** (`zero_number`; `g7-last-packet-gone.case`). A stream's number in a header that is not that stream's packet
  is the number its sender puts on the wire next. Ahead of the one expected: a packet was missed, and "resend"
  is owed. No timer at the sender: the searches hold with one (`resend_timer`), without, or with only it.
- **G9** (`lost_answers`; `g9-both-lost.case`). A lost end that hears "lost" answers it: both flags in one packet.
  Answering ends at the first good packet without "lost".
- **G10** (`flush`; `g10-data-taken-for-a-packet.case`). After a failed header CRC a receiver takes nothing until it
  has seen 532 zeros in a row †, which no packet can hold. Otherwise it reads the packet's data as a header.
- **G11** (`refuse`; `g11-overrun.case`). A good packet in order that does not fit is thrown away and not
  acknowledged. See "The host's window" below.
- **G4** (`restart`; `g4-power-on.case`). The restart flag (HL, bit 6 of byte 0) with "lost" reads "I have started
  again", with "recovery" "so have I". An end that hears the first drops what it kept, starts its numbers again
  and raises an error. A link carries nothing until both ends are up.
- **G21** (`check_kept`; `g21-kept-packet-bad.case`). A sender runs its CRC unit over the data it sends; a
  difference from the checksum it holds is its own memory's fault, and it starts the link again (G4), error up.
- **G20** (`tell_behind`; `g20-false-acknowledgement.case`). An acknowledge field never goes backwards and never
  passes what was sent. Two such in a row on a stream † and the end starts the link again, error up.

### G22. A packet its own sender made wrong (new; the owner asked for a rule)

- **HL:** a packet with a field that cannot be "is thrown away, just as one that failed a check: the receiver
  sets resend". If the sender made it so (hops 15 from a chain of 17, a length over 32, a stream of 3), the same
  packet comes back, for ever.
- **The case** (`g22-made-wrong.case`): a packet made with hops 15 arrives at an end that would forward it.
- **The rule** (`made_wrong`): **the receiver checks the data of such a packet all the same.** It does not take
  the data, but it runs it through the CRC unit, which needs no buffer.
  - **The data fails too:** it is noise that happened to pass the header CRC. Thrown away and asked for again, as
    HL has it.
  - **The data passes:** two checks agree that this is what the sender sent. Asking again cannot help. **The
    receiver starts the link again** (G4) with the error, which drops the packet at the sender.
- A length over 32 is passed over by the length it claims, at most 255 words, while its data is checked; if that
  fails it is a lost place, as G14 has it.
- **Else:** a count of how often one number has been asked for. Long noise reaches any count.

## Settled as I went, for the owner to confirm

### The host's window

**The host must keep to a window of no more than half a queue: 16 packets. I would give it 8, as the chip has.**

- **Why a limit:** with the stop bit and the acknowledgements both lost, a sender goes on to the end of its
  window. `test_link`, at the real sizes, nothing coming back and the queue not emptied:

  | the host's window | the queue's fullest, of 1,024 words | |
  |---|---|---|
  | 8 | 288 | |
  | 16 | 544 | |
  | 64 | 1,024 | the 33rd packet is thrown away (G11); without G11 the queue overruns |

- **What 8 costs: nothing measured.** 100 random runs each with the host at 8, 16 and 64: the last node's data
  a cycle is 0.5675, 0.5678 and 0.5678. With an acknowledgement for every packet the host seldom has more than
  two or three outstanding; the most it ever had, given 64, was 18.
- **G11 stays** as what catches a wrong size: in the random runs it never fired.
- The same binds a forwarding buffer and the window of the end that feeds it (G16).

### The status repeat: 512, 2,048 or 8,192 cycles

100 random runs each (`figures.sh a`), the same seeds:

| the timer | the last node's data a cycle | packets of length 0, of the data's bytes | the longest a sender waited for an acknowledgement on a link in step |
|---|---|---|---|
| 512 | 0.5412 | 9.5% | 2,422 cycles |
| 2,048 | 0.5396 | 7.1% | 5,240 |
| 8,192 | 0.5421 | 6.7% | 8,958 |

With every source always sending, on whole wires: 0.8400, 0.8397, 0.8397.

- **Throughput does not depend on it:** the status only goes when the line is idle.
- **The worst stall does:** a lost "stop lifted" or a lost last acknowledgement waits for the next status, so the
  stall is the timer and a packet or two.
- **I would keep 512.** It costs a twentieth of an idle line.

### An acknowledgement as soon as the line is free (ruled: yes)

**What it adds** (100 random runs with and without, the same seeds): packets of length 0 go from 5.5% of the
data's bytes to 9.5%; with every source always sending, from 3.1% to 7.1%. That is 20 bytes for a packet taken,
when nothing else is going back. **The data a cycle does not change** (0.5412 and 0.5402; 0.8400 and 0.8392):
they go only when the line is free. 400 one-word packets take 17,553 cycles with it and 29,703 without.

### The chain's rules (the plan's G5, G15, G16), as built and searched

- **G5:** a node acknowledges a packet when it has taken it into its own queue and its forwarding buffer.
- **G15: a node is told whether it has a neighbour down the chain,** by a pin or a register. The last has no
  lower end.
  - I first thought the restart handshake could tell ("a link with nobody on it never comes up"). **It cannot:**
    a lower link that comes up late, because of noise, would look like no link, and the node would take the
    host's packets without passing them on. With the pin, a node holds what it must forward until the link is up.
- **G16:** a forwarding buffer for each stream and direction, 16 KB † like a queue; a link's stop bit is up when
  the node's own queue or the forwarding buffer behind that link is half full. The fullest a forwarding buffer
  got in the runs is 677 words of 1,024; a queue, 626.
- **A node's own packet goes after any it is forwarding,** and a foreground packet waits for the line for one
  packet at most: 532 cycles was the longest in every run, checked as a property at every end.

## F1. The false packet number: a known limit (ruled)

- **What it is now** (`forged-number.case`, kept as a case that fails): a packet is lost; the next packet's
  number is forged to one less, which is the number expected. It is taken for the lost one. The sender then
  releases the lost one, and sends the taken one again as itself. The search finds it with one packet damaged
  and one header forged; with every other field forged the searches hold, and the runs hold (`linkrun --forge`
  without the two number forms: 100 runs, none failed; with them, 6 of 100).
- **Its rate, beside damaged data passing the 32-bit data checksum** (`rates.py`, exact counts):
  - **The header's CRC catches every pattern of up to 7 flipped bits.** Of 8 bits, 4,259 patterns pass; 219 of
    them leave the stream, the length and the data checksum alone, and 159 of those change the foreground
    number.
  - **The data's checksum catches every pattern of up to 5 bits** in a full packet; from 6 bits one in 2³²
    passes, and a full packet has 1.5 × 10⁹ patterns of 6 bits.

  | bit error rate inside a burst | damaged data passes, a full packet | a false number passes | times as likely |
  |---|---|---|---|
  | 10⁻⁵ | 1.5 × 10⁻²¹ | 1.6 × 10⁻³⁸ | 10¹⁷ |
  | 10⁻⁴ | 1.1 × 10⁻¹⁵ | 1.6 × 10⁻³⁰ | 10¹⁵ |
  | 10⁻³ | 5.4 × 10⁻¹¹ | 1.4 × 10⁻²² | 4 × 10¹¹ |
  | 10⁻² | 2.3 × 10⁻¹⁰ | 3.5 × 10⁻¹⁵ | 7 × 10⁴ |
  | 10⁻¹ | 2.3 × 10⁻¹⁰ | 3.7 × 10⁻¹³ | 600 |

  - And a false number does harm only if it is the very number the receiver expects, just after a loss.
  - **So damaged data passing its checksum is the likelier silent fault by 600 times at the least,** and by far
    more at any error rate a link would be run at. The owner's ruling stands on the numbers.

## Rules taken that the search has not shown to matter

The searches hold with each switch either way (`searches.sh rules`).

| | where HL is silent | what the code does |
|---|---|---|
| G2 | Open: does the recovery packet state its number | every header states both streams' next numbers; a receiver never takes its place from them. Closes the Open item |
| G3 | an acknowledgement during a recovery | every good packet's acknowledge fields release what they cover, in any mode; a "lost" packet's are also where the sender goes back to |
| G12 | the header of a packet sent again | written afresh, for the link as it stands. G20 rests on this |
| G14 | a length over 32 under a good header CRC | not "still in step": see G22 |
| G17 | the first packet after "stop" is lifted | whatever was next: a refused packet was asked for again already |
| G19 | a good packet while lost | its data is not taken; its acknowledge fields and stop bits are |
| | an answering end's data | none until the answer is heard: rounds of zeros and "recovery" only |
| | a packet of length 0's stream field | 1 |

## Still open

- **A neighbour that goes silent.** Nothing in HL notices a link whose other end has stopped: the sender waits
  for ever, which is right if the neighbour is slow and wrong if it is dead. A count of status periods with
  nothing heard would tell.
- **The background behind the foreground.** A background packet waits while any foreground packet is ready, a
  packet to be sent again included. That is HL's order, and a foreground that never pauses holds the background
  for as long. Seen in a random run, and not a flaw of the protocol.
