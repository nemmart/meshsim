//! The base tile as the chip's firmware sees it (rtl/tile/base/spec.md): its map, its registers and their fields, the
//! packet's header and the requests, with the few accesses both programs make, the master tile's
//! (src/bin/master.rs) and the memory tile's (src/bin/memory.rs), and the segment's, the tracer tile's
//! (src/bin/tracer.rs) and the compute tiles' replay image (src/bin/replay.rs), and the segment's core-fed expansions,
//! which both of its images feed the same way (`Feeder`). Every value is the specification's, by its section; the
//! segment's requests, counters and headers, and the core-fed set's lookup, are the firmware's own (17.8.13, 17.9.10,
//! 17.9.11).

#![no_std]

use core::arch::asm;
use core::cell::UnsafeCell;
use core::ptr::{read_volatile, write_volatile};

// ---- The map (5.1, 6.3) ----

/// IMEM's base, region 0x01.
pub const IMEM_BASE: u32 = 0x0100_0000;
/// A peripheral's region: 0x03 + n (17.1).
pub const fn peripheral_region(n: u32) -> u32 {
    (0x03 + n) << 24
}
/// The boot ROM's region: a `WRITE` there is a bad packet (4.2).
pub const BOOT_ROM_BASE: u32 = 0x0200_0000;

// ---- Region 0xFF (5.2, 5.5): the tile window, and a peripheral's ----

/// The tile window's base, and peripheral n's window at + 0x200 n (17.1).
pub const TILE_WINDOW: usize = 0xFFFF_F800;
pub const fn peripheral_window(n: usize) -> usize {
    TILE_WINDOW + 0x200 * n
}

// The tile window's registers, by offset (5.5).
pub const EVENTS: usize = 0x00C;
pub const EVENTS_PEEK: usize = 0x010;
pub const EVENTS_MASK: usize = 0x014;
pub const WRITE_HDR: usize = 0x080;
pub const WRITE_DEST_ADDR: usize = 0x084;
pub const WRITE_W0: usize = 0x088;
pub const WRITE_W1: usize = 0x08C;
pub const WRITE_W2: usize = 0x090;
pub const WRITE_SEND_32: usize = 0x094;
pub const WRITE_SEND_128: usize = 0x09C;
pub const REQUEST_OUT_W0: usize = 0x100;
pub const REQUEST_OUT_W1: usize = 0x104;
pub const REQUEST_OUT_W2: usize = 0x108;
pub const REQUEST_SEND: usize = 0x10C;
pub const REQUEST_IN_W0: usize = 0x110;
pub const REQUEST_IN_W1: usize = 0x114;
pub const REQUEST_IN_W2: usize = 0x118;
pub const REQUEST_RECEIVE: usize = 0x11C;
/// COUNTER_n, n = 1-7 (12.1).
pub const fn counter(n: u32) -> usize {
    0x140 + 4 * n as usize
}

// A peripheral's control block and its DMA block (17.1), by offset from its window.
pub const P_EVENTS: usize = 0x00C;
pub const P_EVENTS_PEEK: usize = 0x010;
pub const P_EVENTS_MASK: usize = 0x014;
pub const P_START: usize = 0x018;
pub const P_DMA_HDR: usize = 0x040;
pub const P_DMA_DEST_ADDR: usize = 0x044;
pub const P_DMA_START: usize = 0x048;

// ---- Fields ----

// The tile's events (7.2): EVENTS, EVENTS_PEEK and EVENTS_MASK.
pub const EVENT_REQUEST_PENDING: u32 = 1 << 3;
/// peripheral_n, n = 1-3: live.
pub const fn event_peripheral(n: u32) -> u32 {
    1 << (3 + n)
}
/// counter_n_done, n = 1-7.
pub const fn event_counter_done(n: u32) -> u32 {
    1 << (6 + n)
}

// A peripheral's standard events (17.2).
pub const P_EVENT_DONE: u32 = 1 << 0;
pub const P_EVENT_DMA_DONE: u32 = 1 << 1;

// ---- The packet's header (4.1, 5.4): the header block [7:0], size [1:0], counter [4:2], mode [7:5]; a header
// register's dst at [14:8] ----

pub const SIZE_32: u32 = 0;
pub const SIZE_128: u32 = 2;
pub const MODE_WRITE: u32 = 0;
pub const MODE_COUNT: u32 = 5;

/// A header register's value: the header block and dst.
pub const fn header(mode: u32, counter: u32, size: u32, dst: u32) -> u32 {
    mode << 5 | counter << 2 | size | dst << 8
}

// ---- Tile ids (4.1, 2.1): TILE_ID_BITS = ceil(log2(TILE_COUNT + 5)), 7 at 8x8, its top value none and the one below
// it broadcast ----

pub const TILE_ID_BITS: u32 = 7;
pub const BROADCAST: u32 = (1 << TILE_ID_BITS) - 2;

// ---- Requests (11.2, 11.5): word 0's requester [6:0], target [9:7], op [17:10] and the op's fields [31:18] ----

pub const TARGET_MEMORY: u32 = 1;

