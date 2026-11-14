//! The compute tiles' replay image: the segment's replay side (rtl/tile/base/spec.md 17.9.10), the replayer's controller
//! loop (17.8.13; meshsim v1.35's `replay_ctl.c`, its layout 5) on this repository's map, with no commands: a fixed
//! schedule of two barriers after the load's release, and the requests to the tracer tile, `ARMED` and
//! `STREAMS_ARMED`, as the specification's handshakes.
//!
//! The master tile loads it on every compute tile at its `LOAD` (6.5, the chip's test's trace run: src/bin/master.rs),
//! and the boot ROM starts it at the load's release. The segment's header and the replayer's tables are in the tile's
//! scratchpad, where the chip's test writes them before the run as the host would (src/lib.rs, `segment`): the windows,
//! their rows, RUN_CONFIG, the decode table and the ROM, the core-fed set's index and templates (17.8.13: every tile
//! keeps the whole set), and the replay region the tracer tile sends the tile's window into, its record and its three
//! streams. Tile n holds window n, if the segment has one for it.
//!
//! 1. **Arming** (17.9.10), woken by the load's release, every counter at 0: COUNTER_START for the record's start
//!    (words 0 and 2-66) and COUNTER_TOTALS for its totals (words 1 and 67), then `ARMED` to the tracer tile.
//! 2. **What needs no part of the window**, while the tracer tile traces it: the decode table and the ROM into the
//!    replayer's region (17.8.9); the core-fed set (src/lib.rs, `load_core_fed_set`, `Feeder::new`, which the tracer
//!    tile's image shares): its index and the shared NOP row into DMEM, its lookup, an open-addressed hash of the
//!    templates' keys (`replay_ctl.c`'s `build_hash`) and an empty cache of code values, and each template's first
//!    chunk, half a queue or the whole template, in a STREAM entry of its own (`replay_ctl.c`'s preloaded streams).
//! 3. **The pass's setup** (17.8.13): RUN_CONFIG, and the record's words 0-65 reloaded verbatim from the replay region
//!    into part 0, the state word first: words 0 and 2-65 once the record's start has landed, while the tracer tile
//!    traces the window, word 1, the stream word, with the totals (step 4). It needs the record, not the release; the
//!    pass starts at the release (17.9.10).
//! 4. **The streams' handshake**: once the totals have landed, the stream word into DMEM and into part 0, one transfer;
//!    its word counts (17.8.2) give the streams' entries, COUNTER_STREAMS armed for their sum, then `STREAMS_ARMED`.
//!    The tracer tile pops `ARMED` before it sends the totals, so the tile has one request to it outstanding at a time
//!    (11.3). Then the wait for the streams.
//! 5. **The first barrier** (7.3): every window has landed, every counter at 0.
//! 6. **The pass** (17.8.10, 17.8.13; `replay_ctl.c`'s `run_pass`): START, then, on the replayer's events through the
//!    tile's peripheral_1, each load of its EVENTS taking a credit from every credit bit its EVENTS_MASK selects:
//!    - **a new expansion** first (17.8.6), src/lib.rs's `Feeder::present`: the expansion credit taken (a new
//!      expansion fills it, 17.8.8, and its first chunk takes one), then its first chunk by the template's STREAM
//!      entry, the template's first word carrying its rd0, the template found by its code value in a cache of the code
//!      values seen; a code value not there goes the general way, its template found by its key: the pass's first
//!      expansion may start mid-sequence, at `EXPANSION_OFFSET` (17.8.2: the row offset at a pass's start), its first
//!      word then sent by the write unit carrying the template's rd0, and under a NOP policy with rd x0 the shared NOP
//!      row goes alone, carrying the template's rd0 (17.8.13);
//!    - **the refills**, one transfer: each credit taken answered with half a queue of its stream from the replay
//!      region, or the rest, then the expansion's next chunk on its credit, half a queue or the rest; a queue with
//!      nothing behind its credits out of EVENTS_MASK;
//!    - each transfer landed before the next WFI, so that one source at a time feeds a queue (17.3), credits returned
//!      meanwhile waking the next; to `done`.
//!
//!    Then its checks: the replayer's ERRORS 0 (no underflow, no leftover, 17.8.10) and ROWS_RUN its rows.
//! 7. **The second barrier**: every pass has ended.
//! 8. **The end**: the exit request to the master (`OP_EXIT`): word 1 the status (`Status`), word 2 the replayer's
//!    ERRORS and, from bit 16, the tile's, word 3 ROWS_RUN; its reply awaited; then the jump to the reset PC (6.5).
//!
//! A tile with no window takes part in both barriers and exits. The window's end state is the replayer's own: no core
//! path reads it (17.8.13), so the chip's test compares it with the next window's start.
//!
//! **Its cost** (rtl/chip/README.md, Results): from the release, some 155 cycles to the first row; then the rows'
//! schedules, 3 cycles a row and 5 an M row (17.8.7); and 50 cycles of the replayer's stall for each core-fed expansion
//! whose code value the cache holds, the post's wake and some 29 cycles of this core to the transfer's start, then the
//! transfer to the first row. An expansion waits longer in three cases: some 22 cycles more when it is posted in the
//! row after another's first row, the controller then still at that one's transfer's end, on its way back to its WFI;
//! the rest of a refill's transfer when one holds the DMA unit; and some 140 to 160 cycles at a code value's first in
//! the window, the general way, every tile's cache empty at its pass's start. Each part is written for this core,
//! whose register-window loads take 2 cycles and taken branches 4.
//!
//! Where it differs from `replay_ctl.c`, the replayer sees the same packets on each queue, in the same order, on the
//! same credits, and its record's words 0-65 with the state word first:
//! - the lookup: `replay_ctl.c` finds each expansion's template by its key, in a direct-mapped table of the window's
//!   bitmap's templates (its fast path) or in its hash (its slow path); here the cache of code values comes first and
//!   the hash after it, for a value's first expansion, so that no expansion but those computes a key (some 30 cycles of
//!   this core for a CSR instruction, against the cache's 3 or 4), and nothing needs the bitmap or a multiplier search;
//! - steps 2 and 3 come before the release, while the tracer tile traces and delivers the window, where `replay_ctl.c`
//!   does them after its final GO, and the reload is two transfers, around the totals, where `replay_ctl.c`'s is one
//!   of the 66 words;
//! - a wait for a transfer is `wait_all`'s, its counter's done and `dma_done` each consumed as seen, for want of
//!   meshsim's EVENTS_REQUIRED.
//!
//! The core-fed set's tables, 6 KiB, are in .noinit (loaded.ld): the start-up zeroes .bss before main, some 7 cycles a
//! word, which would hold `ARMED` back by some 11,000 cycles; `Feeder::new` empties what needs it (the hash's keys,
//! the cache's code values) once the image has sent `ARMED`.

