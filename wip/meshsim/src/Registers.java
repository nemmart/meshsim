import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The one register table (spec 8, 4.10-4.12, 4.17-4.20) and the software conventions the C side shares with the
 * simulator. It generates sw/mesh.h ({@code java Registers sw/mesh.h}), so the C, assembly and Java views cannot disagree.
 *
 * Offsets (v1.1, spec 8.1 as renumbered in part E): 0xFFFFF800 + 4 i, 40 offsets 0x800-0x89C with every family
 * contiguous and no holes (TILE_, ROUTER_, DMA_, SRAM_, WAKE_, REQUEST_, SENDER_, PERIPH_STATUS_, UART_); 0x8A0 onward is
 * unmapped. The request port's four offsets are shared: a store reaches REQUEST_SET_W0-W2 / REQUEST_SEND, a load
 * REQUEST_GET_W0-W2 / REQUEST_RECEIVE (8.10), so {@link #storeAt} and {@link #loadAt} decode separately. Field macros
 * carry their word's prefix. {@link Fields} holds the Java encoders the engines use (CtrlTest checks they agree).
 */
final class Registers {
    private Registers() {}

    // ---------------------------------------------------------------- the controller's address map (spec 4.11)
    static final int IMEM_BASE = 0x0000, IMEM_SIZE = 0x2000;
    static final int LOADER_BASE = 0x1F00, LOADER_SIZE = 0x100;
    static final int TABLE_BASE = 0x2000, TABLE_END = 0x4000;
    // 8.9: STREAM is three arrays; GROUP at 0x3400; 0x3600-0x3FFF unmapped
    static final int HIST_BASE = 0x2000, PTR_BASE = 0x2400, STREAM_SRC_BASE = 0x2800, STREAM_DEST_BASE = 0x2C00, STREAM_COUNT_BASE = 0x3000,
            GROUP_BASE = 0x3400, TABLES_END = 0x3600;
    static final int DMEM_BASE = 0x4000, DMEM_SIZE = 0x2000;
    /** The command block (16 slots, 0x5FC0-0x5FFF) and the reply block below it (256 words, 0x5BC0-0x5FBF), 4.12. */
    static final int COMMAND_BASE = 0x5FC0, COMMAND_SLOTS = 16, REPLY_BASE = 0x5BC0, REPLY_WORDS = 256;
    static final int STACK_TOP = REPLY_BASE;
    static final int REG_BASE = 0xFFFFF800;

    // ---------------------------------------------------------------- registers (spec 8)
    static final int R = 1, W = 2, RW = 3;
    static final class Reg {
        final String name; final int index, access; final String doc;
        Reg(String name, int index, int access, String doc) { this.name = name; this.index = index; this.access = access; this.doc = doc; }
        int addr() { return REG_BASE + 4 * index; }
        boolean readable() { return (access & R) != 0; }
        boolean writable() { return (access & W) != 0; }
    }
    static final Reg TILE_STATUS        = new Reg("TILE_STATUS", 0, R, "live bits, peripheral type, endpoint id, big-SRAM size (8.2); read-only");
    static final Reg TILE_CONDITIONS    = new Reg("TILE_CONDITIONS", 1, R, "the error conditions (4.13, 8.3): bit n = condition n (TILE_COND_*); read-only, cleared only on entering HALT");
    static final Reg TILE_PULSE         = new Reg("TILE_PULSE", 2, W, "1 fires: [0] done [1] start [2] PERIPH_RUN [3] GO (wired only on C&C); start and run act before done (8.6)");
    static final Reg ROUTER_SET_HDR     = new Reg("ROUTER_SET_HDR", 3, W, "dst, mode, addr of outgoing packets; addr += 1, 2 or 4 (modes 3, 6, 9, by size) or 1 (mode 7) at each send");
    static final Reg ROUTER_SET_W0      = new Reg("ROUTER_SET_W0", 4, W, "payload bits 31-0 of the next packet; cleared to 0 by each send");
    static final Reg ROUTER_SET_W1      = new Reg("ROUTER_SET_W1", 5, W, "payload bits 63-32; cleared to 0 by each send");
    static final Reg ROUTER_SET_W2      = new Reg("ROUTER_SET_W2", 6, W, "payload bits 95-64; cleared to 0 by each send");
    static final Reg ROUTER_SEND_32     = new Reg("ROUTER_SEND_32", 7, W, "payload bits 31-0; sends a 32-bit packet (stalls while the holding register is occupied and not being injected)");
    static final Reg ROUTER_SEND_64     = new Reg("ROUTER_SEND_64", 8, W, "payload bits 63-32; sends a 64-bit packet with ROUTER_SET_W0 below");
    static final Reg ROUTER_SEND_128    = new Reg("ROUTER_SEND_128", 9, W, "payload bits 127-96; sends a 128-bit packet with ROUTER_SET_W0-W2 below");
    static final Reg DMA_CONFIG         = new Reg("DMA_CONFIG", 10, W, "the engine run, digits, k (8.5); write-only, reads 0");
    static final Reg DMA_RECORD_BASE_ADDRESS = new Reg("DMA_RECORD_BASE_ADDRESS", 11, W, "21-bit chunk address of the run's records: augment ordinal = (landing chunk - base) / 2 (4.9); gather address = base + 2k ordinal (4.4)");
    static final Reg DMA_SRAM_REMAINING = new Reg("DMA_SRAM_REMAINING", 12, RW, "down-counter, modes 2-5 (4.12); a write adds");
    static final Reg DMA_CM_REMAINING   = new Reg("DMA_CM_REMAINING", 13, RW, "down-counter, mode 6, read-port results included; a write adds");
    static final Reg DMA_REPLY_REMAINING = new Reg("DMA_REPLY_REMAINING", 14, RW, "down-counter, mode 8 (replies); a write adds");
    static final Reg DMA_PERIPH_REMAINING = new Reg("DMA_PERIPH_REMAINING", 15, RW, "down-counter, mode 9, on landing or drop (4.17.5); a write adds");
    static final Reg SRAM_RD_ADDR_QUEUE = new Reg("SRAM_RD_ADDR_QUEUE", 16, RW, "read port (4.3): a store queues a read of 1, 2 or 4 words at a 21-bit chunk address aligned to the size (never stalls); a load = free entries");
    static final Reg SRAM_RD_DEST_ADDR  = new Reg("SRAM_RD_DEST_ADDR", 17, W, "read port's setup: destination [13:0] (packed controller-memory word of the next result; + the size in words per queued read), size [15:14]");
    static final Reg WAKE_EVENTS        = new Reg("WAKE_EVENTS", 18, R, "pending wake events; a load consumes those under WAKE_MASK (4.12, 8.10)");
    static final Reg WAKE_PEEK          = new Reg("WAKE_PEEK", 19, R, "pending wake events, read with no side effects");
    static final Reg WAKE_MASK          = new Reg("WAKE_MASK", 20, RW, "the events that wake WFI on their own and that a load of WAKE_EVENTS consumes; [8] | [9] at reset and HALT -> RUN");
    static final Reg WAKE_REQUIRED      = new Reg("WAKE_REQUIRED", 21, RW, "the events that wake WFI only all together; 0 at reset and HALT -> RUN");
    static final Reg REQUEST_SET_W0     = new Reg("REQUEST_SET_W0", 22, W, "store: the request's bits 31-0 (requestor [6:0] written 0, target [9:7]); cleared to 0 by each send");
    static final Reg REQUEST_SET_W1     = new Reg("REQUEST_SET_W1", 23, W, "store: the request's bits 63-32; cleared to 0 by each send");
    static final Reg REQUEST_SET_W2     = new Reg("REQUEST_SET_W2", 24, W, "store: the request's bits 95-64; cleared to 0 by each send");
    static final Reg REQUEST_SEND       = new Reg("REQUEST_SEND", 25, W, "store: the request's bits 127-96; pushes the request (stalls only while the request FIFO is full)");
    static final Reg REQUEST_GET_W0     = new Reg("REQUEST_GET_W0", 22, R, "load: the oldest received request's bits 31-0 (requestor [6:0], target [9:7]); doesn't pop; 0 when empty");
    static final Reg REQUEST_GET_W1     = new Reg("REQUEST_GET_W1", 23, R, "load: its bits 63-32; doesn't pop");
    static final Reg REQUEST_GET_W2     = new Reg("REQUEST_GET_W2", 24, R, "load: its bits 95-64; doesn't pop");
    static final Reg REQUEST_RECEIVE    = new Reg("REQUEST_RECEIVE", 25, R, "load: its bits 127-96, and pops it; empty: 0 and receive underflow");
    static final Reg SENDER_SEND        = new Reg("SENDER_SEND", 26, W, "starts one send of peripheral output (4.17.9); ignored while sender_busy");
    static final Reg SENDER_ADDR        = new Reg("SENDER_ADDR", 27, W, "the first packet's addr for modes 3, 6 and 9; sticky, advancing per packet");
    static final Reg[] PERIPH_STATUS    = new Reg[8];
    static { for (int i = 0; i < 8; i++) PERIPH_STATUS[i] = new Reg("PERIPH_STATUS_" + i, 28 + i, R, i == 0 ? "the peripheral's status registers (4.17.4): read-only, no side effects, meanings per peripheral; 0 where undefined" : "the peripheral's status register " + i); }
    static final Reg UART_TX            = new Reg("UART_TX", 36, W, "pushes a byte into the TX FIFO, stalling while it is full; on an unwired tile dropped (meshsim prints it)");
    static final Reg UART_RX            = new Reg("UART_RX", 37, R, "the oldest received byte [7:0] and valid [8]; pops only when valid");
    static final Reg UART_STATUS        = new Reg("UART_STATUS", 38, RW, "TX occupancy [4:0], RX occupancy [12:8], RX overrun [16], framing error [17] (write 1 to clear)");
    static final Reg UART_DIVISOR       = new Reg("UART_DIVISOR", 39, RW, "reference-oscillator cycles per bit [15:0]; reset round(f_ref / 57,600)");
    static final Reg[] ALL;
    static final Reg[] STORE = new Reg[40], LOAD = new Reg[40];
    static {
        List<Reg> all = new ArrayList<>(List.of(TILE_STATUS, TILE_CONDITIONS, TILE_PULSE, ROUTER_SET_HDR, ROUTER_SET_W0, ROUTER_SET_W1, ROUTER_SET_W2,
                ROUTER_SEND_32, ROUTER_SEND_64, ROUTER_SEND_128, DMA_CONFIG, DMA_RECORD_BASE_ADDRESS, DMA_SRAM_REMAINING, DMA_CM_REMAINING,
                DMA_REPLY_REMAINING, DMA_PERIPH_REMAINING, SRAM_RD_ADDR_QUEUE, SRAM_RD_DEST_ADDR, WAKE_EVENTS, WAKE_PEEK, WAKE_MASK, WAKE_REQUIRED,
                REQUEST_SET_W0, REQUEST_SET_W1, REQUEST_SET_W2, REQUEST_SEND, REQUEST_GET_W0, REQUEST_GET_W1, REQUEST_GET_W2, REQUEST_RECEIVE,
                SENDER_SEND, SENDER_ADDR));
        all.addAll(List.of(PERIPH_STATUS));
        all.addAll(List.of(UART_TX, UART_RX, UART_STATUS, UART_DIVISOR));
        ALL = all.toArray(new Reg[0]);
        for (Reg r : ALL) {
            boolean shared = r.index >= 22 && r.index <= 25;
            if (!shared || r.writable()) STORE[r.index] = r;
            if (!shared || r.readable()) LOAD[r.index] = r;
        }
    }
    /** The send registers (8.4) and the size each sends. */
    static boolean isSend(Reg r) { return r == ROUTER_SEND_32 || r == ROUTER_SEND_64 || r == ROUTER_SEND_128; }
    static int sendSize(Reg r) { return r == ROUTER_SEND_32 ? LinkWord.SIZE_32 : r == ROUTER_SEND_64 ? LinkWord.SIZE_64 : LinkWord.SIZE_128; }
    static boolean isCounter(Reg r) { return r == DMA_SRAM_REMAINING || r == DMA_CM_REMAINING || r == DMA_REPLY_REMAINING || r == DMA_PERIPH_REMAINING; }
    /** A counter register's index into the director's counters (SRAM, CM, REPLY, PERIPH). */
    static int counterIndex(Reg r) { return r.index - DMA_SRAM_REMAINING.index; }
    static int periphStatusIndex(Reg r) { return r.index - PERIPH_STATUS[0].index; }
    /** Offsets 0x800 + 4 N_OFFSETS onward (0x8A0-0xFFC) are unmapped: a bus error. */
    static final int N_OFFSETS = 40, UNMAPPED_FROM = 0x800 + 4 * N_OFFSETS;
    static final int N_REGS = N_OFFSETS;

    /** A document of the programmer's set (REGISTERS.md, RECIPES.md): ../sw/NAME in the v1.0 layout (meshsim/ beside
     *  sw/), else sw/NAME (a meshsim tree on its own). */
    static java.nio.file.Path doc(String name) {
        java.nio.file.Path top = java.nio.file.Path.of("..", "sw", name);
        return java.nio.file.Files.exists(top) ? top : java.nio.file.Path.of("sw", name);
    }

    private static int offIndex(int a) { if ((a & 3) != 0 || a - REG_BASE < 0) return -1; int i = (a - REG_BASE) >> 2; return i < N_OFFSETS ? i : -1; }
    /** The register a load at byte address a reaches, or null (unmapped: a bus error). */
    static Reg loadAt(int a) { int i = offIndex(a); return i < 0 ? null : LOAD[i]; }
    /** The register a store at byte address a reaches, or null (unmapped: a bus error). */
    static Reg storeAt(int a) { int i = offIndex(a); return i < 0 ? null : STORE[i]; }
    /** The register at a (the store's view; the load's at the shared request offsets): for decode-independent uses. */
    static Reg at(int a) { Reg r = storeAt(a); return r != null ? r : loadAt(a); }
    static boolean inRegs(int a) { return Integer.compareUnsigned(a, REG_BASE) >= 0; }
    /** Every register by name. */
    static Reg byName(String n) { for (Reg r : ALL) if (r.name.equals(n)) return r; return null; }

    // ---------------------------------------------------------------- fields (bit positions)
    static final class Field {
        final String word, name; final int lo, width; final String doc;
        Field(String word, String name, int lo, int width, String doc) { this.word = word; this.name = name; this.lo = lo; this.width = width; this.doc = doc; }
        int mask() { return width == 32 ? -1 : (1 << width) - 1; }
        int place(int v) { return (v & mask()) << lo; }
        int get(int w) { return (w >>> lo) & mask(); }
    }
    static final List<Field> FIELDS = new ArrayList<>();
    private static Field f(String word, String name, int lo, int width, String doc) { Field x = new Field(word, name, lo, width, doc); FIELDS.add(x); return x; }

    // DMA_CONFIG (8.5)
    static final Field MC_KIND = f("DMA_CFG", "KIND", 0, 1, "run kind: 0 stream run, 1 group run");
    static final Field MC_START = f("DMA_CFG", "START", 1, 8, "first stream (0-255) or first GROUP entry (0-63)");
    static final Field MC_COUNT = f("DMA_CFG", "COUNT", 9, 9, "streams (1-256) or groups (1-64)");
    static final Field MC_BUCKET = f("DMA_CFG", "BUCKETDIG", 18, 3, "bucket digit (mode 4): 0 none, 1-4 key byte, 1 least significant");
    static final Field MC_HIST = f("DMA_CFG", "HISTDIG", 21, 3, "histogram digit (count-on-arrival, modes 2, 4, 5): 0 off, 1-4");
    static final Field MC_KM1 = f("DMA_CFG", "KM1", 24, 3, "gather value size k - 1, in records");
    // TILE_PULSE (8.6)
    static final Field P_DONE = f("TILE_PULSE", "DONE", 0, 1, "done (READY fires when a command has landed)");
    static final Field P_START = f("TILE_PULSE", "START", 1, 1, "start: load the run from DMA_CONFIG and begin it (the first request when its table entries are in)");
    static final Field P_PERIPH_RUN = f("TILE_PULSE", "PERIPH_RUN", 2, 1, "peripheral run (4.17.2): ignored while periph_running and on a type-0 tile");
    static final Field P_GO = f("TILE_PULSE", "GO", 3, 1, "GO (6.3): drives the GO net only where wired (C&C); elsewhere ignored, meshsim flags it");
    // TILE_STATUS (8.2)
    static final Field ST_RD_RUNNING = f("TILE_ST", "RD_RUNNING", 0, 1, "live: the address generator is running (from the start, lead-in included)");
    static final Field ST_RD_DONE = f("TILE_ST", "RD_DONE", 1, 1, "live: the last run has walked its last entry (cleared by the next start)");
    static final Field ST_GATHER_BUSY = f("TILE_ST", "GATHER_BUSY", 2, 1, "live: the GATHER engine has work");
    static final Field ST_PERIPH_RUNNING = f("TILE_ST", "PERIPH_RUNNING", 3, 1, "live: the peripheral is running (its ownership state, 4.17.2)");
    static final Field ST_SENDER_BUSY = f("TILE_ST", "SENDER_BUSY", 4, 1, "live: a send of peripheral output is in progress (4.17.9)");
    static final Field ST_SEND_BUSY = f("TILE_ST", "SEND_BUSY", 5, 1, "live: the send holding register is occupied");
    static final Field ST_TS_BUSY = f("TILE_ST", "TS_BUSY", 6, 1, "live: a table memory is in use by an engine");
    static final Field ST_MEM_IDLE = f("TILE_ST", "MEM_IDLE", 7, 1, "live: the big-SRAM write path is empty");
    static final Field ST_CM_IDLE = f("TILE_ST", "CM_IDLE", 8, 1, "live: the port-B pipeline is empty");
    static final Field ST_DONE = f("TILE_ST", "DONE", 9, 1, "live: done");
    static final Field ST_LANDED = f("TILE_ST", "LANDED", 10, 1, "live: command landed");
    static final Field ST_REQUEST_BUSY = f("TILE_ST", "REQUEST_BUSY", 11, 1, "live: the request FIFO is full (4.18)");
    static final Field ST_PERIPH_TYPE = f("TILE_ST", "PERIPH_TYPE", 12, 4, "peripheral type: 0 none, 1 the replayer (4.17.1)");
    static final Field ST_ID = f("TILE_ST", "ID", 20, 7, "endpoint id: the tile index on a mesh tile (2.1)");
    static final Field ST_SIZE = f("TILE_ST", "SIZE", 27, 5, "big-SRAM size in 256 KB units - 1 (a shift extracts it)");
    /** Chunks per 256 KB unit of TILE_ST_SIZE, log2 (8.2): sram_chunks() = (TILE_ST_SIZE + 1) << SIZE_UNIT_SHIFT. */
    static final int SIZE_UNIT_SHIFT = 16;
    // ROUTER_SET_HDR (8.4)
    static final Field H_DST = f("ROUTER_HDR", "DST", 0, 7, "destination: tile 0-63, 64 C&C, 126 broadcast");
    static final Field H_MODE = f("ROUTER_HDR", "MODE", 7, 4, "packet mode, 4 bits: 1-9 (0 and 10-15 are bad modes)");
    static final Field H_ADDR = f("ROUTER_HDR", "ADDR", 11, 21, "address field (21 bits)");
    // STREAM[i] (4.2, 8.9)
    static final Field SS_ADDR = f("STREAM_SRC", "ADDR", 0, 21, "source: element 0's chunk address");
    static final Field SD_WORD = f("STREAM_DEST", "WORD", 0, 21, "modes 3, 6 and 9: element 0's addr (mode 6: a packed word), + 1, 2 or 4 per element");
    static final Field SD_D = f("STREAM_DEST", "D", 21, 7, "destination: 0-63 a relative tile offset, dst = (me + d) mod 64; 64 C&C (mode 3 only)");
    static final Field SC_N = f("STREAM_COUNT", "N", 0, 18, "count: elements in the stream (0: sends nothing)");
    static final Field SC_MODE = f("STREAM_COUNT", "MODE", 18, 4, "packet mode: 1-6 or 9 (0, 7, 8 and 10-15 are errors; mode 5 not at 128 bits)");
    static final Field SC_SIZE = f("STREAM_COUNT", "SIZE", 22, 2, "read size: 0 32-bit, 1 64-bit, 2 128-bit (3 undefined)");
    static final Field SC_GAP = f("STREAM_COUNT", "GAP", 24, 3, "stream runs: idle cycles after the stream (group runs ignore it)");
    // GROUP[g] (4.2)
    static final Field GRL_R = f("GROUP_LO", "R", 0, 18, "rotations");
    static final Field GRH_START = f("GROUP_HI", "START", 0, 8, "first stream");
    static final Field GRH_TM1 = f("GROUP_HI", "TM1", 8, 4, "streams in the group - 1");
    static final Field GRH_GAP = f("GROUP_HI", "GAP", 12, 8, "gap after the group, cycles");
    // SRAM_RD_DEST_ADDR (8.8)
    static final Field RD_DEST = f("SRAM_RD_SETUP", "DEST", 0, 14, "packed controller-memory word of the next result (advances by the size in words)");
    static final Field RD_SIZE = f("SRAM_RD_SETUP", "SIZE", 14, 2, "read size: 0 32 bits, 1 64, 2 128 (3 reserved)");
    // SENDER_SEND, SENDER_ADDR (8.12)
    static final Field SN_DEST = f("SENDER", "DEST", 0, 7, "destination endpoint id");
    static final Field SN_MODE = f("SENDER", "MODE", 7, 4, "packet mode stamped on every packet");
    static final Field SN_QUEUE = f("SENDER", "QUEUE", 11, 2, "the peripheral's output queue (0-3)");
    static final Field SN_COUNT = f("SENDER", "COUNT", 13, 16, "32-bit words to send");
    static final Field SN_ADDR = new Field("SENDER_ADDR", "ADDR", 0, 21, "the first packet's addr for modes 3, 6 and 9");
    // the request (3.3): the hardware's fields
    static final Field RQ_REQUESTOR = f("REQ", "REQUESTOR", 0, 7, "the sender's endpoint id, stamped by the tree (software writes 0)");
    static final Field RQ_TARGET = f("REQ", "TARGET", 7, 3, "which receiver the root delivers the request to");
    // the UART (8.13)
    static final Field UR_BYTE = f("UART_RX", "BYTE", 0, 8, "the oldest received byte");
    static final Field UR_VALID = f("UART_RX", "VALID", 8, 1, "a byte was returned (and popped)");
    static final Field US_TX_OCC = f("UART_ST", "TX_OCC", 0, 5, "TX FIFO occupancy (0-16)");
    static final Field US_RX_OCC = f("UART_ST", "RX_OCC", 8, 5, "RX FIFO occupancy (0-16)");
    static final Field US_OVERRUN = f("UART_ST", "OVERRUN", 16, 1, "RX overrun, sticky (write 1 to clear)");
    static final Field US_FRAMING = f("UART_ST", "FRAMING", 17, 1, "framing error, sticky (write 1 to clear)");

    // ---------------------------------------------------------------- wake events (4.12, 8.10)
    static final int EV_SRAM_DONE = 0, EV_CM_DONE = 1, EV_REPLY_DONE = 2, EV_PERIPH_DONE = 3, EV_GO = 8, EV_COUNTERS_DONE = 9,
            EV_COMMAND = 10, EV_RUN_DONE = 11, EV_REQUEST_ROOM = 12, EV_PERIPH_1 = 13, EV_REQUEST_PENDING = 19, EV_SENDER_IDLE = 20,
            EV_UART_RX = 21, EV_UART_TX = 22;
    /** The bits WAKE_EVENTS, WAKE_MASK, WAKE_REQUIRED and WAKE_PEEK cover: [3:0] and [22:8]; [7:4] and [31:23] read 0. */
    static final int WAKE_VALID = 0xF | (0x7FFF << 8);
    /** The live events (4.12): the service lines' kinds are the peripheral's; [19], [21], [22] are always live. */
    static final int WAKE_LIVE = 1 << EV_REQUEST_PENDING | 1 << EV_UART_RX | 1 << EV_UART_TX;
    static final int WAKE_PERIPH_LINES = 0x3F << EV_PERIPH_1;
    static final int WAKE_MASK_RESET = 1 << EV_GO | 1 << EV_COUNTERS_DONE;
    static final String[] EV_NAME = new String[32];
    static {
        EV_NAME[0] = "SRAM_DONE"; EV_NAME[1] = "CM_DONE"; EV_NAME[2] = "REPLY_DONE"; EV_NAME[3] = "PERIPH_DONE"; EV_NAME[8] = "GO";
        EV_NAME[9] = "COUNTERS_DONE"; EV_NAME[10] = "COMMAND"; EV_NAME[11] = "RUN_DONE"; EV_NAME[12] = "REQUEST_ROOM";
        for (int n = 1; n <= 6; n++) EV_NAME[12 + n] = "PERIPH_" + n;
        EV_NAME[19] = "REQUEST_PENDING"; EV_NAME[20] = "SENDER_IDLE"; EV_NAME[21] = "UART_RX"; EV_NAME[22] = "UART_TX";
    }

    // ---------------------------------------------------------------- packet modes (3.1)
    static final String[] MODE_NAME = { "INVALID", "GATHER", "SRC", "EXPLICIT", "BUCKET", "AUGMENT", "CM", "COMMAND", "REPLY", "PERIPH" };
    static final String[] MODE_DOC = { null, "gather request (to the tile itself; never counted)", "big-SRAM write by source, at PTR[src], at the packet's size",
            "big-SRAM write at an explicit addr, aligned to its size", "bucket write at PTR[bucket digit], at the packet's size",
            "augmented write: a 32- or 64-bit packet's low word, the ordinal in bits 63-32 (a 64-bit record)",
            "controller-memory write, 1, 2 or 4 words by size", "command write: 32 bits to command slot addr[3:0] (never counted)",
            "reply: 32 or 64 bits, landing by source in the reply block", "peripheral write, into the peripheral's address space" };
    static final int DST_CNC = 64, DST_BROADCAST = 126;

    // ---------------------------------------------------------------- the minimal resident loader
    /** Command words in command slot 0 (6.3): [31:24] type, [23] argument block (parity), [22:0] argument. Commands
     *  alternate argument blocks: even commands slots 1-7, odd commands slots 8-15 (owner, 2026-10-26). */
    static final int CMD_PARITY_BIT = 23, ARGS_EVEN = 1, ARGS_ODD = 8, ARGS_PER_BLOCK = 7;
    static final int CMD_JUMP = 0x4A;           // the loader's only command: jump to the address in the block's first slot
    /** REPORT (6.6): C&C's debugging command; a tile answers with one packet to C&C: (tile index, TILE_CONDITIONS). */
    static final int CMD_REPORT = 0x52;
    /** STAGE (6.2, owner 2026-10-26): C&C announces a staging broadcast of the argument's packet count; the tile arms
     *  DMA_SRAM_REMAINING with it and waits for counters done, then waits for its next command. */
    static final int CMD_STAGE = 0x53;
    /** Big SRAM's top: the top 128 KB of A7 is sort scratch (spec 6.2), C&C's staging area is the 64 KB below
     *  it: PTR[64] = top - STAGE_CHUNKS (0xF4000 on the 4 MB device). Chunks are 32 bits. */
    static final int SCRATCH_CHUNKS = 0x8000, STAGE_SIZE_CHUNKS = 0x4000, STAGE_CHUNKS = SCRATCH_CHUNKS + STAGE_SIZE_CHUNKS;

    // ================================================================ mesh.h
    public static void main(String[] args) throws IOException {
        Path out = Path.of(args.length > 0 ? args[0] : "sw/mesh.h");
        String h = header();
        if (Files.exists(out) && Files.readString(out).equals(h)) { System.out.println(out + " unchanged"); return; }
        Files.writeString(out, h);
        System.out.println("wrote " + out);
    }

    static String hex(int v) { return String.format("0x%X", v); }

    static String header() {
        StringBuilder sb = new StringBuilder();
        sb.append("""
                /* mesh.h version 1.1 -- the tile controller's view of the mesh sorter (spec/machine-spec.md v1.1, 2026-10-26n:
                 * 3.1, 4.10-4.20, 6, 8). GENERATED by meshsim v1.1 (src/Registers.java); do not edit. The programmer's reference is
                 * sw/REGISTERS.md, the conventions sw/CONVENTIONS.md. C and assembly: assembly sees only the #defines (__ASSEMBLER__).
                 * 26n: modes renumbered (3 explicit write, 4 bucket, 5 augment, 6 controller memory, 7 command, 8 reply, 9 peripheral);
                 * the command block (16 slots) and the reply block (by source) replace the mailbox; counter writes add; the register
                 * map is renumbered (0x800-0x89C, no holes); the wake events; the request port, the peripheral slot, the sender and
                 * the UART; the send port's ROUTER_SET_W0-W2 below the sending word; 21-bit addresses. */
                #ifndef MESH_H
                #define MESH_H
                #define MESH_H_VERSION 0x0101   /* 1.1: major << 8 | minor */

                /* ---- address map (4.11) ---- */
                """);
        def(sb, "IMEM_BASE", IMEM_BASE, "instruction memory, 8 KB (fetch only; port B: mode 6)");
        def(sb, "IMEM_SIZE", IMEM_SIZE, null);
        def(sb, "LOADER_BASE", LOADER_BASE, "resident loader, 64 words; HALT -> RUN starts here");
        def(sb, "TABLE_BASE", TABLE_BASE, "table window (4.10): word accesses only");
        def(sb, "HIST_BASE", HIST_BASE, "HIST[256], 17 bits, read/write");
        def(sb, "PTR_BASE", PTR_BASE, "PTR[256], 21 bits, write-only (a load is a bus error)");
        def(sb, "STREAM_SRC_BASE", STREAM_SRC_BASE, "STREAM_SRC[256], one word each; write-only (a load is a bus error)");
        def(sb, "STREAM_DEST_BASE", STREAM_DEST_BASE, "STREAM_DEST[256], one word each; write-only");
        def(sb, "STREAM_COUNT_BASE", STREAM_COUNT_BASE, "STREAM_COUNT[256], one word each; write-only");
        def(sb, "GROUP_BASE", GROUP_BASE, "GROUP[64], two words each (low, high); write-only (a load is a bus error)");
        def(sb, "DMEM_BASE", DMEM_BASE, "data memory, 8 KB");
        def(sb, "DMEM_SIZE", DMEM_SIZE, null);
        def(sb, "COMMAND_BASE", COMMAND_BASE, "the command block: 16 slots at the top of data memory, written only by command writes (mode 7)");
        def(sb, "REPLY_BASE", REPLY_BASE, "the reply block: 256 words below it, two per source, written only by replies (mode 8)");
        def(sb, "STACK_TOP", STACK_TOP, "the stack grows down from below the reply block");
        sb.append("\n/* packed controller-memory word map (mode-6 addr, STREAM_DEST.WORD for mode 6, SRAM_RD_DEST): words 0x000-0x7FF instruction,\n * 0x800-0xFFF data; the reply block 0xEF0-0xFEF and the command block 0xFF0-0xFFF are not for mode 6 */\n");
        sb.append("#define CM_WORD(byte_addr) (((byte_addr) < DMEM_BASE) ? ((byte_addr) >> 2) : (0x800 + (((byte_addr) - DMEM_BASE) >> 2)))\n");

        sb.append("\n/* ---- registers (8.1): each one lw/sw with rs1 = x0; 0x800-0x89C, no holes; offsets 0x8A0-0xFFC are bus errors.\n * The request port's four offsets are shared: a store reaches REQUEST_SET_W0-W2 or REQUEST_SEND, a load REQUEST_GET_W0-W2 or REQUEST_RECEIVE ---- */\n");
        for (Reg r : ALL) sb.append(String.format("#define %-28s %s   /* %s: %s */%n", r.name + "_ADDR", hex(r.addr()), r.access == R ? "R" : r.access == W ? "W" : "RW", r.doc));

        sb.append("\n/* ---- fields: NAME_SHIFT, NAME_MASK (unshifted), NAME(v) placed ---- */\n");
        String prefix = "";
        for (Field x : FIELDS) {
            String n = x.word + "_" + x.name;
            if (!x.word.equals(prefix)) { sb.append("\n"); prefix = x.word; }
            sb.append(String.format("#define %-24s %d%n", n + "_SHIFT", x.lo));
            sb.append(String.format("#define %-24s %s   /* %s */%n", n + "_MASK", hex(x.mask()), x.doc));
            sb.append(String.format("#define %-24s (((unsigned)(v) & %s) << %d)%n", n + "(v)", hex(x.mask()), x.lo));
        }
        sb.append(String.format("%n#define %-20s %d   /* %s */%n", "SIZE_UNIT_SHIFT", SIZE_UNIT_SHIFT, "chunks per 256 KB unit of TILE_ST_SIZE, log2 (8.2): sram_chunks() = (TILE_ST_SIZE + 1) << SIZE_UNIT_SHIFT"));
        sb.append("\n/* TILE_CONDITIONS (4.13, 8.3): bit n = condition n, grouped by unit. A correct program never sets one; read-only, cleared\n * only on entering HALT; any non-zero condition raises the tile's error line to C&C (6.6) */\n");
        for (int i = 0; i < Conditions.N; i++) def(sb, "TILE_COND_" + Conditions.CNAME[i], 1 << i, Conditions.NAME[i]);
        def(sb, "TILE_COND_MASK", Conditions.MASK, "every condition ([31:15] reserved, read 0)");

        sb.append("\n/* WAKE_EVENTS, WAKE_PEEK, WAKE_MASK, WAKE_REQUIRED (4.12, 8.10): the event bits; [7:4] and [31:23] reserved */\n");
        def(sb, "WAKE_SRAM_DONE", 1 << EV_SRAM_DONE, "DMA_SRAM_REMAINING done (sticky)");
        def(sb, "WAKE_CM_DONE", 1 << EV_CM_DONE, "DMA_CM_REMAINING done (sticky)");
        def(sb, "WAKE_REPLY_DONE", 1 << EV_REPLY_DONE, "DMA_REPLY_REMAINING done (sticky)");
        def(sb, "WAKE_PERIPH_DONE", 1 << EV_PERIPH_DONE, "DMA_PERIPH_REMAINING done (sticky)");
        def(sb, "WAKE_GO", 1 << EV_GO, "GO (sticky)");
        def(sb, "WAKE_COUNTERS_DONE", 1 << EV_COUNTERS_DONE, "every counter done (sticky; the v1.0 local wake)");
        def(sb, "WAKE_COMMAND", 1 << EV_COMMAND, "command landed (sticky)");
        def(sb, "WAKE_RUN_DONE", 1 << EV_RUN_DONE, "engine run done: rd_done rose (sticky)");
        def(sb, "WAKE_REQUEST_ROOM", 1 << EV_REQUEST_ROOM, "request FIFO no longer full (sticky)");
        sb.append("#define WAKE_PERIPH(n)       (1u << (12 + (n)))   /* PERIPH_SERVICE_n, n = 1-6: the peripheral's service lines [18:13] */\n");
        def(sb, "WAKE_PERIPH_LINES", WAKE_PERIPH_LINES, "all six service lines");
        def(sb, "WAKE_REQUEST_PENDING", 1 << EV_REQUEST_PENDING, "a received request is waiting (live)");
        def(sb, "WAKE_SENDER_IDLE", 1 << EV_SENDER_IDLE, "sender idle: a send has finished (sticky)");
        def(sb, "WAKE_UART_RX", 1 << EV_UART_RX, "UART RX not empty (live)");
        def(sb, "WAKE_UART_TX", 1 << EV_UART_TX, "UART TX not full (live)");
        def(sb, "WAKE_MASK_RESET", WAKE_MASK_RESET, "WAKE_MASK at chip reset and HALT -> RUN: GO and counters done");

        sb.append("\n/* packet modes (3.1): groups big SRAM 2-5, controller memory 6-8, the peripheral 9; 0 and 10-15 are bad modes, and so are\n * mode 5 at 128 bits, a misaligned mode 3, mode 7 above 32 bits and mode 8 at 128 bits */\n");
        for (int m = 1; m < MODE_NAME.length; m++) def(sb, "MODE_" + MODE_NAME[m], m, MODE_DOC[m]);
        def(sb, "DST_CNC", DST_CNC, null);
        def(sb, "DST_BROADCAST", DST_BROADCAST, null);
        sb.append("\n/* read sizes (3.1, 4.2): an element is 1 << size chunks; size 3 is undefined. And run kinds */\n");
        def(sb, "SIZE_32", LinkWord.SIZE_32, null); def(sb, "SIZE_64", LinkWord.SIZE_64, null); def(sb, "SIZE_128", LinkWord.SIZE_128, null);
        def(sb, "SRAM_RD_SIZE_32", LinkWord.SIZE_32, "the read port's sizes (SRAM_RD_DEST)");
        def(sb, "SRAM_RD_SIZE_64", LinkWord.SIZE_64, null); def(sb, "SRAM_RD_SIZE_128", LinkWord.SIZE_128, null);
        def(sb, "RUN_STREAMS", 0, null); def(sb, "RUN_GROUPS", 1, null);

        sb.append("\n/* ---- software conventions (sw/CONVENTIONS.md) ---- */\n");
        sb.append("""
                /* Commands (6.3): C&C writes the arguments, then the command word to slot 0, last, all as 32-bit command writes.
                 * Commands alternate argument blocks: even commands use slots 1-7, odd commands slots 8-15, and the command word
                 * carries the parity, so the next command never overwrites the block a woken tile is still reading.
                 * Command word: [31:24] type, [23] parity, [22:0] argument. */
                """);
        def(sb, "CMD_PARITY_SHIFT", CMD_PARITY_BIT, null);
        def(sb, "ARGS_EVEN", ARGS_EVEN, "first argument slot of an even command");
        def(sb, "ARGS_ODD", ARGS_ODD, "first argument slot of an odd command");
        def(sb, "ARGS_PER_BLOCK", ARGS_PER_BLOCK, "arguments per block");
        def(sb, "CMD_JUMP", CMD_JUMP, "the loader's command: jump to the address in the block's first slot");
        def(sb, "CMD_REPORT", CMD_REPORT, "REPORT (6.6): reply to C&C with (tile index, TILE_CONDITIONS) after a delay loop of about (me + 1) * 4 cycles");
        def(sb, "CMD_STAGE", CMD_STAGE, "STAGE (6.2): arm DMA_SRAM_REMAINING with the argument (C&C's staging packets), wait for them, then the next command");
        def(sb, "SCRATCH_CHUNKS", SCRATCH_CHUNKS, "the top 128 KB of big SRAM (A7's top on 4 MB) is sort scratch (6.2)");
        def(sb, "STAGE_SIZE_CHUNKS", STAGE_SIZE_CHUNKS, "C&C's staging area: 64 KB ...");
        def(sb, "STAGE_CHUNKS", STAGE_CHUNKS, "... starting STAGE_CHUNKS below the top: the loader sets PTR[64] = top - STAGE_CHUNKS (6.2)");
        sb.append("#define CMD_TYPE(w)   ((unsigned)(w) >> 24)\n#define CMD_PARITY(w) (((unsigned)(w) >> CMD_PARITY_SHIFT) & 1)\n#define CMD_ARG(w)    ((unsigned)(w) & 0x7FFFFF)\n");
        sb.append("#define REPLY_WORD(src) (REPLY_BASE + 8 * (src))   /* byte address of src's reply pair (word 0; word 1 at + 4) */\n");
        sb.append("#define SRAM_RD_DEST(dst, size) (SRAM_RD_SETUP_DEST(dst) | SRAM_RD_SETUP_SIZE(size))   /* the read port's setup word (8.8) */\n");

        sb.append("""

                #ifndef __ASSEMBLER__
                #include <stdint.h>
                #define MESH_REG(a) (*(volatile uint32_t *)(a))
                """);
        for (Reg r : ALL) if (r.index < 28 || r.index >= 36) sb.append(String.format("#define %-24s MESH_REG(%s_ADDR)%n", r.name, r.name));
        sb.append("#define PERIPH_STATUS(n)        MESH_REG(PERIPH_STATUS_0_ADDR + 4 * (n))   /* n = 0-7 */\n");
        sb.append("""
                #define READ_PORT_DEPTH     64   /* guaranteed minimum (4.3) */

                #define HIST    ((volatile uint32_t *)HIST_BASE)     /* read/write */
                #define PTR     ((volatile uint32_t *)PTR_BASE)      /* write-only: a load is a bus error */
                #define STREAM_SRC   ((volatile uint32_t *)STREAM_SRC_BASE)     /* write-only; STREAM_SRC[i]: the source (4.2) */
                #define STREAM_DEST  ((volatile uint32_t *)STREAM_DEST_BASE)    /* write-only; STREAM_DEST[i]: destination word and d */
                #define STREAM_COUNT ((volatile uint32_t *)STREAM_COUNT_BASE)   /* write-only; STREAM_COUNT[i]: count, mode, size, gap */
                #define GROUP   ((volatile uint32_t *)GROUP_BASE)    /* write-only; GROUP[2g] low, GROUP[2g+1] high */
                #define COMMAND ((volatile uint32_t *)COMMAND_BASE)  /* the command block: COMMAND[0] the command word, [1-15] arguments */
                #define REPLY   ((volatile uint32_t *)REPLY_BASE)    /* the reply block: REPLY[2 src], REPLY[2 src + 1] */
                /* the command's argument block: CMD_ARGS(w)[0] is its first argument */
                #define CMD_ARGS(w) (COMMAND + (CMD_PARITY(w) ? ARGS_ODD : ARGS_EVEN))

                /* table entries and the run */
                #define STREAM_DEST_ENTRY(word, d) (STREAM_DEST_WORD(word) | STREAM_DEST_D(d))
                #define STREAM_COUNT_ENTRY(count, mode, size, gap) (STREAM_COUNT_N(count) | STREAM_COUNT_MODE(mode) | STREAM_COUNT_SIZE(size) | STREAM_COUNT_GAP(gap))
                #define GROUP_HI(start, t, gap) (GROUP_HI_START(start) | GROUP_HI_TM1((t) - 1) | GROUP_HI_GAP(gap))
                #define DMA_CFG_RUN(kind, start, count) (DMA_CFG_KIND(kind) | DMA_CFG_START(start) | DMA_CFG_COUNT(count))
                #define ROUTER_HDR(dst, mode, addr) (ROUTER_HDR_DST(dst) | ROUTER_HDR_MODE(mode) | ROUTER_HDR_ADDR(addr))
                #define SENDER_SEND_WORD(dest, mode, queue, count) (SENDER_DEST(dest) | SENDER_MODE(mode) | SENDER_QUEUE(queue) | SENDER_COUNT(count))
                /* STREAM[i] (4.2, 8.9): one store per array; a new round usually rewrites only STREAM_SRC and STREAM_COUNT */
                static inline void set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t count) { STREAM_SRC[i] = src; STREAM_DEST[i] = dest; STREAM_COUNT[i] = count; }
                static inline void set_group(unsigned g, uint32_t lo, uint32_t hi) { GROUP[2 * g] = lo; GROUP[2 * g + 1] = hi; }

                /* status */
                static inline uint32_t status(void) { return TILE_STATUS; }
                /* the error conditions (4.13): 0 in a correct program; any bit is an error (TILE_COND_*) */
                static inline uint32_t conditions(void) { return TILE_CONDITIONS; }
                /* the tile's index: its endpoint id, 0-63 on a mesh tile (8.2) */
                static inline unsigned tile_index(void) { return (TILE_STATUS >> TILE_ST_ID_SHIFT) & TILE_ST_ID_MASK; }
                static inline uint32_t sram_chunks(void) { return ((TILE_STATUS >> TILE_ST_SIZE_SHIFT) + 1) << SIZE_UNIT_SHIFT; }
                /* C&C's staging area and the sort scratch, from the device's size (6.2) */
                static inline uint32_t stage_base(void) { return sram_chunks() - STAGE_CHUNKS; }
                static inline uint32_t scratch_base(void) { return sram_chunks() - SCRATCH_CHUNKS; }

                /* Touch STREAM, GROUP, PTR or HIST only when no engine uses them: the address generator not running (its lead-in
                 * included) and ts_busy = 0 (4.10). The counters alone don't say so: arrivals can finish before this tile's own run. */
                static inline void wait_tables(void) {
                    while (status() & (TILE_ST_RD_RUNNING(1) | TILE_ST_TS_BUSY(1))) ;
                }

                /* The wait helpers (4.12, 6.3). A wake consumes nothing, so every WFI is followed by a load of WAKE_EVENTS,
                 * which consumes the events it returns that are in WAKE_MASK. wfi() returns that load: at the reset mask it
                 * consumes counters done (and a GO). wake_peek() looks without consuming anything. */
                static inline uint32_t wfi(void) { __asm__ volatile ("wfi" ::: "memory"); return WAKE_EVENTS; }
                static inline uint32_t wake_peek(void) { return WAKE_PEEK; }
                /* wait_for_command (6.3): pulse done, then WFI immediately -- two back-to-back instructions; on waking (by GO
                 * alone: the done gate), load WAKE_EVENTS to consume GO (keep WAKE_GO in WAKE_MASK), then read the command word
                 * in command slot 0 and return it. Every wake from a command goes through here, so no command word is ever
                 * overwritten unread. */
                static inline uint32_t wait_for_command(void) {
                    __asm__ volatile ("sw %0, %1(zero)\\n\\twfi" :: "r"(1u << TILE_PULSE_DONE_SHIFT), "i"((int)TILE_PULSE_ADDR) : "memory");
                    (void)WAKE_EVENTS;
                    return COMMAND[0];
                }
                /* start the run in DMA_CONFIG (8.6): loads the run and begins it; its first request goes out when its table
                 * entries are in (4 ts_engine_latency + 3 cycles after the store for a group run, 2 ts_engine_latency + 2 for a
                 * stream run). The store a synchronised start must reach with identical code on every tile (6.3). */
                static inline void start_run(void) { TILE_PULSE = TILE_PULSE_START(1); }

                /* the send port (4.11, 8.4): one ROUTER_SET_HDR per run of consecutive words, then one store per 32 bits of each
                 * packet in ascending word order, the last store sending; words beyond the size go out as 0, and W0-W2 clear at
                 * every send. Arguments are given top word first. */
                static inline void send32(uint32_t w0) { ROUTER_SEND_32 = w0; }
                static inline void send64(uint32_t w1, uint32_t w0) { ROUTER_SET_W0 = w0; ROUTER_SEND_64 = w1; }
                static inline void send128(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0) { ROUTER_SET_W0 = w0; ROUTER_SET_W1 = w1; ROUTER_SET_W2 = w2; ROUTER_SEND_128 = w3; }
                /* the request port (4.18, 8.10): a 128-bit request, words stored in ascending order, the last store sending */
                static inline void request_send(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0) { REQUEST_SET_W0 = w0; REQUEST_SET_W1 = w1; REQUEST_SET_W2 = w2; REQUEST_SEND = w3; }
                /* the UART (4.20, 8.13): a byte to the console (meshsim prints every tile's TX, wired or not) */
                static inline void uart_putc(char c) { UART_TX = (unsigned char)c; }
                static inline void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
                #endif /* __ASSEMBLER__ */
                #endif /* MESH_H */
                """);
        return sb.toString();
    }
    private static void def(StringBuilder sb, String name, int v, String doc) {
        sb.append(String.format("#define %-20s %s%s%n", name, v >= 0 && v < 10 ? Integer.toString(v) : hex(v), doc == null ? "" : "   /* " + doc + " */"));
    }
}