pub const fn request_word_0(target: u32, op: u32, fields: u32) -> u32 {
    target << 7 | op << 10 | fields << 18
}
pub const fn requester(word_0: u32) -> u32 {
    word_0 & 0x7F
}
pub const fn request_op(word_0: u32) -> u32 {
    (word_0 >> 10) & 0xFF
}

/// The master tile's REPORT (11.5): number [25:18]; 24 and 25 the boot ROM's (6.5).
pub const OP_REPORT: u32 = 1;
/// The master tile's LOAD (11.5): the boot ROM's, counter 7 armed, its answer the load (6.5).
pub const OP_LOAD: u32 = 3;
/// The counter a load's `COUNT` drains (6.5).
pub const LOAD_COUNTER: u32 = 7;
/// The master's firmware's own op: a tile's report on its program's return (the sort's port layer, port.c), words
/// 1-3 its return value, its records and its ERRORS; word 0 [20:18] its reply's counter and [31:21] its reply's DMEM
/// address over 16, where the master answers with a 32-bit `WRITE` of 0 (11.4).
pub const OP_EXIT: u32 = 2;
pub const EXIT_REPLY_COUNTER_SHIFT: u32 = 18;
pub const EXIT_REPLY_ROW_SHIFT: u32 = 21;
/// The memory tile's (11.5).
pub const OP_READ: u32 = 1;
pub const OP_WRITE: u32 = 2;

// ---- The core ----

/// mcause of an NMI (6.2).
pub const CAUSE_NMI: u32 = 0x8000_0000;

// ---- Accesses ----

/// A load of the word at `address` in region 0xFF.
pub fn load(address: usize) -> u32 {
    // SAFETY: a register or a table word of region 0xFF, whole (5.4).
    unsafe { read_volatile(address as *const u32) }
}

/// A store of `value` to the word at `address` in region 0xFF.
pub fn store(address: usize, value: u32) {
    // SAFETY: a register or a table word of region 0xFF, whole (5.4).
    unsafe { write_volatile(address as *mut u32, value) }
}

/// The tile window's register at `offset`.
pub fn tile_load(offset: usize) -> u32 {
    load(TILE_WINDOW + offset)
}

pub fn tile_store(offset: usize, value: u32) {
    store(TILE_WINDOW + offset, value)
}

/// The low 32 bits of mcycle: the chip's one clock (6.1).
pub fn mcycle() -> u32 {
    let cycle: u32;
    // SAFETY: reads a CSR.
    unsafe { asm!("csrr {0}, mcycle", out(reg) cycle, options(nomem, nostack)) };
    cycle
}

/// WFI: the wake is (EVENTS & EVENTS_MASK) != 0 (7.2), which mstatus.MIE 0 makes a resume, never a trap (6.2).
pub fn wfi() {
    // SAFETY: waits; mstatus.MIE is 0, so no interrupt is taken.
    unsafe { asm!("wfi", options(nomem, nostack)) };
}

/// A request (11.1): words 0-2 into REQUEST_OUT_W0-W2, then word 3 into REQUEST_SEND, which sends it, stalling
/// while the request queue is full.
pub fn send_request(words: [u32; 4]) {
    tile_store(REQUEST_OUT_W0, words[0]);
    tile_store(REQUEST_OUT_W1, words[1]);
    tile_store(REQUEST_OUT_W2, words[2]);
    tile_store(REQUEST_SEND, words[3]);
}

/// The oldest request in the receive queue (11.1), popped: REQUEST_IN_W0-W2, then REQUEST_RECEIVE, which pops it.
/// Only with EVENTS.request_pending seen: an empty queue's pop is an error.
pub fn receive_request() -> [u32; 4] {
    let words = [tile_load(REQUEST_IN_W0), tile_load(REQUEST_IN_W1), tile_load(REQUEST_IN_W2)];
    [words[0], words[1], words[2], tile_load(REQUEST_RECEIVE)]
}

/// The oldest request's word 0 (11.1), the request popped, for a receiver that needs no other word: REQUEST_IN_W0, then
/// REQUEST_RECEIVE. Only with EVENTS.request_pending seen.
pub fn receive_word_0() -> u32 {
    let word_0 = tile_load(REQUEST_IN_W0);
    tile_load(REQUEST_RECEIVE);
    word_0
}

/// Sleeps until any event of `events` is pending (EVENTS_MASK set to them), and returns EVENTS_PEEK: a wake
/// consumes nothing, and neither does this.
pub fn sleep_until(events: u32) -> u32 {
    tile_store(EVENTS_MASK, events);
    loop {
        let pending = tile_load(EVENTS_PEEK);
        if pending & events != 0 {
            return pending;
        }
        wfi();
    }
}

// ---- The tile window's other registers (5.5), and the tables (5.2) ----

pub const ARRIVE: usize = 0x018;
pub const PARAMS: usize = 0x000;
pub const ERRORS: usize = 0x008;
pub const DMA_CONFIG: usize = 0x040;
pub const DMA_START: usize = 0x044;

/// The tables of `STREAM` (8.3): entry i of each array at + 4i.
pub const STREAM_SRC_ADDR: usize = 0xFFFF_E800;
pub const STREAM_DEST_ADDR: usize = 0xFFFF_EC00;
pub const STREAM_HDR: usize = 0xFFFF_F000;

