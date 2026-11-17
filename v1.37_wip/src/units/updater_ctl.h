// updater_ctl.h: the Updater's controller, in the shape of hardware (updater_wip, WORKER6; the plan is
// ../updater_wip/planning/sessions/WORKER6/NOTES.md, "NOTES" below). It sits behind the black box's registers
// (../v1.4_wip/planning/UPDATER-INTERFACE.md, "UI"); updater_top.h makes it the slot's peripheral. Header-only, as the RW
// engine is.
//
// ITS PORTS, every output a flip-flop (axi_port.h's cycle convention):
//   the word port (word_port.h), the sink: `ready` out; `valid` and 128 bits in.
//   the AXI port (axi_port.h), client 1: MasterOut out; SlaveOut in. One burst at a time each way.
//   the look-up port 1 (lookup_port.h): Request out; the cache's `ready` and the shared reply bus in.
//   to the control block (updater_top.h): `busy`, the error bits, and two pulses, `done` and `free_page_credit`.
//   the window's block 2 and 3, and the free page queue's write port (region part 2), written by the bus.
//
// A CYCLE (NOTES section 2). `Regs` is everything kept between cycles. `out()` is a function of `Regs` alone. `edge(t,
// in)` takes the other units' outputs of cycle t and computes the registers at the end of t from those at its start
// (`q`, const) into `d`, top to bottom: the input FIFO, the decoder, the state machine with the AXI channels and the
// look-up port, the statistics; then q = d. The page memory (PageMemory) is a unit of its own with one port, driven by
// the edge's logic as an SRAM is: what it is given in cycle t it takes at the edge that ends t, and a row read then is
// in its read register in t + 1.
//
// WHAT IT DOES (NOTES sections 5 to 7): the decoder turns the input into update words, one a cycle at most; a header
// word opens a run of values; each value goes to its 64-bit word of the page held in the page memory. A page is brought
// in by a look-up of the image (and with tracer_copy of the Tracer's copy); where a side does not own it, every level
// from the first it does not own down is made its own: a free page taken, built (read with its owned bits cleared, or
// holes), written whole, its B awaited, then the entry above changed by an update and its reply awaited. The data page
// is held until a value needs another page or the run ends, then written (an owned page in place by its dirty halves; a
// new page whole) and, if new, named by an update. Halves are read when first touched unless the run covers them whole.
#pragma once

#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "units/axi_port.h"
#include "units/lookup_port.h"
#include "units/word_port.h"

namespace units {
namespace updctl {

// ================================================================================================ the page memory
// 4 KB: 256 rows of 128 bits. One port: a read or a write a cycle; a write enable for each 64-bit half of a row; a read's
// data in `rdata` from the next cycle. One port is enough: R beats in, patches and W beats out never share a cycle
// (NOTES section 4).
struct PageMemory {
    static constexpr unsigned ROWS = 256;
    struct Port {                                  // what the controller gives it in a cycle
        bool en = false;                           // an access
        bool we[2] = {};                           // a write of half 0 (bits 63:0), half 1 (127:64); neither: a read
        uint8_t row = 0;
        uint64_t wdata[2] = {};
    };
    uint64_t mem[ROWS][2] = {};
    uint64_t rdata[2] = {};                        // the read register
    uint64_t reads = 0, writes = 0;                // counts (the figures)
    void edge(const Port& p) {
        if (!p.en) return;
        if (p.we[0] || p.we[1]) {
            if (p.we[0]) mem[p.row][0] = p.wdata[0];
            if (p.we[1]) mem[p.row][1] = p.wdata[1];
            writes++;
        } else {
            rdata[0] = mem[p.row][0]; rdata[1] = mem[p.row][1];
            reads++;
        }
    }
};

// ================================================================================================ the controller
struct Ctl {
    // ---- the window's words (UI blocks 2 and 3): the black box's numbers
    enum : int { W_RUN_CONFIG = 32, W_WORD_COUNT = 33, W_PAGE_COUNT = 34, W_IMAGE_ROOT = 35, W_TRACER_ROOT = 36, W_SOURCE_ROOT = 37,
                 W_STAT_CYCLES = 48, W_STAT_PAGES_WRITTEN = 53 };
    enum : unsigned { S_CYCLES, S_STALL_DATA, S_STALL_PAGES, S_STALL_MEMORY, S_PAGES_READ, S_PAGES_WRITTEN };
    enum : uint32_t { RC_LRU = 1u << 0, RC_CONTINUE = 1u << 1, RC_TRACER_COPY = 1u << 2, RC_DEPTH_LO = 3, RC_ROOT_LO = 5, RC_BITS = 0x7F };
    enum : unsigned { ROOT_EXISTS = 0, ROOT_EMPTY = 1, ROOT_COPY = 2 };
    // ERRORS' bits (UI): bit 0 is the free page queue's overrun here; the top ORs the shim's in
    enum : uint8_t { ERR_QUEUE_OVERRUN = 1 << 0, ERR_BEYOND = 1 << 1, ERR_LPDDR = 1 << 2, ERR_OUT_OF_PAGES = 1 << 3, ERR_NOT_OWNED = 1 << 4 };

