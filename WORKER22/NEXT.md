# Parked for WORKER22 (remove once sent; the owner may already have interrupted with it)

**Scope change (owner, 2026-11-05): the host-side inputs move to WORKER26.** WORKER26 now builds, besides the jump map and
frame 6, **each segment's start image** (cycle-0 image plus earlier segments' updates, owned = the pages the segment writes,
checked against gold's memory at its first row) and word 66's values and the hash constants. Your item 3 becomes: **load
WORKER26's outputs** (per segment a list of pages, 4 KB content and owned flag, plus word 66's values; the map's lines and
constants) into your `FileStore`, which lays them out as section 20 trees, and convert frame 2's record. Agree the format with
WORKER26. Until its outputs land, build only the minimal image your first functional tests need. Everything else in the reply
stands.