/// The tile's events (7.2): release and dma_done.
pub const EVENT_RELEASE: u32 = 1 << 0;
pub const EVENT_DMA_DONE: u32 = 1 << 1;

/// PARAMS.tile_id (7.1).
pub fn tile_id() -> u32 {
    tile_load(PARAMS) & 0x7F
}

// ---- The scratchpad (5.1, 5.4): region 0x03, and its word addresses ----

pub const SCRATCHPAD_BASE: u32 = 0x0300_0000;

/// Word address `word`'s byte address in the scratchpad (5.4).
pub const fn scratchpad_address(word: u32) -> u32 {
    SCRATCHPAD_BASE | word << 2
}

// ---- The DMA unit (8): stream transfers ----

/// STREAM[index]: `count` 128-bit elements from the scratchpad's word address `source` to `destination`, a byte
/// address at `dst` (on a compute tile 0 is the tile itself, on a special tile an absolute id: 8.3), `WRITE`s counted
/// on `counter`.
pub fn set_stream(index: usize, source: u32, destination: u32, count: u32, counter: u32, dst: u32) {
    store(STREAM_SRC_ADDR + 4 * index, source);
    store(STREAM_DEST_ADDR + 4 * index, destination);
    store(STREAM_HDR + 4 * index, header(MODE_WRITE, counter, SIZE_128, dst) | count << 15);
}

/// A stream transfer of STREAM[first..first + streams] (8.1, 8.2): DMA_CONFIG, then DMA_START, which clears
/// EVENTS.dma_done.
pub fn start_streams(first: u32, streams: u32) {
    tile_store(DMA_CONFIG, first << 1 | streams << 9);
    tile_store(DMA_START, 1);
}

/// Sleeps until every event of `events`, the tile's, has been seen, each consumed as a load of EVENTS returns it
/// (EVENTS_MASK `events`, 7.2, which it leaves so).
pub fn wait_all(events: u32) {
    tile_store(EVENTS_MASK, events);
    let mut seen = 0;
    loop {
        seen |= tile_load(EVENTS) & events;
        if seen == events {
            break;
        }
        wfi();
    }
}

/// A transfer the tile's own packets land from, counted on `counter`: `entries` packets, armed before the start, then
/// the wait for its drain and the transfer's end (dma_done). STREAM[0..streams] are set.
pub fn run_streams(streams: u32, entries: u32, counter_number: u32) {
    tile_store(counter(counter_number), entries);
    start_streams(0, streams);
    wait_all(event_counter_done(counter_number) | EVENT_DMA_DONE);
}

/// `entries` 128-bit words of the scratchpad from word address `source` into DMEM at `destination`, 16-byte aligned,
/// landed (counted on `counter_number`); `me` is the tile's own dst (0 on a compute tile, its id on a special tile).
pub fn copy_to_dmem(source: u32, destination: u32, entries: u32, counter_number: u32, me: u32) {
    set_stream(0, source, destination, entries, counter_number, me);
    run_streams(1, entries, counter_number);
}

// ---- The write unit (9) ----

/// One 128-bit `WRITE` of `words` (low first) to `address` at `dst`, counted on `counter_number`.
pub fn send_128(dst: u32, address: u32, counter_number: u32, words: &[u32; 4]) {
    tile_store(WRITE_HDR, header(MODE_WRITE, counter_number, SIZE_128, dst));
    tile_store(WRITE_DEST_ADDR, address);
    tile_store(WRITE_W0, words[0]);
    tile_store(WRITE_W1, words[1]);
    tile_store(WRITE_W2, words[2]);
    tile_store(WRITE_SEND_128, words[3]);
}

// ---- The barrier (7.3) ----

/// ARRIVE, then the canonical wait: a WFI right after it, which only the release ends (the arrival gate), and the load
/// of EVENTS that consumes the release (EVENTS_MASK holds it). Every counter must be 0 (7.3: an early arrival).
pub fn arrive_and_wait() {
    tile_store(EVENTS_MASK, EVENT_RELEASE);
    // SAFETY: a store to ARRIVE, a WFI and a load of EVENTS, x0-relative (5.2); mstatus.MIE is 0.
    unsafe {
        asm!(
            "sw {one}, {arrive}(zero)",
            "wfi",
            "lw {events}, {events_offset}(zero)",
            one = in(reg) 1,
            events = out(reg) _,
            arrive = const (TILE_WINDOW + ARRIVE) as i32,
            events_offset = const (TILE_WINDOW + EVENTS) as i32,
            options(nostack),
        )
    };
}

// ---- The end of a loaded image (6.5, 11.4) ----

/// The image's report to the master tile, `OP_EXIT`, words 1-3 `words`, its reply counted on `counter_number`
/// into `slot`, 16-byte aligned in DMEM below 32 KiB (EXIT_REPLY_ROW_SHIFT's 11 bits), armed first (11.4); then the wait for the reply, so that the image ends with one request to the
/// master outstanding at most (11.3), the boot ROM's `LOAD`.
pub fn exit_to_master(words: [u32; 3], counter_number: u32, slot: &Buffer<4>) {
    tile_store(counter(counter_number), 1);
    let address = slot.address();
    let word_0 = request_word_0(TARGET_MASTER, OP_EXIT, 0)
        | counter_number << EXIT_REPLY_COUNTER_SHIFT
        | (address >> 4) << EXIT_REPLY_ROW_SHIFT;
    send_request([word_0, words[0], words[1], words[2]]);
    wait_all(event_counter_done(counter_number));
}

