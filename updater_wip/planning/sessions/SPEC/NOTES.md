# The v1.37 specification session: notes

*The outline for the owner's go-ahead (2026-11-18), the list of daggers and silences, the name choices, and what I
found wrong or contradictory in the planning documents. Branch `v137-spec`. Abbreviations for the sources: II
`INGRESS-INTERFACE.md`, HL `HOST-LINK.md`, HF `HOST-FRAMES.md`, UI `UPDATER-INTERFACE.md`, CI `CACHE-INTERFACE.md`,
AI `AXI-INTERFACE.md`, RCR `RTL-CHANGE-REQUESTS.md`, DN14/15/16 the design notes, MS `MILESTONE-v1.37.md`, HW
WORKER8's `HARDWARE.md` (branch `updater-w8`).*

**Status (2026-11-18, 05:26): the outline has the owner's go-ahead; class B is accepted as a block; S1, S2 and S3
are ruled** (section 8). Writing in section 3's order; the remaining class A daggers and silences go to the owner one
at a time.

**The owner's rulings of 05:26, in short:**
- **S1:** one reset net, as v1.36; it reaches the link and the host-side state too, so II-16 falls away. Error
  recovery is not designed in v1.37: the text states what raises each error and that it stops the chip; everything
  after (what the host does, draining the queues, a ready frame, S6, II-18) is one open item.
- **S2:** the memory system (the cache, the AXI arbiter and the `lpddr` port) is the memory tile's peripheral 1, at
  its minimum: it exists to put the cache's error on the tile's error line. The RW engine moves to peripheral 2: its
  new window and region are a request to Tony and an entry in `FOR-THE-SIMULATOR.md`. The hardware keeps v1.36's RW
  engine; nothing in v1.37's proof runs uses it.
- **S3:** the links between the special tiles stay in, specified from RCR 1; what RCR 1 leaves unsaid (the ports,
  the routing rule, the flow control) comes to the owner as questions.

## 1. The documents and where each thing goes

v1.36's structure is kept: `machine-spec.md` with chapters 1 to 27 and Appendix A, `core.md`, `jolt/`,
`schedule.md`, the overview, the guidelines, a guide from the last version. The numbering rule (GUIDELINES 8) is
kept: a section removed or moved leaves a one-line stub, and item and invariant numbers are never reused.

