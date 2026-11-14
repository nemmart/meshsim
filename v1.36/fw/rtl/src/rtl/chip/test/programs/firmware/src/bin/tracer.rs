//! The tracer tile's firmware: the segment around the Tracer, its peripheral 1 (rtl/tile/base/spec.md 17.9.10,
//! 17.9.11), meshsim v1.35's C&C 3 (`tracer_ctl.c`) on this repository's tracer tile, with no commands: the compute
//! tiles' requests, `ARMED` and `STREAMS_ARMED`, are its handshakes, so it assumes no timing.
//!
//! The master tile loads it at the tracer tile's `LOAD` (6.5; src/bin/master.rs's trace run), and the boot ROM starts it
//! once the load has landed: the tracer tile takes no part in the barrier (7.3). The segment is in the tile's
//! scratchpad, where the chip's test writes it before the run as the host would (src/lib.rs, `segment`): its header
//! (the windows, their rows, RUN_CONFIG, the map's constants), the tables, the core-fed set's index and templates, the
//! loaded record (the segment's start, its advice field the advice's values), the advice, two values an entry, the
//! expected end state, and the replay region's addresses on the compute tiles. The guest's memory and the map are
//! files behind the Tracer's fill port (17.9.5), outside the chip.
//!
//! 1. **The segment's start** (17.9.11), the Tracer idle: the core-fed set's index and the shared NOP row into DMEM
//!    (src/lib.rs, `load_core_fed_set`); HASH_MUL, HASH_STEP, HASH_LINES and RUN_CONFIG; then one transfer of the DMA
//!    unit's from the scratchpad into the Tracer's region, the decode table and the ROM, then the record, state word
//!    first (the state write, 17.9.3), landed on COUNTER_LOCAL, while the core builds the set's lookup (`Feeder::new`:
//!    the hash of the templates' keys, an empty cache of code values, and each template's first chunk in a `STREAM`
//!    entry of its own).
//! 2. **Window 0's start** (17.9.10): once tile 0's `ARMED` has come, the record's words 0 and 2-66 from the tile's own
//!    copy, by the DMA unit to tile 0's replay region, counted on its COUNTER_START; before any run, so that only one
//!    of the tile's sending engines sends at a time.
//! 3. **The runs**, one a window (`Controller::run`), on the Tracer's events through the tile's peripheral_1 and on the
//!    compute tiles' requests (request_pending), each request popped as it comes, the requester its tile (11.2):
//!    - **the rows**: START, then each new expansion fed first, the Tracer waiting for its words (src/lib.rs, `Feeder`:
//!      the code value's template from a cache of the values seen, its first chunk sent by its `STREAM` entry, an
//!      inline's number stored in `EXPANSION_TEMPLATE` before it; a value not in the cache found by its key, from
//!      `EXPANSION_OFFSET`), its later chunks on its credits, the advice on its credits, half a queue or the rest
//!      (17.9.3), both in one transfer when both credits come together, and a request popped a wake; to the rows' end,
//!      `state_ready`;
//!    - **the drain**, one transfer of the Tracer's DMA at a time (17.4), each one's next once its `dma_done` has come,
//!      so that a chain goes back to back: output queue 0 in its four transfers, word 0 and words 2-66 to tile n + 1's
//!      record, on its COUNTER_START, and words 1 and 67 to tile n's, on its COUNTER_TOTALS, once both tiles' `ARMED`
//!      have come, so that tile n's totals land before its streams; then output queues 1-3 into tile n's streams, on
//!      its COUNTER_STREAMS, each credit as its queue holds it, 128 entries or exactly the rest from the length words
//!      (17.9.11), once tile n's `STREAMS_ARMED` has come; to `done`. At the last run, the record's 68 words to the
//!      tile itself, its own id as dst, landed on its COUNTER_START, and then, once tile n's `ARMED` has come, words 1
//!      and 67 forwarded from there to tile n by the DMA unit.
//! 4. **The segment's end** (17.9.11): the last record's advice field 0, and its end state, words 0 and 2-66, the
//!    expected one, the next segment's start.
//! 5. **The end**: the exit request to the master (`OP_EXIT`): word 1 the status (`Status`), word 2 the Tracer's
//!    ERRORS and, from bit 16, the tile's, word 3 the segment's cycles from the first START to the last `done`; its
//!    reply awaited; then the jump to the reset PC (6.5), whose `LOAD` the master leaves unanswered.
//!
//! A Tracer error stops the Tracer (17.9.9) and raises the tile's NMI, whose boot ROM handler reports it (6.5). The
//! image keeps a report of its cycles in its DMEM (`report`), which the chip's test reads after the run.
//!
//! **Its cost** (rtl/chip/README.md, The trace's cycles): the Tracer waits some 55 cycles for a core-fed expansion
//! whose code value the cache holds, the post's wake, some 39 cycles of this core from its load of EVENTS to the
//! transfer's start, then the transfer to its first row, and some 110 cycles of the core more at a value's first, the
//! general way; a run's drain is its scans' at a real window's size, and at a few thousand rows its transfers' and its
//! handshakes', some 1,050 to 1,300 cycles a window; the segment's start takes some 5,600 cycles from main, the
//! lookup's build some 5,100 of them, and the end's checks some 1,700. Each part is written for this core, whose
//! register-window loads take 2 cycles and taken branches 4.
//!
//! Where it differs from `tracer_ctl.c`, the Tracer sees the same events and packets on each queue, in the same order,
//! on the same credits, the same stores, `EXPANSION_TEMPLATE` before an inline's first word, and the tiles the same
//! handshakes:
//! - the lookup: `tracer_ctl.c` finds each expansion's template by its key in its hash; here a cache of code values
//!   comes first, filled at a value's first expansion, which goes by the key and the hash, so that no other computes a
//!   key, and the first chunk is a `STREAM` entry set at the start; a value's first expansion reads EXPANSION_OFFSET,
//!   which the Tracer makes non-zero only at the segment's first, while the cache is empty (17.9.4, 17.9.11);
//! - the controller loop is two, the rows' and the drain's, split at `state_ready`: no drain event comes before the
//!   rows' end, nor a new expansion after it, and the drain's loop serves the credits as the rows' does;
//! - an expansion's chunk and the advice, their credits taken in one load, go in one transfer, the chunk first, where
//!   `tracer_ctl.c` makes two, in that order;
//! - the setup before the first START is ordered for this core: the constants before the tables, then the tables and the
//!   record in one transfer, while the lookup is built; there is no "started" request, nor `stage_delay`: the requests
//!   are the handshakes.
//!
//! Its DMEM is in .noinit (loaded.ld): the start-up zeroes .bss before main, some 7 cycles a word, which would hold the
//! first START back by some 7,500 cycles; each buffer is landed before it is read, and the lookup empties its tables
//! itself.

