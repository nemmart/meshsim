WORKER6, round 3 (closing). Pull first.

1. Fix the mismatches WORKER5 found in WORKER5/RECIPE-CHECK.md (M3 onwards): stale bit numbers in comments and prose (DMA run done is EVENTS [10]; a peripheral's live bit is [12 + n]), "region 0" for the replayer's part 0, the command and reply block addresses and the 16 reserved bytes now decided rather than proposed, and the rest of its list. M1 is answered by the log's sections 43 and 44; M2 was a spec fix, and your recipe was right.

2. Fold in the decisions log's sections 43 and 44:
   - 43: mode 5 is "count only": it decrements its counter and writes nothing. Add it to the header (the mode constant) and add a fence recipe: a stream run that ends with a count-only packet to the tile itself on a spare counter, then a command sent from the send port once that counter reaches 0. Keep recipe 22's command-in-the-run approach as the boot recipe, and mention the fence as the general pattern. Show a count-only packet as a "done" signal between tiles where it's natural.
   - 44: ordering holds per sending unit (address generator, send port, GATHER engine, read port, each peripheral DMA), not per tile; between units there is none. Add this to the rules that bite, and close your F17 with it.
   - 44: a read is outstanding until its data lands (a software convention); make sure the request recipes count reads against the 1/3/1 limits that way.

Keep the book generated from the compiled code, and the code compiling. Mark F17 answered in FINDINGS.md and write WORKER6/REPORT-3.md when done.

This prompt is the go-ahead for wip/sw/RECIPES.md, wip/sw/CONVENTIONS.md, wip/sw/mesh-v1.2-draft.h and WORKER6/. Run `git pull --rebase` before every push.
