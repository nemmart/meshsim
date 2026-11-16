//! The memory tile's firmware: it serves the memory tile's requests (rtl/tile/base/spec.md 11.5), `READ` and `WRITE`,
//! with the RW engine, its peripheral 1 (17.7), one request at a time, in the order they arrive.
//!
//! The chip's tests place it in the memory tile's IMEM and DMEM before the reset ends, and the boot ROM starts it at
//! `_start` (start.S; 6.5, PARAMS.boot_local 1). mtvec stays at the boot ROM's handler, so a trap sends the master tile
//! reports 24 and 25 and parks (6.2). The firmware sleeps until a request is pending (EVENTS.request_pending, 11.1),
//! pops it and serves it to its end before it pops the next:
//!
//! - `READ`: a read job of `beats` beats from the request's LPDDR beat address into the RW engine's output queue 0,
//!   and its DMA's transfer of them to the requester, 128-bit `WRITE`s from the request's address up, counted on its
//!   data counter (17.7), a packet a beat; both started together, the DMA's draining the queue as the job fills it.
//!   Then, with a reply counter, the reply: a 128-bit `WRITE` of 0 to the reply address, from output queue 1 (11.4,
//!   11.5).
//! - `WRITE`: a part of the write buffer, its region 0x04, for the request's beats: the whole buffer from its offset
//!   0, since one request is served at a time. COUNTER_1 armed with the beats, then the 128-bit reply: the part's
//!   address in [31:0], the counter, 1, in [34:32], and the bits above 0 (11.5). Once COUNTER_1 has drained, the
//!   requester's beats have landed in the part (12.3), and a write job copies them to LPDDR; the request is served
//!   when LPDDR has answered its last write (17.7). So a request served after it, a `READ` among them, sees what it
//!   wrote.
//! - any other op: undefined (11.5); it is popped and ignored.
//!
//! Every wait sleeps in WFI on its event (7.2, 17.2): the tile's request_pending and counter_1_done, and the RW
//! engine's done and dma_done through the tile's peripheral_1, each re-checked after every wake.

#![no_std]
#![no_main]

use core::arch::global_asm;
use core::panic::PanicInfo;
use tile::*;

global_asm!(include_str!("../../start.S"));

// ---- The RW engine, peripheral 1 (17.7) ----

const RW: usize = peripheral_window(1);
const RW_CONFIG: usize = 0x080;
const RW_LPDDR_ADDR: usize = 0x084;
const RW_BUFFER_ADDR: usize = 0x088;
const RW_REPLY_W0: usize = 0x08C;
const RW_REPLY_SEND: usize = 0x090;
/// RW_CONFIG's kind [0]: 0 a read, 1 a write; its beats [10:1].
const KIND_READ: u32 = 0;
const KIND_WRITE: u32 = 1;
/// The output queues (17.7): 0 the read's beats, 1 the replies.
const QUEUE_BEATS: u32 = 0;
const QUEUE_REPLY: u32 = 1;
/// The write buffer's address in the memory tile's map: its region, 0x04 (17.1, 17.7).
const WRITE_BUFFER: u32 = peripheral_region(1);
/// The counter that counts a `WRITE` request's beats into the buffer.
const BUFFER_COUNTER: u32 = 1;

/// A peripheral's DMA_HDR (17.1): the header block (its size the DMA's, ignored), dst [14:8], count [26:15], in
/// 128-bit packets, and the output queue [28:27].
const fn dma_header(counter: u32, dst: u32, packets: u32, queue: u32) -> u32 {
    header(MODE_WRITE, counter, 0, dst) | packets << 15 | queue << 27
}

fn rw_store(offset: usize, value: u32) {
    store(RW + offset, value)
}

/// Sleeps until the RW engine has posted every event of `events` (its EVENTS_MASK holds both of its standard ones,
/// so the tile's peripheral_1 wakes the core), consuming each as its load of EVENTS returns it (17.2).
fn wait_rw(events: u32) {
    let mut seen = 0;
    while seen & events != events {
        sleep_until(event_peripheral(1));
        seen |= load(RW + P_EVENTS);
    }
}

/// A DMA transfer from output queue `queue`: `packets` 128-bit packets to `dst`'s `address`, counted on `counter`
/// (17.4).
fn start_dma(queue: u32, counter: u32, dst: u32, address: u32, packets: u32) {
    rw_store(P_DMA_HDR, dma_header(counter, dst, packets, queue));
    rw_store(P_DMA_DEST_ADDR, address);
    rw_store(P_DMA_START, 1);
}

/// A request's fields (11.5).
struct Request {
    requester: u32,
    beats: u32,
    data_counter: u32,
    lpddr: u32,
    data_address: u32,
    reply_address: u32,
    reply_counter: u32,
}

impl Request {
    fn new(words: [u32; 4]) -> Self {
        Request {
            requester: requester(words[0]),
            beats: (words[0] >> 18) & 0x3FF,
            data_counter: (words[0] >> 28) & 0x7,
            lpddr: words[1],
            data_address: words[2],
            reply_address: words[3] & 0xFF_FFFF,
            reply_counter: (words[3] >> 24) & 0x7,
        }
    }

    /// `READ`: the job and its DMA's transfer, then the reply, if the request asks for one.
    fn read(&self) {
        rw_store(RW_CONFIG, KIND_READ | self.beats << 1);
        rw_store(RW_LPDDR_ADDR, self.lpddr);
        rw_store(P_START, 1);
        start_dma(QUEUE_BEATS, self.data_counter, self.requester, self.data_address, self.beats);
        wait_rw(P_EVENT_DONE | P_EVENT_DMA_DONE);
        if self.reply_counter != 0 {
            // The value 0: RW_REPLY_W0 is 0, as every send leaves it, and the send's word 0.
            rw_store(RW_REPLY_SEND, 0);
            start_dma(QUEUE_REPLY, self.reply_counter, self.requester, self.reply_address, 1);
            wait_rw(P_EVENT_DMA_DONE);
        }
    }

    /// `WRITE`: the part armed, the reply, the beats' landing, then the job.
    fn write(&self) {
        if self.beats != 0 {
            tile_store(counter(BUFFER_COUNTER), self.beats);
        }
        rw_store(RW_REPLY_W0, WRITE_BUFFER);
        rw_store(RW_REPLY_SEND, BUFFER_COUNTER);
        start_dma(QUEUE_REPLY, self.reply_counter, self.requester, self.reply_address, 1);
        wait_rw(P_EVENT_DMA_DONE);
        if self.beats == 0 {
            return;
        }
        // The counter's drain: the beats are in the part. The load of EVENTS consumes counter_1_done.
        sleep_until(event_counter_done(BUFFER_COUNTER));
        tile_load(EVENTS);
        rw_store(RW_CONFIG, KIND_WRITE | self.beats << 1);
        rw_store(RW_LPDDR_ADDR, self.lpddr);
        rw_store(RW_BUFFER_ADDR, 0);
        rw_store(P_START, 1);
        wait_rw(P_EVENT_DONE);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn main() -> ! {
    rw_store(P_EVENTS_MASK, P_EVENT_DONE | P_EVENT_DMA_DONE);
    loop {
        sleep_until(EVENT_REQUEST_PENDING);
        let words = receive_request();
        let request = Request::new(words);
        match request_op(words[0]) {
            OP_READ => request.read(),
            OP_WRITE => request.write(),
            _ => {}
        }
    }
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop {
        wfi();
    }
}