#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use tile::*;

global_asm!(include_str!("../../loaded_start.S"));

// ---- The Tracer, peripheral 1 (17.9.1) ----

const RUN_CONFIG: usize = 0x080;
const CODE_LENGTH: usize = 0x094; // then READ_LOG_LENGTH and WRITE_LOG_LENGTH
const HASH_MUL: usize = 0x0A0;
const HASH_STEP: usize = 0x0A4;
const HASH_LINES: usize = 0x0A8;
/// The statistics counters, STAT_CYCLES then the seven others (17.9.8).
const STATS: usize = 0x0B0;
const P_ERRORS: usize = 0x008;

// Its EVENTS.
const DONE: u32 = 1 << 0;
const DMA_DONE: u32 = 1 << 1;
const ADVICE_CREDIT: u32 = 1 << 2;
const EXPANSION_CREDIT: u32 = 1 << 3;
const NEW_EXPANSION: u32 = 1 << 4;
const COUNTED: u32 = DONE | DMA_DONE | ADVICE_CREDIT | EXPANSION_CREDIT | NEW_EXPANSION;
/// Output queue q's `_ready`, q = 0-3: state_ready, code_ready, read_log_ready, write_log_ready.
const fn ready(queue: usize) -> u32 {
    1 << (5 + queue)
}

