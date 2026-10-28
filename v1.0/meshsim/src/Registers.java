import java.io.IOException;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The one register table (spec 8, 4.10-4.12) and the software conventions the C side shares with the simulator. It
 * generates sw/mesh.h ({@code java Registers sw/mesh.h}), so the C, assembly and Java views cannot disagree.
 *
 * Offsets: 0xFFFFF800 + 4 i in V1.0-CHANGES.md's order (TILE_, ROUTER_, DMA_, SRAM_). Field macros carry their register's
 * prefix: DMA_CFG_, DMA_PULSE_, TILE_ST_, TILE_COND_, ROUTER_HDR_. {@link Fields} holds the Java encoders the engines use.
 */
final class Registers {
    private Registers() {}

    // ---------------------------------------------------------------- the controller's address map (spec 4.11)
    static final int IMEM_BASE = 0x0000, IMEM_SIZE = 0x2000;
    static final int LOADER_BASE = 0x1F00, LOADER_SIZE = 0x100;
    static final int TABLE_BASE = 0x2000, TABLE_END = 0x4000;
    static final int HIST_BASE = 0x2000, PTR_BASE = 0x2400, STREAM_BASE = 0x2800, GROUP_BASE = 0x3000, TABLES_END = 0x3200;
    static final int DMEM_BASE = 0x4000, DMEM_SIZE = 0x2000;
    static final int MAILBOX_BASE = 0x5C00, MAILBOX_SLOTS = 256;
    static final int STACK_TOP = MAILBOX_BASE;
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
    // v1.0 (V1.0-CHANGES.md, 11-12 October): 13 registers prefixed by owner, ordered TILE_, ROUTER_, DMA_, SRAM_
    static final Reg TILE_STATUS        = new Reg("TILE_STATUS", 0, R, "live bits, tile index, big-SRAM size (8.3); read-only");
    static final Reg TILE_CONDITIONS    = new Reg("TILE_CONDITIONS", 1, R, "the error conditions (4.13, 8.3a): bit n = condition n (TILE_COND_*); read-only, cleared only on entering HALT");
    static final Reg ROUTER_SET_HDR     = new Reg("ROUTER_SET_HDR", 2, W, "dst, mode, addr of outgoing packets; addr += 2 (mode 5) or 1 (modes 6, 7) at each send");
    static final Reg ROUTER_SET_HI      = new Reg("ROUTER_SET_HI", 3, W, "high 32 bits of the next payload; cleared to 0 by each send");
    static final Reg ROUTER_SEND_LO     = new Reg("ROUTER_SEND_LO", 4, W, "low 32 bits; sends (stalls while the holding register is occupied and not being injected)");
    static final Reg DMA_CONFIG         = new Reg("DMA_CONFIG", 5, W, "the engine run, digits, k (8.1); write-only, reads 0");
    static final Reg DMA_PULSE          = new Reg("DMA_PULSE", 6, W, "1 fires: [0] done [1] start ([3:2] reserved, ignored); start acts before done (8.2)");
    static final Reg DMA_SRAM_REMAINING = new Reg("DMA_SRAM_REMAINING", 7, RW, "down-counter, modes 2, 3 and 4 (4.12); a store clears the local-wake latch");
    static final Reg DMA_CM_REMAINING   = new Reg("DMA_CM_REMAINING", 8, RW, "down-counter, mode 5; a store clears the local-wake latch");
    static final Reg DMA_REPLY_REMAINING = new Reg("DMA_REPLY_REMAINING", 9, RW, "down-counter, mode 7; a store clears the local-wake latch");
    static final Reg DMA_RECORD_BASE_ADDRESS = new Reg("DMA_RECORD_BASE_ADDRESS", 10, W, "20-bit chunk address of the run's records: augment ordinal = (landing chunk - base) / 2 (4.9); gather address = base + 2k ordinal (4.4)");
    static final Reg SRAM_RD_ADDR_QUEUE = new Reg("SRAM_RD_ADDR_QUEUE", 11, RW, "read port (4.3): write queues a 32-bit read of a 20-bit chunk address (never stalls); read = free entries");
    static final Reg SRAM_RD_DEST_ADDR  = new Reg("SRAM_RD_DEST_ADDR", 12, W, "read port: packed data-memory word for the next result; +1 per queued read");
    static final Reg[] ALL = { TILE_STATUS, TILE_CONDITIONS, ROUTER_SET_HDR, ROUTER_SET_HI, ROUTER_SEND_LO, DMA_CONFIG, DMA_PULSE,
            DMA_SRAM_REMAINING, DMA_CM_REMAINING, DMA_REPLY_REMAINING, DMA_RECORD_BASE_ADDRESS, SRAM_RD_ADDR_QUEUE, SRAM_RD_DEST_ADDR };
    static final int N_REGS = ALL.length;

