# Worker 8 (updater_wip): a robust simulation of the host link's protocol

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. The ASICs sit on a daisy chain from an FPGA host and talk over a link of 8 LVDS lines. **The link's
protocol is designed and has never been run:** packets with two CRCs, packet numbers and acknowledgements for each
link, sending again, recovery from a lost place, stop and resume, and forwarding along the chain. It is
`v1.4_wip/planning/HOST-LINK.md`.

**Why you are here** (the owner): "If this has some hidden flaw, and we go out to hardware with it, we'll be
absolutely miserable." **Your part is to find the flaws before hardware does:** build the protocol as it is
written, put it on wires that misbehave in every way you can think of, and show either that it holds or exactly
where it breaks.

**It is a study of its own:** no chip, no mesh, none of `v1.37_wip/`. Everything is in a new folder,
`updater_wip/link/`. The link end you build will later become the link part of the chip's Ingress/Egress model, so
it is written to be kept.

READ FIRST: the top-level `README.md`; `updater_wip/README.md` and `planning/MILESTONE-v1.37.md`;
**`v1.4_wip/planning/HOST-LINK.md` in full: it is your specification;** `INGRESS-INTERFACE.md` ("The pipeline",
"Holding the host back", "Egress", "Reset"); `DESIGN-NOTES-2026-11-16.md` sections 12, 19, 20 and 21;
`updater_wip/planning/sessions/WORKER2-REPORT.md` (gold's packetizer and its vectors). The header's layout
changed on 2026-11-17 (18-bit numbers, a hops field): `HOST-LINK.md` has it, with three example headers to check
yourself against; gold's vectors with the new layout are on branch `updater-w7` until it is merged.

WHAT TO BUILD, in `updater_wip/link/`
1. **A link end,** from `HOST-LINK.md` alone, as a state machine in the shape of hardware: its registers in one
   place, the state an enumeration among them; its outputs a function of its registers; one function that
   advances a clock, a byte a cycle each way. It has:
   - the receiver: finding a packet's start, the header CRC, the fields, the data checksum, the packet number;
     a stream's words into its queue;
   - the sender: a queue for each stream, the packets kept until acknowledged (8 a stream), sending again from
     the oldest not acknowledged, the status at a change and again after 512 idle cycles, obeying the other
     side's stop;
   - "lost" and recovery, both sides of it;
   - the sizes as parameters: the queues (8 KB a stream by default), the window, the timer.
2. **A chain node:** two link ends, one up the chain and one down, with forwarding in both directions (check,
   the link's fields written afresh, hops plus one, the header sealed again, the data and its checksum
   untouched), a forwarded packet before the node's own, and "stop" for each link.
3. **The host end:** the same protocol with the memory to keep every packet.
4. **A wire that misbehaves,** for each direction of each link, on its own:
   - single bits flipped, and bursts; noise for a long time, hundreds of milliseconds (owner: a phone call in
     the server room);
   - a packet's first byte turned to zero; zeros turned to bytes; a header that still passes one check;
   - damage aimed at the worst moments: a "lost" packet, a recovery packet, a status packet, a packet sent
     again, the packet at the number's wrap, the first packet after a stop is lifted;
   - noise on one direction only, on both, on every link of a chain at once, and none.
5. **Consumers that misbehave** (owner): whatever takes words out of a queue stops for a long time, **up to 1 ms,
   a million cycles,** at random, while the noise happens and while it does not; and senders that have nothing to
   send for a long time, then a great deal.
6. **Traffic:** bulk on the background stream with short messages on the foreground between; replies coming
   back from every node of a chain at once; nothing at all. Chains of 1, 2, 4 and 16.

WHAT MUST HOLD, checked in every run
- **Every word of every stream is delivered exactly once, in order,** at every node and at the host: none lost,
  none twice, none out of order, none that was not sent.
- **At the host, each node's replies are whole and in order** when sorted by hops.
- **No queue overruns,** ever, whatever the consumer does.
- **The foreground stream keeps moving while the background is stopped.** Report the longest a foreground
  packet waited, a hop and end to end.
- **Once the noise ends, every link recovers and everything sent arrives.** Report how long recovery took, its
  worst case, and what the worst case was.
- **No state from which nothing moves,** with a quiet wire and work to do.

AND A SEARCH THAT MISSES NOTHING
- **Shrink the model** until every case can be tried: tiny queues, a window of 2, packet numbers that wrap after
  8, a few packets, one or two hops. Then **try every order of loss, damage and delay,** a packet at a time, and
  check the same properties in every state reached. Random runs miss the cases this finds.
- Use the same link end's code if you can, with small parameters. If you cannot, say why, and what keeps the
  small model honest.

WHERE THE DOCUMENT IS SILENT OR WRONG
- **This is the most useful thing you will find.** Known gaps: when a packet that is not acknowledged is sent
  again with no "resend" asked; whether the recovery packet states the number it restarts from; what an
  acknowledgement means during recovery; what happens when one side starts again from power-on and the other
  does not; when a forwarding node acknowledges a packet it has yet to pass on.
- **For each gap or flaw: write it in `QUESTIONS.md`** with the case that shows it, the rule you propose, and
  what else would do. Take your proposed rule, mark it in the code, and carry on. Do not settle one silently.
- **A flaw in the protocol as written is a finding, not a bug of yours:** keep the case that shows it as a test
  that fails without the fix.
- **Do not edit anything in `v1.4_wip/`.** List what you would change.

HOW YOUR OWN WORK IS CHECKED
- The three example headers of `HOST-LINK.md` and the CRC's test values come out byte for byte.
- **Planted bugs:** put flaws into the link end one at a time (an acknowledgement one off; the window not kept;
  the wrap compared wrongly; the timer not started again; a duplicate delivered after sending again; a header
  not sealed again; a stop ignored; recovery answered once only) and show that the runs or the search catch
  each. A check that catches no planted bug proves nothing.
- Every random run is from a seed, and a failing seed replays exactly.
- Builds with gcc and clang, `-Wall -Wextra -Werror`, and passes with the address and undefined-behaviour
  sanitizers.

RULES
- **Propose your plan first** in `NOTES.md` and wait for the owner's go-ahead before writing code: the link
  end's parts and states, how the wire and the consumers misbehave, the properties and how each is checked, the
  shrunken model and how it is searched, how long the runs take.
- **Branch `updater-w8`.** Your notes, questions and report are in `updater_wip/planning/sessions/WORKER8/`; the
  code in `updater_wip/link/`, with its own makefile and a `README.md` that says how to run it. You touch
  nothing else. The integrator merges into `main`.
- C++20, as the simulator: no exceptions, no RTTI.
- Exactness is the requirement: a property that fails once in a million runs fails.
- Long campaigns go to the runner (the Quest repository; `PIPELINE.md` there). If your session cannot attach
  Quest, do not work around it: say which runs to queue, and the integrator queues them.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the chip's Ingress/Egress model and its registers; the frames inside the packets; the three
processes and the driver of the milestone; the specification.

DELIVERABLES
- `updater_wip/link/`: the link end, the chain node, the host end, the wires and consumers, the random runs, the
  search, the planted bugs, a `README.md`.
- `WORKER8/NOTES.md`: the plan, the state diagrams, the "Choices" list.
- `WORKER8/QUESTIONS.md`: every gap and flaw, each with its case and the rule proposed.
- `WORKER8/REPORT.md`: whether the protocol holds, and under what; each flaw found; the figures (recovery
  times, the longest waits, the most packets ever outstanding, the fullest a queue ever got, which bear on the
  window of 8 and the queues' size); and what you would change in `HOST-LINK.md`, as a list for the owner.