/// Output queue 0's four transfers (17.9.10): its words from each one's first, to tile n + 1 (1) or tile n (0).
const PIECES: [(u32, u32, u32); 4] = [(0, 1, 1), (1, 1, 0), (2, 65, 1), (67, 1, 0)];

/// The tracer tile's status, the exit request's word 1.
#[derive(Clone, Copy, PartialEq)]
enum Status {
    Passed = 0,
    /// The header is not the segment's.
    Header = 1,
    /// A core-fed expansion's code value has no template in the set.
    NoTemplate = 2,
    /// The last record's advice field is not 0.
    AdviceLeft = 3,
    /// The last record's end state is not the expected one.
    EndState = 4,
    /// A request of no handshake came.
    Stray = 5,
}

// DMEM, in .noinit (loaded.ld), each landed before it is read: the header, the last record and the expected end
// state, and the exit's reply.
#[unsafe(link_section = ".noinit")]
static HEADER: Buffer<{ segment::WORDS }> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static FINAL: Buffer<{ 4 * RECORD_WORDS as usize }> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static EXPECTED: Buffer<{ 4 * RECORD_WORDS as usize }> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static REPLY: Buffer<4> = Buffer::new();

/// The report of the trace's cycles, which the chip's test reads from DMEM once the run has ended and prints
/// (rtl/chip/test/src/chip_trace_test.rs, `print_trace`; rtl/chip/README.md, Results): mcycle values (6.1), the chip's
/// one clock, as the master's, and counts, its words by index.
mod report {
    /// main's entry; the Tracer and the core-fed set ready; tile 0's `ARMED` seen; the end's checks done.
    pub const STARTED: usize = 0;
    pub const READY: usize = 1;
    pub const ARMED: usize = 2;
    pub const ENDED: usize = 3;
    /// The core-fed expansions that went the general way.
    pub const GENERAL: usize = 4;
    /// The statistics counters at the segment's end, eight (17.9.8).
    pub const STATS: usize = 8;
    /// Window n's: its START, its rows' end, as its core's wake sees `state_ready` (17.9.6; `Controller::rows`), and its
    /// `done`, from 16 + 3n.
    pub const WINDOW: usize = 16;
    pub const WORDS: usize = WINDOW + 3 * 64;
}
#[unsafe(link_section = ".summary")]
static REPORT: Buffer<{ report::WORDS }> = Buffer::new();

fn header(index: usize) -> u32 {
    HEADER.word(index)
}

fn tracer_load(offset: usize) -> u32 {
    load(PERIPHERAL + offset)
}

fn tracer_store(offset: usize, value: u32) {
    store(PERIPHERAL + offset, value)
}

/// A transfer of the Tracer's DMA (17.4): `count` entries of output queue `queue` to `dst`'s `address`, `WRITE`s
/// counted on `counter_number`.
fn start_dma(queue: u32, count: u32, dst: u32, address: u32, counter_number: u32) {
    tracer_store(P_DMA_HDR, header_word(counter_number, dst) | count << 15 | queue << 27);
    tracer_store(P_DMA_DEST_ADDR, address);
    tracer_store(P_DMA_START, 1);
}

/// DMA_HDR's header block and dst (17.1): its size is the DMA's own.
fn header_word(counter_number: u32, dst: u32) -> u32 {
    tile::header(MODE_WRITE, counter_number, 0, dst)
}

/// The compute tiles' handshakes so far, a bit per tile (17.9.10).
#[derive(Default)]
struct Handshakes {
    armed: u64,
    streams_armed: u64,
    /// Requests of no handshake: from no compute tile, or of another op.
    strays: u32,
}

impl Handshakes {
    /// The oldest request, if one is pending, popped (its word 0 alone holds the handshake): whether one was.
    fn pop_one(&mut self) -> bool {
        if tile_load(EVENTS_PEEK) & EVENT_REQUEST_PENDING == 0 {
            return false;
        }
        let word_0 = receive_word_0();
        let tile = requester(word_0);
        match request_op(word_0) {
            OP_ARMED if tile < 64 => self.armed |= 1 << tile,
            OP_STREAMS_ARMED if tile < 64 => self.streams_armed |= 1 << tile,
            _ => self.strays += 1,
        }
        true
    }