#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use tile::*;

global_asm!(include_str!("../../loaded_start.S"));

// ---- The replayer, peripheral 1 (17.8.1) ----

const RUN_CONFIG: usize = 0x080;
const ROWS_RUN: usize = 0x084;
const P_ERRORS: usize = 0x008;

// Its EVENTS: done, then each input queue's credit (code, read log, write log, expansion) and new_expansion.
const DONE: u32 = 1 << 0;
const fn credit(queue: usize) -> u32 {
    1 << (2 + queue)
}
const EXPANSION_CREDIT: u32 = credit(3);
const CREDITS: u32 = credit(0) | credit(1) | credit(2) | EXPANSION_CREDIT;
const NEW_EXPANSION: u32 = 1 << 6;
/// RUN_CONFIG's rows and store_log (17.8.1).
const ROWS_MASK: u32 = 0xFF_FFFF;
const STORE_LOG: u32 = 1 << 24;

/// The record's register image, words 2-65 (17.8.2).
const IMAGE_WORDS: u32 = 64;

/// The image's status, the exit request's word 1.
#[derive(Clone, Copy)]
enum Status {
    Passed = 0,
    /// The header is not the segment's.
    Header = 1,
    /// A core-fed expansion's code value has no template in the set.
    NoTemplate = 2,
    /// The pass ended with the replayer's ERRORS non-zero.
    Errors = 3,
    /// ROWS_RUN is not the pass's rows.
    Rows = 4,
}

static HEADER: Buffer<{ segment::WORDS }> = Buffer::new();
static ROW: Buffer<4> = Buffer::new();
static REPLY: Buffer<4> = Buffer::new();

