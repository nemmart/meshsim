//! The master tile's firmware for the chip's tests: the sort design's C&C for the sort's runs, the memory tile's user
//! for the memory run, and the segment's C&C 1 for the trace run, on the master tile (rtl/tile/master), the base tile
//! with Ingress/Egress, its peripheral 1 (rtl/tile/base/spec.md 17.6).
//!
//! `cargo xtask sort-images` (xtask/src/sort_images.rs) builds it once per run, with the run linked in as read-only
//! data (`RUN_IMAGE`): its kind and parameters, and the image the master loads on the tiles. The chip's test places it
//! in the master's IMEM and DMEM before the chip's reset ends, and the boot ROM starts it (6.5, PARAMS.boot_local 1).
//!
//! **A sort run** (meshsim v1.25's SortTest.boot and sortRun, on CnC.java's sequencer). There are no commands (6.5):
//! the sort's phases are a fixed schedule in the tile image (sort/sw/main.c), each ending at the barrier, and the
//! master's part is the load and the releases.
//!
//! 1. The load (6.5): every compute tile's boot ROM arms counter 7 and sends a `LOAD` request (11.5, op 3); once every
//!    one has been popped, the tile image's IMEM rows, its DMEM rows and the arguments' row (the records each tile
//!    holds, the port layer's input-load stand-in, and the format: sort/port/mesh.h, `ARGS_BASE`), all broadcast as
//!    uncounted 128-bit `WRITE`s from the write unit, then one broadcast `COUNT` on counter 7, from the same unit and of
//!    the same kind, so that it lands last (14.4). Each tile arrives once its counter 7 drains; once every one has, the
//!    master releases them, and each ROM jumps to IMEM's base. The tracer tile's boot ROM sends its `LOAD` too (6.5),
//!    which the master pops with the compute tiles' before the load and leaves unanswered: no run traces (the memory
//!    system behind its fill port is Open, 2.1), so the tracer tile waits in its boot ROM.
//! 2. The sort: for each phase of the schedule, the split's 66 barriers among them (`barriers`), every compute tile's
//!    arrival, then `RELEASE`.
//! 3. The end: each tile's exit request (the port layer's, op `OP_EXIT`), answered at once by its reply (11.4), for
//!    which the tile waits, so that it has one request outstanding to the master at a time (11.3); then its next
//!    `LOAD`, which its image's end, a jump to the reset PC, sends from its boot ROM (6.5); a tile's `EXIT` popped
//!    before its `LOAD` (the request tree keeps one sender's order, 11.3). Then the links' latency: each
//!    tile's `LOAD` answered in turn by a load that is its `COUNT` alone, the image being in place, unicast, and that
//!    tile's arrival awaited alone (7.3, 17.6: `ARRIVE_EXPECT` that tile): the cycles from the send to the wake, the
//!    links' latency, the network idle (`latency`). No release follows: the tiles stay arrived in their boot ROMs.
//! 4. The line checks, in the trap handler: a bad packet to the memory tile (a `WRITE` to its boot ROM, 4.2), whose
//!    error line raises the master's `ERRORS.peripheral_1` through Ingress/Egress's `tile_error`, and with it its NMI
//!    (6.2, 17.6); then, in the handler, a bad packet to each compute tile in turn, and the cycles until its bit of
//!    `ERROR_LINES` rises, polled (`line_latency`); then every tile's reports 24 and 25, which its boot ROM's handler
//!    sends on the NMI its error raises (6.5).
//! 5. The summary (`Summary`, DMEM's first data) and, last, its done word; then it parks.
//!
//! **The memory run**: the memory tile runs its firmware (src/bin/memory.rs), which the chip's test places in its
//! IMEM and DMEM before the reset ends (6.5, PARAMS.boot_local 1); the master sends it requests (11.5): a `READ` of
//! LPDDR beats the test's model holds into the summary, a `WRITE` of beats the master sends into the write buffer
//! part the reply names, and a `READ` of those beats back; then the credits' requests, `CREDIT_REQUESTS` `WRITE`s of
//! 0 beats, more than the memory target's receive queue holds (11.3), so that the request tree's root runs out of
//! credits for the memory tile unless it returns each as it pops its request: `MEMORY_LIMIT` outstanding at a time,
//! the per-tile limit (11.3), each answered by its reply alone, into a slot of its own. The memory tile's replies are
//! 128-bit (11.4, 11.5), so each reply slot is 16 bytes, 16-byte aligned. Each answer is awaited on the master's
//! counters, armed before its request (11.4). The compute tiles' `LOAD`s stay in the master's receive queue,
//! unanswered.
//!
//! **The trace run** (17.9.10; meshsim v1.35's C&C 1, its `CC_SEGMENT`, with no commands): the load, then the segment's
//! fixed schedule of releases, the master's part the barriers (7.3, 17.6), with every request served as it comes
//! (`Segment`, apart from the sort's code, so that the sort's timing is its own).
//! 1. The load (6.5): every compute tile's `LOAD` and the tracer tile's popped, then the run's first image, the compute
//!    tiles' replay image (src/bin/replay.rs), broadcast, its rows then its `COUNT`, and its second, the tracer tile's
//!    (src/bin/tracer.rs), unicast to the tracer tile likewise, all from the write unit; no arguments: the segment's
//!    input is in the tiles' scratchpads. The tracer tile starts once its `COUNT` lands, and takes no part in the
//!    barrier.
//! 2. Three releases: the load's, on which each compute tile arms for its window and sends `ARMED` to the tracer tile;
//!    the windows' landing's, once every compute tile has its window, which starts every pass together; and the
//!    passes' end's.
//! 3. Every compute tile's exit request, and the tracer tile's, which may come at any time from the load on, each
//!    answered by its reply (11.4), and each one's `LOAD` after it, its image's end (6.5), left unanswered. A tile's
//!    error report (6.5) ends the run. The summary is `TraceSummary`; no line checks follow.
//!
//! Every wait sleeps in WFI, its events in EVENTS_MASK (7.2) and Ingress/Egress's (17.6) through the tile's
//! peripheral_1, with the watchdog set first, so that a tile that never arrives, or an answer that never comes, ends
//! the run with the reason in the summary rather than hanging it. An NMI out of the line checks (a tile's error) also
//! ends the run, through the handler, with the error lines and the tiles' reports in the summary. Per-phase mcycle
//! values go in the summary, for comparison with meshsim's phase log.