    /// Every request pending, popped.
    fn pop(&mut self) {
        while self.pop_one() {}
    }

    fn armed(&self, tile: u32) -> bool {
        self.armed >> tile & 1 != 0
    }

    fn streams_armed(&self, tile: u32) -> bool {
        self.streams_armed >> tile & 1 != 0
    }
}

/// The segment's advice, pushed into the read queue, part 2, a credit at a time (17.9.3, 17.9.11).
struct Advice {
    next: u32,
    entries: u32,
}

impl Advice {
    fn left(&self) -> bool {
        self.next < self.entries
    }

    /// The next half queue, or the rest, in STREAM[`stream`] for the caller's transfer: its entries.
    fn refill(&mut self, stream: usize, me: u32) -> u32 {
        let count = HALF_QUEUE.min(self.entries - self.next);
        set_stream(stream, header(segment::ADVICE) + 4 * self.next, region_part(2), count, COUNTER_LOCAL, me);
        self.next += count;
        count
    }
}

/// A run's output, delivered (17.9.10): output queue 0's transfers made, the streams' lengths and entries sent, the
/// last run's record landed on the tile itself and its totals forwarded, and the Tracer's DMA busy.
struct Delivery {
    window: u32,
    last: bool,
    pieces: usize,
    lengths: [u32; 3],
    sent: [u32; 3],
    landed: bool,
    forwarded: bool,
    busy: bool,
}

impl Delivery {
    fn new(window: u32, last: bool) -> Self {
        Delivery {
            window,
            last,
            pieces: 0,
            lengths: [0; 3],
            sent: [0; 3],
            landed: false,
            forwarded: false,
            busy: false,
        }
    }

    /// Output queue 0's next transfer is allowed: both tiles' `ARMED` have come, or this is the last run.
    fn state_allowed(&self, handshakes: &Handshakes) -> bool {
        self.pieces < 4 && (self.last || handshakes.armed(self.window) && handshakes.armed(self.window + 1))
    }

    /// The streams' transfers are allowed: the record's done, the last run's totals forwarded, and tile n's
    /// `STREAMS_ARMED` has come.
    fn streams_allowed(&self, handshakes: &Handshakes) -> bool {
        self.pieces == 4 && (!self.last || self.forwarded) && handshakes.streams_armed(self.window)
    }

    /// The output queues' `_ready` events to wake on: those whose service is allowed, with no transfer in progress.
    fn ready_mask(&self, handshakes: &Handshakes) -> u32 {
        let mut mask = 0;
        if self.busy {
            return mask;
        }
        if self.state_allowed(handshakes) {
            mask |= ready(0);
        }
        if self.streams_allowed(handshakes) {
            for queue in 0..3 {
                if self.sent[queue] < self.lengths[queue] {
                    mask |= ready(queue + 1);
                }
            }
        }
        mask
    }

    /// The last run's record is still to land on the tile itself.
    fn awaiting_record(&self) -> bool {
        self.last && self.pieces == 4 && !self.landed
    }