    static constexpr unsigned FIFO = 4;            // words kept behind the port (NOTES C1)
    static constexpr unsigned BB_BITS = 194;       // the decoder's bit buffer: 65 left over + a word of 128, + 1
    static constexpr unsigned LRU = 32;
    static constexpr unsigned FREE_PAGES = 256, FREE_HALF = 128;
    static constexpr uint32_t NONE = lookup::NO_PAGE, OWNED = lookup::OWNED;

    // a value's type (NOTES section 5): what the run delivers from it, counting only this run's values
    enum : uint8_t { T_WORDS = 0, T_HALF = 1, T_PAGE = 2 };
    // where a W beat's half of a page comes from: the page memory, or a constant
    enum : uint8_t { W_MEM = 0, W_ZERO = 1, W_ONES = 2 };
    // the two files a run may write: the image, and with tracer_copy the Tracer's copy
    enum : uint8_t { IMAGE = 0, TRACER = 1 };

    // The state machine (NOTES section 6).
    enum State : uint8_t {
        IDLE,           // no run
        STOPPED,        // an error ended the run: busy stays 1, no done
        CHECK,          // START taken: the roots checked; the root made if asked
        ROOT_READ,      // root 2: SOURCE_ROOT read into the page memory, owned bits cleared
        ROOT_WRITE,     // the root written: IMAGE_ROOT, then with root 2 and tracer_copy TRACER_ROOT; awaiting B
        NEXT,           // the decoder's next value: patched, or what it needs first
        HALF_READ,      // a half of the held page read
        FILL,           // a half of a held hole filled with zeros
        FLUSH,          // the held page to be written
        FLUSH_READ,     // its missing halves read
        FLUSH_WRITE,    // written; awaiting B
        FLUSH_UPDATE,   // its entry updated; awaiting the reply
        LOOKUP,         // the page's look-up, for `side`; awaiting the reply
        LEVEL,          // a fault: what (level, side) does
        LOAD,           // a source page read into the page memory
        TAKE,           // a free page taken
        NEW_WRITE,      // the new page written; awaiting B
        NEW_UPDATE,     // the entry above it updated; awaiting the reply
    };

    struct Regs {
        // block 2 and 3
        uint32_t run_config = 0, word_count = 0, page_count = 0, image_root = 0, tracer_root = 0, source_root = 0;
        uint32_t stat[6] = {};
        // the control block's
        bool busy = false;
        uint8_t errors = 0;
        bool done_pulse = false, credit_pulse = false;
        // the input FIFO
        uint64_t fifo[FIFO][2] = {};
        uint8_t fifo_head = 0, fifo_n = 0;
        bool ready = true;
        // the decoder
        uint64_t bb[4] = {};                       // bit k of the stream at bit k; BB_BITS used
        uint8_t nb = 0;
        uint64_t lru[LRU] = {};
        uint32_t run_addr = 0, run_left = 0;
        bool out_valid = false;
        uint32_t out_addr = 0;
        uint64_t out_value = 0;
        uint8_t out_type = T_WORDS;
        // the free page queue: 256 entries of 32 bits
        uint32_t fp[FREE_PAGES] = {};
        uint8_t fp_head = 0;
        uint16_t fp_n = 0;
        uint8_t fp_left = 0;                       // entries gone since the last credit
        // the state machine
        State state = IDLE;
        uint8_t level = 0, side = TRACER;
        uint8_t look_status[2] = {}, look_level[2] = {};
        uint32_t look_entry[2] = {};               // the entry where the walk stopped; ok: the data page's
        bool takes[2] = {};                        // the side makes a page its own from look_level down
        uint32_t src[2] = {};                      // the side's source at `level`: the page to copy, or NONE
        bool pm_ok = false;                        // the page memory holds `pm_src` for `pm_level` ...
        uint32_t pm_src = 0;
        uint8_t pm_level = 0;
        uint32_t pm_next = NONE;                   // ... and the entry at the next level, caught as it streamed in
        uint32_t new_page = 0;                     // the page just taken
        uint16_t fill_row = 0;
        // the held page
        bool held = false, held_owned = false, held_hole = false;
        uint32_t held_page = 0;                    // its number in the file
        uint32_t held_at = 0;                      // where it is written (128-byte units)
        uint32_t held_src = 0;                     // where its missing halves come from
        bool have[2] = {}, dirty[2] = {};
        // the AXI port: AR and the read in progress
        bool ar_valid = false;
        uint64_t ar_addr = 0;
        uint8_t ar_len = 0;
        bool rd_active = false, rd_xform = false, rd_err = false;
        uint16_t rd_row = 0;                       // the page memory row of the next R beat
        uint16_t rd_catch = 0xFFFF;                // the entry to catch: its index in the page (or none)
        // AW, W and the write in progress (the RW engine's W path: a row read in c is pending in c + 1)
        bool aw_valid = false;
        uint64_t aw_addr = 0;
        uint8_t aw_len = 0;
        bool wr_active = false, wr_err = false;
        uint8_t wr_src[2] = {};                    // each half of the page: W_MEM, W_ZERO or W_ONES
        uint16_t wr_rows_left = 0, wr_row = 0;     // the rows not yet taken into W, and the next of them
        bool w_pending = false;
        uint16_t w_pend_row = 0;
        bool w_valid = false, w_last = false;
        uint64_t w_data[2] = {};
        // the look-up port
        lookup::Request lk;
        bool lk_wait = false;                      // a request taken, its reply not yet in
        uint8_t rp_status = 0, rp_level = 0;
        uint32_t rp_entry = 0;
    };