#![no_std]
#![no_main]

use core::arch::{asm, global_asm};
use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{Ordering, compiler_fence};
use tile::*;

global_asm!(include_str!("../../start.S"));

// The master's trap entry: a fresh stack, then `trap`, which does not return (every trap is terminal, 6.2).
global_asm!(
    "    .section .text",
    "    .balign 4",
    "master_trap_entry:",
    "    la      sp, __stack_top",
    "    tail    trap",
);

// ---- Ingress/Egress, peripheral 1 (17.6) ----

const IE: usize = peripheral_window(1);
const ERROR_LINES: usize = 0x080; // ERROR_LINES_0-2, word w at + 4w
const ARRIVE_EXPECT: usize = 0x094; // ARRIVE_EXPECT_0-1
const RELEASE: usize = 0x09C;
const WATCHDOG: usize = 0x0A0;
const IE_EVENT_ALL_ARRIVED: u32 = 1 << 2;
const IE_EVENT_WATCHDOG: u32 = 1 << 3;

fn ie_load(offset: usize) -> u32 {
    load(IE + offset)
}

fn ie_store(offset: usize, value: u32) {
    store(IE + offset, value)
}

// ---- The chip ----

/// The sort design's mesh: 64 compute tiles; the memory tile's id.
const TILES: usize = 64;
const MEMORY_TILE: u32 = TILES as u32 + 1;
const TRACER_TILE: u32 = TILES as u32 + 2;

// The watchdog's limits, in cycles: for each wait for every arrival, a base and as many more per record each tile
// holds (a phase's work grows with them); for the requests and the reports; for each answer of the memory run.
const PHASE_LIMIT: u32 = 1 << 18;
const PHASE_LIMIT_PER_RECORD: u32 = 16;
const REPORT_LIMIT: u32 = 1 << 16;
const ANSWER_LIMIT: u32 = 1 << 16;
/// A line check's wait for its bit of ERROR_LINES, and for the NMI the first one raises.
const LINE_LIMIT: u32 = 1024;
/// A trace run's wait for every arrival: the segment's tracing and its windows' delivery come before its second, the
/// windows traced one after another, so that it holds the largest segment, a window of 65,536 rows on each of the 64
/// compute tiles (17.9.10), at some 17 cycles a row (rtl/chip/README.md, Results), 71M cycles.
const TRACE_LIMIT: u32 = 1 << 27;

// ---- The tile image's schedule (sort/sw/main.c), which the master's releases follow ----

const FORMAT_K128: u32 = 1;
/// The split's own barriers, unshaped (meshsim's split-bisect/BINDING.md).
const SPLIT_BARRIERS: u32 = 66;
/// The skew sequence's layers per two-hop step (SortTest: K1.0-4, K2.0-4, V1.0-4, V2.0-4).
const LAYERS: u32 = 5;

/// The barriers of a sort, after the load's release: one at each phase's end, the start-up's (crt0's), L1, SPLIT (and
/// the split's own), XB, RED, XC, TH, SWEEP, DESC, K, K1.0-4, K2.0-4, with 128-bit values V, V1.0-4 and V2.0-4, then
/// L2.
const fn barriers(format: u32) -> u32 {
    let values = if format == FORMAT_K128 { 1 + 2 * LAYERS } else { 0 };
    1 + 1 + SPLIT_BARRIERS + 1 + 7 + 2 * LAYERS + values + 1
}

/// The boot ROM's reports (6.5).
const REPORT_ERRORS: u32 = 24;
const REPORT_MTVAL: u32 = 25;

// ---- The run (linked in by cargo xtask) ----

/// The run image: its header, `RUN_HEADER_WORDS` words (magic, kind, format, records per tile, the image's arguments'
/// address, the compute tiles, the image's IMEM rows and DMEM rows, its DMEM rows' load address), then the IMEM rows
/// and the DMEM rows, four words each (xtask/src/sort_images.rs writes it).
const RUN_MAGIC: u32 = 0x5352_554E; // "SRUN"
const RUN_HEADER_WORDS: usize = 9;
const KIND_SORT: u32 = 0;
const KIND_MEMORY: u32 = 1;
const KIND_TRACE: u32 = 2;
/// A trace run's second image, the tracer tile's, after the first's rows: its IMEM rows, its DMEM rows and their load
/// address, then its rows.
const TRACER_IMAGE_WORDS: usize = 3;

unsafe extern "C" {
    /// The run image's first word (an object cargo xtask makes from the image and links in).
    static RUN_IMAGE: u32;
}

/// The run image's word `index`.
fn run_word(index: usize) -> u32 {
    // SAFETY: the run image is linked in whole at RUN_IMAGE; `index` is within it (its header's counts).
    unsafe { read_volatile((&raw const RUN_IMAGE).add(index)) }
}

/// The run's parameters, from the run image's header.
struct Run {
    kind: u32,
    /// 0 32-bit values, 1 128-bit values (sort.h's FMT_*).
    format: u32,
    /// Records per tile, the same on every tile: the image's argument ARG_RECORDS.
    records: u32,
    /// The image's arguments' row: its records per tile, then its format (sort/port/mesh.h, ARGS_BASE).
    arguments: u32,
    imem_rows: usize,
    dmem_rows: usize,
    dmem_address: u32,
}