| document | what it holds in v1.37 | v1.36's text |
|---|---|---|
| `machine-spec.md` | the platform, as now; its preamble says which parts are the RTL's, which are meshsim's design not yet built, and which wait on Tony | edited in place (section 2 below) |
| `machine-spec.md` 17.6 | **Ingress/Egress, rewritten:** the control lines, the incoming queues, the DMA, the forward count, the word port, the service flags, Egress, the UART, its reset, its errors. The link inside it is chapter 28 | replaced; v1.36's text is in the frozen `v1.36/` |
| `machine-spec.md` 17.10 (new) | **the Updater,** the master tile's peripheral 2, type 5. Platform, not Jolt: it applies update words to files and knows nothing of Jolt | new |
| `machine-spec.md` 17.11 (new); 17.7 | **the memory system, the memory tile's peripheral 1** (S2): the minimum, a window whose `ERRORS` carries the cache's error; the hardware is chapter 23. **17.7, the RW engine, becomes peripheral 2:** window 0xFFFF_FC00, region 0x05, *asked of the RTL* | new; 17.7 edited |
| `machine-spec.md` 23 | **the memory system, now specified:** the cache's controller and memory, the AXI arbiter, the LPDDR controller's port and the simulator's model of LPDDR; what is kept of the memory tile's requests; the file system still deferred | see the table below |
| `host-link.md` (new), chapter 28 | **the host link:** the link packet and its header, the two checks, the mask, the CRC units, a bad packet, recovery, starting a link again, "stop", what a sender keeps, the chain, **and the link end as hardware** to HW's depth: every register with its width, the receiver's and the sender's states and the link's mode, the forwarding buffers, the chain node, what hardware spreads over cycles. `machine-spec.md` keeps a stub | new |
| `host-frames.md` (new), chapter 29 | **the frames:** the frame header, update words and their LRU coding, *N* and *s*, frames 1 to 9, and the reply frame. Stub in `machine-spec.md` | new |
| `master-firmware.md` (new), chapter 30 | **the master tile's firmware, as a contract:** the boot, each frame, the Updater's runs and the free page queue, the hand-over to the tracer tile, the segment's end and the reply, an error. Stub in `machine-spec.md` | new; 22.27 and 17.9.15 point to it |
| `jolt/tracer.md` | 17.9.5 (the fill port's other side is now 23.6), 17.9.10 and 17.9.15 (the master tile delivers the segment), 17.9.14 (the model behind the fill port: a stub to 23.6) | edited, Tony's 17.9–17.9.12 only where the other side changed |
| `jolt/replayer.md`, `replayer-opcodes.md`, `core.md`, `schedule.md` | unchanged | kept |
| `OVERVIEW.md` | brought to v1.37: the host link, the Updater on the master tile, the memory system, the chain; status | rewritten sections 2, 4, 5 |
| `README.md` | the list of documents | edited |
| `GUIDELINES.md` | touched only where v1.37 changes a rule (section 6, silence S2, and the "no load from 32 up acts" rule II states); each change put to the owner | edited, little |
| `CHANGES-v1.36-to-v1.37.md` (new) | the guide: in brief; unit by unit what changed, each marked whether it touches the RTL; the requests to Tony; the name table; where v1.36's sections went | new |
| `sessions/SPEC/FOR-THE-SIMULATOR.md` | every place where `main`'s simulator or firmware differs from the specification | new, kept as I go |

**Why three new documents and not chapters in `machine-spec.md`:** each is as long as the Tracer's chapter, the
link and the frames are read by people who never read the tile (the FPGA's and the host's side), and the
firmware's contract is software, which the hardware chapters keep apart (chapter 22). Their sections are
numbered 28.*x*, 29.*x*, 30.*x*, so a citation looks like every other.

**Why Ingress/Egress and the Updater stay in `machine-spec.md`:** they are platform peripherals, as 17.6 and 17.7
are; only `jolt/` is split out.

### Chapter 23, section by section

v1.36's 23.1–23.9 keep their numbers. 23.6, "the memory system behind the fill port (Open)", is the section the
cache replaces in place; the arbiter and LPDDR are added after 23.9.

| section | v1.37 | v1.36's |
|---|---|---|
| 23 (intro) | the memory side: who its clients are, the figure in words; the file system still deferred | rewritten |
| 23.1 Where it sits | the memory tile, the cache and the arbiter, the LPDDR controller; the Updater's two ports reaching across from the master tile | rewritten |
| 23.2–23.4 | the memory tile's requests, reads and writes through the RW engine, which reaches LPDDR through the arbiter | kept, edited |
| 23.5 The file system | **what the hardware now fixes:** a file, an entry and its owned bit, the hole, levels counted from the top, the path, a root, a line; the rest deferred | edited |
| 23.6 The cache | **replaced:** 23.6.1 its parts and memories; 23.6.2 the look-up port (every signal and width); 23.6.3 look up, update, discard, every reply in every case; 23.6.4 the fill port's service; 23.6.5 the controller's parts and states, from the code (front end, walker, miss handler, AXI writer, update slot, page sequencer), with what moves between them; 23.6.6 an update's sequence and what it guarantees; 23.6.7 timing; 23.6.8 what clients keep; 23.6.9 errors; 23.6.10 reset; 23.6.11 the simulator's checks and parameters | replaced |
| 23.7–23.9 | errors, pacing, what a program keeps: extended | kept, edited |
| 23.10 (new) | a client's AXI port: every signal and width, a burst's rules, the sizes in use, IDs, what a client may count on | new |
| 23.11 (new) | the AXI arbiter: pieces, turns, its registers and what it keeps per client, timing, reset, its checks | new |
| 23.12 (new) | the LPDDR controller's port (AXI4 with IDs), its rules; the simulator's model of LPDDR, its timing, modes and faults, as the simulator's parameters (19.1), not contract | new |
| 23.13 (new) | the memory system's checks and parameters: the arbiter's and the model's (the cache's own are 23.6.11) | new |

### The depth each new component gets

The Tracer's chapter is the measure (owner). For each of Ingress/Egress, the Updater, the cache, the arbiter and
the link:

| | 17.6 Ingress/Egress | 17.10 Updater | 23.6 cache | 23.11 arbiter | 28 link |
|---|---|---|---|---|---|
| words, bits, ports with every signal and width | yes | yes | yes | yes | yes |
| regions, sizes, ownership | outgoing queue | free page queue | — | — | — |
| what `START` does | UB | a run | — | — | — |
| states and transitions | the service level, the forward count | from `updater_ctl.h` | from `cache_ctl.*` | from `axi_arbiter.*` | from `link_end.*`, `node.*` |
| behaviour to the cycle | at the registers and the word port | at its ports | at its ports | at its ports | on the wire, in byte times |
| memory side | the word port | AXI client 1, look-up port 1 | AXI client 0 | the controller's port | — |
| timing, statistics | yes | its six counters | the figures as a guide | yes | the protocol's constants and figures |
| errors, what software keeps, reset | yes | yes | yes | yes | yes |
| the simulator's checks and parameters | yes | yes | yes | yes | yes |