    struct Outputs {
        wordport::SinkOut wp;
        axi::MasterOut axi;
        lookup::Request lk;
        bool busy = false, done = false, free_page_credit = false;
        uint8_t errors = 0;
    };
    struct Inputs {
        wordport::SourceOut wp;
        axi::SlaveOut axi;
        bool lk_ready = false;
        lookup::Reply reply;
    };

    Regs q;
    PageMemory pm;
    int64_t lpddr_units = int64_t(1) << 25;        // LPDDR's end in 128-byte units: a root or a free page must lie below
    // counts for the figures (not state)
    uint64_t lookups = 0, updates = 0, read_bursts = 0, write_bursts = 0, beats_in = 0, beats_out = 0, words_in = 0, values = 0;
    // A shadow for the comparison with the black box, not a register: whether the value in the output register would be
    // "covered" by the black box's test (its address a multiple of 256 and run_left >= 256, WORD_COUNT not counted), and
    // the halves this run read that the black box's test skips (NOTES section 5).
    bool shadow_uncut = false;
    uint64_t cut_half_reads_run = 0;

    Ctl() { reset(); }
    void reset() { q = Regs{}; pm = PageMemory{}; }

    // ---- the outputs: the registers
    Outputs out() const {
        Outputs o;
        o.wp.ready = q.ready;
        o.axi.arvalid = q.ar_valid; o.axi.araddr = q.ar_addr; o.axi.arlen = q.ar_len;
        o.axi.awvalid = q.aw_valid; o.axi.awaddr = q.aw_addr; o.axi.awlen = q.aw_len;
        o.axi.wvalid = q.w_valid; o.axi.wdata[0] = q.w_data[0]; o.axi.wdata[1] = q.w_data[1]; o.axi.wlast = q.w_last;
        o.lk = q.lk;
        o.busy = q.busy; o.done = q.done_pulse; o.free_page_credit = q.credit_pulse;
        o.errors = q.errors;
        return o;
    }

    // ---- the bus's side: a store or START at the end of its cycle; a WRITE into region part 2 as it lands
    bool word(int w, uint32_t& v) const {
        switch (w) {
            case W_RUN_CONFIG: v = q.run_config; return true;
            case W_WORD_COUNT: v = q.word_count; return true;
            case W_PAGE_COUNT: v = q.page_count; return true;
            case W_IMAGE_ROOT: v = q.image_root; return true;
            case W_TRACER_ROOT: v = q.tracer_root; return true;
            case W_SOURCE_ROOT: v = q.source_root; return true;
            default:
                if (w >= W_STAT_CYCLES && w <= W_STAT_PAGES_WRITTEN) { v = q.stat[w - W_STAT_CYCLES]; return true; }
                return false;
        }
    }
    void write_block2(int w, uint32_t v) {
        switch (w) {
            case W_RUN_CONFIG: q.run_config = v & RC_BITS; break;
            case W_WORD_COUNT: q.word_count = v; break;
            case W_PAGE_COUNT: q.page_count = v; break;
            case W_IMAGE_ROOT: q.image_root = v; break;
            case W_TRACER_ROOT: q.tracer_root = v; break;
            default: q.source_root = v; break;
        }
    }
    // START: busy from the next cycle; `continue` 0 clears the decoder (UI)
    void start() {
        q.busy = true;
        q.state = CHECK;
        if (!(q.run_config & RC_CONTINUE)) {
            std::memset(q.bb, 0, sizeof q.bb); q.nb = 0;
            std::memset(q.lru, 0, sizeof q.lru);
            q.run_addr = q.run_left = 0;
        }
        q.out_valid = false;
        q.held = false;
        cut_half_reads_run = 0;
    }
    // a free page queue entry (its page in [31:0]); a push into a full queue is dropped
    void push_free_page(uint32_t page) {
        if (q.fp_n == FREE_PAGES) { q.errors |= ERR_QUEUE_OVERRUN; return; }
        q.fp[uint8_t(q.fp_head + q.fp_n)] = page;
        q.fp_n++;
    }

    // ---- nothing in progress: no run, no burst, no request, no pulse
    bool quiet() const {
        return !(q.busy && q.state != STOPPED) && !q.done_pulse && !q.credit_pulse && !q.ar_valid && !q.rd_active && !q.aw_valid &&
               !q.wr_active && !q.w_valid && !q.lk.valid && !q.lk_wait;
    }
    bool stopped() const { return q.busy && q.state == STOPPED; }