fn run_image() -> Option<Run> {
    let header: [u32; RUN_HEADER_WORDS] = core::array::from_fn(run_word);
    let [magic, kind, format, records, arguments, tiles, imem_rows, dmem_rows, dmem_address] = header;
    if magic != RUN_MAGIC || tiles as usize != TILES {
        return None;
    }
    Some(Run {
        kind,
        format,
        records,
        arguments,
        imem_rows: imem_rows as usize,
        dmem_rows: dmem_rows as usize,
        dmem_address,
    })
}

// ---- The summary, DMEM's first data (link.ld), which the chip's test reads once `done` is DONE ----

const DONE: u32 = 0x444F_4E45; // "DONE"
const MAX_PHASES: usize = 128;
const NONE: u32 = 0xFFFF_FFFF;
/// The memory run's beats: a READ of the model's words, a WRITE and its READ back, each crossing a 4 KiB boundary
/// of LPDDR (17.7), so that the RW engine splits it into bursts.
const READ_BEATS: usize = 20;
const WRITE_BEATS: usize = 24;
const READ_LPDDR: u32 = 0x0FF8; // beat addresses: 0xFF80, 8 beats below 64 KiB
const WRITE_LPDDR: u32 = 0x20F4; // 0x20F40, 12 beats below 0x21000
/// The memory run's credits' requests: more than the memory target's 256 receive queue entries at 8 × 8 (11.3), sent
/// `MEMORY_LIMIT` at a time, the memory target's per-tile limit (11.3).
const CREDIT_REQUESTS: usize = 300;
const MEMORY_LIMIT: usize = 3;

/// Why a run ended early (`Summary::failure`); 0 when it ran to its end.
#[derive(Clone, Copy)]
enum Failure {
    /// The run image's header is not one this firmware takes.
    Image = 1,
    /// The watchdog expired before every compute tile arrived.
    Timeout = 2,
    /// A request came while the master waited for the tiles' arrivals.
    EarlyRequest = 3,
    /// Fewer exit requests, or LOADs after them, than tiles within the watchdog.
    MissingReports = 4,
    /// A trap before the line checks: a tile's error, or the master's own.
    Trap = 5,
    /// The first line check's NMI never came.
    NoNmi = 6,
    /// An answer of the memory run never came.
    NoAnswer = 7,
    /// The firmware panicked.
    Panic = 8,
    /// Fewer LOADs than tiles (the compute tiles' and the tracer tile's) within the watchdog, at the boot.
    MissingLoads = 9,
    /// A trace run: a tile's error report (6.5) came.
    Report = 10,
}

/// The summary's layout, words from DMEM's first data, as the chip's tests read it (rtl/chip/test/src/chip_test.rs,
/// chip_memory_test.rs): a header, then the run's own.
#[repr(C, align(16))]
struct Summary {
    /// 0 while the run goes on, DONE once every other field is written.
    done: u32,
    /// 0, or the `Failure` that ended the run.
    failure: u32,
    /// The phase in which it failed (an index into `phase`).
    failure_phase: u32,
    /// The run's kind.
    kind: u32,
    run: RunSummary,
}