    /** A document of the programmer's set (REGISTERS.md, RECIPES.md): ../sw/NAME in the v1.0 layout (meshsim/ beside
     *  sw/), else sw/NAME (a meshsim tree on its own). */
    static java.nio.file.Path doc(String name) {
        java.nio.file.Path top = java.nio.file.Path.of("..", "sw", name);
        return java.nio.file.Files.exists(top) ? top : java.nio.file.Path.of("sw", name);
    }

    /** The register at byte address a, or null (an unmapped register address is a bus error). */
    static Reg at(int a) {
        if ((a & 3) != 0) return null;
        int i = (a - REG_BASE) >> 2;
        return a - REG_BASE >= 0 && i < N_REGS ? ALL[i] : null;
    }
    static boolean inRegs(int a) { return Integer.compareUnsigned(a, REG_BASE) >= 0; }

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

    // DMA_CONFIG (8.1)
    static final Field MC_KIND = f("DMA_CFG", "KIND", 0, 1, "run kind: 0 stream run, 1 group run");
    static final Field MC_START = f("DMA_CFG", "START", 1, 8, "first stream (0-255) or first GROUP entry (0-63)");
    static final Field MC_COUNT = f("DMA_CFG", "COUNT", 9, 9, "streams (1-256) or groups (1-64)");
    static final Field MC_BUCKET = f("DMA_CFG", "BUCKETDIG", 18, 3, "bucket digit (mode 3): 0 none, 1-4 key byte, 1 least significant");
    static final Field MC_HIST = f("DMA_CFG", "HISTDIG", 21, 3, "histogram digit (count-on-arrival): 0 off, 1-4");
    static final Field MC_KM1 = f("DMA_CFG", "KM1", 24, 3, "gather value size k - 1, in records");   // The attach bit [24] left (mode 4)
    // DMA_PULSE (8.2; v1.0: arm merged into start)
    static final Field P_DONE = f("DMA_PULSE", "DONE", 0, 1, "done (READY fires when a command has landed)");
    static final Field P_START = f("DMA_PULSE", "START", 1, 1, "start: load the run from DMA_CONFIG and begin it (the first request when its table entries are in)");
    /** DMA_PULSE [3:2] are reserved (v1.0: [2] was start, [3] the clear pulse). The hardware ignores a 1 there; meshsim
     *  flags it. Not generated into mesh.h. */
    static final int PULSE_RESERVED_MASK = 0xC;
    // TILE_STATUS (8.3): live [9:0] ([2] spare: rd_ready until v1.0), [20:10] spare, tile [26:21], [28:27] spare, size [31:29]
    // (at the top: a shift with no mask extracts it).
    static final Field ST_RD_RUNNING = f("TILE_ST", "RD_RUNNING", 0, 1, "live: the address generator is running (from the start, lead-in included)");
    static final Field ST_RD_DONE = f("TILE_ST", "RD_DONE", 1, 1, "live: the last run has walked its last entry (cleared by the next start)");
    static final Field ST_SEND_BUSY = f("TILE_ST", "SEND_BUSY", 3, 1, "live: the send holding register is occupied");
    static final Field ST_TS_BUSY = f("TILE_ST", "TS_BUSY", 4, 1, "live: a table memory is in use by an engine");
    static final Field ST_MEM_IDLE = f("TILE_ST", "MEM_IDLE", 5, 1, "live: the big-SRAM write path is empty");
    static final Field ST_CM_IDLE = f("TILE_ST", "CM_IDLE", 6, 1, "live: the port-B pipeline is empty");
    static final Field ST_DONE = f("TILE_ST", "DONE", 7, 1, "live: done");
    static final Field ST_LANDED = f("TILE_ST", "LANDED", 8, 1, "live: command landed");
    static final Field ST_GATHER_BUSY = f("TILE_ST", "GATHER_BUSY", 9, 1, "live: the GATHER engine has work");
    static final Field ST_TILE = f("TILE_ST", "TILE", 21, 6, "tile index");
    static final Field ST_SIZE = f("TILE_ST", "SIZE", 29, 3, "big-SRAM size in 512 KB areas - 1");
    /** Chunks per big-SRAM area, log2 (4.1: 128 bits x 32K rows = 512 KB = 2^17 chunks): TILE_ST_SIZE's unit. */
    static final int AREA_CHUNKS_SHIFT = 17;
    // ROUTER_SET_HDR (8.4)
    static final Field H_DST = f("ROUTER_HDR", "DST", 0, 7, "destination: tile 0-63, 64 C&C, 126 broadcast");
    static final Field H_MODE = f("ROUTER_HDR", "MODE", 7, 4, "packet mode (4 bits)");
    static final Field H_ADDR = f("ROUTER_HDR", "ADDR", 11, 20, "address field");
    // STREAM[i] (4.2)
    static final Field STL_SRC = f("STREAM_LO", "SRC", 0, 20, "source chunk address");
    static final Field STL_CM = f("STREAM_LO", "CM", 20, 12, "controller-memory word, packed map (mode 5: destination)");
    static final Field STH_COUNT = f("STREAM_HI", "COUNT", 0, 18, "elements");
    static final Field STH_D = f("STREAM_HI", "D", 18, 6, "destination offset: dst = (me + d) mod 64");
    static final Field STH_MODE = f("STREAM_HI", "MODE", 24, 3, "packet mode, zero-extended to 4 bits: 1-5 (0, 6, 7 reserved)");
    static final Field STH_SIZE = f("STREAM_HI", "SIZE", 27, 2, "read size: 1 32-bit, 2 64-bit, 3 128-bit (0 reserved)");
    static final Field STH_GAP = f("STREAM_HI", "GAP", 29, 3, "gap after, cycles");
    // GROUP[g] (4.2)
    static final Field GRL_R = f("GROUP_LO", "R", 0, 18, "rotations");
    static final Field GRH_START = f("GROUP_HI", "START", 0, 8, "first stream");
    static final Field GRH_TM1 = f("GROUP_HI", "TM1", 8, 4, "streams in the group - 1");
    static final Field GRH_GAP = f("GROUP_HI", "GAP", 12, 8, "gap after the group, cycles");