    // ================================================================ the edge that ends cycle t
    void edge(int64_t t, const Inputs& in) {
        (void)t;
        Regs d = q;
        PageMemory::Port port;
        d.done_pulse = false;
        d.credit_pulse = false;
        const unsigned depth = q.run_config >> RC_DEPTH_LO & 3;
        const bool lru = q.run_config & RC_LRU, tracer_copy = q.run_config & RC_TRACER_COPY;
        const bool running = q.busy && q.state != STOPPED;
        auto pm_use = [&](const PageMemory::Port& p) {
            if (port.en) FATAL("the Updater's controller: two accesses to the page memory's one port in a cycle");
            port = p;
        };

        // ---------------------------------------------------------------- the AXI port: R
        if (q.ar_valid && in.axi.arready) d.ar_valid = false;
        if (in.axi.rvalid) {
            if (!q.rd_active) FATAL("the Updater's controller: an R beat with no read in progress");
            uint64_t b[2] = {in.axi.rdata[0], in.axi.rdata[1]};
            if (q.rd_xform)                                    // an index page: every entry no longer owned
                for (int h = 0; h < 2; h++)
                    for (int l = 0; l < 2; l++) {
                        const uint32_t e = uint32_t(b[h] >> (32 * l));
                        if (e != NONE) b[h] &= ~(uint64_t(OWNED) << (32 * l));
                    }
            if (q.rd_catch != 0xFFFF && q.rd_catch >> 2 == (q.rd_row & 0xFF)) {   // four entries a beat
                const unsigned lane = q.rd_catch & 3;
                d.pm_next = uint32_t(b[lane >> 1] >> (32 * (lane & 1)));
            }
            PageMemory::Port p; p.en = true; p.we[0] = p.we[1] = true; p.row = uint8_t(q.rd_row); p.wdata[0] = b[0]; p.wdata[1] = b[1];
            pm_use(p);
            d.rd_row = uint16_t(q.rd_row + 1);
            if (in.axi.rresp != axi::OKAY) d.rd_err = true;
            if (in.axi.rlast) d.rd_active = false;
            beats_in++;
        }
        // ---------------------------------------------------------------- the AXI port: AW, W, B
        if (q.aw_valid && in.axi.awready) d.aw_valid = false;
        {
            const bool take = q.w_pending && (!q.w_valid || in.axi.wready);
            const uint16_t rows_after = uint16_t(q.wr_rows_left - (take ? 1 : 0));
            const bool fetch = q.wr_active && rows_after != 0;
            const uint16_t row_after = uint16_t(q.wr_row + (take ? 1 : 0));
            if (take) {
                const uint8_t s = q.wr_src[(q.w_pend_row >> 7) & 1];
                const uint64_t c = s == W_ONES ? ~uint64_t(0) : 0;
                d.w_data[0] = s == W_MEM ? pm.rdata[0] : c;
                d.w_data[1] = s == W_MEM ? pm.rdata[1] : c;
                d.w_last = q.wr_rows_left == 1;
                beats_out++;
            }
            d.w_valid = take || (q.w_valid && !in.axi.wready);
            d.w_pending = fetch;
            d.w_pend_row = row_after;
            d.wr_rows_left = rows_after;
            d.wr_row = row_after;
            if (fetch && q.wr_src[(row_after >> 7) & 1] == W_MEM) {
                PageMemory::Port p; p.en = true; p.row = uint8_t(row_after);
                pm_use(p);
            }
        }
        if (in.axi.bvalid) {
            if (!q.wr_active) FATAL("the Updater's controller: a B response with no write in progress");
            d.wr_active = false;
            if (in.axi.bresp != axi::OKAY) d.wr_err = true;
        }
        // ---------------------------------------------------------------- the look-up port
        if (q.lk.valid && in.lk_ready) { d.lk.valid = false; d.lk_wait = true; }
        if (in.reply.valid && in.reply.client == lookup::UPDATER) {
            if (!q.lk_wait) FATAL("the Updater's controller: a reply with no request outstanding");
            d.lk_wait = false;
            d.rp_status = in.reply.status; d.rp_level = in.reply.level; d.rp_entry = in.reply.entry;
        }
        const bool reply_in = !q.lk.valid && !q.lk_wait;   // the last request's reply is in rp_*

        // ---------------------------------------------------------------- the input FIFO and the decoder
        const bool push = in.wp.valid && q.ready;
        bool shadow_next = shadow_uncut;
        bool take_value = false;                           // the state machine takes the decoder's output (below)
        // The state machine is evaluated first, so the decoder knows whether its output register is freed this cycle.
        // ================================================================ the state machine
        auto stop = [&](uint8_t e) { d.errors |= e; d.state = STOPPED; };
        auto page_addr_ok = [&](uint32_t a) { return int64_t(a) % lookup::PAGE_UNITS == 0 && int64_t(a) < lpddr_units; };
        auto read_burst = [&](uint32_t unit, unsigned half, unsigned halves, bool xform, int catch_entry) {
            d.ar_valid = true; d.ar_addr = (uint64_t(unit) << axi::UNIT_SHIFT) + 2048u * half; d.ar_len = uint8_t(128 * halves - 1);
            d.rd_active = true; d.rd_row = uint16_t(128 * half); d.rd_xform = xform; d.rd_err = false;
            d.rd_catch = catch_entry < 0 ? 0xFFFF : uint16_t(catch_entry);
            d.stat[S_PAGES_READ]++;
            read_bursts++;
        };
        auto write_burst = [&](uint32_t unit, unsigned half, unsigned halves, uint8_t src0, uint8_t src1) {
            d.aw_valid = true; d.aw_addr = (uint64_t(unit) << axi::UNIT_SHIFT) + 2048u * half; d.aw_len = uint8_t(128 * halves - 1);
            d.wr_active = true; d.wr_err = false; d.wr_src[0] = src0; d.wr_src[1] = src1;
            d.wr_rows_left = uint16_t(128 * halves); d.wr_row = uint16_t(128 * half);
            // the first row read now (in effect: fetch with nothing taken); the W path above sees wr_active from next cycle
            d.w_pending = true; d.w_pend_row = uint16_t(128 * half);
            if ((half == 0 ? src0 : src1) == W_MEM) { PageMemory::Port p; p.en = true; p.row = uint8_t(128 * half); pm_use(p); }
            d.stat[S_PAGES_WRITTEN]++;
            write_bursts++;
        };
        auto request = [&](uint8_t op, uint32_t root, uint32_t page, uint8_t level, uint32_t entry) {
            d.lk.valid = true; d.lk.op = op; d.lk.root = root; d.lk.depth = uint8_t(depth); d.lk.page = page; d.lk.level = level; d.lk.entry = entry;
            if (op == lookup::LOOK_UP) lookups++; else updates++;
        };
        auto digit = [&](uint32_t pg, unsigned level) { return int((pg >> (10 * (depth - 1 - level))) & 1023); };
        auto root_of = [&](unsigned side) { return side == IMAGE ? q.image_root : q.tracer_root; };
        auto reply_fatal = [&](const char* what) {
            FATAL("the Updater's controller: a reply that cannot come at %lld: status %u, level %u, to %s", (long long)t, unsigned(q.rp_status),
                  unsigned(q.rp_level), what);
        };
        // the next (level, side) of a fault's walk: the Tracer's copy, then the image, a level at a time
        auto next_step = [&]() {
            if (q.side == TRACER) { d.side = IMAGE; d.level = q.level; }
            else { d.side = TRACER; d.level = uint8_t(q.level + 1); }
            d.state = LEVEL;
        };
        // the page held: the image's own (owned), with its halves to read as touched
        auto hold_own = [&](uint32_t at) {
            d.held = true; d.held_owned = true; d.held_hole = false; d.held_at = at; d.held_src = at;
            d.have[0] = d.have[1] = false; d.dirty[0] = d.dirty[1] = false;
            d.state = NEXT;
        };
        const uint32_t page_of_out = q.out_addr >> 9;

        switch (q.state) {
            case IDLE:
            case STOPPED:
                break;

            case CHECK: {
                const unsigned root = q.run_config >> RC_ROOT_LO & 3;
                if (!page_addr_ok(q.image_root) || (tracer_copy && !page_addr_ok(q.tracer_root)) ||
                    (root == ROOT_COPY && !page_addr_ok(q.source_root)) || root == 3) { stop(ERR_LPDDR); break; }
                if (root == ROOT_EXISTS) { d.state = NEXT; break; }
                if (root == ROOT_EMPTY) {                      // every entry "no page"; at depth 0 the root is the data page: zeros
                    const uint8_t c = depth > 0 ? W_ONES : W_ZERO;
                    write_burst(q.image_root, 0, 2, c, c);
                    d.side = IMAGE; d.state = ROOT_WRITE;
                    break;
                }
                read_burst(q.source_root, 0, 2, depth > 0, -1);
                d.state = ROOT_READ;
                break;
            }
            case ROOT_READ:
                if (q.rd_active || q.ar_valid) break;
                if (q.rd_err) { stop(ERR_LPDDR); break; }
                write_burst(q.image_root, 0, 2, W_MEM, W_MEM);
                d.side = IMAGE; d.state = ROOT_WRITE;
                break;
            case ROOT_WRITE: {
                if (q.wr_active || q.aw_valid || q.w_valid || q.w_pending) break;
                if (q.wr_err) { stop(ERR_LPDDR); break; }
                const unsigned root = q.run_config >> RC_ROOT_LO & 3;
                if (q.side == IMAGE && root == ROOT_COPY && tracer_copy) {
                    write_burst(q.tracer_root, 0, 2, W_MEM, W_MEM);
                    d.side = TRACER;
                    break;
                }
                d.state = NEXT;
                break;
            }

            case NEXT: {
                if (!q.out_valid) {
                    if (q.word_count != 0) { d.stat[S_STALL_DATA]++; break; }   // waiting on the data
                    if (q.held) { d.state = FLUSH; break; }                     // the run's end: the held page, then done
                    d.state = IDLE; d.busy = false; d.done_pulse = true;   // done
                    break;
                }
                if (depth < 3 && page_of_out >= (1u << (10 * depth))) { stop(ERR_BEYOND); break; }   // NOTES C6
                if (q.held && q.held_page != page_of_out) { d.state = FLUSH; break; }
                if (!q.held) {                                                  // bring the page in
                    d.held_page = page_of_out;
                    d.pm_ok = false;
                    if (depth == 0) { hold_own(q.image_root); break; }          // NOTES C11
                    request(lookup::LOOK_UP, q.image_root, page_of_out, 0, 0);
                    d.side = IMAGE; d.state = LOOKUP;
                    break;
                }
                const unsigned at = q.out_addr & 511, half = at >> 8;
                if (!q.have[half]) {
                    if (q.out_type != T_WORDS) d.have[half] = true;             // covered whole by this run: not read
                    else if (q.held_hole) { d.fill_row = uint16_t(128 * half); d.state = FILL; break; }
                    else {
                        read_burst(q.held_src, half, 1, false, -1);
                        if (shadow_uncut) cut_half_reads_run++;
                        d.state = HALF_READ;
                        break;
                    }
                }
                PageMemory::Port p; p.en = true; p.row = uint8_t(at >> 1); p.we[at & 1] = true; p.wdata[at & 1] = q.out_value;
                pm_use(p);
                d.dirty[half] = true;
                take_value = true;
                values++;
                break;
            }
            case HALF_READ:
                if (q.rd_active || q.ar_valid) break;
                if (q.rd_err) { stop(ERR_LPDDR); break; }
                d.have[(q.out_addr >> 8) & 1] = true;
                d.state = NEXT;
                break;
            case FILL: {
                PageMemory::Port p; p.en = true; p.row = uint8_t(q.fill_row); p.we[0] = p.we[1] = true;
                pm_use(p);
                d.fill_row = uint16_t(q.fill_row + 1);
                if ((q.fill_row & 127) == 127) { d.have[q.fill_row >> 7] = true; d.state = NEXT; }
                break;
            }

            case FLUSH: {
                if (q.held_owned) {                                             // in place, by its dirty halves (NOTES C10)
                    if (q.dirty[0] && q.dirty[1]) write_burst(q.held_at, 0, 2, W_MEM, W_MEM);
                    else if (q.dirty[0] || q.dirty[1]) write_burst(q.held_at, q.dirty[0] ? 0 : 1, 1, W_MEM, W_MEM);
                    else { d.held = false; d.state = NEXT; break; }
                    d.state = FLUSH_WRITE;
                    break;
                }
                const bool miss0 = !q.have[0] && !q.held_hole, miss1 = !q.have[1] && !q.held_hole;
                if (miss0 || miss1) {                                           // a new page is written whole: the rest read first
                    read_burst(q.held_src, miss0 ? 0 : 1, miss0 && miss1 ? 2 : 1, false, -1);
                    d.state = FLUSH_READ;
                    break;
                }
                write_burst(q.held_at, 0, 2, q.have[0] ? W_MEM : W_ZERO, q.have[1] ? W_MEM : W_ZERO);
                d.state = FLUSH_WRITE;
                break;
            }
            case FLUSH_READ:
                if (q.rd_active || q.ar_valid) break;
                if (q.rd_err) { stop(ERR_LPDDR); break; }
                d.have[0] = d.have[1] = true;
                d.state = FLUSH;
                break;
            case FLUSH_WRITE:
                if (q.wr_active || q.aw_valid || q.w_valid || q.w_pending) { d.stat[S_STALL_MEMORY]++; break; }
                if (q.wr_err) { stop(ERR_LPDDR); break; }
                if (q.held_owned) { d.held = false; d.state = NEXT; break; }
                request(lookup::UPDATE, q.image_root, q.held_page, uint8_t(depth - 1), q.held_at | OWNED);   // after B: the data first
                d.state = FLUSH_UPDATE;
                break;
            case FLUSH_UPDATE:
                if (!reply_in) { d.stat[S_STALL_MEMORY]++; break; }
                if (q.rp_status == lookup::OK) { d.held = false; d.state = NEXT; break; }
                if (q.rp_status == lookup::NOT_OWNED) { stop(ERR_NOT_OWNED); break; }
                if (q.rp_status == lookup::LPDDR_ERROR) { stop(ERR_LPDDR); break; }
                reply_fatal("an update");
                break;

            case LOOKUP: {
                if (!reply_in) { d.stat[S_STALL_MEMORY]++; break; }
                const uint8_t s = q.side;
                if (q.rp_status == lookup::LPDDR_ERROR) { stop(ERR_LPDDR); break; }
                if (q.rp_status != lookup::OK && q.rp_status != lookup::HOLE && q.rp_status != lookup::NOT_OWNED) { reply_fatal("a look-up"); break; }
                d.look_status[s] = q.rp_status; d.look_level[s] = q.rp_level; d.look_entry[s] = q.rp_entry;
                d.takes[s] = q.rp_status != lookup::OK;
                d.src[s] = q.rp_status == lookup::HOLE ? NONE : q.rp_entry;
                if (s == IMAGE && tracer_copy) {
                    request(lookup::LOOK_UP, q.tracer_root, q.held_page, 0, 0);
                    d.side = TRACER;
                    break;
                }
                // both in: the pages the fault takes, counted before any is taken (NOTES C7)
                const bool ti = d.takes[IMAGE], tt = tracer_copy && d.takes[TRACER];
                if (!tracer_copy) d.takes[TRACER] = false;
                const unsigned needed = (ti ? depth - d.look_level[IMAGE] : 0) + (tt ? depth - d.look_level[TRACER] : 0);
                if (needed > q.page_count) { stop(ERR_OUT_OF_PAGES); break; }
                if (!ti && !tt) { hold_own(d.look_entry[IMAGE] & lookup::ADDRESS); break; }
                d.level = 0; d.side = TRACER; d.state = LEVEL;
                break;
            }
            case LEVEL: {
                const uint8_t k = q.level, s = q.side;
                if (k == depth) { hold_own(q.look_entry[IMAGE] & lookup::ADDRESS); break; }   // the image owned the page
                if (!q.takes[s] || k < q.look_level[s]) { next_step(); break; }
                const bool data = k == depth - 1;
                const uint32_t from = q.src[s];
                if (data && s == IMAGE) { d.state = TAKE; break; }             // the image's data page: taken and held
                if (from == NONE || (q.pm_ok && q.pm_src == from && q.pm_level == k)) { d.state = TAKE; break; }
                read_burst(from & lookup::ADDRESS, 0, 2, !data, data ? -1 : digit(q.held_page, k + 1));
                d.pm_ok = false; d.pm_src = from; d.pm_level = k;
                d.state = LOAD;
                break;
            }
            case LOAD:
                if (q.rd_active || q.ar_valid) { d.stat[S_STALL_MEMORY]++; break; }
                if (q.rd_err) { stop(ERR_LPDDR); break; }
                d.pm_ok = true;
                d.state = TAKE;
                break;
            case TAKE: {
                if (q.fp_n == 0) { d.stat[S_STALL_PAGES]++; break; }
                const uint32_t p = q.fp[q.fp_head];
                d.fp_head = uint8_t(q.fp_head + 1); d.fp_n = uint16_t(q.fp_n - 1);
                d.fp_left = uint8_t(q.fp_left + 1);
                if (d.fp_left == FREE_HALF) { d.fp_left = 0; d.credit_pulse = true; }
                d.page_count = q.page_count - 1;
                if (!page_addr_ok(p)) { stop(ERR_LPDDR); break; }
                const uint8_t k = q.level, s = q.side;
                const bool data = k == depth - 1;
                const uint32_t from = q.src[s];
                if (data && s == IMAGE) {                                       // held: written when the page is done
                    const bool loaded = from != NONE && q.pm_ok && q.pm_src == from && q.pm_level == k;
                    d.held = true; d.held_owned = false; d.held_hole = from == NONE; d.held_at = p;
                    d.held_src = from & lookup::ADDRESS;
                    d.have[0] = d.have[1] = loaded; d.dirty[0] = d.dirty[1] = false;
                    d.state = NEXT;
                    break;
                }
                const uint8_t c = from == NONE ? (data ? W_ZERO : W_ONES) : W_MEM;
                write_burst(p, 0, 2, c, c);
                d.new_page = p;
                d.state = NEW_WRITE;
                break;
            }
            case NEW_WRITE:
                if (q.wr_active || q.aw_valid || q.w_valid || q.w_pending) { d.stat[S_STALL_MEMORY]++; break; }
                if (q.wr_err) { stop(ERR_LPDDR); break; }
                request(lookup::UPDATE, root_of(q.side), q.held_page, q.level, q.new_page | OWNED);   // after B: the data first
                d.state = NEW_UPDATE;
                break;
            case NEW_UPDATE: {
                if (!reply_in) { d.stat[S_STALL_MEMORY]++; break; }
                if (q.rp_status == lookup::NOT_OWNED) { stop(ERR_NOT_OWNED); break; }
                if (q.rp_status == lookup::LPDDR_ERROR) { stop(ERR_LPDDR); break; }
                if (q.rp_status != lookup::OK) { reply_fatal("an update"); break; }
                // the side's source at the next level: the entry caught from the page copied, or a hole below a hole
                d.src[q.side] = q.src[q.side] == NONE ? NONE : q.pm_next;
                next_step();
                break;
            }
        }
        if (q.busy && q.state != STOPPED) d.stat[S_CYCLES]++;
        // the memory waits the states above did not count: a burst or a request in the root and half states
        if (running && (q.state == ROOT_READ || q.state == ROOT_WRITE || q.state == HALF_READ || q.state == FLUSH_READ) &&
            (q.rd_active || q.ar_valid || q.wr_active || q.aw_valid || q.w_valid || q.w_pending))
            d.stat[S_STALL_MEMORY]++;

        // ================================================================ the decoder (NOTES section 5)
        {
            const bool en = running;
            const bool out_free = !q.out_valid || take_value;
            if (take_value) d.out_valid = false;
            // the bits a code needs, from the two at the buffer's bottom; 0: not known yet (fewer than its first bits)
            auto need_of = [&](const uint64_t* bb, unsigned nb) -> unsigned {
                if (!lru) return 64;
                if (nb < 1) return 0;
                if (bb[0] & 1) return 6;
                if (nb < 2) return 0;
                return bb[0] >> 1 & 1 ? 66 : 34;
            };
            const unsigned need = need_of(q.bb, q.nb);
            const bool dec = en && out_free && q.word_count != 0 && need != 0 && q.nb >= need;
            uint64_t bb[4] = {q.bb[0], q.bb[1], q.bb[2], q.bb[3]};
            unsigned nb = q.nb;
            if (dec) {
                uint64_t v;
                if (!lru) v = bb[0];
                else if (need == 6) {
                    const unsigned i = unsigned(bb[0] >> 1 & 31);
                    v = q.lru[i];
                    for (unsigned k = i; k > 0; k--) d.lru[k] = q.lru[k - 1];
                    d.lru[0] = v;
                } else {
                    v = need == 34 ? (bb[0] >> 2 & 0xFFFFFFFFu) : (bb[0] >> 2 | bb[1] << 62);
                    for (unsigned k = LRU - 1; k > 0; k--) d.lru[k] = q.lru[k - 1];
                    d.lru[0] = v;
                }
                shift_right(bb, need);
                nb -= need;
                d.word_count = q.word_count - 1;
                if (q.run_left == 0) {                                          // a header word: a run of values opens
                    d.run_addr = uint32_t(v); d.run_left = uint32_t(v >> 32);
                } else {                                                        // a value, with its type
                    const uint32_t n = q.run_left < q.word_count ? q.run_left : q.word_count;   // this run's values from here
                    d.out_valid = true; d.out_addr = q.run_addr; d.out_value = v;
                    d.out_type = (q.run_addr & 511) == 0 && n >= 512 ? T_PAGE : (q.run_addr & 255) == 0 && n >= 256 ? T_HALF : T_WORDS;
                    d.run_addr = q.run_addr + 1; d.run_left = q.run_left - 1;
                    shadow_next = (q.run_addr & 255) == 0 && q.run_left >= 256;
                }
            }
            // a refill, only when the next code's bits are not all there (NOTES C2)
            const uint32_t wc_after = dec ? q.word_count - 1 : q.word_count;
            const unsigned need2 = need_of(bb, nb);
            const bool refill = en && wc_after != 0 && q.fifo_n != 0 && (need2 == 0 || nb < need2);
            if (refill) {
                if (nb > 65) FATAL("the Updater's controller: a refill past the shifter's reach");
                const uint64_t* w = q.fifo[q.fifo_head];
                insert(bb, w, nb);                                              // the shifter: a word at 0 to 65
                nb += 128;
                words_in++;
            }
            std::memcpy(d.bb, bb, sizeof bb); d.nb = uint8_t(nb);
            // the FIFO
            const unsigned n_after = q.fifo_n + (push ? 1 : 0) - (refill ? 1 : 0);
            if (push) {
                const unsigned at = (q.fifo_head + q.fifo_n) % FIFO;
                d.fifo[at][0] = in.wp.data[0]; d.fifo[at][1] = in.wp.data[1];
            }
            if (refill) d.fifo_head = uint8_t((q.fifo_head + 1) % FIFO);
            d.fifo_n = uint8_t(n_after);
            d.ready = n_after < FIFO;
        }

        pm.edge(port);
        q = d;
        shadow_uncut = shadow_next;
    }

private:
    static void shift_right(uint64_t* b, unsigned n) {
        while (n >= 64) { b[0] = b[1]; b[1] = b[2]; b[2] = b[3]; b[3] = 0; n -= 64; }
        if (n == 0) return;
        for (int k = 0; k < 4; k++) b[k] = b[k] >> n | (k < 3 ? b[k + 1] << (64 - n) : 0);
    }
    static void insert(uint64_t* b, const uint64_t* w, unsigned at) {   // the bits above `at` are 0
        const unsigned k = at / 64, s = at % 64;
        b[k] |= w[0] << s;
        if (s) { b[k + 1] |= w[0] >> (64 - s) | w[1] << s; b[k + 2] |= w[1] >> (64 - s); }
        else b[k + 1] |= w[1];
    }
};

}  // namespace updctl
}  // namespace units