#[repr(C)]
union RunSummary {
    sort: SortSummary,
    memory: MemorySummary,
    trace: TraceSummary,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct SortSummary {
    /// The phases recorded in `phase`.
    phases: u32,
    /// The exit requests received, one from each compute tile.
    exits: u32,
    /// Requests out of turn before the line checks: from no compute tile, a second LOAD at the boot, an EXIT twice, a
    /// LOAD before its tile's EXIT or twice, or of another op.
    strays: u32,
    /// The handler's entry: mcause and mepc, NONE if no trap was taken.
    trap_cause: u32,
    trap_pc: u32,
    /// ERROR_LINES_0-2 at the handler's entry and after the line checks.
    trap_error_lines: [u32; 3],
    error_lines: [u32; 3],
    /// The reports 24 and 25 received in the handler.
    error_reports: u32,
    /// Each compute tile's exit request: words 0 to 3 (zeros if none came).
    exit: [[u32; 4]; TILES],
    /// Each release: its index, mcycle when every compute tile had arrived, mcycle at its RELEASE store. Release 0
    /// ends the load, and starts the image's start-up; release n ends phase n - 1's barrier, and release 1 starts
    /// L1.
    phase: [[u32; 3]; MAX_PHASES],
    /// The links' latency to each tile: the cycles from its load's `COUNT`'s send to the wake on that tile's arrival,
    /// or NONE.
    latency: [u32; TILES],
    /// The line checks: the cycles from each compute tile's bad packet's send to its bit of ERROR_LINES, or NONE.
    line_latency: [u32; TILES],
    /// Each tile's reports 24 and 25, words 1-3 each, by requester: the compute tiles, then the memory tile.
    error_report: [[u32; 6]; TILES + 1],
}

/// The memory run's summary. Every reply slot is a 128-bit reply's (11.4, 11.5): 16 bytes, 16-byte aligned, the
/// value in its low bits and the bits above it 0.
#[repr(C, align(16))]
#[derive(Clone, Copy)]
struct MemorySummary {
    /// The WRITE's reply: the part's address in [31:0] and the memory tile's counter in [34:32] (11.5).
    part: [u32; 4],
    /// The READs' replies, each a 128-bit 0 (11.5).
    replies: [[u32; 4]; 2],
    /// The first READ's beats, then the WRITE's beats read back: 16 bytes each, in the order of LPDDR's addresses.
    read: [[u32; 4]; READ_BEATS],
    read_back: [[u32; 4]; WRITE_BEATS],
    /// Each credits' request's reply, as the first WRITE's: the part's address and the memory tile's counter.
    answers: [[u32; 4]; CREDIT_REQUESTS],
}

/// The releases a trace run records: the load's, the windows' landing's and the passes' end's.
const TRACE_PHASES: usize = 3;

/// A trace run's summary.
#[repr(C)]
#[derive(Clone, Copy)]
struct TraceSummary {
    /// The releases recorded in `phase`.
    phases: u32,
    /// The compute tiles' exit requests, and the LOADs after them, the compute tiles' and the tracer tile's.
    exits: u32,
    loads: u32,
    /// Requests out of turn: from no tile of the run, a second EXIT, a LOAD before its tile's EXIT, or of another op.
    strays: u32,
    /// Error reports (6.5) received, and the first one: words 0-3.
    reports: u32,
    report: [u32; 4],
    /// The tracer tile's exit request: words 0 to 3 (zeros if none came).
    tracer_exit: [u32; 4],
    /// Each compute tile's exit request: words 0 to 3 (zeros if none came).
    exit: [[u32; 4]; TILES],
    /// Each release, as a sort run's: its index, mcycle when every compute tile had arrived, mcycle at its RELEASE.
    phase: [[u32; 3]; TRACE_PHASES],
    /// ERROR_LINES_0-2 at the run's end, every tile's error line (17.6).
    error_lines: [u32; 3],
}

// A 128-bit reply's address is a multiple of 16 (11.4), and so is every beat's.
const _: () = assert!(core::mem::offset_of!(Summary, run) % 16 == 0);
const _: () = assert!(core::mem::offset_of!(MemorySummary, part) % 16 == 0);
const _: () = assert!(core::mem::offset_of!(MemorySummary, replies) % 16 == 0);
const _: () = assert!(core::mem::offset_of!(MemorySummary, read) % 16 == 0);
const _: () = assert!(core::mem::offset_of!(MemorySummary, read_back) % 16 == 0);
const _: () = assert!(core::mem::offset_of!(MemorySummary, answers) % 16 == 0);

/// The summary's storage: one core, which writes it from `main`, then from `trap` or the panic handler, which end
/// the run.
struct SummaryCell(UnsafeCell<Summary>);

// SAFETY: the master has one core, and nothing interrupts it (mstatus.MIE is 0); a trap ends `main` for good.
unsafe impl Sync for SummaryCell {}

#[unsafe(link_section = ".summary")]
static SUMMARY: SummaryCell = SummaryCell(UnsafeCell::new(Summary {
    done: 0,
    failure: 0,
    failure_phase: 0,
    kind: NONE,
    run: RunSummary {
        sort: SortSummary {
            phases: 0,
            exits: 0,
            strays: 0,
            trap_cause: NONE,
            trap_pc: NONE,
            trap_error_lines: [0; 3],
            error_lines: [0; 3],
            error_reports: 0,
            exit: [[0; 4]; TILES],
            phase: [[0; 3]; MAX_PHASES],
            latency: [NONE; TILES],
            line_latency: [NONE; TILES],
            error_report: [[0; 6]; TILES + 1],
        },
    },
}));

/// Where the run is, for the handler: the line checks' NMI is expected, any other trap ends the run.
static mut LINE_CHECKS: bool = false;

fn summary() -> &'static mut Summary {
    // SAFETY: the only writer is the one core, from main and then from the handler, which never returns to it.
    unsafe { &mut *SUMMARY.0.get() }
}

fn sort_summary() -> &'static mut SortSummary {
    // SAFETY: a sort run's summary (the union's sort, its initial value).
    unsafe { &mut summary().run.sort }
}

fn memory_summary() -> &'static mut MemorySummary {
    // SAFETY: the memory run's summary: every field a u32, any value valid.
    unsafe { &mut summary().run.memory }
}

fn trace_summary() -> &'static mut TraceSummary {
    // SAFETY: a trace run's summary: every field a u32, any value valid.
    unsafe { &mut summary().run.trace }
}

// ---- Sending ----

/// `rows` rows of four words, from the run image's word `from`, to `dst` from `address` up: 128-bit uncounted
/// `WRITE`s, one per row, WRITE_DEST_ADDR stepping by 16 each (9).
fn land_rows(dst: u32, address: u32, from: usize, rows: usize) {
    tile_store(WRITE_HDR, header(MODE_WRITE, 0, SIZE_128, dst));
    tile_store(WRITE_DEST_ADDR, address);
    for row in 0..rows {
        let word = from + 4 * row;
        tile_store(WRITE_W0, run_word(word));
        tile_store(WRITE_W1, run_word(word + 1));
        tile_store(WRITE_W2, run_word(word + 2));
        tile_store(WRITE_SEND_128, run_word(word + 3));
    }
}

/// The arguments' row (sort/port/mesh.h, ARGS_BASE) to `dst`: one 128-bit uncounted `WRITE` at `address` of the
/// records per tile and the format.
fn land_arguments(dst: u32, address: u32, records: u32, format: u32) {
    tile_store(WRITE_HDR, header(MODE_WRITE, 0, SIZE_128, dst));
    tile_store(WRITE_DEST_ADDR, address);
    tile_store(WRITE_W0, records);
    tile_store(WRITE_W1, format);
    tile_store(WRITE_W2, 0);
    tile_store(WRITE_SEND_128, 0);
}

/// A load's last packet (6.5): a `COUNT` on counter 7 to `dst`.
fn send_count(dst: u32) {
    tile_store(WRITE_HDR, header(MODE_COUNT, LOAD_COUNTER, SIZE_32, dst));
    tile_store(WRITE_SEND_32, 0);
}

/// An `EXIT`'s reply (11.4) to `dst`: a 32-bit `WRITE` of 0 to the address its word 0 names, counted on the counter
/// it names (`EXIT_REPLY_*`), which the tile armed before it sent the request and on which it waits before its image
/// ends (sort/port/port.c).
fn send_exit_reply(dst: u32, word_0: u32) {
    let counter = (word_0 >> EXIT_REPLY_COUNTER_SHIFT) & 0x7;
    let address = (word_0 >> EXIT_REPLY_ROW_SHIFT) << 4;
    tile_store(WRITE_HDR, header(MODE_WRITE, counter, SIZE_32, dst));
    tile_store(WRITE_DEST_ADDR, address);
    tile_store(WRITE_SEND_32, 0);
}