/// The image's end (6.5): a jump to the reset PC, whose boot ROM asks for the next load.
pub fn end_image() -> ! {
    // SAFETY: the boot ROM's first instruction; the image ends there.
    unsafe { asm!("jr {0}", in(reg) BOOT_ROM_BASE, options(noreturn)) }
}

pub const TARGET_MASTER: u32 = 0;

// ---- The segment around the Tracer (17.9.10): the tracer tile's requests, its firmware's ops ----

/// The tracer tile's target (11.2).
pub const TARGET_TRACER: u32 = 2;
/// A compute tile, woken by the release, has armed its counters for its record's start and totals.
pub const OP_ARMED: u32 = 1;
/// A compute tile has its totals, and has armed its stream counter for its three streams.
pub const OP_STREAMS_ARMED: u32 = 2;

/// The counters a compute tile receives its window on (17.9.10): its record's start, words 0 and 2-66; its totals,
/// words 1 and 67; its three streams.
pub const COUNTER_START: u32 = 1;
pub const COUNTER_TOTALS: u32 = 2;
pub const COUNTER_STREAMS: u32 = 3;
/// Its own packets: the DMA unit's transfers into its region and DMEM, and the write unit's into its region.
pub const COUNTER_LOCAL: u32 = 4;
/// The exit request's reply (11.4).
pub const COUNTER_REPLY: u32 = 6;
/// The record's start: 66 packets; its totals: 2.
pub const START_WORDS: u32 = 66;
pub const TOTALS_WORDS: u32 = 2;

/// The record (17.8.2): 68 words, its stream word word 1, its register image words 2-65, its files word 66 and its
/// template bitmap word 67.
pub const RECORD_WORDS: u32 = 68;

/// A peripheral's region (17.8.9, 17.9.2): the record from offset 0, the decode table at 0x1000 and the ROM at 0x2000,
/// which the tables' 768 rows fill in one transfer; and its queues' parts, part p at p << 20.
pub const REGION_TABLES: u32 = 0x1000;
pub const TABLE_ROWS: u32 = 768;
pub const fn region_part(part: u32) -> u32 {
    peripheral_region(1) | part << 20
}

/// The segment's headers, which the chip's test writes at the scratchpad's start on every tile of the segment
/// (rtl/chip/test/src/chip_trace_test.rs), as the host would: word addresses in the tile's scratchpad, by index.
pub mod segment {
    /// Both headers: a magic, the windows, the rows of each.
    pub const MAGIC: usize = 0;
    pub const WINDOWS: usize = 1;
    pub const ROWS: usize = 2;
    /// RUN_CONFIG, the replayer's or the Tracer's (17.8.1, 17.9.1).
    pub const RUN_CONFIG: usize = 3;
    /// The tables (the decode table's 256 rows, then the ROM's 512), the core-fed set's index (template_index.bin:
    /// key, word offset, length, rd0 and class) and its templates (templates.bin).
    pub const TABLES: usize = 4;
    pub const INDEX: usize = 5;
    pub const TEMPLATES_COUNT: usize = 6;
    pub const TEMPLATES: usize = 7;
    /// A compute tile's replay region (17.8.13): its window's record and its three streams, as the tracer tile
    /// sends them; the tracer tile's header holds them too, the same on every compute tile.
    pub const RECORD: usize = 8;
    pub const CODE: usize = 9;
    pub const READ_LOG: usize = 10;
    pub const WRITE_LOG: usize = 11;
    /// The tracer tile's own: the map's constants (17.9.4), the loaded record, the segment's advice (two values an
    /// entry) and its entries, the expected end state, the next segment's start (a record, words 0 and 2-66 compared),
    /// and where the last run's record lands.
    pub const HASH_MUL: usize = 12;
    pub const HASH_STEP: usize = 13;
    pub const HASH_LINES: usize = 14;
    pub const TRACER_RECORD: usize = 15;
    pub const ADVICE: usize = 16;
    pub const ADVICE_ENTRIES: usize = 17;
    pub const EXPECTED: usize = 18;
    pub const FINAL: usize = 19;
    pub const WORDS: usize = 20;

    pub const REPLAY_MAGIC: u32 = 0x5250_4C59; // "RPLY"
    pub const TRACER_MAGIC: u32 = 0x5452_4345; // "TRCE"
}

// ---- The core-fed set (17.8.13): its index and its key ----

/// A template's index entry, template_index.bin's 16 bytes: key, word offset, length, then rd0 and class.
#[derive(Clone, Copy)]
#[repr(C)]
struct IndexEntry {
    key: u32,
    offset: u32,
    length: u32,
    meta: u32,
}

impl IndexEntry {
    fn rd0(&self) -> u32 {
        self.meta & 0xFF
    }
    fn class(&self) -> u32 {
        (self.meta >> 8) & 0xFF
    }
}

