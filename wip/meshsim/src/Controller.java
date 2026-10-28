/**
 * The tile controller (spec 4.11): an in-order RV32I + Zmmul + WFI core, ported from meshsim v3.10's RV32Controller
 * (the instruction semantics and timing are v3.10's; the memory system, registers and control are design v2's).
 *
 * Address map: instruction memory 0x0000-0x1FFF (fetch only), the table window 0x2000-0x3FFF (HIST 0x2000, PTR 0x2400,
 * STREAM_SRC 0x2800, STREAM_DEST 0x2C00, STREAM_COUNT 0x3000, GROUP 0x3400; 0x3600-0x3FFF unmapped), data memory
 * 0x4000-0x5FFF, registers 0xFFFFF800-0xFFFFF89C (0x8A0-0xFFC unmapped). Loads and stores to data memory take one cycle;
 * a load from the registers or the table window takes Config.regLoadLatency cycles (4.11: 1 or 2, meshsim's reference
 * 2), sampling in its last cycle; stores are single-cycle and posted. A loaded value is usable by the next instruction;
 * a taken branch or jump costs 1 extra cycle, a multiply 3 in all. A send store (ROUTER_SEND_32, _64 or _128) puts the
 * packet into the send holding register in the store's cycle; if it is refused the store is retried and the core
 * stalls, one cycle at a time; a REQUEST_SEND store stalls while the request FIFO is full (4.18).
 *
 * Bus errors (halt, bus-error sticky bit): fetch outside instruction memory; loads and stores to instruction memory,
 * misaligned or unmapped accesses, stores to TILE_STATUS, sub-word stores to registers or the table window.
 * Illegal (halt, illegal sticky bit): divide opcodes, CSR instructions, anything unimplemented. ecall and ebreak halt
 * (a simulator-only {@link DebugHook} may continue an ebreak). The cause of every halt is logged ({@link Chip#log}).
 *
 * WFI (0x10500073) stalls until the wake term holds (TileControl.takeWake; 4.12). A WFI with a wake already available
 * falls through in one cycle; a stalled core resumes by executing the next instruction in the wake cycle (DEV).
 * HALT (the RUN/HALT line low): no fetch. HALT -> RUN: pc = 0x1F00, x1-x31 = 0, a halted core restarts.
 */
final class Controller implements Component {
    static final int RUN = 0, HALT_ILLEGAL = 1, HALT_ECALL = 2, HALT_EBREAK = 3, HALT_BUSERR = 4;
    static final String[] HALT_NAME = { "running", "illegal instruction", "ecall", "ebreak", "bus error" };
    static final int WFI = 0x10500073;
    interface DebugHook { boolean onEbreak(Controller c, long t); }

    final Tile tile;
    final TileControl ctl;
    final ControllerMemories cm;
    DebugHook debugHook;
    int branchPenalty = 1, mulLatency = 3;

    // ---- architectural state ----
    final int[] x = new int[32];
    int pc = Registers.LOADER_BASE;
    int halt = RUN, haltPc, haltInstr, haltAddr;
    boolean inWfi;
    boolean enabled = true;                 // false: a coreless test machine (the core never runs)
    private int stall;
    // ---- the send-store retry (ROUTER_SEND_32, _64, _128) ----
    private long pendingT = Long.MIN_VALUE;
    private int curStall;
    // ---- statistics ----
    long instructions, cyclesStalled, stallBranch, stallMul, stallSend, stallRegLoad, stallRequest, longestSendStall, cyclesHalted, cyclesWfi, cyclesHeld;
    private long loadWaitT = Long.MIN_VALUE; private int loadWaitPc;
    long tableReads, tableWrites, busErrors, illegals;
    int profLo, profHi;                     // simulator-only profile: cycles with pc in [profLo, profHi), and read-port stores from there
    long profCycles, profStores;
    boolean traceReg;                       // simulator-only: stores to 0xFFFFFFF0 are trace marks (worker 4's TRACE_REG), not bus errors
    boolean inLoop; long loopStart, loopCycles, loopStores, traceMarks;
    long wfiAt = -1, wokeAt = -1;          // the last WFI's entry and wake cycles (tests)
    final java.util.List<long[]> wfiLog = new java.util.ArrayList<>();   // {entered, woke, cause} per WFI