/// A bad packet to `dst` (4.2): a 32-bit `WRITE` to its boot ROM.
fn send_bad_packet(dst: u32) {
    tile_store(WRITE_HDR, header(MODE_WRITE, 0, SIZE_32, dst));
    tile_store(WRITE_DEST_ADDR, BOOT_ROM_BASE);
    tile_store(WRITE_SEND_32, 0xBAD0_0000 | dst);
}

// ---- Waiting ----

/// Sleeps until every event of `events`, the tile's, has been seen, or the watchdog has expired: EVENTS_MASK
/// `events` and peripheral_1 (Ingress/Egress's watchdog), each load of EVENTS consuming what it returns of them.
fn wait_events(events: u32, limit: u32) -> bool {
    ie_store(P_EVENTS_MASK, IE_EVENT_WATCHDOG);
    ie_store(WATCHDOG, limit);
    tile_store(EVENTS_MASK, events | event_peripheral(1));
    let mut seen = 0;
    loop {
        seen |= tile_load(EVENTS) & events;
        if seen == events {
            ie_store(WATCHDOG, 0);
            return true;
        }
        if ie_load(P_EVENTS_PEEK) & IE_EVENT_WATCHDOG != 0 {
            return false;
        }
        wfi();
    }
}

struct Master {
    /// The watchdog's limit for each wait for every arrival, from the records per tile.
    phase_limit: u32,
    /// The releases so far: the next one's index.
    releases: u32,
}

impl Master {
    /// Sleeps until the tiles of `expect` (ARRIVE_EXPECT_0-1's) have arrived: Ingress/Egress's all_arrived, through
    /// the tile's peripheral_1, with the watchdog set first. A request meanwhile is a tile's out of turn.
    fn wait_arrivals(&mut self, expect: [u32; 2]) -> Result<u32, Failure> {
        ie_store(ARRIVE_EXPECT, expect[0]);
        ie_store(ARRIVE_EXPECT + 4, expect[1]);
        ie_store(P_EVENTS_MASK, IE_EVENT_ALL_ARRIVED | IE_EVENT_WATCHDOG);
        ie_store(WATCHDOG, self.phase_limit);
        tile_store(EVENTS_MASK, event_peripheral(1) | EVENT_REQUEST_PENDING);
        loop {
            let events = ie_load(P_EVENTS_PEEK);
            if events & IE_EVENT_ALL_ARRIVED != 0 {
                let arrived_at = mcycle();
                ie_store(WATCHDOG, 0);
                return Ok(arrived_at);
            }
            if events & IE_EVENT_WATCHDOG != 0 {
                return Err(Failure::Timeout);
            }
            if tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
                return Err(Failure::EarlyRequest);
            }
            wfi();
        }
    }

    /// The barrier (17.6): every compute tile's arrival, then RELEASE, recorded.
    fn release_all(&mut self) -> Result<(), Failure> {
        let arrived_at = self.wait_arrivals([NONE, NONE])?;
        ie_store(RELEASE, 1);
        let released_at = mcycle();
        let sort = sort_summary();
        let index = sort.phases as usize;
        if index < MAX_PHASES {
            sort.phase[index] = [self.releases, arrived_at, released_at];
            sort.phases += 1;
        }
        self.releases += 1;
        Ok(())
    }

    // ---- The sort run ----

    /// The load (6.5): every compute tile's LOAD, and the tracer tile's, left unanswered (above), then the image, the
    /// arguments and the COUNT, all broadcast from the write unit, then the release.
    fn boot(&mut self, run: &Run) -> Result<(), Failure> {
        let mut loaded = [false; TILES];
        let mut loads = 0;
        let mut tracer_loaded = false;
        while loads < TILES || !tracer_loaded {
            if !wait_events(EVENT_REQUEST_PENDING, REPORT_LIMIT) {
                return Err(Failure::MissingLoads);
            }
            while tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
                let words = receive_request();
                let tile = requester(words[0]) as usize;
                if tile < TILES && request_op(words[0]) == OP_LOAD && !loaded[tile] {
                    loaded[tile] = true;
                    loads += 1;
                } else if tile as u32 == TRACER_TILE && request_op(words[0]) == OP_LOAD && !tracer_loaded {
                    tracer_loaded = true;
                } else {
                    sort_summary().strays += 1;
                }
            }
        }
        land_rows(BROADCAST, IMEM_BASE, RUN_HEADER_WORDS, run.imem_rows);
        land_rows(BROADCAST, run.dmem_address, RUN_HEADER_WORDS + 4 * run.imem_rows, run.dmem_rows);
        land_arguments(BROADCAST, run.arguments, run.records, run.format);
        send_count(BROADCAST);
        self.release_all()
    }

    /// The sort's schedule (sort/sw/main.c): a release at each of its barriers.
    fn sort(&mut self, format: u32) -> Result<(), Failure> {
        for _ in 0..barriers(format) {
            self.release_all()?;
        }
        Ok(())
    }

    /// Every compute tile's exit request, then its next LOAD, its image having ended (6.5), until each has come or
    /// the watchdog expires.
    fn collect_exits(&mut self) -> Result<(), Failure> {
        let mut loaded = [false; TILES];
        let mut loads = 0;
        let sort = sort_summary();
        while (sort.exits as usize) < TILES || loads < TILES {
            if !wait_events(EVENT_REQUEST_PENDING, REPORT_LIMIT) {
                return Err(Failure::MissingReports);
            }
            while tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
                let words = receive_request();
                let tile = requester(words[0]) as usize;
                let exited = tile < TILES && sort.exit[tile] != [0; 4];
                match request_op(words[0]) {
                    OP_EXIT if tile < TILES && !exited => {
                        sort.exit[tile] = words;
                        sort.exits += 1;
                        send_exit_reply(tile as u32, words[0]);
                    }
                    OP_LOAD if exited && !loaded[tile] => {
                        loaded[tile] = true;
                        loads += 1;
                    }
                    _ => sort.strays += 1,
                }
            }
        }
        Ok(())
    }

    /// The links' latency, the network idle: once every tile has exited and waits in its boot ROM for a load, its
    /// LOAD popped, each compute tile's load in turn, its `COUNT` alone, the image in place (6.5), unicast, and its
    /// arrival awaited alone (17.6: `ARRIVE_EXPECT` that tile), the cycles from the send to the wake recorded. No
    /// release follows: the tiles stay arrived.
    fn measure_links(&mut self) -> Result<(), Failure> {
        ie_store(P_EVENTS_MASK, IE_EVENT_ALL_ARRIVED | IE_EVENT_WATCHDOG);
        tile_store(EVENTS_MASK, event_peripheral(1));
        for tile in 0..TILES {
            let bit = 1 << (tile % 32);
            let expect = if tile < 32 { [bit, 0] } else { [0, bit] };
            ie_store(ARRIVE_EXPECT, expect[0]);
            ie_store(ARRIVE_EXPECT + 4, expect[1]);
            ie_store(WATCHDOG, LINE_LIMIT);
            tile_store(WRITE_HDR, header(MODE_COUNT, LOAD_COUNTER, SIZE_32, tile as u32));
            let cycles = send_and_sleep(0);
            loop {
                let events = ie_load(P_EVENTS_PEEK);
                if events & IE_EVENT_ALL_ARRIVED != 0 {
                    break;
                }
                if events & IE_EVENT_WATCHDOG != 0 {
                    return Err(Failure::Timeout);
                }
                wfi();
            }
            sort_summary().latency[tile] = cycles;
        }
        ie_store(WATCHDOG, 0);
        tile_store(WRITE_HDR, 0);
        Ok(())
    }

    // ---- The memory run ----

    /// A READ, a WRITE and the READ of its beats back, to the memory tile, which runs from its placed image.
    fn memory(&mut self) -> Result<(), Failure> {
        let memory = memory_summary();
        read_request(READ_LPDDR, READ_BEATS, address_of(&memory.read), address_of(&memory.replies[0]))?;
        // The WRITE: its reply on counter 3, then its beats into the part, on the memory tile's counter it names.
        tile_store(counter(3), 1);
        send_request([
            request_word_0(TARGET_MEMORY, OP_WRITE, WRITE_BEATS as u32),
            WRITE_LPDDR,
            0,
            address_of(&memory.part) | 3 << 24,
        ]);
        if !wait_events(event_counter_done(3), ANSWER_LIMIT) {
            return Err(Failure::NoAnswer);
        }
        let [part, part_counter, _, _] = memory.part;
        tile_store(WRITE_HDR, header(MODE_WRITE, part_counter, SIZE_128, MEMORY_TILE));
        tile_store(WRITE_DEST_ADDR, part);
        for beat in 0..WRITE_BEATS as u32 {
            let word = |w: u32| written_word(beat, w);
            tile_store(WRITE_W0, word(0));
            tile_store(WRITE_W1, word(1));
            tile_store(WRITE_W2, word(2));
            tile_store(WRITE_SEND_128, word(3));
        }
        read_request(WRITE_LPDDR, WRITE_BEATS, address_of(&memory.read_back), address_of(&memory.replies[1]))?;
        // The credits' requests: WRITEs of 0 beats, MEMORY_LIMIT at a time, their replies on counter 3, armed with
        // as many first.
        for batch in memory.answers.chunks(MEMORY_LIMIT) {
            tile_store(counter(3), batch.len() as u32);
            for answer in batch {
                send_request([request_word_0(TARGET_MEMORY, OP_WRITE, 0), 0, 0, address_of(answer) | 3 << 24]);
            }
            if !wait_events(event_counter_done(3), ANSWER_LIMIT) {
                return Err(Failure::NoAnswer);
            }
        }
        Ok(())
    }
}

