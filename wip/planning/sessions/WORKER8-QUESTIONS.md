# WORKER8 questions (for the integrator)

**Q1-Q3 answered: DECISIONS-v1.2 section 47 (2026-10-31); recipe 23 and CONVENTIONS updated. The sort follows the
answers (REPORT.md, "What the recipes didn't cover").**

Each: what the spec or the recipes say, the sort's case, and the proposed answer the sort follows meanwhile.

**Q1. May a program write one table while arrivals still update another?** RECIPES 23 says the tables are free when
the arrivals' counter *and* DMA run done have fired. Spec 4.10 is per memory: a window access is an error only "when an
engine is using that memory". BT's step-2 configure (`bt_run.c`, `bt_phase`) waits for DMA run done alone, then
rewrites bank 0's `STREAM` entries while the layer's arrivals still update `PTR`, so the next layer's half-words go out
early. *Proposed:* allowed, as 4.10 reads; RECIPES 23 to say "free" per memory (`STREAM`/`GROUP`: DMA run done;
`PTR`/`HIST`: the arrivals' counter).

**Q2. May a program poll a word in data memory to learn that a sender's earlier packets have landed?** BT's sweep
(a sequence word after each token's 16 words) and DESC (a marker after each sender's descriptors) poll rather than wait
for a counter, which stays armed for the whole command. D44 orders one unit's packets to one destination, and each
lands through port B in order. *Proposed:* valid (the poll sees the last word only after the earlier ones are in data
memory); CONVENTIONS 6 to say so beside D44.

**Q3. Is `wfi()` on counters done ([7], under the reset mask) a sanctioned wait?** Spec 6.5 step 5 uses it; the recipes
use `wait_all(EV_CTR(n))` everywhere. The sort keeps v1.1's form where only counters are pending (exchanges, tree
reductions, the split's search steps), because `wait_all` costs four register stores per wait and the split makes
about 2,000 waits. *Proposed:* sanctioned; CONVENTIONS 5 to list it beside `wait_all`.