fn header(index: usize) -> u32 {
    HEADER.word(index)
}

fn replayer_load(offset: usize) -> u32 {
    load(PERIPHERAL + offset)
}

fn replayer_store(offset: usize, value: u32) {
    store(PERIPHERAL + offset, value)
}

/// A request to the tracer tile (17.9.10): the op alone.
fn tell_tracer(op: u32) {
    send_request([request_word_0(TARGET_TRACER, op, 0), 0, 0, 0]);
}

// ---- Before the release ----

/// The window's arrival (17.9.10), and the rest of what the pass needs before its START: the counters armed and
/// `ARMED`; meanwhile the tables and the core-fed set (17.8.13), its index and the shared NOP row in DMEM, and its
/// lookup built by the expansions' feeder (src/lib.rs, `load_core_fed_set`, `Feeder::new`); once the record's start
/// has landed, while the tracer tile traces the window, RUN_CONFIG and the record's start reloaded into part 0; once the
/// totals have landed, the stream word into DMEM and into part 0, the streams' counter and `STREAMS_ARMED`; then the
/// streams landed. The pass, from the stream word, with the feeder.
fn receive_window() -> Pass {
    tile_store(counter(COUNTER_START), START_WORDS);
    tile_store(counter(COUNTER_TOTALS), TOTALS_WORDS);
    tell_tracer(OP_ARMED);
    // The decode table and the ROM, one transfer into the region's part 0 (17.8.9).
    set_stream(0, header(segment::TABLES), peripheral_region(1) | REGION_TABLES, TABLE_ROWS, COUNTER_LOCAL, 0);
    run_streams(1, TABLE_ROWS, COUNTER_LOCAL);
    let templates = header(segment::TEMPLATES_COUNT).min(MAX_TEMPLATES as u32);
    load_core_fed_set(header(segment::INDEX), templates, header(segment::TABLES), 0);
    let feeder = Feeder::new(templates, header(segment::TEMPLATES), 0);
    // The pass's setup (17.8.13): RUN_CONFIG, and the record's words 0-65 reloaded verbatim into part 0, the state
    // word first (17.8.2: the record is the working state). Words 0 and 2-65 come with the record's start, word 1, the
    // stream word, with the totals: each goes once it has landed, so that the reload meets no stream on its way in.
    let record = header(segment::RECORD);
    wait_all(event_counter_done(COUNTER_START));
    replayer_store(RUN_CONFIG, header(segment::RUN_CONFIG));
    set_stream(0, record, region_part(0), 1, COUNTER_LOCAL, 0);
    set_stream(1, record + 4 * 2, region_part(0) + 16 * 2, IMAGE_WORDS, COUNTER_LOCAL, 0);
    run_streams(2, 1 + IMAGE_WORDS, COUNTER_LOCAL);
    set_stream(0, record + 4, ROW.address(), 1, COUNTER_LOCAL, 0);
    set_stream(1, record + 4, region_part(0) + 16, 1, COUNTER_LOCAL, 0);
    wait_all(event_counter_done(COUNTER_TOTALS));
    run_streams(2, 2, COUNTER_LOCAL);
    let [low, high, _, _] = ROW.row(0);
    let entries = [low & 0xFFFF, low >> 16, high & 0xFFFF];
    let total = entries[0] + entries[1] + entries[2];
    if total != 0 {
        tile_store(counter(COUNTER_STREAMS), total);
    }
    tell_tracer(OP_STREAMS_ARMED);
    let pass = Pass::new(entries, feeder);
    if total != 0 {
        wait_all(event_counter_done(COUNTER_STREAMS));
    }
    pass
}

// ---- The pass ----

/// The pass's controller (`replay_ctl.c`'s `run_pass`): each stream's base, entries and entries sent, and the
/// core-fed expansions' feeder, which keeps the replayer's EVENTS_MASK.
struct Pass {
    rows: u32,
    bases: [u32; 3],
    entries: [u32; 3],
    sent: [u32; 3],
    feeder: Feeder<EXPANSION_CREDIT, false>,
}