// ---- The trace run ----

/// A segment (rtl/tile/base/spec.md 17.9.10), its schedule fixed in the images: the load, the replay image on every
/// compute tile and the tracer tile's image on the tracer tile, then the releases, TRACE_PHASES of them: the load's,
/// after which each compute tile arms for its window and the tracer tile, which takes no part in the barrier, runs the
/// segment; the windows' landing's, which starts every pass together; and the passes' end's. Then every tile's exit
/// request and its next LOAD, the tracer tile's served whenever it comes.
struct Segment {
    /// The releases so far: the next one's index.
    releases: u32,
}

impl Segment {
    fn run(&mut self, run: &Run) -> Result<(), Failure> {
        wait_loads()?;
        land_rows(BROADCAST, IMEM_BASE, RUN_HEADER_WORDS, run.imem_rows);
        land_rows(BROADCAST, run.dmem_address, RUN_HEADER_WORDS + 4 * run.imem_rows, run.dmem_rows);
        send_count(BROADCAST);
        let tracer = RUN_HEADER_WORDS + 4 * (run.imem_rows + run.dmem_rows);
        let [imem_rows, dmem_rows, dmem_address] = core::array::from_fn(|index| run_word(tracer + index));
        let rows = tracer + TRACER_IMAGE_WORDS;
        land_rows(TRACER_TILE, IMEM_BASE, rows, imem_rows as usize);
        land_rows(TRACER_TILE, dmem_address, rows + 4 * imem_rows as usize, dmem_rows as usize);
        send_count(TRACER_TILE);
        for _ in 0..TRACE_PHASES {
            self.release_all()?;
        }
        // The summary read afresh at each check: serve_trace writes it.
        while (trace_summary().exits as usize) < TILES || (trace_summary().loads as usize) < TILES + 1 {
            if !wait_events(EVENT_REQUEST_PENDING, REPORT_LIMIT) {
                return Err(Failure::MissingReports);
            }
            serve_trace()?;
        }
        trace_summary().error_lines = core::array::from_fn(|w| ie_load(ERROR_LINES + 4 * w));
        Ok(())
    }

