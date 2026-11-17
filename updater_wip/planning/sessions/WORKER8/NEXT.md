# Worker 8, next: the owner's changes to the protocol, made and tested

2026-11-17. The owner has been through your findings one at a time and understood each. Most of your rules stand.
**Several are changed, one is dropped, and the numbering is different.** `v1.4_wip/planning/HOST-LINK.md` on `main`
now has all of it and is meant to be complete: read it in full before anything else, with
`DESIGN-NOTES-2026-11-16.md` sections 26 to 28 for how each ruling was reached.

**The job:** make the changes below in `updater_wip/link/`, then test the protocol as it now stands, as hard as you
tested the first. No plan to propose first: go straight to it. Use the runner as you need.

## When to stop (owner)

**Stop as soon as you are sure the protocol as it now stands is not sound, and you have identified a cause.**

- "Not sound" is a property that fails because of the rules, not because of a mistake in your code: a link that
  stops for good, a word lost, doubled or out of order with no error raised, a queue overrun, a stream that
  stands still behind the other.
- **Then do not mend it and do not carry on testing.** Keep the shortest case that shows it, say in a few lines
  what fails and why, and what you would change, and report to the owner. The rule is the owner's to choose.
- A mistake of your own is not a reason to stop: fix it and go on.
- If nothing fails, carry on to the end of this brief.

## First: the two seeds that failed

Quest 227's first attempt finished and **failed on 2 seeds of 3,777:** 200137 (4 nodes) and 140162 (1 node), each
"a sender stood still: no acknowledgement in 20801 cycles on a link in step". Both had the host's window at 64.
The results are in the Quest repository, `results/227-link-w8-campaign/`.

- **Find the cause before you change anything,** on the code that failed, and keep a case for it.
- Say whether it is the host's window and nothing else, and whether it survives the changes below.

## The changes

1. **One run of packet numbers for a link, shared by both streams.** The owner: "there is really just one stream
   with two destination queues."
   - One packet number, one acknowledgement, one resend flag, one window of 8 packets, one set of kept packets,
     for each direction of a link. Both streams' stop bits are in every header.
   - The header is one 128-bit word again (HL, "A packet"): resend, lost, recovery and restart in bits [7:4];
     the number in [33:16]; the acknowledgement in [51:34]; hops in [55:52]; the two stop bits in [57:56]. Four
     example headers to check against. **The 20-byte header is dropped,** and with it your G8 and G13.
   - **Show first, by the search, that a stopped stream cannot block the other.** The argument is in HL: both
     stop bits reach the sender in anything it hears; a queue says stop with 8 KB free; a window is 4 KB; so a
     good packet always has room and is never refused. A case where the foreground stands still behind the
     background is the most important thing you could report.
   - **Measure what it costs:** how far a damaged background packet sets the foreground back; how long a
     foreground packet waits when the window is full of background packets; whether 8 is still the right window
     now that the two streams share it.

2. **The restart packet is a full packet with a fixed pattern.** Length 32; the data is the first 32 primes as
   bytes, 2 to 131, repeated 16 times; its CRC-32C is 0xC8981D21 (HL, "Starting a link again").
   - An end acts on a restart only if the header, the data checksum and the pattern all agree. Anything less is a
     damaged packet.
   - "Lost" and its answer stay 16 bytes.
   - HL marks as proposed that it takes no packet number and its data goes to no queue. Settle the details and
     say what you chose.

3. **Both checks are computed afresh whenever a packet is made ready to send:** an end's own packets, forwarded
   ones, and ones sent again. The owner: "use the fresh value as the packet checksum, recompute the header
   checksum."
   - **Your G21 is dropped.** A sender keeps the data, not a checksum to compare with.
   - **The data checksum no longer travels end to end** along a chain: each node makes its own.
   - **So the checks cover the wire and not the memory.** A bit flipped in a queue, a kept packet or a forwarding
     buffer is now delivered and not seen. That is decided. Take such flips out of the properties, or run them
     apart and report what they do; they must not hang a link.
   - **A sending end now passes over a packet's data before the packet leaves,** since the checksum is in the
     header. At a byte a cycle that is as long as the data. **Model it,** and report what it does to the
     foreground's waits and to the data rate, on a busy link and on an idle one.