    // ---------------------------------------------------------------- packet modes (3.1)
    static final String[] MODE_NAME = { "INVALID", "GATHER", "SRC", "BUCKET", "AUG", "CMEM", "MBOX", "REPLY" };
    static final String[] MODE_DOC = { null, "gather request (to the tile itself)", "write at PTR[src]", "bucket write at PTR[bucket digit]",
            "augmented write: as mode 2, the ordinal in bits 63-32", "controller-memory write, 2 words", "mailbox write", "reply: one data-memory word" };
    static final int DST_CNC = 64, DST_BROADCAST = 126;

    // ---------------------------------------------------------------- the minimal resident loader (M6 owns it)
    /** Command words in mailbox slot 0: [31:24] type, [23] argument block (parity), [22:0] argument (lead, 2 October:
     *  commands alternate argument blocks, even commands slots 1-31, odd commands slots 32-63). */
    static final int CMD_PARITY_BIT = 23, ARGS_EVEN = 1, ARGS_ODD = 32, ARGS_PER_BLOCK = 31;
    static final int CMD_JUMP = 0x4A;           // the loader's only command: jump to the address in the block's first slot
    /** REPORT (6.6): C&C's debugging command; a tile answers with one packet to C&C: (tile index, TILE_CONDITIONS). */
    static final int CMD_REPORT = 0x52;
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
        PrintWriter p = null;
        sb.append("""
                /* mesh.h version 1.0 -- the tile controller's view of the mesh sorter (spec/machine-spec.md v1.0: 4.10-4.12, 6, 8).
                 * GENERATED by meshsim v1.0 (src/Registers.java); do not edit. The programmer's reference is sw/REGISTERS.md,
                 * the conventions sw/CONVENTIONS.md. C and assembly: assembly sees only the #defines (__ASSEMBLER__). */
                #ifndef MESH_H
                #define MESH_H
                #define MESH_H_VERSION 0x0100   /* 1.0: major << 8 | minor */

                /* ---- address map (4.11) ---- */
                """);
        def(sb, "IMEM_BASE", IMEM_BASE, "instruction memory, 8 KB (fetch only; port B: mode 5)");
        def(sb, "IMEM_SIZE", IMEM_SIZE, null);
        def(sb, "LOADER_BASE", LOADER_BASE, "resident loader, 64 words; HALT -> RUN starts here");
        def(sb, "TABLE_BASE", TABLE_BASE, "table window (4.10): word accesses only");
        def(sb, "HIST_BASE", HIST_BASE, "HIST[256], 17 bits, read/write");
        def(sb, "PTR_BASE", PTR_BASE, "PTR[256], 20 bits, write-only (a load is a bus error)");
        def(sb, "STREAM_BASE", STREAM_BASE, "STREAM[256], two words each (low, high); write-only (a load is a bus error)");
        def(sb, "GROUP_BASE", GROUP_BASE, "GROUP[64], two words each (low, high); write-only (a load is a bus error)");
        def(sb, "DMEM_BASE", DMEM_BASE, "data memory, 8 KB");
        def(sb, "DMEM_SIZE", DMEM_SIZE, null);
        def(sb, "MAILBOX_BASE", MAILBOX_BASE, "256 x 32-bit slots, the top 1 KB of data memory (mode 6 only)");
        def(sb, "STACK_TOP", STACK_TOP, "the stack grows down from below the mailbox");
        sb.append("\n/* packed controller-memory word map (mode-5/7 addr, STREAM_LO.CM): words 0x000-0x7FF instruction, 0x800-0xFFF data */\n");
        sb.append("#define CM_WORD(byte_addr) (((byte_addr) < DMEM_BASE) ? ((byte_addr) >> 2) : (0x800 + (((byte_addr) - DMEM_BASE) >> 2)))\n");
        def(sb, "MBOX_WORD", 0x800 + ((MAILBOX_BASE - DMEM_BASE) >> 2), "packed word of mailbox slot 0");