/// The key of a presented code value (the tables' README; meshsim's `tkey`): an inline's register fields cleared; a
/// CSRRW's or CSRRS's rd and rs1 renamed by first appearance; any other word its own.
fn template_key(w: u32) -> u32 {
    let w = w | 1;
    if w & 0x7F == 0x0B {
        return w & !(31 << 7 | 31 << 15 | 31 << 20);
    }
    if w & 0x7F == 0x73 {
        let f3 = (w >> 12) & 7;
        if f3 == 1 || f3 == 2 {
            let (rd, rs1) = ((w >> 7) & 31, (w >> 15) & 31);
            let renamed_rs1 = if rs1 == 0 {
                0
            } else if rd == 0 || rs1 == rd {
                1
            } else {
                2
            };
            return (w & !(31 << 7 | 31 << 15)) | u32::from(rd != 0) << 7 | renamed_rs1 << 15;
        }
    }
    w
}

/// The expansion word's rd0, bits [47:46] (17.8.4): in its word 1, bits [15:14].
fn with_rd0(mut word: [u32; 4], rd0: u32) -> [u32; 4] {
    word[1] = (word[1] & !(3 << 14)) | rd0 << 14;
    word
}

// ---- DMEM buffers that the NoC writes ----

/// `N` words of DMEM, 16-byte aligned, which packets land in (6.3) and the core reads and writes, every access volatile:
/// the compiler cannot see the NoC's writes.
#[repr(C, align(16))]
pub struct Buffer<const N: usize>(UnsafeCell<[u32; N]>);

// SAFETY: one core, and nothing interrupts it (mstatus.MIE is 0); the NoC's writes land between its accesses.
unsafe impl<const N: usize> Sync for Buffer<N> {}

impl<const N: usize> Buffer<N> {
    pub const fn new() -> Self {
        Self(UnsafeCell::new([0; N]))
    }

    /// Its DMEM byte address.
    pub fn address(&self) -> u32 {
        self.0.get() as u32
    }

    /// Its word `index`, or 0 past its end.
    pub fn word(&self, index: usize) -> u32 {
        if index >= N {
            return 0;
        }
        // SAFETY: a word of the buffer.
        unsafe { read_volatile((self.0.get() as *const u32).add(index)) }
    }

    /// Sets its word `index`; past its end, nothing.
    pub fn set(&self, index: usize, value: u32) {
        if index >= N {
            return;
        }
        // SAFETY: a word of the buffer.
        unsafe { write_volatile((self.0.get() as *mut u32).add(index), value) }
    }

    /// Its 128-bit word `index`, words 4 index to 4 index + 3.
    pub fn row(&self, index: usize) -> [u32; 4] {
        core::array::from_fn(|w| self.word(4 * index + w))
    }

    /// Zeroes it, eight stores a loop: some 1.6 cycles a word, where a loop of one store a word takes 7.
    pub fn clear(&self) {
        let words = self.0.get() as *mut u32;
        for row in 0..N / 8 {
            for word in 8 * row..8 * row + 8 {
                // SAFETY: a word of the buffer.
                unsafe { write_volatile(words.add(word), 0) }
            }
        }
        for word in N / 8 * 8..N {
            // SAFETY: a word of the buffer.
            unsafe { write_volatile(words.add(word), 0) }
        }
    }
}

impl<const N: usize> Default for Buffer<N> {
    fn default() -> Self {
        Self::new()
    }
}

// ---- A peripheral's core-fed expansions (17.8.6, 17.8.13; 17.9.4, 17.9.11): the core-fed set's lookup and the
// expansions' feeding, the replay image's (src/bin/replay.rs) and the tracer tile's (src/bin/tracer.rs) ----

/// Peripheral 1's window (17.1): the replayer's on a compute tile, the Tracer's on the tracer tile.
pub const PERIPHERAL: usize = peripheral_window(1);
/// A peripheral's words for its expansions: EXPANSION_OFFSET and EXPANSION_OPCODE (17.8.1, 17.9.1).
const EXPANSION_OFFSET: usize = 0x088;
const EXPANSION_OPCODE: usize = 0x08C;
/// The Tracer's EXPANSION_TEMPLATE (17.9.1): a template's number, [6:0], and `valid`, [31].
const EXPANSION_TEMPLATE: usize = 0x0AC;
const TEMPLATE_VALID: u32 = 1 << 31;
/// Half an input queue (17.8.8, 17.9.3): a credit's entries.
pub const HALF_QUEUE: u32 = 128;
/// The expansion queue's part of the region (17.8.9, 17.9.2).
const EXPANSION_PART: u32 = 4;
/// The templates the index holds at most: their numbers are 7 bits (17.9.1, EXPANSION_TEMPLATE).
pub const MAX_TEMPLATES: usize = 128;
/// The STREAM entries (8.3): from 0 a transfer's streams; from FIRST_CHUNKS template k's first chunk, at
/// FIRST_CHUNKS + k, set once (`replay_ctl.c`'s TPL_STREAM0).
const FIRST_CHUNKS: u32 = 4;