4. **A packet made wrong** (replaces G22). Asked for again the first time, as any bad packet is: it may be noise.
   If the packet sent again fails the same way, the error, and the link starts again.
   - **One flag in each receiver:** set by a packet that carries the number expected, passes both checks and fails
     a field test; cleared only when a packet is taken. Set already when the same happens again: that is the
     second time. Good packets behind the wrong one are ahead of the number expected and are thrown away, so
     they do not clear it.
   - The owner's reason against your rule: noise that damages only the header leaves the data whole, and judging
     by the data would have blamed the sender for it.

5. **A false acknowledgement** (replaces G20). Not watched for. **The error is raised when the other end asks for
   a packet the sender no longer keeps,** and only then; the link then starts again.
   - The owner: "if a resend is never requested, then no harm, no foul." The packets let go early usually arrive
     whole.
   - Show by the search that a loss is always reported when there is one, and that no error is raised when
     nothing was lost.

6. **Confirmed as you have them:**
   - a good packet ahead of the number expected is thrown away and a resend asked for, once; one behind is thrown
     away and acknowledged;
   - an empty packet carries the sender's next number, which is how a lost last packet is found; no resend timer;
   - a lost end that hears a good "lost" answers it; any good packet after the zeros puts a receiver back in step;
   - a lost receiver takes nothing until it has seen 528 zeros in a row; the lost side repeats 1,024 zeros and
     "lost", and that gap is not shortened;
   - the window is no more than half a queue; the queues are 16 KB a stream; a good packet with no room is
     thrown away and not acknowledged;
   - an acknowledgement goes out as soon as the line is free; the status repeats after 512 idle cycles, one timer
     a direction;
   - a node acknowledges a packet when it has taken it into its own queue and its forwarding buffer; a forwarded
     packet goes before the node's own;
   - **the last node is told by a pin** that it has no neighbour below;
   - the false packet number is a known limit, accepted.

7. **Left open, and not to be designed now:** a neighbour that goes silent.

## Then test it

- **Every search again** on the protocol as changed, on one link and on chains of 1, 2 and 3, with the same
  properties. Say what changed in the state counts.
- **A kept case for every rule,** as before: each fails with its rule off and holds with it on. The cases for
  the rules that changed (4 and 5) are new.
- **The planted bugs,** brought up to date: add one for each changed rule (the flag cleared by a thrown-away
  packet; an error raised on an early release with nothing lost; a restart acted on with the pattern wrong; a
  checksum not made afresh on a packet sent again).
- **The random campaign,** on chains of 1, 2, 4 and 16, with the rare events counted. Tasks 227 to 229 are
  parked in the Quest repository's `tasks/hold`; they were on the dropped header. Queue new ones with new numbers
  on the new baseline. **The runner has 16 CPUs and 62 GB:** size the larger searches to it.
- **The numbers HL still marks as proposed:** the host's window of 8; 2,048 cycles before asking again; 528
  zeros; the forwarding buffers' 16 KB; the second wrong packet before the error. Say for each whether it is
  right, with the figures.

## Check the document against what you built

`HOST-LINK.md` was brought up to date by the design session from your notes, and then changed by the owner's
rulings. **Read it as a stranger would, and list every place where it is wrong, unclear, or still silent,**
against the code that passes. That list is as valuable as the tests. Do not edit the document.

## What comes back

- `updater_wip/link/`: the changes; the cases; the planted bugs; the tasks.
- `WORKER8/QUESTIONS.md` and `NOTES.md` brought up to date: the rules as they now are.
- **`WORKER8/REPORT.md`:** whether the protocol as it now stands holds, and under what; the cause of the two
  failing seeds; what one run of numbers costs; what the pass over the data costs; the figures for each proposed
  number; the list of corrections to `HOST-LINK.md`; and the state diagrams.

The rules of your first brief stand: branch `updater-w8`; exactness; never a token in a file; kill by PID; commit
and push after each piece; a clean `git status` at the end of each turn; short replies to the owner.