        sb.append("\n/* ---- registers (8): each one lw/sw with rs1 = x0 ---- */\n");
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
        sb.append(String.format("%n#define %-20s %d   /* %s */%n", "AREA_CHUNKS_SHIFT", AREA_CHUNKS_SHIFT, "chunks per 512 KB big-SRAM area, log2 (4.1): TILE_ST_SIZE's unit, sram_chunks() = (TILE_ST_SIZE + 1) << AREA_CHUNKS_SHIFT"));
        sb.append("\n/* TILE_CONDITIONS (4.13, 8.3a): bit n = condition n. A correct program never sets one; read-only, cleared only on\n * entering HALT; any non-zero condition raises the tile's error line to C&C (6.6) */\n");
        for (int i = 0; i < Conditions.N; i++) def(sb, "TILE_COND_" + Conditions.CNAME[i], 1 << i, Conditions.NAME[i]);
        def(sb, "TILE_COND_MASK", Conditions.MASK, "every condition ([31:" + Conditions.N + "] reserved, read 0)");
        sb.append("\n/* packet modes (3.1) and destinations */\n");
        for (int m = 1; m < MODE_NAME.length; m++) def(sb, "MODE_" + MODE_NAME[m], m, MODE_DOC[m]);   /* 0 and 8-15 are bad modes (3.1) */
        def(sb, "DST_CNC", DST_CNC, null);
        def(sb, "DST_BROADCAST", DST_BROADCAST, null);
        sb.append("\n/* read sizes and run kinds */\n");
        def(sb, "SIZE_32", 1, null); def(sb, "SIZE_64", 2, null); def(sb, "SIZE_128", 3, null);
        def(sb, "RUN_STREAMS", 0, null); def(sb, "RUN_GROUPS", 1, null);