    Controller(Tile tile, TileControl ctl, ControllerMemories cm) { this.tile = tile; this.ctl = ctl; this.cm = cm; }

    // ================================================================ cycle
    @Override public void runCycle(long t) {
        if (!enabled) return;
        if (ctl.runEdge(t)) reset();
        if (!ctl.running(t)) { cyclesHeld++; return; }
        if (halt != RUN) { cyclesHalted++; return; }
        if (pc >= profLo && pc < profHi) profCycles++;
        if (pendingT == t - 1 && tile.sendHold.refused(t)) {           // a send store's packet was not taken: retry, core stalls (any of the three)
            tile.sendHold.retry(t); pendingT = t; stallSend++; cyclesStalled++;
            if (++curStall > longestSendStall) longestSendStall = curStall;
            return;
        }
        curStall = 0;
        if (inWfi) {
            if (!ctl.takeWake(t, false)) { cyclesWfi++; return; }
            inWfi = false; wokeAt = t; wfiLog.add(new long[] { wfiAt, t, ctl.lastWake });
        }
        if (stall > 0) { stall--; cyclesStalled++; return; }
        step(t);
    }

    /** HALT -> RUN: the core only (6.1). */
    void reset() {
        java.util.Arrays.fill(x, 0);
        pc = Registers.LOADER_BASE; halt = RUN; inWfi = false; stall = 0; pendingT = Long.MIN_VALUE;
    }

    private void step(long t) {
        int ipc = pc;
        if ((ipc & 3) != 0 || Integer.compareUnsigned(ipc, Registers.IMEM_SIZE) >= 0) { busError(t, ipc, 0, ipc, "instruction fetch outside instruction memory"); return; }
        int ins = cm.imem.read32(ControllerMemories.PORT_A, t, ipc >> 2);
        execute(t, ins, ipc);
    }

    private void haltWith(long t, int reason, int at, int ins, String why) {
        halt = reason; haltPc = at; haltInstr = ins;
        tile.slot.setHalted(t, true); tile.sender.haltReset(t);           // a halted controller stops the peripheral and the sender (4.17.7)
        tile.chip.log(t, tile.idx, "controller halted: " + HALT_NAME[reason] + " at pc=0x" + Integer.toHexString(at)
                + String.format(" instr=0x%08x", ins) + (why != null ? " (" + why + ")" : ""));
    }
    private void illegal(long t, int ipc, int ins) { illegals++; haltWith(t, HALT_ILLEGAL, ipc, ins, null); }
    private void busError(long t, int ipc, int ins, int ea, String why) { busErrors++; haltAddr = ea; haltWith(t, HALT_BUSERR, ipc, ins, why + ", address 0x" + Integer.toHexString(ea)); }

