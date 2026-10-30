WORKER5, round 3 (closing). Pull first.

Fold the decisions log's sections 43 and 44 into the spec, which close your W5-23 to W5-26 and WORKER6's F17:

- 43: mode 5 is "count only": it decrements its counter at the destination and writes nothing; any size, payload and address ignored; from a stream the address generator reads its source as usual and discards the payload; to the tile itself it takes a read token; counter 0 does nothing and meshsim flags it. Modes 6-7 stay reserved. The counter field counts for modes 0-3 and 5. The fence: a run that something must follow ends with a count-only packet to the tile itself on a spare counter; when that counter reaches 0, everything earlier in the run has been injected. "DMA run done" keeps its meaning (the last table read). Count-only packets also serve as "done" signals between tiles.
- 44: ordering holds per sending unit, not per tile: packets from one unit in one tile, to one destination, of one kind, arrive in the order sent. The units are the address generator, the send port, the GATHER engine, the read port, and each sending peripheral's DMA. Between units there is no ordering; a fence or putting the packet in the run imposes it. Rewrite 3.2 and 6.2 to say so, and close item 136.
- 44, W5-23: "outstanding" is a software convention: a request until its reply lands, a read until its data lands. In hardware a request only holds its receive-queue slot until the target pops it; the convention is conservative and implies the bound.
- 44, W5-24: the 24-report set as you defined it; the header's fields come with C&C 1's request format.
- 44, W5-26: state in 6.2 that C&C 1 lands a tile's first command only when that tile should start at the next GO.

Update CHANGES-v1.2.md and mark W5-23 to W5-26 answered in QUESTIONS.md. Then write WORKER5/REPORT-3.md: what changed, anything still (Proposed), and any question left for the owner.

This prompt is the go-ahead for wip/spec/ and WORKER5/. Run `git pull --rebase` before every push.