        sb.append("\n/* ---- software conventions (sw/CONVENTIONS.md) ---- */\n");
        sb.append("""
                /* Commands (6.3): C&C writes the arguments, then the command word to slot 0, last.
                 * Commands alternate argument blocks: even commands use slots 1-31, odd commands slots 32-63, and the
                 * command word carries the parity, so the next command never overwrites the block a woken tile is
                 * still reading. Command word: [31:24] type, [23] parity, [22:0] argument. */
                """);
        def(sb, "CMD_PARITY_SHIFT", CMD_PARITY_BIT, null);
        def(sb, "ARGS_EVEN", ARGS_EVEN, "first argument slot of an even command");
        def(sb, "ARGS_ODD", ARGS_ODD, "first argument slot of an odd command");
        def(sb, "CMD_JUMP", CMD_JUMP, "the loader's command: jump to the address in the block's first slot");
        def(sb, "CMD_REPORT", CMD_REPORT, "REPORT (6.6): reply to C&C with (tile index, TILE_CONDITIONS) after a delay loop of about (me + 1) * 4 cycles");
        def(sb, "SCRATCH_CHUNKS", SCRATCH_CHUNKS, "the top 128 KB of big SRAM (A7's top on 4 MB) is sort scratch (6.2)");
        def(sb, "STAGE_SIZE_CHUNKS", STAGE_SIZE_CHUNKS, "C&C's staging area: 64 KB ...");
        def(sb, "STAGE_CHUNKS", STAGE_CHUNKS, "... starting STAGE_CHUNKS below the top: the loader sets PTR[64] = top - STAGE_CHUNKS (6.2)");
        sb.append("#define CMD_TYPE(w)   ((unsigned)(w) >> 24)\n#define CMD_PARITY(w) (((unsigned)(w) >> CMD_PARITY_SHIFT) & 1)\n#define CMD_ARG(w)    ((unsigned)(w) & 0x7FFFFF)\n");

