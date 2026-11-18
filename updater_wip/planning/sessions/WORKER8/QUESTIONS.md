# WORKER8 questions: where HOST-LINK.md is silent, wrong, or needs a ruling

2026-11-17, third issue: on HL as it stands that evening. The header is 16 bytes, a link has one run of packet
numbers, "stop" holds only packets not yet numbered, and the data is masked on the wire. The issues before are
in git: the first at `ff4e1c55` (16 bytes, numbers for each stream), the second at `e8f28bcd` (20 bytes).

HL is `v1.4_wip/planning/HOST-LINK.md`. The code is `updater_wip/link/`. `REPORT.md` beside this file has the
results, the figures and the full list of corrections to HL; this file is what is the owner's to decide. A
dagger (†) marks what I chose or propose.

**How to read a case.** Each rule has a case in `updater_wip/link/cases/`, found by the search, a shortest one:
`build/linkcase -t cases/<name>.case` shows it line by line. It fails under the rules its first `run` line names
and holds under HL, and `make check` plays every one both ways. Each rule is a switch in `link_params.h`
(`Rules`); `hl` is all of HL, `hl-flush` is HL without that rule.

## Every rule, its switch and its case

| the rule (HL's section) | switch | without it | case |
|---|---|---|---|
| a packet ahead of the one expected: thrown away, resend ("When a packet is bad") | `resend_ahead` | the link stops for ever after one lost packet | `wrong-number` |
| an empty packet's number ahead: resend | `zero_number` | the last packet of a burst is never sent again | `last-packet-gone` |
| asking again at any sign the sender is ahead | `reask_any` | a sender with new packets stands for the rest of its window | `seeds-227` |
| a lost end that hears "lost" answers it ("Recovery") | `lost_answers` | both ends lost: each waits for the other, for ever | `both-lost` |
| a lost receiver waits for 528 zeros ("Finding a packet's start") | `flush` | data that is a header's image is taken for a packet: but only without the mask too | `data-taken-for-a-packet` |
| the data is masked on the wire | `mask` | a header that reads as zeros, and a packet in the data is taken for the true one | `packet-in-data`, `forged-number` |
| a packet with no room is thrown away, not acknowledged ("When a packet is bad") | `refuse` | a queue overruns when the window is over half of it | `overrun` |
| the restart handshake ("Starting a link again") | `restart` | one end powers on: the link stops for ever | `power-on` |
| a packet made wrong: the second time running, the error ("A packet made wrong") | `wrong_twice` | asked for again for ever | `made-wrong` |
| asked for a packet no longer kept: the error ("Starting a link again") | `ask_not_kept` | after a false acknowledgement and a loss, the link never finishes | `false-acknowledgement` |
| "stop" holds only packets not yet numbered ("One run of numbers") | `stop_holds_again`, off | the foreground stands behind a stopped background | `stopped-blocks` |
| an acknowledgement as soon as the line is free | `ack_now` | nothing fails; short messages are slower | none |
| a good packet while lost: its acknowledge field and stop bits are taken | `lost_uses_ack` | nothing fails in the searches | none |

`data-made-on-purpose` is not a rule's case: it is the mask's limit, question 2.

## For the owner to rule

**1. The mask's sequence: one bit set in its starting value.** †

- HL proposes: the CRC-32C register started at the header CRC, stepped eight times a data byte with nothing fed
  in, its low eight bits masking the byte. That register has **two states that stand still:** 0, whose mask is
  zeros, and 0xFCA42DAF, whose mask is the byte 0xAF over and over. A packet whose header CRC is either goes
  out with its data in the clear, or as good as: 2 header CRCs in 2³².
- † I built: **the same, with bit 16 of the starting value set.** No header CRC then starts it in a state that
  stands still, and from every other state the register returns only after 2³¹ − 1 shifts. It costs one wire.
  Two header CRCs that differ only in bit 16 share a mask, which harms nothing.
- By chance the two states need 2⁻³¹ on top of everything else, so this is tidiness. **To rule: bit 16, or the
  sequence as HL proposes it.** It is one constant in `header.h`.

**2. The mask's limit: data made for the header it will travel under.**

- HL: "to get a packet through in the data, whoever made the data would have to know the header's CRC".
- **What has to be known is less than that:** the header's other fields, which are the stream, the length, the
  packet number, the acknowledge field, hops and the stop bits. The CRC follows from them and from the data,
  and **because the CRC and the mask's sequence are both linear, the data can be solved for**: no search, 33
  evaluations and 32 equations (`bench.cpp`, `made_on_purpose`).
- `cases/data-made-on-purpose.case`: data made so, a header that reads as zeros, and the packet in the data is
  taken, in the same two events as `packet-in-data`. It fails under HL as it stands.
- **How far the fields can be known.** On a quiet link, well: hops is 0 from the host, the stop bits are 0, the
  acknowledge field stands still while nothing comes back, and the number counts up from 1 after a restart.
  With foreground messages going between, the number and the acknowledge field move in ways the data's author
  cannot see.
- **What it still needs:** the line to read as zeros through that packet's header and no further.
- **What would close it:** a value the data's author cannot know, in the mask's starting value: the value for
  each life of a link that HL set aside. A sequence that is not linear would not close it: the solving becomes
  a search of 2³², which is minutes.
- **To rule: a known limit, as HL's sentence already has it, or more.** I have treated it as known and carried on.

**3. The pass over the data, where the two streams meet.**

- With a byte a cycle, a foreground packet that arrives beside a background flood waits 512 cycles for its own
  pass, whatever the line is doing. † As I built it the pass starts at once, the background packet whose pass
  was under way starts over, and the line stands idle until the foreground's is done: the flood lost a tenth of
  its rate with a foreground packet every 2,600 cycles or so (`REPORT.md`, section 5).
- The other way to build it sends the background packet that is ready first: no idle line, and the foreground
  waits up to two packets.
- **To rule: which of the two, or a unit of 128 bits a cycle** (HL: about 2,490 gates, a full packet in 32
  cycles), which makes the question go away. HL's "the longest a foreground packet waits is one background
  packet" holds only with the faster unit.

**4. Hops 15 on a packet from the host.** HL says both that from the host hops "is not tested, and no value of it
is an error", and that "a packet that arrives with hops 15 at an ASIC that would forward it is a bad packet". An
ASIC that forwards adds one, and 16 does not fit. † I test it in both directions.

**5. A packet of length 0 with a field that cannot be.** It has no number of its own; its number field is the next
packet's. † If that field is the number expected, it sets the "made wrong" flag like a packet with data, by HL's
words ("a packet that carries the number expected"). The other reading: it is thrown away and not counted. Noise
needs a forged header for either.

**6. The restart packet's details.** † All mine, HL is silent:

| | what I built |
|---|---|
| its stream field, number field, hops | 1, 1, 0 |
| its acknowledge field and stop bits | written as in any header; not used by the receiver |
| its data on the wire | masked like any data |
| both ends starting at once | restart with lost and recovery both set; the handshake ends at a restart packet without lost, or at any ordinary packet |
| how it is sent | in rounds after 1,024 zeros, like "lost" |
| an end that is starting again | takes nothing from ordinary packets until it has its answer |
| a restart packet whose pattern or checksum is wrong | a damaged packet like any other |

**7. Asking for a packet no longer kept.** † "Asks" is resend, or "lost", with an acknowledge field behind the last
one the sender took. HL names only the first.

**8. The 2,048 cycles and a packet that carries the number expected.** † A thrown-away packet with the number
expected (its data damaged, no room, or made wrong) is asked for at once, every time. The 2,048 cycles hold back
only the asking that a packet ahead sets off. HL does not say which.

## What the mask changed besides, for the owner to know

- **The false packet number is no longer a limit for a packet with data.** A header that noise has forged takes
  the wrong mask off its data, and the data checksum fails: `cases/forged-number.case` fails without the mask and
  holds with it. HL's paragraph "A known limit, accepted" can go. What is left is a forged header on a packet of
  length 0: a false acknowledgement, false stop bits, a false "resend" or "lost".
- **So "a 64-bit check" is now true of the header's fields too,** on any packet with data.
- **The wait for 528 zeros is no longer what keeps data from being taken for a packet.** With the mask the
  searches hold without it. It still spares a lost receiver a string of failed header CRCs, and it is the only
  guard against data made on purpose (question 2). I would keep it.

## Still open, as HL has them

- **A neighbour that goes silent:** not designed, by the owner's ruling.
- **The background behind the foreground:** a foreground that never pauses holds the background. The order as
  decided.
