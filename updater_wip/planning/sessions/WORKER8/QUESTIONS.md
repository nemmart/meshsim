# WORKER8 questions: where HOST-LINK.md is silent or wrong

2026-11-17, after the search on one link. The chain is not built yet: G5, G15 and G16 are still only the plan's.

HL is `v1.4_wip/planning/HOST-LINK.md`. The code is `updater_wip/link/`. "The search" is `linksearch`: the link end's
own code at small sizes (a packet of one word, numbers 1 to 6, a window of 2), every order of loss, damage, stall
and delay within a budget, and in every state reached the question "if nothing more goes wrong from here, does
everything arrive?" (`NOTES.md`, section 10).

**How to read an entry.** Each flaw has a case in `updater_wip/link/cases/`, found by the search, the shortest
there is. `build/linkcase -t cases/<name>.case` shows it line by line. The case fails under the rules its first
`run` line names and holds under the fix: `make check` plays every one both ways. Each rule is a switch in
`link_params.h` (`Rules`), on in `proposed` and off in `as_written`. A dagger (†) marks a number I chose.

**`as_written`** is the least that makes HL run. Where HL gives no rule, nothing is done: a good packet with the
wrong number is thrown away and that is all.

## In short

| | what | as written | with the rule |
|---|---|---|---|
| G6 | a good packet with the wrong number | **the link stops for ever after one lost packet** | holds |
| G7 | the last packet of a burst lost | **stops for ever** | holds; no timer is needed |
| G9 | both ends lost at once | **stops for ever** | holds |
| G10 | a lost receiver hunts in the data | **data is taken for a packet; a packet is lost for ever** | holds |
| G11 | the stop bit lost, the window over half a queue | **a queue overruns** | holds |
| G13 | one stream's status behind the other's data | **the background stands still while replies flow: 102,447 cycles measured, on a whole wire** | 544 cycles |
| G4 | one end starts again from power-on | **stops for ever** | the link starts again and says so |
| G21 | a bit flipped in a kept packet | **the same packet sent again for ever** | the link starts again and says so |
| G20 | a false acknowledgement (a forged header) | **a packet released that never arrived: lost, and the stream stops** | the link tells, starts again and says so |
| F1 | a false packet number (a forged header) | **a packet delivered twice and the next one lost, silently** | **not fixed:** a change to the header is proposed |

Everything else in this file is a rule I had to take to build the link, which the search has not shown to matter.

## The flaws, each with its case

### G6. A good packet with the wrong number

- **HL:** "The packet number is checked as well." "When a packet is bad" has no row for a packet that passes both
  checks and carries a number that is not the next.
- **The case** (`g6-wrong-number.case`): end 0 sends packets 1 and 2; packet 1 is turned to zeros on the wire. End 1
  sees idle, then a good packet 2 where it expects 1. Nothing in HL makes it say anything. End 0 keeps both for ever.
- **The rule** (`resend_ahead`): a good packet **ahead** of the one expected is thrown away and "resend" is owed.
  One **behind** (seen before, after the sender went back) is thrown away and acknowledged as ever.
  - "Resend" is asked once, not at every such packet: every packet behind the lost one is ahead, and each ask
    makes the sender go back again.
- **Else:** nothing else was found that does it without a timer at the sender (G7).
- **In HL:** a third row in "When a packet is bad"; and "ahead" and "behind" defined round the wrap: within half
  the numbers. So the window must stay under half the numbers, the host's too.

### G7. The last packet lost: the Open item on timers

- **HL** (Open): "The timers: how soon an unacknowledged packet is sent again without a 'resend'."
- **The case** (`g7-last-packet-gone.case`): end 0 sends 1 and 2; packet 2 is turned to zeros. End 1 has seen
  nothing wrong. End 0 waits for an acknowledgement that never comes.
- **The rule** (`zero_number`): no timer. A packet of length 0 already holds "the number the stream's next data
  packet will carry". Read that as **the number the sender puts on the wire next**, and a receiver that sees it
  ahead of the one it expects knows a packet was missed, and asks. The sender's status comes at least every 512
  idle cycles, so a loss is always found.
  - It asks again each time a status shows the sender still ahead: that is how a lost "resend" is repeated.
  - The field is then never an error. v1.37's black box and gold's reader treat a mismatch as fatal
    (`sequence_error`); with a way back it is information.