    /// The transfers allowed now, the Tracer's DMA idle, one at a time (17.4), each but the last record's waited for
    /// here, its next once its `dma_done` has come, so that a chain of them goes back to back (`wait_dma`): output
    /// queue 0's four on `state_ready`, which stays up until the last has passed word 67, or at the last run its 68
    /// words to the tile itself; at the last run, once that record has landed and tile n's `ARMED` has come, its
    /// words 1 and 67 forwarded to tile n by the DMA unit; and each stream's credits while its queue holds one, 128
    /// entries or exactly the rest (17.9.11). `events` the Tracer's.
    fn serve(&mut self, events: u32, handshakes: &Handshakes, me: u32) {
        if self.busy {
            return;
        }
        let record = header(segment::RECORD);
        if self.state_allowed(handshakes) && events & ready(0) != 0 {
            for (queue, length) in self.lengths.iter_mut().enumerate() {
                *length = tracer_load(CODE_LENGTH + 4 * queue);
            }
            if self.last {
                // The last record to the tile itself (17.9.10), its 68 words into FINAL, landed on COUNTER_START.
                tile_store(counter(COUNTER_START), RECORD_WORDS);
                start_dma(0, RECORD_WORDS, me, scratchpad_address(header(segment::FINAL)), COUNTER_START);
                self.busy = true;
            } else {
                for (word, count, next) in PIECES {
                    let (dst, counter_number) =
                        if next == 1 { (self.window + 1, COUNTER_START) } else { (self.window, COUNTER_TOTALS) };
                    start_dma(0, count, dst, scratchpad_address(record + 4 * word), counter_number);
                    wait_dma();
                }
            }
            self.pieces = 4;
        } else if self.last && self.pieces == 4 && self.landed && !self.forwarded && handshakes.armed(self.window) {
            // Words 1 and 67 of the last record, forwarded to the last window's tile, on its COUNTER_TOTALS.
            let final_record = header(segment::FINAL);
            for (index, word) in [1, 67].into_iter().enumerate() {
                let address = scratchpad_address(record + 4 * word);
                set_stream(index, final_record + 4 * word, address, 1, COUNTER_TOTALS, self.window);
            }
            start_streams(0, 2);
            wait_all(EVENT_DMA_DONE);
            self.forwarded = true;
        }
        if self.streams_allowed(handshakes) {
            // The streams, in order, each credit as its queue holds it: the first queue with one, then again.
            loop {
                let ready_now = tracer_load(P_EVENTS_PEEK);
                let Some(queue) =
                    (0..3).find(|&queue| self.sent[queue] < self.lengths[queue] && ready_now & ready(queue + 1) != 0)
                else {
                    break;
                };
                let count = HALF_QUEUE.min(self.lengths[queue] - self.sent[queue]);
                let address = scratchpad_address(header(segment::CODE + queue) + 4 * self.sent[queue]);
                start_dma(queue as u32 + 1, count, self.window, address, COUNTER_STREAMS);
                self.sent[queue] += count;
                wait_dma();
            }
        }
    }
}

/// The Tracer's DMA transfer ended, its `dma_done` consumed: EVENTS_MASK `dma_done` alone, on the tile the
/// peripheral's line alone, which the run's loop sets back before its next WFI.
fn wait_dma() {
    tracer_store(P_EVENTS_MASK, DMA_DONE);
    tile_store(EVENTS_MASK, event_peripheral(1));
    while tracer_load(P_EVENTS) & DMA_DONE == 0 {
        wfi();
    }
}

/// The controller's state across the segment's runs: the expansions' feeder (src/lib.rs), the advice, the compute tiles'
/// handshakes, and the tile's own id.
struct Controller {
    feeder: Feeder<EXPANSION_CREDIT, true>,
    advice: Advice,
    handshakes: Handshakes,
    me: u32,
}

impl Controller {
    /// One run, window n's, to its `done`, its output delivered to tile n (and its start to tile n + 1): `tracer_ctl.c`'s
    /// loop on the Tracer's events, through the tile's peripheral_1, and on the compute tiles' requests
    /// (request_pending), in two parts: the rows, then the drain.
    fn run(&mut self, window: u32, last: bool) -> Status {
        let mut delivery = Delivery::new(window, last);
        let marks = report::WINDOW + 3 * window as usize;
        REPORT.set(marks, mcycle());
        tracer_store(P_START, 1);
        let (events, rows_end) = match self.rows() {
            Ok(end) => end,
            Err(status) => return status,
        };
        REPORT.set(marks + 1, rows_end);
        self.drain(&mut delivery, events)
    }

