# WORKER5 questions

For the owner, with the documents (`v1.4_wip/planning/AXI-INTERFACE.md`, `CACHE-INTERFACE.md`). Each has what I did
in the documents meanwhile (†), so nothing waits on an answer.

## Q1. An LPDDR error on a Tracer's read cannot be told over `fill` as v1.36 has it

**The evidence.** `fill`'s read response has a 2-bit status, and all four values are used: `ok`, `hole`,
`not_owned`, `beyond` (`v1.36/spec/machine-spec.md` 3; `spec/jolt/tracer.md` 17.9.5). v1.36's model never fails a
read, so the question did not arise. A real cache fetches the Tracer's lines, and the index lines on their path,
from LPDDR, and AXI can answer any of them with an error. The cache then has a line it cannot give.

**What the documents do †:** the read is answered `beyond`. The Tracer sets `ERRORS.page_missing` and stops, so the
proof fails visibly; and the cache's own error flag is set (Q2), which says why.

**The alternative:** a third bit of status, `lpddr_error`, on `read_response`. That is a change to the Tracer, which is
built: a request to Tony (`RTL-CHANGE-REQUESTS.md`), not a change here.

## Q2. Where an LPDDR error with no client to tell goes

**The evidence.** The Tracer's writes stay dirty in the cache (third note, section 3) and get `ok` at once. When such a
line is evicted, its write-back can be answered with an error long after; nobody is waiting for it. Q1's case is
another.

**What the documents do †:** the cache has an error flag, sticky until the reset, an output level like a peripheral's
`ERRORS` ≠ 0, which on the chip joins the memory side's error line to the master tile (machine-spec 18), and so the
toast pin. The line is lost, as 17.9.5 says of a write-back's `error`.

**The question:** whether the cache is a unit with an error line of its own, or reports through a peripheral's
registers (second note, section 3, open: whether the cache is a peripheral of its own).

## Q3. Who sends a discard

**The evidence.** The notes propose that the file system discards a page when it hands it out (second note, section
3; third note, section 2). In v1.37 the master tile's core hands out the pages, into the Updater's free page queue,
and it has no look-up port. v1.37 frees no page, so no page is handed out twice and no discard is needed yet.

**What the documents do †:** a page is used only after its discard's reply (`CACHE-INTERFACE.md`, section 3); the
Updater sends no discard; in v1.37 nobody does.

**For v1.4, the choice:** a look-up port for the file system's core (registers in its window), or the Updater
discarding each page as it takes it from the free page queue (about 32 cycles of tag look-ups a page, beside a page
write's 300 or so). The second needs no new port, but covers only the Updater's pages; the RW engine's would need the
same.