/// A template's FIRST word, its first chunk's transfer: its entries, half a queue or the whole template, [8:0]; its
/// number, [22:16]; NOP_POLICY for an rd0 of 1 or 3 (17.8.5); and, on the Tracer, INLINE for a template of class 1,
/// an inline, whose number goes into EXPANSION_TEMPLATE before its first word (17.9.11).
const CHUNK: u32 = 0x1FF;
const NUMBER_SHIFT: u32 = 16;
const NUMBER: u32 = 0x7F;
const INLINE: u32 = 1 << 30;
const NOP_POLICY: u32 = 1 << 31;

/// The hash's slots, twice the templates the index holds at most: each a template's key (0 an empty slot: a key's bit
/// 0 is 1, `template_key`) and its FIRST word.
const SLOTS: usize = 2 * MAX_TEMPLATES;
/// The cache of the code values seen, in front of the hash: a line a code value (0 an empty line: a core-fed
/// instruction's value is never 0) and its template's FIRST word, each a value whose expansion went the general way at
/// offset 0 and not under a NOP policy with rd x0, so that its next ones go the short way (`Feeder::present`). A value
/// is in one of the PROBES lines from its own, `line_of`'s, the first that was empty when it came, or, all four taken,
/// its own line, its value replaced: no line is ever emptied, so a look-up ends at the first empty line. So values
/// whose lines collide, some twenty pairs among a hundred values, keep a line each, where a direct-mapped cache would
/// have them evict each other at every expansion.
const LINES: usize = 256;
const PROBES: usize = 4;

// The core-fed set's tables in DMEM, in .noinit (loaded.ld), which nothing lands or zeroes but what is here: the index
// and the shared NOP row, landed; the hash's keys and FIRST words; the cache's code values and FIRST words; a
// template's word, landed.
#[unsafe(link_section = ".noinit")]
static INDEX: Buffer<{ 4 * MAX_TEMPLATES }> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static NOP_ROW: Buffer<4> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static KEYS: Buffer<SLOTS> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static FIRST: Buffer<SLOTS> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static CODE_VALUES: Buffer<LINES> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static CODE_FIRST: Buffer<LINES> = Buffer::new();
#[unsafe(link_section = ".noinit")]
static SCRATCH: Buffer<4> = Buffer::new();

/// Template `number`'s index entry.
#[inline(always)]
fn template(number: u32) -> IndexEntry {
    let [key, offset, length, meta] = INDEX.row(number as usize);
    IndexEntry { key, offset, length, meta }
}

/// A key's first slot: its multiplicative hash's top bits (`replay_ctl.c`'s `hash8`).
fn slot_of(key: u32) -> usize {
    (key.wrapping_mul(2_654_435_761) >> 24) as usize
}

/// A code value's line in the cache, by the same hash.
fn line_of(w: u32) -> usize {
    slot_of(w) % LINES
}

/// The slot of the template of key `key`, if the set holds one: the slots from the key's own to the first that holds
/// it, or to the first empty one (`replay_ctl.c`'s `lookup`).
fn lookup(key: u32) -> Option<usize> {
    let mut slot = slot_of(key);
    loop {
        let held = KEYS.word(slot);
        if held == key {
            return Some(slot);
        }
        if held == 0 {
            return None;
        }
        slot = (slot + 1) % SLOTS;
    }
}

/// The core-fed set's index and the shared NOP row (ROM word 1, 17.8.6) into DMEM, one transfer, landed: `templates`
/// index entries from the word address `index`, and the row from `tables`, the decode table's 256 rows then the ROM;
/// `me` the tile's own dst for the DMA unit (8.3: 0 on a compute tile, its id on a special tile).
pub fn load_core_fed_set(index: u32, templates: u32, tables: u32, me: u32) {
    set_stream(0, index, INDEX.address(), templates, COUNTER_LOCAL, me);
    set_stream(1, tables + 4 * (256 + 1), NOP_ROW.address(), 1, COUNTER_LOCAL, me);
    run_streams(2, templates + 1, COUNTER_LOCAL);
}

/// A transfer of the tile's own packets, STREAM[`first`..`first` + `streams`], `entries` of them counted on
/// COUNTER_LOCAL, started once armed.
pub fn start_transfer(first: u32, streams: u32, entries: u32) {
    tile_store(counter(COUNTER_LOCAL), entries);
    start_streams(first, streams);
}

/// The transfer landed and ended (the squelch). It leaves the tile's EVENTS_MASK at those two events (`wait_all`): a
/// controller sets its own again before its next WFI.
pub fn wait_transfer() {
    wait_all(event_counter_done(COUNTER_LOCAL) | EVENT_DMA_DONE);
}