- **Else:** a timer at the sender (`resend_timer`, G1 of the plan). The case holds with it alone, and the searches
  hold with either or both (`searches.sh rules`). The timer needs a value that is right for every delay; the field
  needs none. I took the field and left the timer off.
- **In HL:** the Open item closed; "The receiver checks the field and does not advance" made exact.

### G9. Both ends lost at once

- **HL:** the side that has lost its place sends zeros and "lost" "until a good recovery packet arrives". The other
  side answers "lost" with "recovery".
- **The case** (`g9-both-lost.case`): the first packet each way has its first byte turned to zero. Both ends fail a
  header CRC. Each sends "lost" and waits for "recovery"; each, being lost, sends only "lost".
- **The rule** (`lost_answers`): a lost end that hears a good "lost" answers it: both flags in one packet. Answering
  ends at the first good packet without "lost". "An ordinary acknowledgement" is then any good packet without
  "lost", a "recovery" packet included: otherwise two ends that have each cured the other answer each other for
  ever.
- **In HL:** "Recovery" has only the case of one side lost. Noise on both directions is the usual case.

### G10. A lost receiver must not hunt in the data

- **HL:** "the first byte that is not zero starts a header". It does not say what a receiver does between failing
  a header CRC and the recovery's zeros.
- **The case** (`g10-data-taken-for-a-packet.case`): one bit of a header is flipped. The receiver fails the CRC and,
  as written, takes the next byte that is not zero for a header: **the first byte of that packet's data.** The data
  here is the image of a good header of length 0 with "recovery" and an acknowledge field. It passes every check.
  The receiver believes it is in step again, and releases a packet of its own that the other end never got.
  - Data that looks like packets is not noise: it is a file of packets sent as data, a test vector, a memory image.
    Nothing in the link escapes it.