    /// The rows (17.9.4): each new expansion fed, first, since the Tracer waits for its words, each credit refilled and
    /// a request popped a wake, so that a post waits for one pop at most, to the rows' end, `state_ready` (17.9.6): the
    /// Tracer's events at it, and mcycle as its wake saw them (the report's rows' end): read each wake before its
    /// refills and its pop, so that at the rows' end, which no new expansion comes with, it follows the wake's load, and
    /// at a new expansion, after its first words.
    fn rows(&mut self) -> Result<(u32, u32), Status> {
        loop {
            let mut mask = NEW_EXPANSION | ready(0);
            if self.feeder.feeding() {
                mask |= EXPANSION_CREDIT;
            }
            if self.advice.left() {
                mask |= ADVICE_CREDIT;
            }
            self.feeder.mask = mask;
            tracer_store(P_EVENTS_MASK, mask);
            tile_store(EVENTS_MASK, event_peripheral(1) | EVENT_REQUEST_PENDING);
            wfi();
            let events = tracer_load(P_EVENTS);
            let mut counted = events & mask & COUNTED;
            if counted & NEW_EXPANSION != 0 {
                if self.feeder.present(counted).is_err() {
                    return Err(Status::NoTemplate);
                }
                counted &= !EXPANSION_CREDIT;
            }
            let woke = mcycle();
            self.refill(counted);
            self.handshakes.pop_one();
            if events & ready(0) != 0 {
                return Ok((events, woke));
            }
        }
    }

    /// The drain (17.9.6, 17.9.10), from the rows' end, `events` the Tracer's then: each transfer as it is allowed
    /// (`Delivery::serve`), on its `dma_done`, the output queues' `_ready`, the requests and, at the last run, its record's
    /// landing on the tile itself; to `done`. A credit returned as the rows ended is refilled as in the rows.
    fn drain(&mut self, delivery: &mut Delivery, mut events: u32) -> Status {
        loop {
            delivery.serve(events, &self.handshakes, self.me);
            // The events to wake on: the counted ones while they have something behind them, and the live ones while
            // their service is allowed and no transfer is in progress; on the tile, the requests, and the last
            // record's landing while it is awaited.
            let mut mask = DONE | DMA_DONE | delivery.ready_mask(&self.handshakes);
            if self.feeder.feeding() {
                mask |= EXPANSION_CREDIT;
            }
            if self.advice.left() {
                mask |= ADVICE_CREDIT;
            }
            let record_event = if delivery.awaiting_record() { event_counter_done(COUNTER_START) } else { 0 };
            self.feeder.mask = mask;
            tracer_store(P_EVENTS_MASK, mask);
            tile_store(EVENTS_MASK, event_peripheral(1) | EVENT_REQUEST_PENDING | record_event);
            wfi();
            events = tracer_load(P_EVENTS);
            let counted = events & mask & COUNTED;
            self.refill(counted);
            self.handshakes.pop();
            if record_event != 0 && tile_load(EVENTS) & record_event != 0 {
                delivery.landed = true;
            }
            if counted & DMA_DONE != 0 {
                delivery.busy = false;
            }
            if counted & DONE != 0 {
                REPORT.set(report::WINDOW + 3 * delivery.window as usize + 2, mcycle());
                return Status::Passed;
            }
        }
    }

    /// The refills of the credits `counted` took, in `tracer_ctl.c`'s order, one transfer: the expansion's next chunk,
    /// half a queue or its rest, then the advice's next half queue or its rest (17.9.3), each a STREAM entry.
    fn refill(&mut self, counted: u32) {
        let mut streams = 0;
        let mut entries = 0;
        if counted & EXPANSION_CREDIT != 0 && self.feeder.feeding() {
            entries += self.feeder.refill(streams);
            streams += 1;
        }
        if counted & ADVICE_CREDIT != 0 && self.advice.left() {
            entries += self.advice.refill(streams, self.me);
            streams += 1;
        }
        if entries != 0 {
            self.feeder.store_mask();
            start_transfer(0, streams as u32, entries);
            wait_transfer();
        }
    }
}

/// The segment's end (17.9.11): the last record's advice field and end state, words 0 and 2-66, the expected ones.
fn check_end(me: u32) -> Status {
    set_stream(0, header(segment::FINAL), FINAL.address(), RECORD_WORDS, COUNTER_LOCAL, me);
    set_stream(1, header(segment::EXPECTED), EXPECTED.address(), RECORD_WORDS, COUNTER_LOCAL, me);
    run_streams(2, 2 * RECORD_WORDS, COUNTER_LOCAL);
    // The advice field, the stream word's [124:102]: its word 3's [28:6].
    if (FINAL.word(4 + 3) >> 6) & 0x7F_FFFF != 0 {
        return Status::AdviceLeft;
    }
    // Each word's differing bits, ORed, row by row: word 0, then words 2-66.
    let differs =
        |row: usize| (0..4).fold(0, |bits, word| bits | FINAL.word(4 * row + word) ^ EXPECTED.word(4 * row + word));
    let mut differ = differs(0);
    for row in 2..67 {
        differ |= differs(row);
    }
    if differ != 0 { Status::EndState } else { Status::Passed }
}