/// A peripheral's core-fed expansions (17.8.13, 17.9.11), fed into peripheral 1's expansion queue from the core-fed set
/// in the scratchpad (`load_core_fed_set`), by the lookup the feeder builds (`new`): `replay_ctl.c`'s new-expansion fast
/// and slow paths, and `tracer_ctl.c`'s `new_expansion`, with its item 145's store. `CREDIT` is expansion_credit in the
/// peripheral's EVENTS, and `TRACER` says it is the Tracer, for the lookup, whose FIRST words flag an inline on the
/// Tracer alone, and for the feeding, which stores a flagged inline's number: one parameter, so that the two cannot
/// disagree. Each source's words land before the next feeds the queue (17.3), the DMA unit's stream transfers or the
/// write unit's 128-bit `WRITE`s, counted on COUNTER_LOCAL; each wait leaves the tile's EVENTS_MASK changed
/// (`wait_transfer`). It keeps the peripheral's EVENTS_MASK, which the feeding changes.
pub struct Feeder<const CREDIT: u32, const TRACER: bool> {
    /// The peripheral's EVENTS_MASK as its controller keeps it, its expansion_credit there while the expansion has
    /// words left (17.8.13: a credit with nothing behind it out of the mask), stored by `store_mask`.
    pub mask: u32,
    /// The tile's own dst for the DMA unit (8.3), and for the write unit (9: its id).
    stream_me: u32,
    write_me: u32,
    /// The templates' words, a word address in the scratchpad.
    words: u32,
    /// The expansion being fed: its next word's word address, and its words left.
    source: u32,
    left: u32,
    /// The expansions that went the general way, counted on the Tracer alone, for the tracer tile's report
    /// (src/bin/tracer.rs): the replay image reports none.
    pub general: u32,
}

impl<const CREDIT: u32, const TRACER: bool> Feeder<CREDIT, TRACER> {
    /// The set's lookup built, from its index in DMEM (`load_core_fed_set`), `templates` templates: the hash's slots
    /// and the cache's lines emptied, every template at the first free slot from its key's own (`replay_ctl.c`'s
    /// `build_hash`) with its FIRST word, and its first chunk in its STREAM entry, FIRST_CHUNKS + its number, from
    /// `words`, the templates' word address in the scratchpad, into the expansion queue's part (`replay_ctl.c`'s
    /// preloaded streams). It writes no STREAM entry below FIRST_CHUNKS, so a transfer of those may run meanwhile
    /// (8.6). Then the feeder, `stream_me` the tile's own dst for the DMA unit (8.3: 0 on a compute tile, its id on a
    /// special tile), its mask 0 until its controller sets one.
    pub fn new(templates: u32, words: u32, stream_me: u32) -> Self {
        KEYS.clear();
        CODE_VALUES.clear();
        for number in 0..templates {
            let entry = template(number);
            let mut slot = slot_of(entry.key);
            while KEYS.word(slot) != 0 {
                slot = (slot + 1) % SLOTS;
            }
            let chunk = HALF_QUEUE.min(entry.length);
            let inline = if TRACER && entry.class() == 1 { INLINE } else { 0 };
            let nop = if entry.rd0() == 1 || entry.rd0() == 3 { NOP_POLICY } else { 0 };
            KEYS.set(slot, entry.key);
            FIRST.set(slot, chunk | number << NUMBER_SHIFT | inline | nop);
            let stream = (FIRST_CHUNKS + number) as usize;
            set_stream(stream, words + 4 * entry.offset, region_part(EXPANSION_PART), chunk, COUNTER_LOCAL, stream_me);
        }
        Self { mask: 0, stream_me, write_me: tile_id(), words, source: 0, left: 0, general: 0 }
    }

    /// Whether the expansion being fed has words left.
    pub fn feeding(&self) -> bool {
        self.left != 0
    }

    /// A new expansion (17.8.6, 17.8.13), posted with `events`, the load of the peripheral's EVENTS that saw it. A code
    /// value the cache holds goes the short way (`present_cached`): one in its own line here, any other from
    /// `present_probe`, which sends the rest the general way. Err(the code value) when the set has no template for it.
    #[inline(always)]
    pub fn present(&mut self, events: u32) -> Result<(), u32> {
        let w = load(PERIPHERAL + EXPANSION_OPCODE);
        let line = line_of(w);
        if CODE_VALUES.word(line) != w {
            return self.present_probe(events, w, line);
        }
        self.present_cached(events, line);
        Ok(())
    }

    /// The short way, the code value in cache line `line`: the expansion credit taken (`take_credit`), on the Tracer an
    /// inline's number into EXPANSION_TEMPLATE, and the first chunk sent by the template's STREAM entry, the template's
    /// first word carrying its rd0.
    #[inline(always)]
    fn present_cached(&mut self, events: u32, line: usize) {
        self.take_credit(events);
        let first = CODE_FIRST.word(line);
        self.latch(first);
        self.send_first_chunk(first);
    }

    /// The code value `w` not in its own line, `line`: the next lines of its probe, to the first that holds it, the
    /// short way, or to the first empty one, or past the last, the general way, the line it then takes the empty one,
    /// or, none, its own. One call for both, so that the general way's first expansion pays one call.
    #[cold]
    #[inline(never)]
    fn present_probe(&mut self, events: u32, w: u32, line: usize) -> Result<(), u32> {
        let mut free = line;
        for probe in 0..PROBES {
            let at = (line + probe) % LINES;
            let held = CODE_VALUES.word(at);
            if held == w {
                self.present_cached(events, at);
                return Ok(());
            }
            if held == 0 {
                free = at;
                break;
            }
        }
        self.present_general(events, w, free)
    }