- **The rule** (`flush`): after a failed header CRC the receiver takes nothing until it has seen **528 zeros in a
  row** †, more than any packet can hold (a header's first byte is never zero, and a packet is 528 bytes). The next
  byte that is not zero is then a true start, and data is never read as a header.
  - **A lost end then hears nothing until the other end sends its recovery's zeros:** an idle line carries a
    status every 512 cycles, so it never holds 528. That is how HL's recovery works anyway.
- **Else:** escaping the data, which costs bytes on every packet.
- **In HL:** "Why the zeros work" says what the zeros are for; the rule for the receiver is missing.

### G11. The stop bit alone does not prevent an overrun

- **HL:** "The queue behind a stream keeps room when it says stop." "The host has the memory" to keep every packet.
- **The case** (`g11-overrun.case`): a window of 4, a queue with room for 2 behind "stop". The packet that says
  "stop" is turned to zeros. The sender has an earlier acknowledgement and goes on to the end of its window. The
  queue overruns.
- **What holds as written:** a header's stop bit and acknowledge field are written together, so a sender that has
  heard no "stop" can be at most its window past an acknowledgement taken with the queue under half. **With the
  window no more than half the queue there is no overrun, whatever is lost.** The chip's sizes are exactly at that
  limit: 8 packets of 32 words are the 256 words above half of 512. The host, which keeps all, is beyond it.
  - Searched without the rule at those proportions (a window of 2, a queue of 4): no overrun in 9.8 million states
    (`searches.sh window`).
- **The rule** (`refuse`): a good packet in order that does not fit is thrown away and not acknowledged, and
  "resend" is owed. The stop bit keeps that rare.
- **Else:** a window for the host of no more than half its neighbour's queue, in packets. I would have both.
- **In HL:** "What a sender must keep" should bound the host's window; and say what a full receiver does.

### G13. One stream's status waits behind the other stream's data

- **HL:** the status goes "on a change, right then, as a packet of length 0 if there is nothing else to send", and
  when a timer that "any packet starts again" reaches 512.
- **The case** is a measurement on a whole wire at the real sizes (`test_link`): end 1 sends 200 foreground
  packets, as a chain's replies would; end 0 sends background bulk. A foreground header carries only the
  foreground's acknowledge field and stop bit, and the line is never idle. **The background's acknowledgements do
  not go out: end 0 fills its window of 8 and stands still for 102,447 cycles,** until the replies end. A
  background stop bit waits the same way.
- **The rule** (`status_each`): a timer for each stream, from that stream's own last packet; and a status that is
  due goes before the other stream's data. The background then stands still for 544 cycles at most.
- **In HL:** "When the status is sent", points 1 to 3.

### G4. One end starts again from power-on: the restart handshake

- **HL** and `INGRESS-INTERFACE.md`: the link is cleared "by power-on only". Nothing says what happens when one end
  is and the other is not: an FPGA reloaded, one ASIC's supply bumped.
- **The case** (`g4-power-on.case`): end 1 starts again in the middle. It expects packet 1 and sends packet 1; end 0
  is at other numbers. Every packet is behind or ahead for ever.
- **The rule** (`restart`; the owner asked for a handshake and offered the reserved byte): **bit 0 of the reserved
  byte is "restart".** With it, "lost" reads "I have started again" and "recovery" reads "so have I, in answer".
  Restart packets are of length 0 and go in rounds like a recovery's, after zeros.
  1. **An end that starts** sends restart with "lost" until it gets a restart with "recovery". Until then it takes
     nothing else: what it hears is from the other end's life before.
  2. **An end that hears restart with "lost"** drops what it kept, sets its numbers back to 1, **raises an error,**
     and answers restart with "recovery" until it hears a packet without restart-and-"lost".
  3. Two ends that start together each hear the other's and answer: both flags, as in G9. No error.
  - So **a link carries nothing until both ends are up,** and a link with nobody on it never comes up: the last
    ASIC of a chain needs no pin to know it is last (G15).
  - What was kept and not acknowledged is lost. The error is how that is known.
- **In HL:** a section of its own; "reserved" becomes 7 bits.

### G21. A packet that is bad where it is kept is sent again for ever

- **HL:** a forwarded packet's data checksum is kept, so that "a bit that flipped in a buffer" is caught at the next
  hop. What is caught is asked for again, **and the same bits come back.** The owner: this must end as a reported
  error and never hang.
- **The case** (`g21-kept-packet-bad.case`): one bit of a kept packet flips. The receiver asks, the sender sends,
  for ever.
- **The rule** (`check_kept`): **a sender runs its CRC unit over the data it sends and compares with the checksum
  it holds.** A difference is its own memory's fault: it starts the link again by G4's handshake, which drops the
  packet and raises the error at both ends.
  - It costs the sending unit the data's bytes, where HL counts 12 for a forwarded packet.
- **Not covered:** a field that cannot be which the sender made itself (hops 15 on a chain of 17, a length over
  32). The receiver throws it away and asks, for ever. A limit on asking for one number would end it, and would
  also fire in long noise. I have no rule for it.
- **Not covered, and outside the link:** a bit flipped after the last check, in a queue. Nothing sees it.

### G20 and F1. What one false header costs (owner: the header's fields are under the header CRC alone)

A wire makes a header that passes its CRC with other fields once in 2³². The search can make one at will
(`--forge`): one field changed, the header sealed again, and one more packet damaged in the ordinary way
(`searches.sh forged`).

| the field forged | what comes of it | does the link tell? |
|---|---|---|
| acknowledge, one more | with a packet lost in the same moment: **that packet is released and never arrives** | **yes, with G20:** it starts again and raises the error |
| packet number, one more, on a packet sent again | **the packet is taken as the next one: delivered twice, and the true next one is thrown away as seen before** | **no.** Sometimes afterwards, when an acknowledgement passes what was sent |
| stop | at most a status period lost, or a packet refused (G11) | nothing to tell |
| "lost", "recovery", "resend" | a recovery or a going back that was not needed | nothing to tell |
| acknowledge, on a packet whose data also fails | nothing: the packet is thrown away whole | nothing to tell |

- **G20, the rule** (`tell_behind`): the other end's acknowledge field never goes backwards, since a header is
  written when it is sent and the wire keeps its order; and it never passes what this end has sent. After a false
  acknowledgement every true one is behind. **Two such in a row on a stream** † and the end starts the link again
  (G4) with the error. The case is `g20-false-acknowledgement.case`: told 160 cycles after the forged header, at
  the small sizes.
  - **What a false acknowledgement costs, then:** the packets it released, at most a window, and what the restart
    drops. Reported, not repaired: the sender no longer has them.
- **F1, not fixed** (`forged-number.case`, kept as a case that fails): the number of a data packet is under 32 bits,
  and a false one is silent.
- **Proposed, a change to the header, for the owner:** put the link's fields under a second check. Two ways:
  1. **Fold a second CRC of header bytes 0 to 7 into the data checksum field,** with another polynomial (the same
     one adds nothing: both checks then pass together). The field on the wire is the data's checksum XOR that
     value. A forwarder takes the old value off and puts the new one on without touching the data, and a bit
     flipped in a buffer is still caught. Every field of every packet, length 0 included, is then under 64 bits.
  2. **A 7-bit CRC in what is left of the reserved byte:** 2⁻³⁹ in place of 2⁻³². Smaller, and no change to the
     checksum's meaning.
- **Or without a change to the header,** for the acknowledge field only: release a kept packet when two good
  headers have acknowledged it. I have not built it.

## Rules taken that the search has not shown to matter

Each is a switch or a choice in the code. The searches hold with each of the switches either way
(`searches.sh rules`).

| | where HL is silent | what the code does | |
|---|---|---|---|
| G2 | Open: does the recovery packet state its number | it is of length 0, so its field is already the number the sender restarts from. The receiver never takes its place from it | closes the Open item |
| G3 | an acknowledgement during a recovery | every good packet's acknowledge field releases what it covers, in any mode; a "lost" packet's is also where the sender goes back to | |
| G8 | "lost" and "recovery" hold one stream's numbers | a packet for each stream in every round (`lost_each_stream`). With one, the other stream is found by G6 and G7 a status later | not needed, faster |
| G12 | the header of a packet sent again | written afresh: the acknowledge field and flags of now. G20 rests on this | no switch |
| G14 | a length over 32 under a good header CRC is "still in step" | it cannot be: treated as a lost place (`long_is_lost`). Passing over 16 × length bytes also holds | HL's table is wrong on this row, harmlessly |
| G17 | the first packet after "stop" is lifted | whatever was next (`stop_lift_back` off): a refused packet was asked for again already | not needed |
| G18 | when an acknowledgement is sent | as HL: on whatever goes back, and with the status. See the figure below | the owner's call |
| G19 | a good packet while lost | its data is not taken; its acknowledge field and stop bit are (`lost_uses_ack`) | not needed |
| | an answering end's data | none until the answer is heard: rounds of zeros and "recovery" only. HL can be read as data after each "recovery" | simpler |
| | "resend" for a packet of length 0 that fails | none: there is nothing to send again | |
| | a stream that cannot be | "resend" on both streams | |
| | when a status rides on data | a stream's next data packet carries its status; a packet of length 0 goes only when that stream has no data to send now | |

## Figures for the owner

- **G18: short packets wait on the window.** 400 one-word foreground packets on a whole wire take 28,463 cycles as
  HL has acknowledgements, and 15,313 with one sent as soon as the line is free (`ack_now`): 8 go out in 256
  cycles and the next acknowledgement is a status period away. Full packets do not care (187,328 and 187,073).
  If the foreground is short messages, `ack_now` halves their time.
- **What one damaged packet costs at the real sizes** (`test_link`; cycles from the damaged packet's first byte
  to the next word delivered; 528 on a whole wire):

  | | cycles |
  |---|---|
  | a bit of its data ("resend") | 1,072 |
  | the whole packet turned to zeros (found by the next packet, G6) | 1,616 |
  | a bit of its header, or its first byte zero ("lost" and recovery) | 2,672 |
  | the same, with the other direction hit as well (both lost, G9) | 3,712 |

- **A "resend" is not repeated on the status timer:** the timer is 512 and a full packet is 528, so the repeat
  would cross the packet coming back, every time. It is repeated when the sender's status shows it has not gone
  back (G7).
- **The window of 8 and the queue of 8 KB** are tied by G11: the window may not pass half the queue.
- **The restart handshake costs about 2,100 cycles at power-on** (two rounds each way), before any data.

## Still the plan's, for the chain

- **G5:** a forwarding node acknowledges when it has taken a packet into its own queue and its forwarding buffer.
- **G15:** the last ASIC. G4's handshake may answer it: a link with nobody on it never comes up, so nothing is
  forwarded onto it. To be shown on the chain. A neighbour that goes silent later is a different matter: nothing
  in HL notices it.
- **G16:** where a forwarded packet waits, how large, and when "stop" is said for it.
