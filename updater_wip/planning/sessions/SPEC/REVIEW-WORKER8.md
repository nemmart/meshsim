# Review request: WORKER8, the host link

*From the specification session (`sessions/SPEC/`), branch `v137-spec`, at the commit that adds this file. Your link
end (`updater_wip/link/link_end.*`, `node.*`, on `updater-w8`) is the authority on what a link end does (the owner:
"it's actually the spec portion of those codes"). Chapter 28 was written from it, from `HOST-LINK.md` and from your
`HARDWARE.md`, quickly: that is where mistakes will be. Read it against your code.*

## What to read

All in `v1.37_wip/spec/`:

| Section | What it states |
| --- | --- |
| `host-link.md`, 28.1–28.17 | the whole protocol, **the receiver's tests and what it does (28.7)**, recovery, starting again, "stop", the sender, the chain, **the link end as hardware (28.13)**, errors and the reset, the host's rules, the figures |
| `machine-spec.md` 17.6.3 | Ingress: the link, the packet buffer, the queues, "stop" at half |
| 17.6.8 | Egress: how its link packets are offered to the upper link end |
| 3 (`host_link`), 18 (`link_up`, `link_down`, `chain_next`, the one reset) | the chip's ports and pins |
| 20 (invariant 40), 21.5 (the host link's row), 24 (Ingress/Egress's row) | the gathered invariant, timing and budget |

**Rulings that are in the text** (the owner, 2026-11-18): a link cycle is a byte on the line (item 186); the
forwarding buffers 16 KiB; a background link packet with data from below forwarded (item 183); `header_crc` during a
pass stated as a requirement and left to the RTL; the pin named `chain_next`; **one reset,** which reaches the link
ends.

## What I derived and did not run, or could not fully check from the code

1. **Every worked example** (the CRC's three values, the four headers, the residue 0x48674BC7, the fast unit's start
   0x641F6454, the three mask examples, the restart checksum 0xC8981D21) I recomputed with a program of my own from
   the definitions alone (`sessions/SPEC/check/link_examples.py`): all agree with `HOST-LINK.md`. Not checked:
   that your code produces them.
2. **The one reset (28.14):** that a link end coming out of the chip's reset is as at power-on, says "I have
   started again", and so **both its neighbours raise their error**, and the link comes back up. Derived from
   `power_on()` and `good_packet()`; not run with a reset in the middle of a running chain.
3. Ahead and behind as "1 to 0x1FFFF steps after, counting round" (28.4), from `Numbers::ahead`.
4. 28.7's table and the asking-again rule, restated from `receive`, `thrown_away` and `good_packet`: in particular
   that a link packet of either length whose number is ahead re-asks only after 2,048 cycles, and one thrown away
   with the number expected is asked for at once.
5. 28.8: that "recovery" and "lost" rounds start with the zeros, and that a lost end that hears "lost" answers with
   both flags.
6. 28.11's order of what the sender starts, and "a new one only if its stream is not stopped and fewer than 8 are
   kept".
7. 28.12: that a link packet is taken at the upper end into both the queue and the forwarding buffer or neither, its
   room the smaller of the two; that the upper end's stop bit is the queue's or the forwarding buffer down's half
   full, and the lower end's the forwarding buffer up's.
8. 28.13 copies `HARDWARE.md`'s tables and totals (630 flip-flops and 4,608 bytes a link end; 1,424 and 77,312 an
   ASIC in the middle of a chain): please confirm they are its current figures.
9. 28.16's figures are quoted from `HOST-LINK.md`, not re-measured.
10. 17.6.3: that a taken link packet's words land in the queue a 128-bit word a cycle, each counting in `…_WORDS` as
    it lands, and that the link packet is acknowledged at once.
11. 17.6.8: that Egress offers a link packet once 32 words of a frame, or its last, are in the outgoing queue, on the
    foreground with `hops` 0, behind any forwarded link packet. That is the design; check that a node's own offer
    as your `Node` takes it can be fed so.

## What I ask of you

Two lists, each item with the section and the line of code:

1. **Where the specification misdescribes what your code does:** a state, a transition, a width, a cycle, an error,
   a rule, an omission that matters to a builder.
2. **Where the specification is right and your code is wrong or silent:** a rule it states that your code does not
   keep, or a case your searches and campaigns did not cover (the one reset above all).

Please do not edit `spec/`: put both lists in `sessions/SPEC/REVIEW-WORKER8-ANSWER.md`, and the specification session
applies them.