    /// The general way (`replay_ctl.c`'s `new_expansion_slow`, `tracer_ctl.c`'s `new_expansion`): the template found by
    /// the code value's key, the expansion credit taken and, on the Tracer, an inline's number stored, then its words
    /// from `EXPANSION_OFFSET`, which is 0 but at a pass's first expansion, or a segment's (17.8.2, 17.9.4), which may
    /// start mid-sequence:
    /// - under a NOP policy with rd x0 the shared NOP row goes alone, carrying the template's rd0 (17.8.13);
    /// - at offset 0 the first chunk goes by the template's STREAM entry, and the cache's line `line` then holds the
    ///   code value, so that its next expansions go the short way (the cache is empty at the start, so the first never
    ///   does);
    /// - mid-sequence, the word at the offset goes by the write unit, carrying the template's rd0, then the rest of
    ///   the first chunk by the DMA unit.
    #[inline(always)]
    fn present_general(&mut self, events: u32, w: u32, line: usize) -> Result<(), u32> {
        let slot = lookup(template_key(w)).ok_or(w)?;
        if TRACER {
            self.general += 1;
        }
        self.take_credit(events);
        let first = FIRST.word(slot);
        self.latch(first);
        let offset = load(PERIPHERAL + EXPANSION_OFFSET);
        let entry = template(first >> NUMBER_SHIFT & NUMBER);
        self.left = 0;
        if first & NOP_POLICY != 0 && (w >> 7) & 31 == 0 {
            self.push_word(with_rd0(NOP_ROW.row(0), entry.rd0()));
        } else if offset >= entry.length {
            // Nothing to send: an empty template or an offset past its end, malformed tables (`replay_ctl.c`'s `off >=
            // len`). Its first chunk would arm a counter of 0 and wait for ever.
        } else if offset == 0 {
            CODE_VALUES.set(line, w);
            CODE_FIRST.set(line, first);
            self.send_first_chunk(first);
            return Ok(());
        } else {
            self.source = self.words + 4 * (entry.offset + offset);
            self.left = entry.length - offset;
            copy_to_dmem(self.source, SCRATCH.address(), 1, COUNTER_LOCAL, self.stream_me);
            self.push_word(with_rd0(SCRATCH.row(0), entry.rd0()));
            self.source += 4;
            self.left -= 1;
            let rest = self.left.min(HALF_QUEUE - 1);
            if rest != 0 {
                set_stream(0, self.source, region_part(EXPANSION_PART), rest, COUNTER_LOCAL, self.stream_me);
                start_transfer(0, 1, rest);
                wait_transfer();
                self.source += 4 * rest;
                self.left -= rest;
            }
        }
        self.store_mask();
        Ok(())
    }

    /// The expansion credit its first words take (17.8.8: the post fills it), unless `events`, the load that saw the
    /// post, took it: a load of EVENTS with the credit alone in EVENTS_MASK, or with none, which takes nothing; then
    /// `store_mask` sets the mask back. No branch: the post never comes with the credit in the controller's mask (it is
    /// there only while an expansion has words left), so the load takes it every time.
    #[inline(always)]
    fn take_credit(&self, events: u32) {
        store(PERIPHERAL + P_EVENTS_MASK, CREDIT & !events);
        load(PERIPHERAL + P_EVENTS);
    }

    /// On the Tracer, an inline's number into EXPANSION_TEMPLATE, before its first word (17.9.4, 17.9.11; meshsim's
    /// item 145), from its template's FIRST word `first`.
    #[inline(always)]
    fn latch(&self, first: u32) {
        if TRACER && first & INLINE != 0 {
            store(PERIPHERAL + EXPANSION_TEMPLATE, TEMPLATE_VALID | first >> NUMBER_SHIFT & NUMBER);
        }
    }

    /// The first chunk of the template whose FIRST word is `first`, half a queue or the whole template, by its STREAM
    /// entry, landed; its words left for its credits.
    #[inline(always)]
    fn send_first_chunk(&mut self, first: u32) {
        let number = first >> NUMBER_SHIFT & NUMBER;
        let chunk = first & CHUNK;
        start_transfer(FIRST_CHUNKS + number, 1, chunk);
        let entry = template(number);
        self.source = self.words + 4 * (entry.offset + chunk);
        self.left = entry.length - chunk;
        self.store_mask();
        wait_transfer();
    }

    /// One word into the expansion queue by the write unit, landed.
    fn push_word(&self, word: [u32; 4]) {
        tile_store(counter(COUNTER_LOCAL), 1);
        send_128(self.write_me, region_part(EXPANSION_PART), COUNTER_LOCAL, &word);
        wait_all(event_counter_done(COUNTER_LOCAL));
    }

    /// The peripheral's EVENTS_MASK stored, its expansion credit there while the expansion has words left (17.8.13).
    #[inline(always)]
    pub fn store_mask(&mut self) {
        self.mask = if self.left != 0 { self.mask | CREDIT } else { self.mask & !CREDIT };
        store(PERIPHERAL + P_EVENTS_MASK, self.mask);
    }

    /// The expansion's next chunk, on its credit, half a queue or the rest, in STREAM[`stream`] for the caller's
    /// transfer: its entries, 0 when the expansion has none left.
    #[inline(always)]
    pub fn refill(&mut self, stream: usize) -> u32 {
        let count = HALF_QUEUE.min(self.left);
        if count != 0 {
            set_stream(stream, self.source, region_part(EXPANSION_PART), count, COUNTER_LOCAL, self.stream_me);
            self.source += 4 * count;
            self.left -= count;
        }
        count
    }
}
