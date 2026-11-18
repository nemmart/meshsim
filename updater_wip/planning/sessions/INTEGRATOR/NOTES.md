# INTEGRATOR notes: v1.37 built to its specification

**Status (2026-11-18): the plan, for the owner's go-ahead. No code yet.** Branch `v137-build`, from `main` at
`973dfbb8`. The brief is `PROMPT.md` beside this file.

Read for it: everything `PROMPT.md` lists under "Read first", and of the specification `README.md`, `OVERVIEW.md`,
`GUIDELINES.md`, `CHANGES-v1.36-to-v1.37.md`, 16, 17.6, 17.7, 17.11, 18, 20 (35–40), 21.5, 28.1–28.2, 28.12,
28.14, 29.14 and 30.6–30.7. Each other chapter is read in full as its stage starts. A baseline: `make MODE=suite`
builds here and `test_master` passes, 34 of 34.

Abbreviations: **FTS** `sessions/SPEC/FOR-THE-SIMULATOR.md` (the worklist); **FFS** `sessions/INTEGRATOR/FOR-THE-SPEC.md`;
**the controllers** `cache_ctl.*`, `axi_arbiter.*`, `updater_ctl.h`; **the link end** `updater_wip/link/`; **the
wire** `updater_wip/wire/`. Paths without a folder are in `v1.37_wip/`. A dagger (†) marks a choice of mine the
owner may want otherwise.

## 0. How I will work

- **One branch, `v137-build`.** `main` merged in at the start of each stage and whenever it moves; never rebased.
  Each stage ends with its checks passing, its FTS rows closed with the commit, `REPORT.md` added to, a
  `git merge --no-ff` into `main`, a pull without rebasing, a push, and a few lines to the owner. Then the next
  stage, without waiting.
- **The checks here and on the runner.** This machine has 2 CPUs and 7 GB and no workloads' inputs: the units'
  tests, `test_master` (the five guests), the benches, `run_tests.sh quick --check` and the planted bugs run here.
  The 37 proof runs, `run_tests.sh all --check` and `make check` go to the runner, **three short items a stage at
  most** (`suites`, `suites-varied` where timing is varied, `check`), from `updater_wip/tasks/v137-check/`. I claim
  the numbers in Quest's `NUMBERS.md` and queue them myself, pulling first and never pushing while the runner has
  results it has not pushed. I do not touch 238 to 242 in `tasks/hold/`.
- **Reference cycles move only on purpose.** Each stage that changes timing (1, 2, 3, 4, and 5 by a little) moves
  them in one commit of its own: the golden logs and the proof runs' figures (`proof-numbers.md`), with why they
  moved, and a check that nothing outside the stage's reach moved (below, per stage). The runner's `suites` item
  of the stage before is the "before".
- **The specification is the authority** (`PROMPT.md`, "The authority"). Where it is wrong, unclear or silent: a
  row in FFS, and if it holds me up, one question to the owner at a time. I edit nothing in `spec/`.
- **The controllers and the link end** change only where an FTS row or a finding says so, and their own tests stay
  passing: `make check` in `updater_wip/link/`; `test_cache`, `test_axi_arbiter`, `test_lpddr_model`,
  `test_updater_ctl`. **The wire is used as it is.**
- **The firmware** is built with the pinned xPack toolchain and committed with its hashes. It is not on this
  machine: see Question 1.