    /// The barrier (17.6): every compute tile's arrival, the watchdog set first, every request meanwhile served, then
    /// RELEASE, recorded.
    fn release_all(&mut self) -> Result<(), Failure> {
        ie_store(ARRIVE_EXPECT, NONE);
        ie_store(ARRIVE_EXPECT + 4, NONE);
        ie_store(P_EVENTS_MASK, IE_EVENT_ALL_ARRIVED | IE_EVENT_WATCHDOG);
        ie_store(WATCHDOG, TRACE_LIMIT);
        tile_store(EVENTS_MASK, event_peripheral(1) | EVENT_REQUEST_PENDING);
        let arrived_at = loop {
            let events = ie_load(P_EVENTS_PEEK);
            if events & IE_EVENT_ALL_ARRIVED != 0 {
                let arrived_at = mcycle();
                ie_store(WATCHDOG, 0);
                break arrived_at;
            }
            if events & IE_EVENT_WATCHDOG != 0 {
                return Err(Failure::Timeout);
            }
            if tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
                serve_trace()?;
            }
            wfi();
        };
        ie_store(RELEASE, 1);
        let released_at = mcycle();
        let trace = trace_summary();
        if (trace.phases as usize) < TRACE_PHASES {
            trace.phase[trace.phases as usize] = [self.releases, arrived_at, released_at];
            trace.phases += 1;
        }
        self.releases += 1;
        Ok(())
    }
}

/// The boot's LOADs, every compute tile's and the tracer tile's, popped, or the watchdog's expiry; any other request
/// is a stray.
fn wait_loads() -> Result<(), Failure> {
    let mut loaded = [false; TILES];
    let mut loads = 0;
    let mut tracer_loaded = false;
    while loads < TILES || !tracer_loaded {
        if !wait_events(EVENT_REQUEST_PENDING, REPORT_LIMIT) {
            return Err(Failure::MissingLoads);
        }
        while tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
            let words = receive_request();
            let tile = requester(words[0]) as usize;
            if tile < TILES && request_op(words[0]) == OP_LOAD && !loaded[tile] {
                loaded[tile] = true;
                loads += 1;
            } else if tile as u32 == TRACER_TILE && request_op(words[0]) == OP_LOAD && !tracer_loaded {
                tracer_loaded = true;
            } else {
                trace_summary().strays += 1;
            }
        }
    }
    Ok(())
}

/// A trace run's requests, every one pending, popped: each compute tile's exit request and the tracer tile's, each
/// answered at once by its reply (11.4), and each tile's LOAD after it, its image's end (6.5), which stays
/// unanswered; an error report (6.5, a tile's error) ends the run.
fn serve_trace() -> Result<(), Failure> {
    let trace = trace_summary();
    while tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
        let words = receive_request();
        let tile = requester(words[0]);
        let op = request_op(words[0]);
        if op == OP_REPORT {
            if trace.reports == 0 {
                trace.report = words;
            }
            trace.reports += 1;
            return Err(Failure::Report);
        }
        let exit = if tile == TRACER_TILE { Some(&mut trace.tracer_exit) } else { trace.exit.get_mut(tile as usize) };
        match (op, exit) {
            (OP_EXIT, Some(exit)) if *exit == [0; 4] => {
                *exit = words;
                if tile != TRACER_TILE {
                    trace.exits += 1;
                }
                send_exit_reply(tile, words[0]);
            }
            (OP_LOAD, Some(exit)) if *exit != [0; 4] => trace.loads += 1,
            _ => trace.strays += 1,
        }
    }
    Ok(())
}

/// The written beats' words: word `w` of beat `beat`, as the chip's memory test computes them.
fn written_word(beat: u32, w: u32) -> u32 {
    0x5700_0000 | beat << 8 | w
}

/// A DMEM address of the master's, a request's byte address there (11.5).
fn address_of<T>(at: &T) -> u32 {
    at as *const T as u32
}

/// A READ (11.5): `beats` beats from LPDDR beat address `lpddr` into DMEM at `data`, on counter 1, and its reply
/// at `reply`, on counter 2, both armed first (11.4); then the wait for both.
fn read_request(lpddr: u32, beats: usize, data: u32, reply: u32) -> Result<(), Failure> {
    tile_store(counter(1), beats as u32);
    tile_store(counter(2), 1);
    send_request([request_word_0(TARGET_MEMORY, OP_READ, beats as u32 | 1 << 10), lpddr, data, reply | 2 << 24]);
    if wait_events(event_counter_done(1) | event_counter_done(2), ANSWER_LIMIT) {
        Ok(())
    } else {
        Err(Failure::NoAnswer)
    }
}

/// A store of `data` to WRITE_SEND_32 and, right after it, a WFI: the cycles from just before the store to just after
/// the wake.
fn send_and_sleep(data: u32) -> u32 {
    let (start, end): (u32, u32);
    // SAFETY: a store to the write unit's send and a WFI; mstatus.MIE is 0, so no interrupt is taken.
    unsafe {
        asm!(
            "csrr {start}, mcycle",
            "sw {data}, 0({send})",
            "wfi",
            "csrr {end}, mcycle",
            start = out(reg) start,
            end = out(reg) end,
            data = in(reg) data,
            send = in(reg) TILE_WINDOW + WRITE_SEND_32,
            options(nostack),
        )
    };
    end.wrapping_sub(start)
}