        sb.append("""

                #ifndef __ASSEMBLER__
                #include <stdint.h>
                #define MESH_REG(a) (*(volatile uint32_t *)(a))
                #define TILE_STATUS             MESH_REG(TILE_STATUS_ADDR)
                #define TILE_CONDITIONS         MESH_REG(TILE_CONDITIONS_ADDR)
                #define ROUTER_SET_HDR          MESH_REG(ROUTER_SET_HDR_ADDR)
                #define ROUTER_SET_HI           MESH_REG(ROUTER_SET_HI_ADDR)
                #define ROUTER_SEND_LO          MESH_REG(ROUTER_SEND_LO_ADDR)
                #define DMA_CONFIG              MESH_REG(DMA_CONFIG_ADDR)
                #define DMA_PULSE               MESH_REG(DMA_PULSE_ADDR)
                #define DMA_SRAM_REMAINING      MESH_REG(DMA_SRAM_REMAINING_ADDR)
                #define DMA_CM_REMAINING        MESH_REG(DMA_CM_REMAINING_ADDR)
                #define DMA_REPLY_REMAINING     MESH_REG(DMA_REPLY_REMAINING_ADDR)
                #define DMA_RECORD_BASE_ADDRESS MESH_REG(DMA_RECORD_BASE_ADDRESS_ADDR)
                #define SRAM_RD_ADDR_QUEUE      MESH_REG(SRAM_RD_ADDR_QUEUE_ADDR)
                #define SRAM_RD_DEST_ADDR       MESH_REG(SRAM_RD_DEST_ADDR_ADDR)
                #define READ_PORT_DEPTH     64   /* guaranteed minimum (4.3) */

                #define HIST    ((volatile uint32_t *)HIST_BASE)     /* read/write */
                #define PTR     ((volatile uint32_t *)PTR_BASE)      /* write-only: a load is a bus error */
                #define STREAM  ((volatile uint32_t *)STREAM_BASE)   /* write-only; STREAM[2i] low word, STREAM[2i+1] high word */
                #define GROUP   ((volatile uint32_t *)GROUP_BASE)    /* write-only; GROUP[2g] low, GROUP[2g+1] high */
                #define MBOX   ((volatile uint32_t *)MAILBOX_BASE)
                /* the command's argument block: CMD_ARGS(w)[0] is its first argument */
                #define CMD_ARGS(w) (MBOX + (CMD_PARITY(w) ? ARGS_ODD : ARGS_EVEN))

                /* table entries and the run */
                #define STREAM_LO(src, cm) (STREAM_LO_SRC(src) | STREAM_LO_CM(cm))
                #define STREAM_HI(count, d, mode, size, gap) (STREAM_HI_COUNT(count) | STREAM_HI_D(d) | STREAM_HI_MODE(mode) | STREAM_HI_SIZE(size) | STREAM_HI_GAP(gap))
                #define GROUP_HI(start, t, gap) (GROUP_HI_START(start) | GROUP_HI_TM1((t) - 1) | GROUP_HI_GAP(gap))
                #define DMA_CFG_RUN(kind, start, count) (DMA_CFG_KIND(kind) | DMA_CFG_START(start) | DMA_CFG_COUNT(count))
                #define ROUTER_HDR(dst, mode, addr) (ROUTER_HDR_DST(dst) | ROUTER_HDR_MODE(mode) | ROUTER_HDR_ADDR(addr))
                static inline void set_stream(unsigned i, uint32_t lo, uint32_t hi) { STREAM[2 * i] = lo; STREAM[2 * i + 1] = hi; }
                static inline void set_group(unsigned g, uint32_t lo, uint32_t hi) { GROUP[2 * g] = lo; GROUP[2 * g + 1] = hi; }

                /* status */
                static inline uint32_t status(void) { return TILE_STATUS; }
                /* the error conditions (4.13): 0 in a correct program; any bit is an error (TILE_COND_*) */
                static inline uint32_t conditions(void) { return TILE_CONDITIONS; }
                static inline unsigned tile_index(void) { return (TILE_STATUS >> TILE_ST_TILE_SHIFT) & TILE_ST_TILE_MASK; }
                static inline uint32_t sram_chunks(void) { return (((TILE_STATUS >> TILE_ST_SIZE_SHIFT) & TILE_ST_SIZE_MASK) + 1) << AREA_CHUNKS_SHIFT; }
                /* C&C's staging area and the sort scratch, from the device's size (6.2) */
                static inline uint32_t stage_base(void) { return sram_chunks() - STAGE_CHUNKS; }
                static inline uint32_t scratch_base(void) { return sram_chunks() - SCRATCH_CHUNKS; }

                /* Touch STREAM, GROUP, PTR or HIST only when no engine uses them: the address generator not running (its lead-in
                 * included) and ts_busy = 0 (4.10). The counters alone don't say so: arrivals can finish before this tile's own run. */
                static inline void wait_tables(void) {
                    while (status() & (TILE_ST_RD_RUNNING(1) | TILE_ST_TS_BUSY(1))) ;
                }

                /* WFI (4.11): stalls until GO or the local wake (6.3, 4.12) */
                static inline void wfi(void) { __asm__ volatile ("wfi" ::: "memory"); }
                /* wait_for_command (6.3): pulse done, then WFI immediately -- two back-to-back instructions; on waking,
                 * read the command word in mailbox slot 0 (one load) and return it. Every wake from a command goes through
                 * here, so no command word is ever overwritten unread. The done gate (4.12): from the done pulse until GO
                 * only GO wakes the core, so this WFI is woken by GO and nothing else. */
                static inline uint32_t wait_for_command(void) {
                    __asm__ volatile ("sw %0, %1(zero)\\n\\twfi" :: "r"(1u << DMA_PULSE_DONE_SHIFT), "i"((int)DMA_PULSE_ADDR) : "memory");
                    return MBOX[0];
                }
                /* start the run in DMA_CONFIG (8.2): loads the run and begins it; its first request goes out when its table
                 * entries are in (4 ts_engine_latency + 2 cycles for a group run, 2 ts_engine_latency + 1 for a stream run).
                 * The store a synchronised start must reach with identical code on every tile (6.3). */
                static inline void start_run(void) { DMA_PULSE = DMA_PULSE_START(1); }

                /* the send port (4.11): one ROUTER_SET_HDR per run of consecutive words, then ROUTER_SET_HI + ROUTER_SEND_LO per 64-bit packet */
                static inline void send64(uint32_t hi, uint32_t lo) { ROUTER_SET_HI = hi; ROUTER_SEND_LO = lo; }
                static inline void send32(uint32_t lo) { ROUTER_SEND_LO = lo; }
                #endif /* __ASSEMBLER__ */
                #endif /* MESH_H */
                """);
        return sb.toString();
    }
    private static void def(StringBuilder sb, String name, int v, String doc) {
        sb.append(String.format("#define %-20s %s%s%n", name, v >= 0 && v < 10 ? Integer.toString(v) : hex(v), doc == null ? "" : "   /* " + doc + " */"));
    }
}