    private void execute(long t, int ins, int ipc) {
        instructions++;
        int op = ins & 0x7F, rd = (ins >>> 7) & 31, f3 = (ins >>> 12) & 7, rs1 = (ins >>> 15) & 31, rs2 = (ins >>> 20) & 31, f7 = ins >>> 25;
        int a = x[rs1], b = x[rs2];
        int next = ipc + 4;
        int res = 0; boolean wr = false;
        switch (op) {
            case 0x37 -> { res = ins & 0xFFFFF000; wr = true; }                                   // lui
            case 0x17 -> { res = ipc + (ins & 0xFFFFF000); wr = true; }                           // auipc
            case 0x6F -> {                                                                        // jal
                int imm = ((ins >> 31) << 20) | (ins & 0xFF000) | (((ins >>> 20) & 1) << 11) | ((ins >>> 21) & 0x3FF) << 1;
                res = next; wr = true; next = ipc + imm; stall += branchPenalty; stallBranch += branchPenalty;
            }
            case 0x67 -> {                                                                        // jalr
                if (f3 != 0) { illegal(t, ipc, ins); return; }
                res = next; wr = true; next = (a + (ins >> 20)) & ~1; stall += branchPenalty; stallBranch += branchPenalty;
            }
            case 0x63 -> {                                                                        // branches
                int imm = ((ins >> 31) << 12) | (((ins >>> 7) & 1) << 11) | (((ins >>> 25) & 0x3F) << 5) | (((ins >>> 8) & 0xF) << 1);
                boolean take;
                switch (f3) {
                    case 0 -> take = a == b; case 1 -> take = a != b; case 4 -> take = a < b; case 5 -> take = a >= b;
                    case 6 -> take = Integer.compareUnsigned(a, b) < 0; case 7 -> take = Integer.compareUnsigned(a, b) >= 0;
                    default -> { illegal(t, ipc, ins); return; }
                }
                if (take) { next = ipc + imm; stall += branchPenalty; stallBranch += branchPenalty; }
            }
            case 0x03 -> {                                                                        // loads
                int ea = a + (ins >> 20);
                if (tile.cfg.regLoadLatency > 1 && (Registers.inRegs(ea) || inTables(ea & ~3)) && !(loadWaitT == t - 1 && loadWaitPc == ipc)) {
                    loadWaitT = t; loadWaitPc = ipc; instructions--; stallRegLoad++; cyclesStalled++; return;   // 4.11: a 2-cycle register or table load samples in its last cycle
                }
                int width = 1 << (f3 & 3);
                if (f3 > 5 || f3 == 3) { illegal(t, ipc, ins); return; }
                if ((ea & (width - 1)) != 0) { busError(t, ipc, ins, ea, "misaligned load"); return; }
                Integer v = load(t, ea, width);
                if (v == null) { busError(t, ipc, ins, ea, "load from an unmapped, fetch-only or write-only address, or a sub-word load from the table window or a register"); return; }
                int sh = (ea & 3) * 8;
                res = switch (f3) {
                    case 0 -> (byte) (v >> sh); case 1 -> (short) (v >> sh); case 2 -> v;
                    case 4 -> (v >> sh) & 0xFF; default -> (v >> sh) & 0xFFFF;
                };
                wr = true;
            }
            case 0x23 -> {                                                                        // stores
                int ea = a + (((ins >> 25) << 5) | rd);
                if (f3 == 2 && Registers.inRegs(ea) && Registers.storeAt(ea) == Registers.REQUEST_SEND && !tile.req.canPush(t)) {
                    instructions--; stallRequest++; cyclesStalled++; return;                       // 4.18: a full request FIFO stalls the store
                }
                if (f3 > 2) { illegal(t, ipc, ins); return; }
                int width = 1 << f3;
                if ((ea & (width - 1)) != 0) { busError(t, ipc, ins, ea, "misaligned store"); return; }
                int sh = (ea & 3) * 8;
                int mask = (f3 == 2 ? -1 : f3 == 1 ? 0xFFFF : 0xFF) << sh;
                String err = store(t, ea, b << sh, mask);
                if (err != null) { busError(t, ipc, ins, ea, err); return; }
            }
            case 0x13 -> {                                                                        // op-imm
                int imm = ins >> 20, sh = rs2;
                switch (f3) {
                    case 0 -> res = a + imm; case 2 -> res = a < imm ? 1 : 0; case 3 -> res = Integer.compareUnsigned(a, imm) < 0 ? 1 : 0;
                    case 4 -> res = a ^ imm; case 6 -> res = a | imm; case 7 -> res = a & imm;
                    case 1 -> { if (f7 != 0) { illegal(t, ipc, ins); return; } res = a << sh; }
                    default -> { if (f7 == 0) res = a >>> sh; else if (f7 == 0x20) res = a >> sh; else { illegal(t, ipc, ins); return; } }
                }
                wr = true;
            }
            case 0x33 -> {                                                                        // op
                if (f7 == 1) {                                                                    // Zmmul; divide opcodes are illegal
                    long la = a, lb = b;
                    switch (f3) {
                        case 0 -> res = a * b;
                        case 1 -> res = (int) ((la * lb) >> 32);
                        case 2 -> res = (int) ((la * (lb & 0xFFFFFFFFL)) >> 32);
                        case 3 -> res = (int) (((la & 0xFFFFFFFFL) * (lb & 0xFFFFFFFFL)) >>> 32);
                        default -> { illegal(t, ipc, ins); return; }
                    }
                    stall += mulLatency - 1; stallMul += mulLatency - 1;
                } else if (f7 == 0 || f7 == 0x20) {
                    if (f7 == 0x20 && f3 != 0 && f3 != 5) { illegal(t, ipc, ins); return; }
                    res = switch (f3) {
                        case 0 -> f7 == 0 ? a + b : a - b;
                        case 1 -> a << (b & 31);
                        case 2 -> a < b ? 1 : 0;
                        case 3 -> Integer.compareUnsigned(a, b) < 0 ? 1 : 0;
                        case 4 -> a ^ b;
                        case 5 -> f7 == 0 ? a >>> (b & 31) : a >> (b & 31);
                        case 6 -> a | b;
                        default -> a & b;
                    };
                } else { illegal(t, ipc, ins); return; }
                wr = true;
            }
            case 0x0F -> { /* fence: no-op */ if (f3 > 1) { illegal(t, ipc, ins); return; } }
            case 0x73 -> {
                if (ins == 0x00000073) { haltWith(t, HALT_ECALL, ipc, ins, null); return; }
                if (ins == 0x00100073) {
                    if (debugHook == null || !debugHook.onEbreak(this, t)) { haltWith(t, HALT_EBREAK, ipc, ins, null); return; }
                } else if (ins == WFI) {
                    wfiAt = t;
                    if (ctl.takeWake(t, true)) { wokeAt = t; wfiLog.add(new long[] { t, t, ctl.lastWake }); } else inWfi = true;
                } else { illegal(t, ipc, ins); return; }                                         // CSR instructions, mret, ...
            }
            default -> { illegal(t, ipc, ins); return; }
        }
        if (wr && rd != 0) x[rd] = res;
        pc = next;
    }

