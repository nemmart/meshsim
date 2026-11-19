# WORKER8 questions: where HOST-LINK.md is silent, wrong, or needs a ruling

2026-11-18, fourth issue: on HL as the owner brought it up to the code (`870ed9d9`). The header is 16 bytes, a link has one run of packet
numbers, "stop" holds only packets not yet numbered, and the data is masked on the wire. The issues before are
in git: the first at `ff4e1c55` (16 bytes, numbers for each stream), the second at `e8f28bcd` (20 bytes), the
third at `e60d7c91` (one run of numbers, the mask, the slow sending unit).

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

`data-made-on-purpose` and `false-alarm` are not rules' cases: they are known limits, and fail under HL as it
stands. The first is the mask's limit; the second is the error raised with nothing lost, after two forged headers.

## Ruled by the owner on 2026-11-17, and now in HL

| | the question | the ruling |
|---|---|---|
| 1 | the mask's sequence | as built: bit 16 of the starting value set |
| 2 | the mask's limit, data made for the header it travels under | accepted as known: it takes someone making the host's own data against the link |
| 3 | the pass over the data where the streams meet | two fast units for sending, 128 bits a cycle; two slow ones for receiving. The code now has the fast unit by default |
| 4 | hops 15 on a packet from the host | tested by an ASIC that must forward, in either direction |
| 5 | a packet of length 0 with a field that cannot be | counted, if its number field is the one expected |
| 6 | the restart packet's details | as built |
| 7 | "asks for a packet no longer kept" | a resend, or a "lost", with an acknowledge field behind |
| 8 | the 2,048 cycles | hold back only the asking that a packet ahead sets off |

The false packet number is closed by the mask, and HL says so.

## New, for the owner

**1. The error with nothing lost.** HL: the error for a packet no longer kept "is raised only when there is real
harm". With two forged headers, a false acknowledgement and then a false "resend" or "lost" before the packet
lands, it is raised and nothing was lost (`cases/false-alarm.case`; `REPORT.md`, section 4). With one forged
header HL's sentence holds. † I would change the sentence and nothing else. If it is to be closed: the sender
waits for the request to come a second time; a true one always does.

**2. Three small choices that came with the fast unit,** all †, HL silent (`REPORT.md`, section 1, S2, S3, S8):
the header waits until the pass is done; the restart packet has no pass; a packet of length 0 may go while a
packet with data is still being passed over.

**3. The fast unit's starting value for the header CRC** is 0x641F6454. (Now in HL.)

**4. A background packet with data that comes from below.** HL has the background empty towards the host but does
not say what an ASIC does with one. The model forwards it, through a forwarding buffer that nothing else ever
uses: 16 KB an ASIC. † Were it a packet made wrong, that buffer could go (`HARDWARE.md`, section 8).

**5. The header's CRC while a pass is running** (`HARDWARE.md`, section 4). A packet of length 0 may start while
the fast unit is passing over the next packet's data, and its header needs that unit for a cycle. The model
charges nothing for it. † Either the pass waits a cycle or the header's CRC has logic of its own; no rule depends
on the cycle.

## Still open, as HL has them

- **A neighbour that goes silent:** not designed, by the owner's ruling.
- **The background behind the foreground:** a foreground that never pauses holds the background. The order as
  decided.