impl Pass {
    /// The pass over streams of `entries` entries, its expansions fed by `feeder`: the streams it uses in EVENTS_MASK,
    /// each while it has entries to send, the write log only with the store log (17.8.3).
    fn new(entries: [u32; 3], mut feeder: Feeder<EXPANSION_CREDIT, false>) -> Self {
        let run_config = header(segment::RUN_CONFIG);
        let used = [true, true, run_config & STORE_LOG != 0];
        let mut mask = DONE | NEW_EXPANSION;
        for queue in 0..3 {
            if used[queue] && entries[queue] != 0 {
                mask |= credit(queue);
            }
        }
        feeder.mask = mask;
        Pass {
            rows: run_config & ROWS_MASK,
            bases: [header(segment::CODE), header(segment::READ_LOG), header(segment::WRITE_LOG)],
            entries,
            sent: [0; 3],
            feeder,
        }
    }

    /// The credits `events` took, answered in one transfer (`replay_ctl.c`'s refills): each stream's half a queue or
    /// its rest, then the expansion's next chunk, half a queue or the rest, each the transfer's next STREAM entry. A
    /// queue with nothing left leaves the mask.
    fn refill(&mut self, events: u32) {
        let mut streams = 0;
        let mut total = 0;
        self.refill_stream(0, events, &mut streams, &mut total);
        self.refill_stream(1, events, &mut streams, &mut total);
        self.refill_stream(2, events, &mut streams, &mut total);
        if events & EXPANSION_CREDIT != 0 && self.feeder.feeding() {
            total += self.feeder.refill(streams);
            streams += 1;
        }
        self.feeder.store_mask();
        if total != 0 {
            start_transfer(0, streams as u32, total);
            wait_transfer();
        }
    }

    /// Stream `queue`'s refill on its credit, if `events` took one.
    #[inline(always)]
    fn refill_stream(&mut self, queue: usize, events: u32, streams: &mut usize, total: &mut u32) {
        if events & credit(queue) == 0 {
            return;
        }
        let count = HALF_QUEUE.min(self.entries[queue] - self.sent[queue]);
        let source = self.bases[queue] + 4 * self.sent[queue];
        set_stream(*streams, source, region_part(queue as u32 + 1), count, COUNTER_LOCAL, 0);
        *streams += 1;
        *total += count;
        self.sent[queue] += count;
        if self.sent[queue] == self.entries[queue] {
            self.feeder.mask &= !credit(queue);
        }
    }
}

/// The pass over the window (17.8.10, 17.8.13), its RUN_CONFIG and its record loaded before the release: on each
/// wake, a new expansion first (src/lib.rs, `Feeder::present`), then the refills.
fn replay(pass: &mut Pass) -> Status {
    replayer_store(P_EVENTS_MASK, pass.feeder.mask);
    replayer_store(P_START, 1);
    loop {
        // The tile's EVENTS_MASK, the replayer's line: each wait for a transfer leaves it changed.
        tile_store(EVENTS_MASK, event_peripheral(1));
        wfi();
        let mut events = replayer_load(P_EVENTS) & pass.feeder.mask;
        if events & NEW_EXPANSION != 0 {
            if pass.feeder.present(events).is_err() {
                return Status::NoTemplate;
            }
            events &= !EXPANSION_CREDIT;
        }
        if events & DONE != 0 {
            break;
        }
        if events & CREDITS != 0 {
            pass.refill(events);
        }
    }
    replayer_store(P_EVENTS_MASK, 0);
    if replayer_load(P_ERRORS) != 0 {
        Status::Errors
    } else if replayer_load(ROWS_RUN) != pass.rows {
        Status::Rows
    } else {
        Status::Passed
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    copy_to_dmem(0, HEADER.address(), (segment::WORDS / 4) as u32, COUNTER_LOCAL, 0);
    let valid = header(segment::MAGIC) == segment::REPLAY_MAGIC;
    let window = valid && tile_id() < header(segment::WINDOWS);
    let mut pass = if window { Some(receive_window()) } else { None };
    arrive_and_wait();
    let status = match &mut pass {
        Some(pass) => replay(pass),
        None if valid => Status::Passed,
        None => Status::Header,
    };
    arrive_and_wait();
    let errors = replayer_load(P_ERRORS) | tile_load(ERRORS) << 16;
    exit_to_master([status as u32, errors, replayer_load(ROWS_RUN)], COUNTER_REPLY, &REPLY);
    end_image()
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop {
        wfi();
    }
}
