/**
 * One class models every memory (brief 2.5): 32-bit words, a set of ports, each port at most
 * one access per cycle (a second throws). Reads return the contents as of the start of the
 * cycle; writes are buffered per port and committed at the end of the cycle (lazily, on the
 * first access at a later cycle), so the macro is order-independent. Accesses of 1, 2 or 4
 * words start at any word address (spec 4.1). Two ports writing the same word in one cycle is
 * undefined in hardware; here port order decides, and the case is counted in {@link #writeClash}.
 *
 * Host access (no timing): {@link #peek}/{@link #poke}.
 */
final class SRAMMacro {
    final String name;
    final int[] w;
    final int nPorts;
    final boolean readOnly;
    private final long[] portT;       // last access cycle per port
    private final boolean[] pend;     // write pending per port
    private final int[] pAddr, pWidth, pMask, pW0, pW1, pW2, pW3;
    private long lastT = Long.MIN_VALUE;
    int pendingPorts;
    long reads, writes, writeClash;
    long rdLo, rdHi;                  // result of the last read (lo = words a, a+1; hi = a+2, a+3)

    SRAMMacro(String name, int words, int nPorts, boolean readOnly) {
        this.name = name;
        this.w = new int[words];
        this.nPorts = nPorts;
        this.readOnly = readOnly;
        portT = new long[nPorts];
        java.util.Arrays.fill(portT, Long.MIN_VALUE);
        pend = new boolean[nPorts];
        pAddr = new int[nPorts]; pWidth = new int[nPorts]; pMask = new int[nPorts];
        pW0 = new int[nPorts]; pW1 = new int[nPorts]; pW2 = new int[nPorts]; pW3 = new int[nPorts];
    }

    int size() { return w.length; }

    private void enter(long t) {
        if (t == lastT) return;
        if (t < lastT) throw new IllegalStateException(name + ": time went backwards");
        if (pendingPorts > 0) commitAll();
        lastT = t;
    }

    private void commitAll() {
        for (int p = 0; p < nPorts; p++) {
            if (!pend[p]) continue;
            pend[p] = false;
            int a = pAddr[p];
            switch (pWidth[p]) {
                case 1 -> { int m = pMask[p]; w[a] = (w[a] & ~m) | (pW0[p] & m); }
                case 2 -> { w[a] = pW0[p]; w[a + 1] = pW1[p]; }
                default -> { w[a] = pW0[p]; w[a + 1] = pW1[p]; w[a + 2] = pW2[p]; w[a + 3] = pW3[p]; }
            }
        }
        pendingPorts = 0;
    }

    private void access(int port, long t, int addr, int width) {
        enter(t);
        if (portT[port] == t) throw new IllegalStateException(name + " port " + port + ": two accesses in cycle " + t);
        portT[port] = t;
        if (addr < 0 || addr + width > w.length) throw new IllegalStateException(name + ": address out of range " + addr + " width " + width);
    }

    int read32(int port, long t, int addr) { access(port, t, addr, 1); reads++; return w[addr]; }
    /** 64-bit read: result in rdLo. */
    long read64(int port, long t, int addr) {
        access(port, t, addr, 2); reads++;
        rdLo = (w[addr] & 0xFFFFFFFFL) | ((long) w[addr + 1] << 32);
        return rdLo;
    }
    /** 128-bit read: result in rdLo (words addr, addr+1) and rdHi (addr+2, addr+3). */
    void read128(int port, long t, int addr) {
        access(port, t, addr, 4); reads++;
        rdLo = (w[addr] & 0xFFFFFFFFL) | ((long) w[addr + 1] << 32);
        rdHi = (w[addr + 2] & 0xFFFFFFFFL) | ((long) w[addr + 3] << 32);
    }

    private void writeCheck(int port, long t, int addr, int width) {
        if (readOnly) throw new IllegalStateException(name + ": write to read-only memory");
        access(port, t, addr, width);
        writes++;
        for (int p = 0; p < nPorts; p++)
            if (p != port && pend[p] && pAddr[p] < addr + width && addr < pAddr[p] + pWidth[p]) writeClash++;
        pend[port] = true; pendingPorts++;
        pAddr[port] = addr; pWidth[port] = width;
    }
    /** 32-bit write with a bit mask (byte enables expanded to bits). */
    void write32(int port, long t, int addr, int value, int mask) {
        writeCheck(port, t, addr, 1); pW0[port] = value; pMask[port] = mask;
    }
    void write32(int port, long t, int addr, int value) { write32(port, t, addr, value, -1); }
    void write64(int port, long t, int addr, long value) {
        writeCheck(port, t, addr, 2); pW0[port] = (int) value; pW1[port] = (int) (value >>> 32);
    }
    void write128(int port, long t, int addr, long lo, long hi) {
        writeCheck(port, t, addr, 4);
        pW0[port] = (int) lo; pW1[port] = (int) (lo >>> 32); pW2[port] = (int) hi; pW3[port] = (int) (hi >>> 32);
    }

    // ---- host access, no timing (test setup, result checking, back door); only between cycles ----
    int peek(int addr) { flushPending(); return w[addr]; }
    long peek64(int addr) { flushPending(); return (w[addr] & 0xFFFFFFFFL) | ((long) w[addr + 1] << 32); }
    /** Host writes commit any buffered port write first, so a later lazy commit cannot overwrite them (v4.0). */
    void poke(int addr, int v) { flushPending(); w[addr] = v; }
    void poke64(int addr, long v) { flushPending(); w[addr] = (int) v; w[addr + 1] = (int) (v >>> 32); }
    /** Host-side commit of buffered writes (for inspection at the end of a run). */
    void flushPending() { if (pendingPorts > 0) commitAll(); }
    void resetStats() { reads = writes = writeClash = 0; }
}
