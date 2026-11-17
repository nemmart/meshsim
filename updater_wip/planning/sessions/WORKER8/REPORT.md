# WORKER8 report: HOST-LINK.md as it stands on 2026-11-17

**Stopped, as NEXT.md orders. The protocol as it stands is not sound: the foreground can stand still behind the
background.** The cause is known and a shortest case is kept. Nothing is mended. The rest of NEXT.md is not done:
the last section says what is.

Names: HL is `v1.4_wip/planning/HOST-LINK.md`. A dagger (†) marks what I propose. Every figure here was measured
on this branch on 2026-11-17.

## What fails

**One lost background packet, while the background is stopped, stops the foreground** for as long as the
background stays stopped. If the Updater never takes its queue again, for good.

The case is `updater_wip/link/cases/stopped-blocks.case`: 4 events on the shrunken model, a shortest one.

| cycle | what happens |
|---|---|
| 249, 281 | end 0 sends background packets 1 and 2. End 1 takes both. Nothing takes them out of its background queue, which is now half full |
| 313 | end 0 sends background packet 3. It is lost on the wire |
| 329 | end 1 says "stop" for the background |
| 345 | end 0 sends foreground packet 4 |
| 377, 384 | end 1 gets packet 4, ahead of the 3 it expects: throws it away and asks, "resend" |
| 400 | end 0 goes back to packet 3. It is the background's, and the background is stopped: it is not sent |
| after | packet 4 is behind packet 3 in the one run of numbers. It cannot be taken first. The foreground stands |

**Found by** `linksearch --packets 1 3 0 0 --queue 4 --damage 1 --hold 2`: 21,208 states before the failure.
`--hold 2` is new: from every state, the background's queues are never emptied again, nothing more goes wrong,
and the foreground must still finish.

## Why

- **HL's rule:** "A stream that is stopped simply has no packets sent."
- **HL's argument** that a stopped stream cannot block the other has two steps: both stop bits are in every header,
  and a good packet always has room, so "nothing waits behind a refused packet".
- **The argument misses the other way to be behind a packet:** a packet of the stopped stream that was damaged and
  must be sent again. It has its number already. Go-back-N takes packets in order. The stop holds it, and with it
  every packet numbered after it, of either stream.
- **With numbers for each stream this could not happen.** It is the cost of one run of numbers that HL did not list.
- HL says nothing of packets sent again on a stopped stream. My code held them, as it always has, and as the
  sentence above reads.

## What I would change

† **A packet that already has a number is sent again whatever its stream's stop bit. "Stop" holds only packets
that have no number yet.**

- **Its room is already counted:** it was within the window when it was first sent, and the window is no more than
  half a queue. The receiver takes it once, in order.
- **What it changes for the receiver:** up to a window of data can arrive on a stream after it said stop. That was
  true already of packets on their way; now it is true of packets sent again too.
- **Tried only far enough to see that this is the cause and nothing else,** behind a switch (`-stop_holds_again`):

| search | HL as it stands | † with the change |
|---|---|---|
| `--packets 1 3 0 0 --queue 4 --damage 1 --hold 3` | fails, the case above | every property held, 604,477 states |
| `--packets 2 4 1 0 --queue 4 --damage 1 --hold 3` | fails the same way, 100,189 states | stopped by me at 2,000,000 states, not complete, no failure so far |

`--hold 3` holds each stream in turn. The change is not adopted and not tested further.

**The other way out** is numbers for each stream again, and the header of 20 bytes.

## The two seeds that failed (Quest 227: 200137, 140162)

- **Cause, found on the code that failed:** a receiver asked "resend" once, and that packet was lost. It asked
  again only when a packet of length 0, or the other stream's number field, showed the sender ahead. A sender with
  new packets to send sends neither: every packet of the rest of its window arrived ahead, was thrown away, and
  nothing was asked. The sender stood until its window was full and its line went idle.
- **Not the host's window and nothing else.** The window sets how long the sender stands: the rest of a window
  of packets. Both seeds had the host's window at 64. The gap was in my rule for asking again.
- **Confirmed on the code that failed:** with asking again at a packet with data too, both seeds pass with the
  host's window at 64.
- **It survives the changes** unless the receiver asks again, after the 2,048 cycles, at any sign that the sender
  is ahead. † That is built, behind a switch (`reask_any`).
- **Kept:** `cases/seeds-227.case`. It fails without the switch and holds with it.

## What is done, and what is not

| piece of NEXT.md | state |
|---|---|
| the header of 16 bytes, one run of numbers | built. HL's four example headers come out byte for byte |
| the restart packet | built: length 32, the pattern, checksum 0xC8981D21 agrees. † Stream field 1, number field 1, no number taken, data to no queue |
| both checks made afresh, the pass over the data | built. The cost is not measured |
| a packet made wrong, one flag | built. No case yet |
| a false acknowledgement, the error on a packet no longer kept | built. † "Lost" with an acknowledge field behind counts as asking. No case yet, no search with forged headers yet |
| the search, one link and chains, `--hold` | runs. Only the searches above and those of `make check` were run |
| random runs | run: 12 seeds here, 40 in `make check`, no failure. No campaign |
| kept cases for every rule | not done. The ten of the header before are in `cases/not-yet`, not brought up to date |
| planted bugs | the four new ones are in the code. `plants.sh` is not brought up to date and not run |
| Quest tasks | none queued. 227 to 229 stay in `tasks/hold` |
| figures for the proposed numbers, the costs | not done |
| HL checked against the code | not done, but for the one place above |
| `QUESTIONS.md`, `NOTES.md`, the state diagrams | not brought up to date |

`make check` passes: the header, the link's quick tests, the two cases, four small searches, 40 random runs, under
gcc with the sanitizers and under clang.