    // ================================================================ the data side
    static boolean inData(int a) { return a >= Registers.DMEM_BASE && a < Registers.DMEM_BASE + Registers.DMEM_SIZE; }
    static boolean inTables(int a) { return a >= Registers.TABLE_BASE && a < Registers.TABLES_END; }

    /** A load of `width` bytes at t (the aligned word containing ea); null = bus error. The table window and the
     *  registers take word loads only; PTR, STREAM and GROUP are write-only (8.4, V1.0-CHANGES item 7): HIST is the
     *  window's only readable table. */
    Integer load(long t, int ea, int width) {
        int a = ea & ~3;
        if (inData(a)) { int w = (a - Registers.DMEM_BASE) >> 2; cm.loaded(w); return cm.dmem.read32(ControllerMemories.PORT_A, t, w); }
        if (inTables(a)) { if (width != 4 || a >= Registers.PTR_BASE) return null; tableReads++; return window(t, a, 0, false); }
        if (Registers.inRegs(a)) return width != 4 ? null : ctl.read(t, a);
        return null;
    }

    /** A store at t; returns the bus-error reason, or null. */
    String store(long t, int ea, int v, int mask) {
        int a = ea & ~3;
        if (inData(a)) { int w = (a - Registers.DMEM_BASE) >> 2; cm.stored(w); cm.dmem.write32(ControllerMemories.PORT_A, t, w, v, mask); return null; }
        if (inTables(a)) { if (mask != -1) return "sub-word store to the table window"; tableWrites++; window(t, a, v, true); return null; }
        if (Registers.inRegs(a)) {
            if (traceReg && a == 0xFFFFFFF0) { traceMarks++; if (v == 0x600) { inLoop = true; loopStart = t; } else if (v == 0x601 && inLoop) { inLoop = false; loopCycles += t - loopStart; } return null; }
            if (mask != -1) return "sub-word store to a register";
            if (!ctl.write(t, a, v)) return Registers.at(a) == null ? "store to an unmapped register" : "store to a read-only register";
            Registers.Reg reg = Registers.storeAt(a);
            if (reg == Registers.SRAM_RD_ADDR_QUEUE) { tile.readPort.store(t, v, ctl.rdDest(), ctl.rdSize()); if (pc >= profLo && pc < profHi) profStores++; if (inLoop) loopStores++; }   // never stalls (4.3)
            if (Registers.isSend(reg)) {                                  // 8.4: the size is the register's
                int size = Registers.sendSize(reg);
                tile.sendHold.put(t, ctl.sendLo(v, size), ctl.sendHeader(size), ctl.sendHi(v, size));
                pendingT = t;
            }
            return null;
        }
        return inTablesGap(a) ? "store to the unmapped part of the table window" : "store to an unmapped address or instruction memory";
    }
    private static boolean inTablesGap(int a) { return a >= Registers.TABLES_END && a < Registers.TABLE_END; }