/// The segment (17.9.10, 17.9.11), and the cycles from its first START to its last `done`.
fn segment(me: u32) -> (Status, u32) {
    // 1. The segment's start, the Tracer idle: the core-fed set's index and the NOP row into DMEM; the map's constants
    //    and RUN_CONFIG; then one transfer into the Tracer's region, the decode table and the ROM, then the record,
    //    state word first, while the core builds the set's lookup.
    let templates = header(segment::TEMPLATES_COUNT).min(MAX_TEMPLATES as u32);
    load_core_fed_set(header(segment::INDEX), templates, header(segment::TABLES), me);
    tracer_store(HASH_MUL, header(segment::HASH_MUL));
    tracer_store(HASH_STEP, header(segment::HASH_STEP));
    tracer_store(HASH_LINES, header(segment::HASH_LINES));
    tracer_store(RUN_CONFIG, header(segment::RUN_CONFIG));
    let start = header(segment::TRACER_RECORD);
    set_stream(0, header(segment::TABLES), peripheral_region(1) | REGION_TABLES, TABLE_ROWS, COUNTER_LOCAL, me);
    set_stream(1, start, region_part(0), RECORD_WORDS, COUNTER_LOCAL, me);
    start_transfer(0, 2, TABLE_ROWS + RECORD_WORDS);
    let feeder = Feeder::new(templates, header(segment::TEMPLATES), me);
    wait_transfer();
    REPORT.set(report::READY, mcycle());
    let mut controller = Controller {
        feeder,
        advice: Advice { next: 0, entries: header(segment::ADVICE_ENTRIES) },
        handshakes: Handshakes::default(),
        me,
    };

    // 2. Window 0's start, from the tile's own copy, once tile 0 has armed.
    tile_store(EVENTS_MASK, EVENT_REQUEST_PENDING);
    while !controller.handshakes.armed(0) {
        if !controller.handshakes.pop_one() {
            wfi();
        }
    }
    REPORT.set(report::ARMED, mcycle());
    let record = header(segment::RECORD);
    set_stream(0, start, scratchpad_address(record), 1, COUNTER_START, 0);
    set_stream(1, start + 4 * 2, scratchpad_address(record + 4 * 2), RECORD_WORDS - 3, COUNTER_START, 0);
    start_streams(0, 2);
    wait_all(EVENT_DMA_DONE);

    // 3. The runs.
    let windows = header(segment::WINDOWS);
    let first = mcycle();
    for window in 0..windows {
        let status = controller.run(window, window + 1 == windows);
        if status != Status::Passed {
            return (status, mcycle().wrapping_sub(first));
        }
    }
    let cycles = mcycle().wrapping_sub(first);
    tracer_store(P_EVENTS_MASK, 0);
    for counter_index in 0..8 {
        REPORT.set(report::STATS + counter_index, tracer_load(STATS + 4 * counter_index));
    }
    REPORT.set(report::GENERAL, controller.feeder.general);
    controller.handshakes.pop();
    let status = if controller.handshakes.strays != 0 { Status::Stray } else { check_end(me) };
    (status, cycles)
}

#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    REPORT.set(report::STARTED, mcycle());
    let me = tile_id();
    copy_to_dmem(0, HEADER.address(), (segment::WORDS / 4) as u32, COUNTER_LOCAL, me);
    let (status, cycles) =
        if header(segment::MAGIC) == segment::TRACER_MAGIC { segment(me) } else { (Status::Header, 0) };
    REPORT.set(report::ENDED, mcycle());
    let errors = tracer_load(P_ERRORS) | tile_load(ERRORS) << 16;
    exit_to_master([status as u32, errors, cycles], COUNTER_REPLY, &REPLY);
    end_image()
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop {
        wfi();
    }
}
