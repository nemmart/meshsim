# Parked for WORKER22 (send now; remove once sent)

Thanks. Your three new questions, ruled (owner, 2026-11-05; DECISIONS-v1.3 section 21, up to the commit after c190a09):
- **Q11:** answered by routing by dst: each transfer names its destination (`DMA_HDR`'s dst 66, `DMA_ADDR` in C&C 3's memory),
  and a packet whose dst is its router's own unit is ejected there like any arrival. No special case.
- **Q12, the shared cache's write-back policy:** the next victim in every set is kept free or clean: when a set's LRU line is
  dirty it is evicted at once, in the background, into the write buffer; a clean LRU line stays (hittable) and is dropped at no
  cost when a fill needs its way. So a fill never waits for a write-back. Write buffer: 4 lines, draining at one line per 80
  cycles (parameters). A read that misses the cache waits until the buffer is empty, so the buffer is never searched. Report
  early evictions, write-backs and read waits per segment (if the waits matter, a 4-entry compare is the cheap fix).
- **Q13:** yes, allocate without an LPDDR read: the Tracer writes whole lines, so its line goes into a free or clean way, dirty,
  and comes back from there on its next access.
- `CC_LINK_OUT` as the down port: fine. Log these in SPEC-QUESTIONS.md as answered.

**Scope change (owner): the host-side inputs move to WORKER26.** WORKER26 now builds, besides the jump map and frame 6, each
segment's start image (cycle-0 image plus earlier segments' updates, owned = the pages the segment writes, checked against
gold's memory at its first row), and word 66's values and the hash constants. Your item 3 becomes: load WORKER26's outputs (per
segment a list of pages, 4 KB content and owned flag, plus word 66's values; the map's lines and constants) into your
`FileStore`, which lays them out as section 20 trees, and convert frame 2's record. Propose that format to WORKER26 along with
the map's. Until its outputs land, build only the minimal image your first functional tests need. Then milestone 1 as you plan.