**For the three pieces of code that are the authority** (the cache's controller, the Updater's controller,
WORKER8's link end), I read the code state by state as I write, and where I cannot tell what it does in a case I
run its tests or write one in this folder. Where the code does in one step what hardware spreads over cycles, the
text says what hardware does (HW section 7 for the link).

## 2. v1.36's text: kept, moved or replaced

| v1.36 | in v1.37 |
|---|---|
| preamble, 1.1 terms | preamble: which parts are the RTL's, which are designed here and not built, which wait on Tony (a marker, *Asked of the RTL*). 1.1: the new terms (section 5 below) |
| 2.1 tile kinds | the master tile's peripheral 2, the Updater; "the fill link" paragraph: the cache serves it (23.6) |
| 2.2, 2.3 | the hierarchy gains `updater_unit` and the memory system's modules (silence S2 decides where) |
| 3 interfaces | `lvds_in`, `lvds_out` removed; added: the link's byte ports (S4), `word_port`, the look-up port and reply bus, a client's AXI port; `lpddr` becomes the arbiter's port to the controller, with IDs; `fill`'s other side is the cache |
| 5.5 register map | the two master-tile windows |
| 6.5 boot ROM | unchanged but the *Open* on the master tile's boot over LVDS, which stays open (item 166, narrowed) |
| 16 router | 16.4: the master tile's peripheral now has a DMA. The links between special tiles: S3 |
| 17.1 | type 5, the Updater; the live-bit sentence |
| 17.4 | "Ingress/Egress has no DMA" goes |
| 17.6 | replaced |
| 17.7 the RW engine | unchanged hardware; its `lpddr` port is now client 2 of the arbiter, so "the chip's one path to LPDDR" goes |
| 17.10 | new |
| 18 nets and reset | the chain pin; the link's pins in place of the LVDS stream; what a reset reaches (S1) |
| 19 parameters | `STREAM_BYTES`, `STREAM_DEPTH`, `UART_RESET_DIVISOR` go; the queues', the link's, the Updater's, the cache's and the arbiter's added; 19.1 gains the LPDDR model's |
| 20 invariants | new numbers for the new UB and checks; none reused |
| 21 timing | 21.5 extended; new rows for the cache, the arbiter, the link |
| 22.27, 22.28 | point to chapter 30; the error end to end gains `link_error` and the reset |
| 23 | the table above |
| 24 budgets | the link (HW: 1,424 flip-flops, 77,312 bytes; 110,080 with the two queues), Egress's queue, the Updater (its page memory, its input, the decoder), the cache (512 KiB and its tags), the arbiter |
| 25 | unchanged |
| 26 open items | new items from 178 for every dagger left open, every silence not ruled, and the requests to Tony; items closed or narrowed: 60, 119 (part), 124 (informed), 166 (narrowed), 172 (superseded by the UART's redesign) |
| 27, Appendix A | unchanged |

## 3. The order I will write in

1. **Terms and the name table** (1.1 and the guide's table): every later section uses them.
2. **23.10–23.12, the AXI port, the arbiter and LPDDR,** against `axi_port.h`, `axi_arbiter.*`, `lpddr_model.*`.
3. **23.5 and 23.6, files and the cache,** against `lookup_port.h`, `cache_ctl.*`, `cache_system.h`.
4. **17.10, the Updater,** against `updater_ctl.h`, `updater_top.h`, `word_port.h`.
5. **Chapter 28, the host link,** against `link_end.*`, `node.*`, `header.h`, `crc32c.h` on `updater-w8`; every
   worked example (the CRCs, the four headers, the mask's bytes, the restart packet's checksum, 0x48674BC7,
   0x641F6454) computed again by a program of mine in this folder.
6. **17.6, Ingress/Egress,** from II and HL (the design leads; the black box is behind).
7. **Chapter 29, the frames,** and **chapter 30, the firmware's contract.**
8. **The gathering chapters:** 2, 3, 5.5, 16.4, 17.1–17.5, 17.7, `jolt/tracer.md`, 18, 19, 20, 21, 22, 24, 26.
9. **`OVERVIEW.md`, `README.md`, `GUIDELINES.md`, `CHANGES-v1.36-to-v1.37.md`.**
10. **`FOR-THE-SIMULATOR.md`,** written as I go and finished last.

A commit and a push after each section; a few lines to the owner when a chapter is done; `main` merged in often.
The daggers go to the owner one at a time from the start, those a program or the RTL sees first (section 4's
order), and the writing goes on around them.

## 4. The daggers

**How they end.** Each ends ruled, an open item, or waiting on Tony. I have sorted them into two kinds:

- **A, seen by a program or the RTL from outside:** a register, a bit, a size, a rule a program keeps. One at a
  time to the owner.
- **B, inside one of the three machines whose code is the authority** (the cache's controller, the Updater's
  controller, the link end), or a value only the simulator's model has. Rule 2 of the brief already says the
  specification states what the code does. **I propose the owner accept class B as a block:** each is stated as
  the code has it, a tunable value becomes a named parameter, and those the RTL may want to change get one open
  item each. If the owner would rather see them one by one, they go after class A.

**Some daggers are already ruled** and the documents were not brought up to it; they are marked R below with the
ruling's place, and need no question.

The cost column: what it would cost to change later, if the specification says it now and it is changed after the
simulator, the firmware and the RTL follow.

### II, Ingress/Egress

| # | dagger | kind | recommendation | cost to change later |
|---|---|---|---|---|
| II-1 | the UART's fields in `STATUS`: [12:8] TX bytes, [20:16] RX bytes, [24] `rx_overrun`, [25] `framing_error` | A | accept: v1.36's `UART_STATUS` layout ([4:0], [12:8], [16], [17]) a byte higher, its fields on byte boundaries | a header and every image that prints; the RTL's UART (already a request) |
| II-2 | `ERRORS` [1] `link_error`, [2] `format_error`, [3] `egress_error` | A | accept: packed from bit 1. Kinds: `link_error` and `format_error` H, `egress_error` P (silence S10) | the header, the handler's decoding |
| II-3 | `UART_CONTROL` at word 42 | A | accept: the first word v1.36's UART frees | the header |
| II-4 | a store of the forward count while a background DMA transfer is in flight is UB | R | ruled, DN16 section 24 | — |
| II-5 | a store replaces the count; 0 stops it | R | ruled, DN16 section 24 | — |
| II-6 | a load gives what remains; 0 is exact | R | ruled, DN16 section 24 | — |
| II-7 | as built: a cycle works from the registers at its start; `discard` stays when the count reaches 0, and a load reads both fields; a store takes effect as any window store | R / mine | `discard` staying is ruled (DN16 section 24); the rest is 6.4's store timing, which the text states | — |
| II-8 | the service rule: a queue asks while it holds 128-bit words and no DMA transfer from it is in flight (and for the background, the count is 0); the flag is posted when the level rises; a transfer of 0 words counts | A | accept, as built and as one level (WORKER7). DN16 section 34 says the review "reached the service flags": if the owner ruled them then, this is R | firmware's wake loop |
| II-9 | `DMA_HDR.queue`: 0 the foreground queue, 1 the background | A | accept | the header, firmware |
| II-10 | a DMA count beyond the words in the queue is UB | A | accept: simulation catches it, so UB (GUIDELINES 2) | small |
| II-11 | the outgoing queue: 8 KiB, 512 128-bit words; its region 0x04 on the master tile, part 1 by offset [22:20] | A | accept both | the queue's SRAM; the firmware's address |
| II-12 | `EGRESS_COUNT` counts down as words land, the last one marked; a store while it is not 0 is UB; a load gives what remains | A | accept: the core can queue the next frame without waiting for the link | Egress's logic, firmware |
| II-13 | with enable write 0 a store to `UART_TX` is dropped, so it cannot stall for ever | A | accept | the RTL's UART (in the request) |
| II-14 | a non-zero `UART_CONTROL` with a divider of 0 is UB; a new divider takes effect at each direction's next idle point | A | accept: v1.36's rule kept | small |
| II-15 | while the UART is in reset, `uart_rx` and `uart_tx` read 0, a store to `UART_TX` is dropped, `UART_RX` reads 0 | A | accept | the RTL's UART |
| II-16 | the host-side state (both incoming queues, the forward count, the outgoing queue, `EGRESS_COUNT`, the credits) is cleared by power-on only, and nothing short of it empties them | A | accept, with silence S1, which it depends on | the reset tree |
| II-17 | one `link_error` bit for both of an ASIC's links | A | accept one bit; which link and why become two status bits only if bring-up wants them (an open item) | a bit and the handler |
| II-18 | after an error the boot firmware reads what is left in the foreground queue out with the DMA and drops it | A | accept: the discard is the background's alone. With S6 | firmware only |
| II-19 | the black box as built: zeros between packets idle; a packet of length 0 takes no number; the flags, the acknowledge field and hops ignored; the end of the file no error | R | superseded by HL as ruled; the black box's behaviour goes to `FOR-THE-SIMULATOR.md`, not the specification | — |

### HL, the link

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| HL-1 | a forwarding buffer for each stream and direction, 16 KiB | A | accept: the owner has said the size is not a concern; 512 128-bit words is exactly the rule's limit | 34 KiB of SRAM if halved later |
| HL-2 | the header is held until the pass over the data is done; the restart packet has no pass; a packet of length 0 may go during a pass | B | as the code | — |
| HL-3 | the header's CRC while a pass is running: the pass waits a cycle, or the CRC has its own logic | A, for Tony | **state the requirement, not the choice:** the header CRC is on the wire by the header's byte 12, and no rule depends on the cycle. Then it is the RTL's to choose and needs no ruling | none |
| HL-4 | the pass starts on the packet that goes next, and starts over when that changes | B | as the code | — |
| HL-5 | a background packet with data that comes from below | A | **as the code: it is forwarded,** through the fourth forwarding buffer; an open item for making it a packet made wrong, which saves 17 KiB but changes the searched code | 17 KiB, and a search |
| HL-6 | the name and place of the error a restarted link raises | A | `ERRORS.link_error` in Ingress/Egress: the same as II-2 and II-17 | — |

### UI, the Updater

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| UI-1 | the master tile's peripheral 2: window 0xFFFF_FC00, region 0x05 | A | accept | the header, the RTL's tile instance |
| UI-2 | type 5 | A | accept: the first free type | the header |
| UI-3 | `EVENTS` [2] unused, `free_page_credit` at [3] | A | **pack now:** `free_page_credit` at [2]. Nothing is built in the RTL; the firmware changes one constant | a constant |
| UI-4 | `ERRORS` [4] `not_owned` | A | accept, kind H (P on a bad file) | — |
| UI-5 | `WORD_COUNT` counts down and the run ends at 0 | B | as the code | — |
| UI-6 | window words, not a state record | A | accept | — |
| UI-7 | `RUN_CONFIG.root` [6:5] and `SOURCE_ROOT` at word 37: the Updater makes a root, while there is no file system | A | **specify it in v1.37 as is,** with an open item: whether the file system takes it over in v1.4 | a field and a word |
| UI-8 | the statistics, block 3, words 48–53, and their definitions | B | as the code (WORKER6's definitions) | — |
| UI-9 | memory segments as three runs | A | accept: the Tracer starts at the second run's `done`, and the third overlaps the trace entirely (WORKER3) | firmware only |
| UI-10 | what the Updater keeps behind the word port | B | as the code: four 128-bit words, `ready` a flip-flop, invisible to a program | — |
| UI-11 | `ready` does not depend on `busy` | B | as the code | — |
| UI-12 | a free page queue entry: the page's address in 128-byte units, in [31:0] | A | accept; an open item for "one page or four" | the queue's width |
| UI-13 | the region's parts other than 2 unmapped | A | accept | — |

### CI, the cache

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| CI-1 | depth 0 answered `beyond` | B | as the code | — |
| CI-2 | `op` 3 reserved, UB | A | accept | — |
| CI-3 | a discard's root a multiple of 32, bits [4:0] ignored | A | accept | — |
| CI-4 | four look-up ports: 0 spare, 1 the Updater, 2 the RW engine, 3 the Binder | A | **`LOOKUP_PORTS` = 4, port 1 the Updater, the others tied off:** in v1.37 the RW engine is v1.36's and has no look-up port (CI and the brief disagree here, section 7) | a port's wiring |
| CI-5 | an update's reply carries the entry as it was before | B | as the code | — |
| CI-6 | an owned index page changes only by `update` (a client's rule) | A | accept, as a rule a client keeps; an invariant the simulator checks | — |
| CI-7 | a page is used only after its discard's reply | A | accept as the rule; in v1.37 nobody discards; who discards is an open item with the file system | — |
| CI-8 | an LPDDR error on a fill read is answered `beyond`, and the cache's error is set | A | accept; RCR comment E stays a comment to Tony, not a request | — |
| CI-9 | replacement: the least recently used of the ways that may be evicted | B | as the code | — |
| CI-10 | empty at the reset only | A | accept: every line it holds is current | — |
| CI-11 | "being filled" as a state; a discard of a way being filled | B | as the code | — |
| CI-12 | the cache's error flag: sticky until the reset, an output level, the memory side's error line | A | **with silence S2** | — |
| CI-13 | the front end: round robin among the sources waiting | B | as the code | — |

### AI, the AXI arbiter and LPDDR

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| AI-1 | whole beats only: `wstrb` a constant | A | accept | — |
| AI-2 | one address space over both LPDDR4X channels | A | accept | the controller's configuration |
| AI-3 | a line aligned to 128 bytes, a half page to 2 KiB | A | accept | — |
| AI-4 | `ID_BITS` 2, one ID a client | A | accept | the controller's port |
| AI-5 | `CLIENT_BURSTS` 4 a direction | B | as the code, a parameter | — |
| AI-6 | a write's beats back to back once its address is raised (a client's rule) | A | accept, as a rule a client keeps | — |
| AI-7 | the clients, the arbiter and the controller's port are reset together | A | accept, with S1 | — |
| AI-8 | `TURN_BEATS` 32 | B | as the code, a parameter | — |
| AI-9 | turns exactly: one AR piece a cycle; one write piece at a time | B | as the code | — |
| AI-10 | every output of the arbiter a flip-flop, a cycle each way | B | as the code; an open item whether the RTL needs it | — |
| AI-11 | the model: an unwritten read returns zeros and fails the run; when the memory changes; `read_latency` and `write_latency` 70, 16 bursts each way; round robin among IDs that are due; the jitter mode; a fault's response; `DECERR` past the end | B | the simulator's model, in 19.1 as its parameters, not contract (GUIDELINES 7) | — |

### HF, the frames

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| HF-1 | frame 1, the program image header, and the program image as frames 1 and 2 | A | accept: built in gold and the firmware | gold and the firmware |
| HF-2 | a base jump table as frames 4 and 5 with a path | A | accept | — |
| HF-3 | the type numbers 1 to 9, in the header's [95:64] | A | accept | — |
| HF-4 | a string: a u32 length, then the bytes, padded to 32-bit words | A | accept | — |
| HF-5 | frame 4's path field | A | accept; which table a base names without a file system is an open item (HF's own) | — |
| HF-6 | a segment's rows in no frame: 2²² by definition | A | accept: a constant of the frames chapter | a frame field |
| HF-7 | gold builds the final table; the chip inserts nothing | A | accept | — |
| HF-8 | added entries must fit the base's probe, else a full table is sent | A | accept, as the host's rule | — |
| HF-9 | frame 8's counts padded to a whole 128-bit word when *N* is not a multiple of 4 | A | accept | — |

### MS and DN16, the milestone and the notes

| # | dagger | kind | recommendation | cost |
|---|---|---|---|---|
| MS-1 | **the reply frame:** the Tracer's end-state record with a few counts; a new type on the foreground, 16; 72 128-bit words; its words 1 to 3 | A | accept the content; the type number and the words' layout to be fixed in chapter 29, put to the owner field by field | the driver, the firmware |
| MS-2 | the tracer tile sends its last record to the master tile with its result; the master does not reach into the tracer tile | A | accept: it keeps the tracer tile's program the only one that knows its layout | firmware |
| MS-3 | the stream words out and the UART redone inside v1.37 or after | R | ruled by the brief: 17.6 has the old words gone and the UART redesigned | — |
| MS-4 | *s* is not the client number; the driver; its language; the wire's delay as the sync window | — | test equipment, not specification (the brief) | — |

## 5. The silences

What a builder needs and no source states. Each is a question like a dagger; my recommendation with each.

| # | silence | recommendation | why it matters first |
|---|---|---|---|
| S1 | **Two resets.** II clears the link and the host's queues at power-on only, and the UART at "the reset". v1.36 has one reset net, "as at power-on" (18; item 124). | **Two nets:** power-on, which reaches everything; and the chip's reset, which reaches everything but Ingress/Egress's link and its host-side state (II-16). The host uses the second after an error. Item 124's soft reset is a third thing, still open | the RTL's reset tree; what the host does after an error |
| S2 | **Where the cache and the arbiter sit, and how the cache's error reaches the toast pin.** GUIDELINES 3: every chip-level function lives in a peripheral. The cache has no window in any source. | **The memory tile's peripheral 2, "the memory system", type 6:** a window with `PARAMS`, `STATUS` and `ERRORS` ([0] `lpddr_error`, the cache's flag), no `START`, no DMA, no region; it holds the cache, the arbiter and the `lpddr` port. Its error reaches the master tile by the memory tile's own error line, with no new net. The alternative: a chip-level block with a new error line, an `ERROR_LINES` bit of its own | the module hierarchy (2.3); the RTL's floorplan question (RCR D) |
| S3 | **The links between the special tiles** (RCR 1, "agreed in principle"). Their reason was the Updater's stream crossing the mesh; with the Updater on the master tile nothing of v1.37 needs them | **leave them out of v1.37,** and close RCR 1 as not needed; an open item if the file system's traffic wants them | the router (16), and a request standing at Tony |
| S4 | **The link at the chip's edge.** v1.36's `lvds_in`/`lvds_out` were AXI4-Stream from an LVDS IO block. The link end is a byte a cycle each way, idle zero, and its timers count those cycles. Nothing says what clock that is | **The link ends run on the link's byte clock,** a byte every cycle by definition, and the protocol's constants are in byte times; the incoming queues and the forwarding buffers cross to the chip's clock. The chip's ports: a byte in and a byte out for each of the two links, and the pin. The alternative, the chip's clock with a byte strobe, changes what a timer counts | the RTL's clocking of Ingress/Egress |
| S5 | **How an ASIC learns *N* and *s*.** "System configuration, told at start-up" (HF); as built, two words placed with the master's image | **arguments of the master tile's image** (6.5's "arguments"), at an address the image fixes; how the image arrives stays item 166 | the firmware's contract |
| S6 | **"Says it is ready."** After an error the host resets the chip, and the boot firmware drops what is left and "says it is ready" (II). No frame says it, and nothing tells the host to stop sending the old proof | **a ready frame** on the foreground, the reply frame's header with a status code and no record, sent at the end of the boot; the host sends nothing new until it has it. Or an open item with the foreground stream | the boot's contract; the host's side |
| S7 | **What no frame carries:** the compute tiles' and the tracer tile's images, the replayer's and the Tracer's tables and templates, the master's own image | an open item, with item 166 (the master tile's boot over LVDS); v1.37 places them before the reset, as the simulator does | the firmware's contract says where they come from |
| S8 | **Ingress/Egress's and the Updater's `PARAMS`:** the version and the constants field | Ingress/Egress version 1 (its words changed); constants 0 on both | the header |
| S9 | **Interfaces' widths and timing at the master tile's edge:** the Updater's AXI and look-up ports cross from the master tile to the memory side (RCR D) | state them as interfaces with every output a flip-flop and no latency promised (the Updater counts on none, owner); the placement stays Tony's | chapter 3 |
| S10 | **The kinds (P or H, 7.4) of every new error bit** | as in the tables above; listed for the owner with II-2 | the handler's report |
| S11 | **The Updater checking a root's and a free page's address** before use: as built it sets `lpddr_error` before any burst | **UB, not a check:** simulation catches a bad address on our firmware (GUIDELINES 2). The model's check stays a simulator check | a comparator in the RTL |
| S12 | **A `START` during an Updater run** (UI, Open) | UB, as on every peripheral (17.2): no question | — |
| S13 | **The cache's two steps** (`hits_under_miss`) | the specification states step 2, the code's default; step 1 is a simulator setting | — |
| S14 | **A neighbour that goes silent** (HL, left open by the owner) | an open item, with the measured figures | — |

**Where I expect more:** the firmware's contract at the hand-over and on an error, and the errors' exact effects on
what is in flight. They come to the owner as I reach them.

## 6. Names

One name for one thing. Where the sources use another, the specification's wins and the other goes in the guide's
name table.

| thing | the sources | the specification | why |
|---|---|---|---|
| 32 bits | "a word" in the spec | **a word** | the specification's |
| 128 bits on the link and in Ingress/Egress's queues | "a word" (II, HL, HF) | **a 128-bit word** in the text; **"words" in register and field names** (`FOREGROUND_WORDS`, `BACKGROUND_WORDS`, a link packet's `length`) | the Tracer's and the replayer's usage (owner, 2026-11-18). "Word" alone stays 32 bits |
| a beat | | **chapter 1's meaning:** one item a handshake moves on a link of the request tree or an AXI port (the host-interface port leaves 1.1's row) | owner, 2026-11-18: not 128 bits in general |
| 64 bits of an update | "an update word", "a 64-bit word" | **an update word** | a defined term |
| the link's packet | "a packet" (HL) | **a link packet** | "a packet" is the NoC's |
| a frame from the host | "a frame" | **a frame** | |
| a UART character | "a frame" (17.6) | **a character** | "frame" is the host's |
| Ingress/Egress | "IEU", "Ingress", "Egress" | **Ingress/Egress**; Ingress and Egress for its two directions | the specification's |
| the arbiter | "the bus driver", "the splitter" | **the AXI arbiter** | AI |
| the cache's two parts | "the cache box", "the cache controller" | **the cache's controller, the cache memory** | CI |
| a link end, a chain node | `End`, `Node` | **a link end, a chain node** | |
| the pin | "the pin", "another ASIC follows" | **`chain_next`** | needs a name in 18; mine, unless the owner has one |
| the master, memory and tracer tiles | C&C 1, 2, 3 | as v1.36 | |
| KiB | "KB" | **KiB** | the specification's sizes are binary |

## 7. What I found wrong or contradictory in the planning documents

The specification supersedes them; I do not edit them.

1. **II still marks † the forward count's rules** that DN16 section 24 records as accepted by the owner (II-4 to
   II-7).
2. **Whether the service flags were ruled is unclear:** DN16 section 34 says the review "reached the service
   flags" while II still marks them †.
3. **CI's port 2 is "the RW engine, for the file system",** but v1.36's RW engine has no look-up port and the brief
   keeps it unchanged.
4. **RCR 1's reason is gone** (S3), and the request still stands as "agreed in principle".
5. **HF's "goes to" column** names C&C 2 and C&C 3, and cites v1.35's 4.21.2 for the record.
6. **GUIDELINES 3,** "every chip-level function lives in a peripheral", is broken by a cache and an arbiter in no
   peripheral (S2).
7. **"Word" means 128 bits** in II, HL and HF, and 32 bits in the specification: the text says "a 128-bit word", as the Tracer's does (section 6).
8. **MS's table says the memory tile has "no RW engine"** in v1.37; the brief keeps v1.36's. The milestone means
   the simulation runs nothing there; the hardware keeps it.
9. **The figure `fig_m1_lpddr_path.svg`** still has the Updater by the RW engine (known; not mine to redo).

More will go here as I check the text against the code.

## 8. Tracking (filled in as the work goes)

| dagger or silence | how it ended | where |
|---|---|---|
| class B (HL-2, HL-4, UI-5, UI-8, UI-10, UI-11, CI-1, CI-5, CI-9, CI-11, CI-13, AI-5, AI-8, AI-9, AI-10, AI-11) | ruled as a block (owner, 2026-11-18): stated as the code has them; tunable values are named parameters | each section |
| S1, II-16 | ruled: one reset net, reaching the link and the host-side state; II-16 falls away | 18, 17.6 |
| S6, II-18, error recovery | ruled: not designed in v1.37; one open item for everything after an error stops the chip | 26 |
| S2, CI-12 | ruled: the memory system is the memory tile's peripheral 1, minimal; the RW engine moves to peripheral 2 (a request to Tony) | 17.7, 23, 2.1 |
| S3 | ruled: the links between the special tiles stay in; their ports, routing and flow control to the owner as questions | 16 |
| S3-1, S3-2: the line's ports and routing | ruled (owner, 05:36): **east and west, as the mesh:** master east ↔ memory west, memory east ↔ tracer west (east is id + 1). Between special tiles, the ring's rule on the difference of the ids. **A peripheral's DMA enters on the south input (the +8 port) on every special tile;** the RTL has it on west for the memory and tracer tiles: asked of the RTL, and in `FOR-THE-SIMULATOR.md` | 16 |
| names: 128 bits | ruled (owner, 2026-11-18): "a 128-bit word" in the text, "words" in names; "beat" keeps chapter 1's meaning | section 6 |

## 9. New open items (chapter 26), numbered as they are made

| item | what | where |
| --- | --- | --- |
| 178 | a flush of the Tracer's dirty lines, at a trace's end or at shutdown: nothing reads them today | 23.6.3 |
| 179 | the cache writing a set's dirty least recently used line back early, in place (WORKER5's recommendation) | 23.6.7 |
| 180 | who discards a page when the file system hands it out again | 23.6.8 |
| 183 | a background link packet with data from below (HL-5; owner: as written) | 28.12 |
| 184 | a neighbour that goes silent | 28.14 |
| 185 | the foreground stream's frames but the reply | 28.2 |
| 186 | the link ends' clock against the chip's (S4; owner: as written) | 28.1 |
| 187 | a base table by name, or the last established (HF) | 29.8 |
| 188 | frame 9 past 2^26 words: continuation frames (HF) | 29.13 |
| 189 | an Updater frame with its own head (HF, DN16 section 10) | 29.1 |
| 190 | what no frame carries: tables, templates, tiles' images (S7) | 29.1, 30 |
| 191 | whether the file system takes over making roots (UI-7) | 17.10.1 |
| 182 | flow control on the line between the special tiles, with the regime that needs it: the file system's | 16.3 |
| 181 | everything after an error stops the chip: the host's reset, draining the queues, a ready frame (S1, S6, II-18) | 23.7, 30 |