    /** The table window (4.10): one cycle, each memory's controller port. Checks the engine is not using it. */
    private int window(long t, int a, int v, boolean write) {
        int off = a - Registers.TABLE_BASE;
        if (a < Registers.PTR_BASE) {
            int i = off >> 2;
            if (tile.hist.busy) ctl.violate(t, "table window: HIST[" + i + "] accessed while count-on-arrival is using HIST (4.10)");
            if (write) { tile.hist.pipe.ctrlWrite(t, i, v & 0x1FFFF); return 0; }        // port B, figure 12
            return tile.hist.pipe.ctrlRead(t, i);
        }
        if (a < Registers.STREAM_SRC_BASE) {
            int i = (a - Registers.PTR_BASE) >> 2;
            if (tile.ts.ptrBusy) ctl.violate(t, "table window: PTR[" + i + "] accessed while the write engine is using PTR (4.10)");
            if (write) { tile.ts.pipe.ctrlWrite(t, i, v & 0x1FFFFF); return 0; }         // port B, figure 12
            return tile.ts.pipe.ctrlRead(t, i);
        }
        // STREAM_SRC 0x2800, STREAM_DEST 0x2C00, STREAM_COUNT 0x3000 (256 words each), GROUP 0x3400 (64 x 2 words): 8.9
        int base = a >= Registers.GROUP_BASE ? Registers.GROUP_BASE : a >= Registers.STREAM_COUNT_BASE ? Registers.STREAM_COUNT_BASE
                : a >= Registers.STREAM_DEST_BASE ? Registers.STREAM_DEST_BASE : Registers.STREAM_SRC_BASE;
        SRAMMacro m = base == Registers.GROUP_BASE ? tile.ts.group : base == Registers.STREAM_COUNT_BASE ? tile.ts.streamCount
                : base == Registers.STREAM_DEST_BASE ? tile.ts.streamDest : tile.ts.streamSrc;
        int w = (a - base) >> 2;
        if (!tile.ag.idle()) ctl.violate(t, "table window: " + m.name + " accessed while the address generator is running, its lead-in included (4.10)");
        if (write) { m.write32(1, t, w, v); return 0; }               // port 1: the controller side
        return m.read32(1, t, w);
    }

    // ================================================================ host back door
    /** Preload a flat image into controller memory at a byte address (instruction or data memory). */
    void loadImage(byte[] img, int byteAddr) {
        for (int i = 0; i < img.length; i += 4) {
            int w = 0;
            for (int k = 0; k < 4; k++) if (i + k < img.length) w |= (img[i + k] & 0xFF) << (8 * k);
            pokeLocal(byteAddr + i, w);
        }
    }
    void pokeLocal(int byteAddr, int w) {
        int a = byteAddr & ~3;
        if (a >= 0 && a < Registers.IMEM_SIZE) cm.imem.poke(a >> 2, w);
        else if (inData(a)) cm.dmem.poke((a - Registers.DMEM_BASE) >> 2, w);
        else throw new IllegalArgumentException("not controller memory: 0x" + Integer.toHexString(byteAddr));
    }
    int peekLocal(int byteAddr) {
        int a = byteAddr & ~3;
        if (a >= 0 && a < Registers.IMEM_SIZE) return cm.imem.peek(a >> 2);
        if (inData(a)) return cm.dmem.peek((a - Registers.DMEM_BASE) >> 2);
        throw new IllegalArgumentException("not controller memory: 0x" + Integer.toHexString(byteAddr));
    }

    String haltReport() {
        return "tile " + tile.idx + ": " + HALT_NAME[halt] + " at pc=0x" + Integer.toHexString(haltPc) + String.format(" instr=0x%08x", haltInstr)
                + (halt == HALT_BUSERR ? " addr=0x" + Integer.toHexString(haltAddr) : "");
    }
    void resetStats() {
        instructions = cyclesStalled = stallBranch = stallMul = stallSend = stallRegLoad = stallRequest = longestSendStall = cyclesHalted = cyclesWfi = cyclesHeld = 0;
        tableReads = tableWrites = 0;
    }
}
