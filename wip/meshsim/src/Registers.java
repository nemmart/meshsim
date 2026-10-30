import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The one register table (spec v1.2: 2.2, 4.11-4.13, 8) and the software constants the C side shares with the simulator.
 * It generates sw/mesh.h ({@code java Registers sw/mesh.h}) to match the draft header (wip/sw/mesh-v1.2-draft.h): the
 * register block and the windows as C structs reached through pointers, the field macros, the conventions and the
 * helpers. Differences from the draft are listed in WORKER4/REPORT.md.
 *
 * The address map (2.2, 4.11, 8.1): data memory 0x00000000 (32 KB), program memory 0x01000000 (32 KB), the boot ROM
 * 0x02000000, big SRAM region 0x03 (packets only), the peripherals' regions 0x04-0x06 (packets only), and region 0xFF:
 * the table window 0xFFFF2000-0xFFFF35FF, the controller's register block 0xFFFFF800-0xFFFFF9FF (128 words), and
 * peripheral n's window at 0xFFFFF800 + 0x200 n (n = 1-3). The request port's four offsets are shared: a store reaches
 * REQUEST_SET_W0-W2 / REQUEST_SEND, a load REQUEST_GET_W0-W2 / REQUEST_RECEIVE (8.10), so {@link #storeAt} and
 * {@link #loadAt} decode separately. {@link Fields} holds the Java encoders the engines use (they must agree: RegDocTest).
 */
final class Registers {
    private Registers() {}

    // ---------------------------------------------------------------- the address map (2.2, 4.11)
    static final int DMEM_BASE = 0x00000000, DMEM_SIZE = 0x8000, PMEM_BASE = 0x01000000, PMEM_SIZE = 0x8000, BOOTROM_BASE = 0x02000000;
    static final int REGION_SRAM = 0x03000000;
    static int regionPeriph(int n) { return 0x03000000 + (n << 24); }
    /** Data memory's layout (4.12, D37.4): the reserved first 16 bytes, the command block (16 slots) and the reply block
     *  (two words per source) at the top, the stack below them. */
    static final int DMEM_FIRST = 0x10, COMMAND_BASE = 0x7FC0, COMMAND_SLOTS = 16, REPLY_BASE = 0x7BC0, REPLY_WORDS = 256;
    static final int STACK_TOP = REPLY_BASE;
    /** PHASE-2 INTERIM (until phase 4's boot ROM): v1.1's resident loader, at the top 256 bytes of program memory; HALT ->
     *  RUN starts the core there. */
    static final int LOADER_BASE = PMEM_BASE + 0x7F00, LOADER_SIZE = 0x100;
    /** The table window (8.9; Proposed, item 120). */
    static final int TABLE_BASE = 0xFFFF2000, HIST_BASE = 0xFFFF2000, PTR_BASE = 0xFFFF2400, STREAM_SRC_BASE = 0xFFFF2800,
            STREAM_DEST_BASE = 0xFFFF2C00, STREAM_COUNT_BASE = 0xFFFF3000, GROUP_BASE = 0xFFFF3400, TABLE_END = 0xFFFF3600;
    /** The controller's block (128 words) and the three peripheral windows (128 words each). */
    static final int REG_BASE = 0xFFFFF800, BLOCK_WORDS = 128, WINDOW_WORDS = 128;
    static int windowBase(int n) { return REG_BASE + 0x200 * n; }
    static boolean inBlock(int a) { return Integer.compareUnsigned(a, REG_BASE) >= 0 && Integer.compareUnsigned(a, REG_BASE + 4 * BLOCK_WORDS) < 0; }
    static boolean inWindows(int a) { return Integer.compareUnsigned(a, windowBase(1)) >= 0; }
    /** The peripheral (1-3) whose window holds a, or 0. */
    static int windowOf(int a) { return inWindows(a) ? (a - REG_BASE) >>> 9 : 0; }
    static boolean inTables(int a) { return Integer.compareUnsigned(a, TABLE_BASE) >= 0 && Integer.compareUnsigned(a, TABLE_END) < 0; }
    static boolean inRegs(int a) { return Integer.compareUnsigned(a, REG_BASE) >= 0; }

    // ---------------------------------------------------------------- the controller's block (8.1)
    static final int R = 1, W = 2, RW = 3;
    static final class Reg {
        final String name; final int index, access; final String doc;
        Reg(String name, int index, int access, String doc) { this.name = name; this.index = index; this.access = access; this.doc = doc; }
        int addr() { return REG_BASE + 4 * index; }
        int off() { return 0x800 + 4 * index; }
        boolean readable() { return (access & R) != 0; }
        boolean writable() { return (access & W) != 0; }
    }
    private static final List<Reg> regs = new ArrayList<>();
    private static Reg reg(String name, int index, int access, String doc) { Reg r = new Reg(name, index, access, doc); regs.add(r); return r; }
    static final Reg PARAMS         = reg("PARAMS", 0, R, "constant: peripherals present [2:0], endpoint id [26:20], big-SRAM size [31:27] (8.2)");
    static final Reg STATUS         = reg("STATUS", 1, R, "live bits, diagnostic (8.2)");
    static final Reg ERRORS         = reg("ERRORS", 2, R, "the errors (4.13), sticky until the reset; [13:11] live (8.3)");
    static final Reg PULSE_DONE     = reg("PULSE_DONE", 3, W, "store 1: the done pulse (6.3); READY fires when a command has landed (8.6)");
    static final Reg PULSE_DMA      = reg("PULSE_DMA", 4, W, "store 1: start the run in DMA_CONFIG, clearing EVENTS [10]; ignored while a run is running (8.6)");
    static final Reg EVENTS         = reg("EVENTS", 6, R, "pending events; a load returns them all and consumes the sticky ones under EVENTS_MASK (4.12, 8.10)");
    static final Reg EVENTS_PEEK    = reg("EVENTS_PEEK", 7, R, "what a load of EVENTS would return; consumes nothing");
    static final Reg EVENTS_MASK    = reg("EVENTS_MASK", 8, RW, "the events that wake WFI on their own and that a load of EVENTS consumes; [8] | [7] at reset");
    static final Reg EVENTS_REQUIRED = reg("EVENTS_REQUIRED", 9, RW, "the events that wake WFI only all together; 0 at reset");
    static final Reg ROUTER_HDR     = reg("ROUTER_HDR", 10, RW, "no-increment [0], counter [21:19], mode [24:22], dst [31:25] of outgoing packets (8.4)");
    static final Reg ROUTER_ADDR    = reg("ROUTER_ADDR", 11, RW, "the next packet's 32-bit address; + 4, 8 or 16 at each mode-0 send unless no-increment (8.4)");
    static final Reg ROUTER_SET_W0  = reg("ROUTER_SET_W0", 12, W, "payload bits 31-0 of the next packet; cleared by each send");
    static final Reg ROUTER_SET_W1  = reg("ROUTER_SET_W1", 13, W, "payload bits 63-32; cleared by each send");
    static final Reg ROUTER_SET_W2  = reg("ROUTER_SET_W2", 14, W, "payload bits 95-64; cleared by each send");
    static final Reg ROUTER_SEND_32 = reg("ROUTER_SEND_32", 15, W, "payload bits 31-0; sends a 32-bit packet (stalls while the holding register is occupied and not being injected)");
    static final Reg ROUTER_SEND_64 = reg("ROUTER_SEND_64", 16, W, "payload bits 63-32; sends a 64-bit packet with ROUTER_SET_W0 below");
    static final Reg ROUTER_SEND_128 = reg("ROUTER_SEND_128", 17, W, "payload bits 127-96; sends a 128-bit packet with ROUTER_SET_W0-W2 below");
    static final Reg REQUEST_SET_W0 = reg("REQUEST_SET_W0", 18, W, "store: the request's bits 31-0 (requestor [6:0] written 0, target [9:7]); cleared by each send");
    static final Reg REQUEST_SET_W1 = reg("REQUEST_SET_W1", 19, W, "store: the request's bits 63-32; cleared by each send");
    static final Reg REQUEST_SET_W2 = reg("REQUEST_SET_W2", 20, W, "store: the request's bits 95-64; cleared by each send");
    static final Reg REQUEST_SEND   = reg("REQUEST_SEND", 21, W, "store: the request's bits 127-96; pushes the request (stalls only while the request FIFO is full)");
    static final Reg REQUEST_GET_W0 = reg("REQUEST_GET_W0", 18, R, "load: the oldest received request's bits 31-0 (requestor [6:0], target [9:7]); doesn't pop; 0 when empty");
    static final Reg REQUEST_GET_W1 = reg("REQUEST_GET_W1", 19, R, "load: its bits 63-32; doesn't pop");
    static final Reg REQUEST_GET_W2 = reg("REQUEST_GET_W2", 20, R, "load: its bits 95-64; doesn't pop");
    static final Reg REQUEST_RECEIVE = reg("REQUEST_RECEIVE", 21, R, "load: its bits 127-96, and pops it; empty: 0 and receive underflow (ERRORS [14])");
    static final Reg DMA_CONFIG     = reg("DMA_CONFIG", 22, RW, "the run, bucket and histogram digits, k (8.5); a load returns what was stored");
    static final Reg DMA_RECORD_BASE_ADDRESS = reg("DMA_RECORD_BASE_ADDRESS", 23, RW, "the run's record buffer, chunk address [20:0] (4.4, 4.9)");
    static final Reg[] WRITE_COUNTER = new Reg[8];               // [1]-[7]
    static { for (int n = 1; n <= 7; n++) WRITE_COUNTER[n] = reg("WRITE_COUNTER_" + n, 23 + n, RW, n == 1 ? "write counter n (4.12), count [19:0]; a store adds N, N > 0 arms it; a load returns the count" : "write counter " + n); }
    static final Reg SRAM_RD_ADDR_QUEUE = reg("SRAM_RD_ADDR_QUEUE", 31, RW, "store: queue a read at a chunk address [20:0], aligned to the size; load: free entries (4.3, 8.8)");
    static final Reg SRAM_RD_DEST_ADDR  = reg("SRAM_RD_DEST_ADDR", 32, RW, "the read port's setup: data-memory byte offset [16:0] (advancing), size [18:17], counter [21:19] (8.8)");
    static final Reg SIM_EXIT       = reg("SIM_EXIT", 126, W, "simulator only (8.15): a store ends a meshsim test with the stored value");
    static final Reg TRACE          = reg("TRACE", 127, W, "simulator only (8.15): a store is logged with the cycle and the unit");
    static final Reg[] ALL = regs.toArray(new Reg[0]);
    static final Reg[] STORE = new Reg[BLOCK_WORDS], LOAD = new Reg[BLOCK_WORDS];
    static {
        for (Reg r : ALL) {                                      // STORE: the writable register at each word; LOAD: the readable one
            if (r.writable()) STORE[r.index] = r;
            if (r.readable()) LOAD[r.index] = r;
        }
    }
    static boolean isSend(Reg r) { return r == ROUTER_SEND_32 || r == ROUTER_SEND_64 || r == ROUTER_SEND_128; }
    static int sendSize(Reg r) { return r == ROUTER_SEND_32 ? LinkWord.SIZE_32 : r == ROUTER_SEND_64 ? LinkWord.SIZE_64 : LinkWord.SIZE_128; }
    static boolean isCounter(Reg r) { return r != null && r.index >= 24 && r.index <= 30; }
    /** A counter register's counter, 1-7. */
    static int counterOf(Reg r) { return r.index - 23; }
    /** Block words that are reserved (8.1: read 0, stores ignored, meshsim flags the access): 5, 33-125. */
    static boolean reservedWord(int i) { return i == 5 || (i >= 33 && i <= 125); }

    private static int blockIndex(int a) { return (a & 3) != 0 || !inBlock(a) ? -1 : (a - REG_BASE) >> 2; }
    /** The register a load at a reaches (null: reserved, or not in the block). */
    static Reg loadAt(int a) { int i = blockIndex(a); return i < 0 ? null : LOAD[i]; }
    static Reg storeAt(int a) { int i = blockIndex(a); return i < 0 ? null : STORE[i]; }
    static Reg at(int a) { Reg r = storeAt(a); return r != null ? r : loadAt(a); }
    static Reg byName(String n) { for (Reg r : ALL) if (r.name.equals(n)) return r; return null; }

    // ---------------------------------------------------------------- the standard peripheral window (4.17.4, 8.11)
    static final String[] STD_WORD = { "PARAMS", "STATUS", "ERRORS", "PULSE_RUN", "PULSE_DMA", "EVENTS", "EVENTS_PEEK", "EVENTS_MASK", "DMA_HDR", "DMA_ADDR" };
    static final int[] STD_ACCESS = { R, R, R, W, W, R, R, RW, RW, RW };
    static final int STD_WORDS = 10, STD_RESERVED_END = 32;      // words 10-31 reserved standard words; 32-127 the peripheral's own

    /** A document of the programmer's set: ../sw/NAME (meshsim beside sw/), else sw/NAME. */
    static Path doc(String name) {
        Path top = Path.of("..", "sw", name);
        return Files.exists(top) ? top : Path.of("sw", name);
    }

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

    // PARAMS (8.2)
    static final Field PA_PERIPH = f("PARAMS", "PERIPH_PRESENT", 0, 3, "bit n - 1: peripheral n present");
    static final Field PA_ID = f("PARAMS", "ID", 20, 7, "endpoint id: 0-63 a mesh tile, 64-66 C&C 1-3");
    static final Field PA_SIZE = f("PARAMS", "SIZE", 27, 5, "big-SRAM size in 256 KB units - 1");
    static final int SIZE_UNIT_SHIFT = 16;                       // chunks per 256 KB unit, log2
    // STATUS (8.2)
    static final int ST_RD_RUNNING = 0, ST_RD_DONE = 1, ST_GATHER_BUSY = 2, ST_SEND_BUSY = 3, ST_DONE = 4, ST_LANDED = 5, ST_REQUEST_BUSY = 6;
    static final String[] ST_NAME = { "RD_RUNNING", "RD_DONE", "GATHER_BUSY", "SEND_BUSY", "DONE", "LANDED", "REQUEST_BUSY" };
    // the common header layout (8.1)
    static final Field HDR_SIZE = f("HDR", "SIZE", 17, 2, "size: 0 32 bits, 1 64, 2 128 (reserved where something else picks the size)");
    static final Field HDR_CTR = f("HDR", "CTR", 19, 3, "counter: 0 uncounted, 1-7 WRITE_COUNTER_1-7 at the receiver");
    static final Field HDR_MODE = f("HDR", "MODE", 22, 3, "mode (3.1): 0-5; 6-7 reserved");
    static final Field HDR_DST = f("HDR", "DST", 25, 7, "dst (STREAM_COUNT: the relative d)");
    // ROUTER_HDR (8.4)
    static final Field RH_NOINC = f("ROUTER_HDR", "NOINC", 0, 1, "no-increment: ROUTER_ADDR stays put after a send");
    // DMA_CONFIG (8.5)
    static final Field MC_KIND = f("DMA_CFG", "KIND", 0, 1, "run kind: 0 stream run, 1 group run");
    static final Field MC_START = f("DMA_CFG", "START", 1, 8, "first stream (0-255) or first GROUP entry (0-63)");
    static final Field MC_COUNT = f("DMA_CFG", "COUNT", 9, 9, "streams (1-256) or groups (1-64)");
    static final Field MC_BUCKET = f("DMA_CFG", "BUCKETDIG", 18, 3, "bucket digit (mode 2): 0 none, 1-4 key byte, 1 least significant");
    static final Field MC_HIST = f("DMA_CFG", "HISTDIG", 21, 3, "histogram digit (count-on-arrival, modes 1-3): 0 off, 1-4");
    static final Field MC_KM1 = f("DMA_CFG", "KM1", 24, 3, "gather value size k - 1, in records");
    // STREAM (4.2, 8.9)
    static final Field SS_CHUNK = f("STREAM_SRC", "CHUNK", 0, 21, "source: element 0's chunk address");
    static final Field SS_GAP = f("STREAM_SRC", "GAP", 21, 3, "stream runs: idle cycles after the stream (group runs ignore it)");
    static final Field SC_N = f("STREAM_COUNT", "N", 0, 17, "count: elements in the stream (0: sends nothing)");
    // GROUP (4.2)
    static final Field GRL_R = f("GROUP_LO", "R", 0, 18, "rotations");
    static final Field GRH_START = f("GROUP_HI", "START", 0, 8, "first stream");
    static final Field GRH_TM1 = f("GROUP_HI", "TM1", 8, 4, "streams in the group - 1");
    static final Field GRH_GAP = f("GROUP_HI", "GAP", 12, 8, "gap after the group, cycles");
    // SRAM_RD_DEST_ADDR (8.8)
    static final Field RD_DEST = f("RD", "DEST", 0, 17, "the next result's byte offset in data memory");
    // DMA_RECORD_BASE_ADDRESS, the counters
    static final Field RB_CHUNK = f("RECORD_BASE", "CHUNK", 0, 21, "the record buffer's chunk address");
    static final Field WC_COUNT = f("WRITE_COUNTER", "COUNT", 0, 20, "the count");
    // a peripheral's DMA_HDR (4.17.9)
    static final Field DH_COUNT = f("DMA_HDR", "COUNT", 0, 12, "32-bit words to send");
    static final Field DH_QUEUE = f("DMA_HDR", "QUEUE", 12, 2, "the peripheral's output queue (0-3)");
    // the request (3.3)
    static final Field RQ_REQUESTOR = f("REQ", "REQUESTOR", 0, 7, "the sender's endpoint id, stamped by the tree (software writes 0)");
    static final Field RQ_TARGET = f("REQ", "TARGET", 7, 3, "which receiver the root delivers the request to");

    // ---------------------------------------------------------------- events (4.12, 8.10)
    static final int EV_COUNTERS_DONE = 7, EV_GO = 8, EV_COMMAND = 9, EV_RUN_DONE = 10, EV_REQUEST_ROOM = 11, EV_REQUEST_PENDING = 12, EV_PERIPH_1 = 13;
    static int evCounter(int n) { return n - 1; }                 // write counter n done: bit n - 1
    static final int EVENTS_VALID = 0xFFFF;
    static final int EVENTS_LIVE = 1 << EV_REQUEST_PENDING | 7 << EV_PERIPH_1;
    static final int EVENTS_MASK_RESET = 1 << EV_GO | 1 << EV_COUNTERS_DONE;
    static final String[] EV_NAME = new String[16];
    static {
        for (int n = 1; n <= 7; n++) EV_NAME[n - 1] = "CTR" + n;
        EV_NAME[7] = "CTRS_DONE"; EV_NAME[8] = "GO"; EV_NAME[9] = "COMMAND"; EV_NAME[10] = "RUN_DONE"; EV_NAME[11] = "REQ_ROOM";
        EV_NAME[12] = "REQ_PENDING"; for (int n = 1; n <= 3; n++) EV_NAME[12 + n] = "PERIPH" + n;
    }

    // ---------------------------------------------------------------- software conventions (CONVENTIONS.md, the draft header)
    static final int CMD_PARITY_BIT = 23, ARGS_EVEN = 1, ARGS_ODD = 8, ARGS_PER_BLOCK = 7;
    static final int CMD_JUMP = 0x4A, CMD_STAGE = 0x53;
    static final int DST_CC1 = 64, DST_CC2 = 65, DST_CC3 = 66, DST_BROADCAST = 126;
    static final int READ_PORT_DEPTH = 64;

    // ================================================================ mesh.h
    public static void main(String[] args) throws IOException {
        Path out = Path.of(args.length > 0 ? args[0] : "sw/mesh.h");
        String h = MeshHeader.header();
        if (Files.exists(out) && Files.readString(out).equals(h)) { System.out.println(out + " unchanged"); return; }
        Files.writeString(out, h);
        System.out.println("wrote " + out);
    }
    static String hex(int v) { return String.format("0x%X", v); }
}