## 10. Class A daggers and new questions, in the order they go to the owner

| order | question | status |
| --- | --- | --- |
| 1 | S3-1 the line's ports | ruled (east and west) |
| 2 | S3-2 routing | ruled with 1 (the ring's rule) |
| 3 | S3-3 the line's flow control | ruled (05:42): none in v1.37; the mesh's regime; item 182. RCR 1's wording corrected on `main` (DN16 section 36) |
| 4 | UI-2: the types | ruled (05:50): 5 the Updater, 6 the memory system |
| 5 | UI-3 | ruled (05:50): `free_page_credit` packed to `EVENTS` [2] |
| 6 | S11 | ruled (05:50): a bad root or free page address is UB; the check stays the simulator's |
| — | UI-1 | R: the brief's table has the Updater as the master tile's peripheral 2 |
| 7 | II-1 the UART's fields in STATUS | ruled (06:02): as proposed, [12:8], [20:16], [24], [25] |
| 8 | HL-1, HL-3, HL-5 (item 183), S4 (item 186), `chain_next` | ruled (06:02): as written |
| 9 | II-2 Ingress/Egress's ERRORS bits | ruled (06:06): as proposed |
| 10 | II-3, II-8 to II-15, S8 | ruled (06:06): as written |
| 11 | MS-1 the reply frame's fields | ruled (06:14): as recommended (29.14) |
| 12 | every dagger whose recommendation is a plain accept or "as written": UI-4, UI-6, UI-9, UI-12, UI-13, CI-2, CI-3, CI-6, CI-7, CI-8, CI-10, AI-1, AI-2, AI-3, AI-4, AI-6, AI-7, HF-1 to HF-9, II-17, MS-2, HL-6; and the silences S7, S9, S10, S12, S13, S14 as recommended | ruled (06:14): accepted as a block |
| 13 | S5: how an ASIC learns *N* and *s* | ruled (06:19): arguments of the master tile's image; item 166 |
| 14 | UI-7: `RUN_CONFIG.root` and `SOURCE_ROOT` | ruled (06:29): as built, a full part of the Updater; item 191 |
| 15 | CI-4: the cache's look-up ports | ruled (06:31): as written in 23.6.2 |
| — | reviews before any merge | owner (06:31): checklists for WORKER5, WORKER6, WORKER7, WORKER8 in this folder |
| 16 | the four reviews' answers | ruled (07:19), applied: WORKER5 list 1 (57d5d6d), its stale read a bug in the code, first in FOR-THE-SIMULATOR; WORKER6 list 1, the reach limits UB (8397c00); WORKER7 both lists, the firmware's frame checks a stop of its own in 30.8 (e6db0dc); WORKER8 list 1, the reset as measured in 28.14, the stale `link_error` under item 181, list 2 to FOR-THE-SIMULATOR. Not merged |
| 17 | an unknown frame type | ruled (07:44): a stop, one of the firmware's content checks (30.8); a known type's frame this ASIC has no use for is discarded (30.3) |
| to come, one at a time | anything new that changes hardware or working code | |

## 11. Found in the sources while writing

- **MS's reply frame says "length 72"** for 72 words that include the header word; the header's `length` counts the
  data alone, so 71 (29.14).
- **HF's frame 7 and the end state:** record *s* + 1 equals the Tracer's record at the segment's end in words 0 and
  2–65 only; its advice field is the next segment's count, and its word 66 has no roots (29.11).