/// The summary's done word, written last; then the master parks for good.
fn finish(failure: Option<Failure>) -> ! {
    let summary = summary();
    if let Some(failure) = failure {
        summary.failure = failure as u32;
        if summary.kind == KIND_SORT {
            summary.failure_phase = sort_summary().phases;
        } else if summary.kind == KIND_TRACE {
            summary.failure_phase = trace_summary().phases;
        }
    }
    compiler_fence(Ordering::SeqCst);
    // SAFETY: the summary's done word, in DMEM.
    unsafe { write_volatile(&raw mut summary.done, DONE) };
    park()
}

/// Parks: no event enabled, WFI for good.
fn park() -> ! {
    // SAFETY: disables every interrupt, then waits; nothing wakes the core.
    unsafe { asm!("csrw mie, zero") };
    loop {
        wfi();
    }
}

/// The trap handler, the master's every trap (6.2): the line checks' NMI, or any other, which ends the run. It
/// records mcause, mepc and ERROR_LINES, runs the line checks if they caused it, then takes the tiles' reports 24
/// and 25 (6.5) until every tile's have come or the watchdog expires, and finishes.
#[unsafe(no_mangle)]
pub extern "C" fn trap() -> ! {
    let (cause, pc): (u32, u32);
    // SAFETY: reads CSRs.
    unsafe { asm!("csrr {0}, mcause", "csrr {1}, mepc", out(reg) cause, out(reg) pc, options(nomem, nostack)) };
    // SAFETY: written once, by main, before the NMI it expects.
    let line_checks = unsafe { read_volatile(&raw const LINE_CHECKS) };
    if summary().kind != KIND_SORT {
        finish(Some(Failure::Trap));
    }
    let sort = sort_summary();
    sort.trap_cause = cause;
    sort.trap_pc = pc;
    sort.trap_error_lines = core::array::from_fn(|w| ie_load(ERROR_LINES + 4 * w));
    let expected = line_checks && cause == CAUSE_NMI;
    if expected {
        check_lines(sort);
    }
    sort.error_lines = core::array::from_fn(|w| ie_load(ERROR_LINES + 4 * w));
    collect_error_reports(sort);
    finish(if expected { None } else { Some(Failure::Trap) })
}

/// The line checks, after the memory tile's: a bad packet to each compute tile in turn, and the cycles from its send
/// until its bit of ERROR_LINES rises, polled.
fn check_lines(sort: &mut SortSummary) {
    for tile in 0..TILES {
        let word = ERROR_LINES + 4 * (tile / 32);
        let bit = 1 << (tile % 32);
        let start = mcycle();
        send_bad_packet(tile as u32);
        loop {
            let cycles = mcycle().wrapping_sub(start);
            if ie_load(word) & bit != 0 {
                sort.line_latency[tile] = cycles;
                break;
            }
            if cycles > LINE_LIMIT {
                break;
            }
        }
    }
}

/// The tiles' reports 24 and 25, by requester, until every tile's have come (the compute tiles' and the memory
/// tile's) or the watchdog expires.
fn collect_error_reports(sort: &mut SortSummary) {
    while (sort.error_reports as usize) < 2 * (TILES + 1) {
        if !wait_events(EVENT_REQUEST_PENDING, REPORT_LIMIT) {
            return;
        }
        while tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING != 0 {
            let words = receive_request();
            let tile = requester(words[0]) as usize;
            let index = if tile < TILES { tile } else { TILES };
            let number = (words[0] >> 18) & 0xFF;
            let slot = match number {
                REPORT_ERRORS => 0,
                REPORT_MTVAL => 3,
                _ => continue,
            };
            if request_op(words[0]) == OP_REPORT && (tile < TILES || tile as u32 == MEMORY_TILE) {
                sort.error_report[index][slot..slot + 3].copy_from_slice(&words[1..]);
                sort.error_reports += 1;
            }
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    // The handler, mtvec's (6.2); mstatus.MIE stays 0 and mie.MEIE 1, as the boot ROM left them.
    // SAFETY: points mtvec at the handler's entry.
    unsafe { asm!("la {0}, master_trap_entry", "csrw mtvec, {0}", out(reg) _) };
    let Some(run) = run_image() else {
        finish(Some(Failure::Image));
    };
    summary().kind = run.kind;
    let mut master = Master { phase_limit: PHASE_LIMIT + PHASE_LIMIT_PER_RECORD * run.records, releases: 0 };
    let result = match run.kind {
        KIND_SORT => master
            .boot(&run)
            .and_then(|()| master.sort(run.format))
            .and_then(|()| master.collect_exits())
            .and_then(|()| master.measure_links()),
        KIND_MEMORY => {
            // Field by field, in place: the summary is larger than the stack.
            let memory = memory_summary();
            memory.part = [NONE; 4];
            memory.replies = [[NONE; 4]; 2];
            memory.read.fill([0; 4]);
            memory.read_back.fill([0; 4]);
            memory.answers.fill([NONE; 4]);
            let result = master.memory();
            finish(result.err());
        }
        KIND_TRACE => {
            // Field by field, in place, as the memory run's.
            let trace = trace_summary();
            trace.phases = 0;
            trace.exits = 0;
            trace.loads = 0;
            trace.strays = 0;
            trace.reports = 0;
            trace.report = [0; 4];
            trace.tracer_exit = [0; 4];
            trace.exit.fill([0; 4]);
            trace.phase.fill([0; 3]);
            trace.error_lines = [NONE; 3];
            let result = Segment { releases: 0 }.run(&run);
            finish(result.err());
        }
        _ => Err(Failure::Image),
    };
    if let Err(failure) = result {
        finish(Some(failure));
    }
    // The line checks: the memory tile's bad packet, whose NMI enters the handler, which does the rest.
    // SAFETY: read by the handler only after the NMI this enables.
    unsafe { write_volatile(&raw mut LINE_CHECKS, true) };
    let start = mcycle();
    send_bad_packet(MEMORY_TILE);
    while mcycle().wrapping_sub(start) < LINE_LIMIT {}
    finish(Some(Failure::NoNmi))
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    finish(Some(Failure::Panic))
}