- No token in any file (the clones' remotes carry it in `.git/config` only); no `pkill -f`; a clean `git status`
  at each turn's end.

## 1. The settings, and how each ends

**The specified machine is the machine** (`PROMPT.md`). What a stage builds is on by default when the stage ends;
a black box a state machine replaces stays only where a test compares against it.

| setting today | what it selects | how it ends | at |
|---|---|---|---|
| `MachConfig::host_link` (off by default) | v1.37's master tile: Ingress's black box with a packet file, the Updater | **gone:** every chip with the special tiles has v1.37's Ingress/Egress and Updater. With nothing on `link_up` the link reads zeros and the Updater is idle, so the RTL's images that use neither run as before | 3 (when v1.36's host interface goes) |
| `MachConfig::cache_ctl`, `--cache`, `CACHE_CTL` (off) | the Tracer on the cache's controller, a harness piece on its fill port | **gone:** the memory system is the memory tile's peripheral 1 in every chip, the Tracer's `fill` on it | 1 |
| `proof::Case::updater_ctl`, `--updater-ctl`, `MESHSIM_UPDATER_CTL` (off) | the Updater's controller on its test LPDDR and stand-in cache | **gone:** the controller is the Updater, on the arbiter's client 1 and the cache's look-up port 1 | 1 |
| the Updater's black box (`updater.*`) | | **kept for comparison only:** `ctl_bench`, `test_updater_ctl`'s runs against it, and `test_updater` (its own tests, which those comparisons rest on). Not buildable into a chip | 1 |
| v1.36's memory model (`mem_system.h`) | the Tracer's memory without `cache_ctl` | **kept for comparison only:** the Tracer-alone steps that compare with the RTL's figures or v1.36's logs (`tracer-block`, `suite_tracer`, `suite_memsys`, `tracer-block-synth`), as `tracer_block --fill model` and their benches. Not in a chip † | 1 |
| the test LPDDR and the stand-in cache (`tests/test_lpddr.h`, `standin_cache.h`) | | kept, for `test_updater_ctl` | |
| new: `MachConfig::lpddr_seed`, `--lpddr-seed S`, `MESHSIM_LPDDR_SEED` | the LPDDR model's jitter mode (0: fixed) | a test setting, off by default | 1 |
| the packet file (`--packets`), the black box's link reader (`host_link.h`) | Ingress's input | **gone as an input:** the chip's input is `link_up`, a byte each way a cycle. In one process the host's side is WORKER8's `End`, fed with gold's frames. `--packets FILE` stays as a check: the `End`'s data link packets against gold's file, byte for byte | 4 |
| `--link-rate B`, `--fast-link` | the link at B bytes a cycle, or as fast as taken | † **`--link-rate B`** clocks the link ends B link cycles a chip cycle (a test setting; the link's clock against the chip's is item 186); **`--fast-link` goes**, as a real link has no such mode. FFS | 4 |
| new: `--clients N --client k`, `--driver` | the chain, three processes on the wire | the milestone's runs; without them, one process | 6 |

**Machines without the special tiles** (the compute tiles' benches) are untouched by all of it. **A chip split over
threads** (`MachConfig::parts` > 1, the sort's runs and `make tsan-chip`): today `host_link` refuses it because the
Updater and the Tracer share one store. Once the memory system is always there, its three tiles' joints must be in
one part; I will keep the special tiles in one part (they are one row band's attach tiles today, to be checked), or
refuse the split only when a machine uses the memory side, whichever the framework allows without a change to its
synchronisation. Settled in stage 1 and recorded in "Choices".

## 2. The stages

For each: what is built, the FTS rows it closes, how it is checked, the planted bugs, and what I expect to go
wrong. The planted bugs are in a script a stage (`tests/<stage>_teeth.sh`, each bug a patch that rebuilds and must
fail a named check), as `updater_ctl_teeth.sh` and `fw_teeth.sh` are.

### Stage 1: the memory side (17.10, 17.11, 23, 17.7)

**Built.**

1. **The memory system, the memory tile's peripheral 1** (17.11): a peripheral of type 6, version 0 in the memory
   tile's slot, holding the cache, the arbiter and one LPDDR model on the file store's pages. Its window: `PARAMS`
   6/0, the standard bits reading 0, `ERRORS` [0] `lpddr_error` sticky from the cache's `out().error`, on the
   tile's error line (so the master's `ERROR_LINES_2` [1], its NMI and the toast pin); a `START` and a `WRITE` to
   its region UB (invariant 38). **It is stepped by the memory tile's slot every cycle** and keeps the slot awake
   while any part of it works; the Tracer's fill port stops ticking it (`CacheSystem::tick` moves into the
   peripheral's step).
2. **The joint** (WORKER9's piece 2): `UpdaterTop::lpddr` on the arbiter's client port 1, `UpdaterTop::cache` on
   the look-up port 1, the Tracer's `fill` on the fill port, one LPDDR for all. **No added latency** (23.1: "latency
   on them does not matter"; the units' convention is a cycle's outputs from registers, then every edge). Since the
   three tiles are stepped one after another in a cycle, the ports cross by a latch of the cycle's outputs, written
   by each side at its step and read by the other, so the tiles' order does not matter (WORKER9's C3); the benches
   run both orders and want the same cycles. **WORKER9's joint checks** (what CI section 3 asks of a client, checked
   where the two meet: no update before its page's write is answered; no direct write to a page the cache holds a
   line of; no direct read of a page with a dirty or in-flight line; LPDDR holds the entry at an update's reply;
   one request outstanding) are counted and fail a run; the stale-line check stays on everywhere.
3. **The RW engine, peripheral 2** (17.7): window 0xFFFF_FC00, region 0x05, its AXI port the arbiter's client 2
   through `RwEngineOnArbiter`, on the one LPDDR; v1.36's own slave goes from the chip. `fw/include/mesh.h`'s RW
   engine addresses (generated: the generator's table changed, not the header by hand), `fw/cc/memory.c` and its
   images rebuilt; `test_rw_engine`, `test_memory` and the memory run move with it.
4. **The reset** reaches the memory system (23.6.10, 23.11, 17.11): the chip's reset calls `CacheCtl::reset`,
   `AxiArbiter::reset`, `LpddrModel::reset` and clears `ERRORS`. Nothing of LPDDR's contents is reset.
5. **The Updater's rows** (17.10): `free_page_credit` at `EVENTS` [2], in every place at once (`updater_top.h`, the
   black box, `host.h`, `master.c`, the benches through `Updater::EV_FREE_PAGE_CREDIT`), `master.elf` rebuilt; a
   bad root or free page address a stop of the simulator, not `lpddr_error` (ctl 408, 576); a run of values past
   index 2³² − 1 and a page at 2²³ or above a stop (ctl 656, 447); a root with bit 31 set a stop at `START`.
6. **The settings** as section 1 has them; the proof bench's view (`updater_view.h`) and its `--cache` and
   `--updater-ctl` paths reduced to the one machine; the cache's figures printed by every proof run.

**FTS rows closed:** all five of "the memory system"; "the Updater" rows 17.10, 17.10.1 (×3), 17.10.2;
"the chapters that gather" 17.1/17.11; the parts of 20 for invariants 36–39.

**Checked.**

| check | where |
|---|---|
| the units' own tests unchanged and passing: `test_cache`, `test_axi_arbiter`, `test_lpddr_model`, `test_updater_ctl`, `updater_ctl_teeth.sh` (14) | here |
| **the joined bench** (`tests/test_joined.cpp`, WORKER9's section 4): gold's five fixtures at *s* 0, 20, 60, 63 against gold's images and the black box (LPDDR byte for byte); the 90 random scenarios run by run; the timing fixed and jittered at several seeds, both orders of the tiles; **a second client at once** (a driver on `fill` reading a frozen file and writing its own while the Updater builds), every reply against `cache_ref.h`; the model's faults on each kind of burst; none of the joint's checks firing | here |
| the memory system's window: `PARAMS`, `ERRORS` [0] from a planted LPDDR fault reaching `ERROR_LINES`, the master's NMI and the toast pin; `START` and a region `WRITE` stopping; the reset emptying the cache (a line cached, the reset, the line fetched again) | `test_memory_system`, here |
| the RW engine as peripheral 2 on client 2: its tests at the new window and region; a write and a read job on the arbiter beside the Tracer's traffic | here |
| `test_master` on the joined machine, fixed and at three seeds | here |
| **the 37 proof runs on the joined machine:** every image equal to gold's as its run ended and at the end; `PAGE_COUNT` 0 at every `done`; the Tracer's last record equal to record *s* + 1; no error bit; the cache's flag clear; no stale line; no read of LPDDR nothing wrote; no joint check | runner: `suites` |
| again with LPDDR's timing varied, three seeds | runner: `suites-varied` |
| **the Updater and the Tracer overlapping on the cache:** counted in every run, the cycles and requests with a look-up and a fill request both outstanding, the requests served under a miss and the replays; shown to be non-zero in the proof runs (the Updater's third run beside the trace) | here and runner |
| `make check`; `run_tests.sh all --check` | runner: `check`, `suites` |
| the reference cycles: they move for the proof steps and `test_master` (the cache in place of the model, the controller in place of the black box), and for the memory run (the RW engine behind the arbiter). **Nothing else moves:** every other golden log is byte-identical | `suites`, against Quest 222/244 |

**Two proofs** (the owner's overlap): on the joined bench, as WORKER9 planned (the real Tracer on a synthetic guest
on the cache while the controller builds a fixture's images, driven by the bench). In the chip it needs the frame
loop and the barrier made one loop in `master.c` (WORKER9's section 4): **not made in stage 1;** the report says
what it would take.

**Planted bugs** (`tests/joined_teeth.sh`): WORKER9's J1 to J7 (the wrong client port; the wrong look-up port; an
update before its write's response; the cache answering from a line an update changed; two write grants at once;
an update's reply before its write's response; the joint a cycle out); and mine: the memory system's error not on
the error line; the reset not reaching the cache; the RW engine on client 0; `free_page_credit` back at [3] in the
firmware alone; the Updater's address check left as `lpddr_error`. One more for each disagreement found.

**What I expect to go wrong:** WORKER9's E1 to E8 (gaps inside a read burst between the arbiter's pieces; `wready`
falling between pieces; W beats with or before the address; a write-back and a page write at once; replays with
two real clients; the stale-line check on index lines the two share; a wait that never ends; the Tracer's first
fill against `done`); and four of my own: **the step order** (the memory tile stepped before or after the tracer
tile, and the Tracer's fill port today ticking the cache at the end of its own step: moving the tick may move the
Tracer-alone cycles by one here and there, which must be shown to be order only and then made exact); **the slot
asleep** while the cache works for another tile; **the split over threads** (section 1); and the memory run's
cycles, now behind the arbiter's register stages.

### Stage 2: the router (16)

**Built.** The line (16.1, 16.2): a special tile's router gets east and west links, master east ↔ memory west and
memory east ↔ tracer west, each a mesh `link` of three stages with no flow control; inside a special tile's router
a packet for another special tile goes along the line by the ring's rule (east if dst > id), a packet arriving on
the line ejects or goes on the way it came, everything else leaves by the attach port; a compute tile's packet for
a special tile never takes it. The queues and the arbiter's order (16.3: local, west, east, north, south, the
attach ports). **Every special tile's DMA on its south input** (16.4), the master tile's Ingress/Egress DMA among
them; `chip.cpp`'s `FROM_LEFT` goes. Invariant 40: an overflow on the line is `router_overflow` on that router.

**FTS rows closed:** 16.1/16.2, 16.4, 17.6.4/16.4; 20's invariant 40.

**Checked.** The router's tests extended: every route between the three special tiles and from each to the mesh
and back; a broadcast from a special tile; the line's latency (four cycles a hop unloaded); overflow on the line;
the arbiter's order with the new inputs. **The master tile's load into the tracer tile goes by the line**
(a counter of packets per link, and none of them on the master's attach port). The tile programs (`special.elf`,
`noc.elf`), the sort, a segment, `test_master`, the proof runs. **Cycles move here, on purpose:** the master's
loads to the tracer tile (shorter: two hops of the line in place of the mesh), and every special tile's DMA
traffic (the south input's place in the arbiter); the moved logs recorded, and every log with no special tile's
traffic in it (the compute tiles' own programs; the sort's runs, if their special tiles' traffic is only on the
attach ports, which I will check) shown not to move.

**Planted bugs:** east and west swapped on the line; the ring's rule reversed; a compute tile's packet for a special
tile put on the line; the DMA left on west; a packet from the line sent back the way it came; the arbiter's order
with south before north.

**What I expect to go wrong:** the RTL's figures that v1.36's steps check "cycle for cycle" (the segment runs, the
tracer tile's deliveries) no longer match once the DMA is on south: the specification wins, and the report says
which RTL figures no longer apply; the special tiles' routers have a fifth-link variant today (`Router::FIFTH_LINK`)
whose port numbering may assume one DMA input on west.

### Stage 3: Ingress/Egress at its registers (17.6)

**Built.**

- **The registers as 17.6.1 has them:** `PARAMS.version` 1; `STATUS` [2], [3] and the UART's fields [12:8],
  [20:16], [24], [25]; `EVENTS` [6]–[8] and `EVENTS_MASK` of 9 bits; words 64–67; v1.36's host interface out
  (words 64–72 as v1.36 had them, `STATUS.stream_last`, `EVENTS.stream_in` and `stream_out`, `STREAM_BYTES`,
  `STREAM_DEPTH`), **with its tests and the RTL images that use it** (the stream run, `test_rtl_stream`, its step);
  `ERRORS` [1] `link_error`, [2] `format_error`, [3] `egress_error`.
- **The UART as redesigned** (17.6.9): `UART_TX` 30, `UART_RX` 31, `UART_CONTROL` 42 (0 at the reset: held in
  reset), the levels and line errors in `STATUS`; `UART_DIVISOR`, `UART_STATUS`, `UART_RESET_DIVISOR` gone. **Every
  image that prints** gets its store to `UART_CONTROL` (the boot ROM? to be checked: if the boot ROM prints, that is
  hardware Tony built, and I ask first); the images rebuilt and their hashes committed.
- **The queues:** `INGRESS_QUEUE_WORDS` 1,024, a parameter in `MachConfig`; "stop" at half (for stage 4's link
  ends); **a link packet's words land a 128-bit word a cycle,** each counted in `…_WORDS` as it lands, with the
  room of the other count, landed and still to land (17.6.3).
- **Egress** (17.6.8): the outgoing queue of 512 words in region 0x04 part 1, `EGRESS_COUNT` counting down as words
  land, the last word marked, `egress_credit` (depth 2, a credit each 256 words left), `egress_error`; its UB
  (invariant 35). Until stage 4 its link packets go to a harness's sink in place of a link end.
- **The firmware's header and images** move with the numbers (`host.h`, `master.c`'s constants), so that
  everything runs as before. Nothing new in what the firmware does (stage 5).

**FTS rows closed:** every row of "Ingress/Egress" but the reset's (17.6.14, stage 4); 17.6.8's register side;
the firmware's 30.2 (the store to `UART_CONTROL`); 19's parameters; invariant 35.

**Checked.** **Every row of 17.6's tables by a test at the registers** (`test_ingress_egress`, rewritten from v1.36's
and WORKER7's: each word's offset and access, each field, each `STATUS`, `ERRORS` and `EVENTS` bit set and cleared
as its row says, the unmapped words faulting, a load from 32 up with no side effect); the UART at its registers
(a character each way at a divider, the levels, an overrun and a framing error, off at the reset); the landing a
word a cycle and the room's count; Egress's count, credit and error; **each UB of invariant 35** stopping the
simulator; `test_master` and the proof runs. **Cycles move:** the proof runs (the queues of 1,024, the landing),
and every image that prints (its store to `UART_CONTROL`); v1.36's stream run is gone. Recorded.

**Planted bugs:** a word at 80 in place of 64; `STATUS` [3] for the background read as [4]; `EGRESS_COUNT` counting
as words leave; a credit each 512 words; the landing all at once (WORKER7's model); version 0; `enable_read` and
`enable_write` swapped; "stop" at full; a store to `EGRESS_COUNT` while not 0 not caught.

**What I expect to go wrong:** v1.36's tests and RTL images that print through the UART running from the reset at
115,200 baud: every log with a UART line moves, and a test that times a character must be rewritten to the
divider; the service flag's rule (WORKER7's 1.1 to 1.4) with words landing a cycle at a time, where today a whole
link packet lands at once.

### Stage 4: the link and the chain (28, 17.6.3, 17.6.8, 17.6.14, 18)

**Built.**

- **The black box's link becomes WORKER8's `Node`,** used through `hostlink.h` only, the library built from
  `updater_wip/link/` in place (†, "Choices"): a link end up, a link end down, the four forwarding buffers,
  `chain_next` a `MachConfig` pin. **The Node's queues are Ingress's queues:** Ingress takes words out of `Node`
  as the DMA or the forward count consumes them, and models the landing a word a cycle itself (a count of words
  landed beside the Node's count of words taken, which is the room's): 17.6.3's two counts, with no change to the
  link's code (FTS 28.13.6). `ERRORS.link_error` is either end's `status().error`, `format_error` its `made_wrong`;
  nothing calls `clear_error()` (FTS 28.13.2); a damaged link packet is no error.
- **Egress feeds `Node::push()`** a link packet of 32 words of a frame or its last; the credit counts words as they
  leave Egress's queue into the Node. FTS 17.6.8 notes the Node's own send queue (1,024) and no "taken" strobe: I
  will push only when the Node's send queue can take the packet and count a word as left when it is pushed, and
  ask the owner only if that cannot give 17.6.8's credit exactly (FFS).
- **The reset restarts the link** (17.6.14, 28.14): the chip's reset calls `Node::power_on()`.
- **The chip's input is `link_up`, a byte each way a cycle;** `link_down` and `chain_next` beside it. **In one
  process** the host's side is WORKER8's `End`, fed with gold's frames, each frame starting a link packet, at most
  32 words each, and the chip's replies taken out by hops (stage 5 sends them).
- **Gold brought to chapter 28** (MILESTONE step 2c): gold's packetizer, reader and vectors to one run of numbers a
  link, both stop bits in byte 7, the restart flag, the mask on the data, the restart link packet; the Python
  reader too; HOST-LINK's worked examples checked byte for byte. **"Agree byte for byte"** I take as: the data link
  packets an `End` sends first, on a clean link whose other end only acknowledges, equal gold's packet file of the
  same frames, numbers from the first (†; the `ack` and stop fields depend on what comes back, so the capture is
  defined to make them so). FFS if the chapter means more.

**FTS rows closed:** every row of "The host link" (28, 28.14, 28.13.2, 28.12, 28.13.6, 17.6.8); 17.6.14; 3/18;
the reset row of "the chapters that gather". 28.9/28.7's runner campaign (Quest 238) stays the owner's.

**Checked.** **With no chips:** a host `End` and two or three black boxes back to back (the Ingress/Egress model with
its Node, a harness on its registers in place of the core): every word to every ASIC once and in order, the
replies sorted by hops, the stop bits holding the host back when a queue fills, the reset of one in the middle
raising both neighbours' `link_error` and no other. **Then one chip behind a host end:** `test_master` and the 37
proof runs as before, every check of today. Gold's packet files against the `End`'s, byte for byte, on every proof
run's frames (in `suites`). The link's own `make check`. **The milestone's planted bugs in the chain,** each
caught: hops not counted; a header not sealed again; an own link packet before a forwarded one; a stop bit ignored;
a reply's link packets out of order; and `chain_next` ignored, `clear_error()` called by the chip.

**Cycles move once more:** the link's start (about 4,657 cycles from power-on before the first data), its status
link packets, its acknowledgements; recorded.

**What I expect to go wrong:** the Node's queue, its stop bit and Ingress's landing disagreeing by a word at the
half-full mark; `--link-rate` (section 1, FFS); the proof bench's link figures (`MR_LINK_*`, the waits for room)
changing meaning; the black box's file rewind at the reset, which tests rely on.

### Stage 5: chapter 30

**Built.** The store to `UART_CONTROL` in every image that prints, if not already in stage 3; **the tracer tile's
result carrying its last record to the master tile** (30.6; `tracer_ctl.c`, `tracer_layout.h`, `master.c`); **the
reply frame** (29.14, 30.7): `master.c` builds 72 words in its memory, stores 72 to `EGRESS_COUNT`, writes them into
its region 0x04 part 1 with 128-bit `WRITE`s from one sending unit; Egress sends three link packets. The images
rebuilt, their hashes committed.

**FTS rows closed:** 30.2, 30.6, 30.7/29.14; 30.8's two rows checked as specified.

**Checked.** **Chapter 30 section by section against `master.c` and `tracer_ctl.c`,** a table in the report (each
rule, the lines that keep it, the test that shows it); **the reply whole, in order, and equal to what gold's files
give in the words 29.14 says can be compared** (word 0; *s*, *N*, the rows; the error words 0; the record against
record *s* + 1; `PAGE_COUNT`, `mcycle` and the Updater's statistics against the simulator's own values, not gold's);
`test_master` with the reply checked by the host `End`; the proof runs. **Planted bugs** (`fw_teeth.sh`, extended):
*s* and *N* swapped; `EGRESS_COUNT` 71; the record from record *s* + 1 in the master's memory in place of the
tracer tile's; the tracer tile's result without the record; the reply's words written out of order; a missing
`UART_CONTROL` store in an image that prints; WORKER7's 20 kept.

**What I expect to go wrong:** the tracer tile's result is a request today of a fixed size; a 68-word record may not
fit it, and the master's request handling may need a larger receive; the tracer's own check of record *s* + 1 stays
(30.5 still hands it over), and the two checks must agree.

### Stage 6: the driver and the three processes

**Built.** **The driver** (MILESTONE, "The driver"): a C++ program beside the simulator, `tests/driver_main.cpp`,
on `hostlink.h` and the wire: it reads gold's frame file, sends the frames through its `End` (each frame a new link
packet), obeys the stop bits, takes what comes back sorted by hops, rebuilds each ASIC's reply and checks it
against gold's (as stage 5's check, in the comparable words), and fails on a difference or when a reply does not
come within a limit counted in cycles. Tested first against **stand-in ASICs made of `Node`** that answer each
frame set with a reply built from gold's files, on the wire under its launcher. **The chip's simulation** (`proof_run`)
takes `--clients N --client k`, parses the wire's arguments, and calls `exchange` on its two link ends every cycle
from cycle 0, `finish()` at its end; *s* defaults to *k* (MILESTONE).

**Checked:** every row of MILESTONE's "How the milestone is checked": each ASIC's images and its last record; the
replies whole and in order, sorted by hops, equal to gold's; **the same run twice the same to the cycle in all
three processes**; a chain of one (`--clients 1`) giving the single chip's results of stage 5; both controllers on
in every run (they are the machine); the planted bugs in the chain (stage 4's, now across processes, and: the
driver's sort by hops wrong, a reply missing not counted in cycles, a cycle without `exchange`); and with the chain
off, `run_tests.sh all --check` and `make check` as stage 5 left them. **On the small inputs here** (the guests,
DRIVER ↔ ASIC0 ↔ ASIC1), **2²⁹ on the runner** (both workloads, one task each, short: segments 0 and 1).

**What I expect to go wrong:** the chip's run is long and the driver idles; WORKER10's figure for a window of 64 is
1.5%, so nothing to change. A chip whose run ends (its reply sent) must still clock its link ends while the replies
of the ASICs below pass through it: `finish()` only once the link below is quiet and the driver has said so, or the
chip runs on until every process is done. To be settled with the wire's rules, without changing the wire.

### Stage 7: closing, with the owner

- Every FTS row closed with its commit, or an open item with the owner's word.
- **Chapter 20's invariants each a check** in the simulator: a table in the report, each invariant, its check, the
  test that fires it; those that cannot be checked (39's back-to-back W beats, as WORKER5's answer says) named.
- **Chapter 21's timing rows measured** where v1.37 adds them (21.5's new rows, the line's hop), each against the
  specification's figure; a wrong figure reported in FFS, not changed.
- `RUNNING-v1.37.md` and `CHANGES-v1.37.md` brought up to date (the settings gone, the chain, the driver), and the
  top-level and `updater_wip/` READMEs. What is frozen, and when, is the owner's word.

## 3. What I will not do

A new design for anything the specification states; the link's protocol and its searches; recovery after a stop
(item 181); flow control on the line (item 182); the file system; the foreground's frames but the reply; the RW
engine beyond its move; edits to `spec/`, the wire, the controllers or the link end beyond what a row or a finding
requires; anything that would change hardware Tony has built without asking.

## 4. Questions for the go-ahead

1. **The xPack toolchain.** Stages 1, 3 and 5 rebuild images (`master.elf`, `memory.elf`, every image that
   prints). It is not on this machine. As WORKER9 did (its go-ahead, item 4): may I download xPack
   `riscv-none-elf-gcc` 15.2.0-1, linux-x64, from its GitHub release, check it against the SHA-256 WORKER9 recorded
   (`aaaa8060…a33758`) before unpacking, and keep it outside the repository?

The rest are choices (†), in "Choices" below, which I will take unless the owner says otherwise.

## Choices

| | choice | why; the alternative |
|---|---|---|
| C1 † | the black boxes (the Updater's, v1.36's memory model) stay only in the comparisons, not as chip settings | the brief's "only where a test compares against it"; keeping them as chip settings would keep two machines alive |
| C2 † | the joint has no latency: a latch of the cycle's outputs between tiles | 23.1 ("latency does not matter to it"); it keeps today's cache cycles exact. The alternative, a framework link of one stage, adds two cycles to every look-up |
| C3 † | the link end and the wire are built from `updater_wip/link/` and `updater_wip/wire/` in place, by `v1.37_wip`'s Makefile, through their headers only | no copy to drift; at the freeze they are copied into the frozen folder with their hashes (`COPIES.sha256`'s way), on the owner's word |
| C4 † | Ingress models the landing a word a cycle outside the Node | 17.6.3's two counts with no change to the link's code (FTS 28.13.6) |
| C5 † | gold's "byte for byte": the first sendings of data link packets on a clean link | the only reading in which a file can equal a link end's output; FFS |
| C6 † | `--link-rate B` clocks the link ends B times a chip cycle; `--fast-link` goes | item 186 is open; a test setting only; FFS |
| C7 † | two proofs on the bench only, not in the chip | the chip's needs a change to `master.c`'s loop (WORKER9, section 4) |
