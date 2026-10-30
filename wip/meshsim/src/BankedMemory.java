/**
 * One controller memory (spec 4.11, v1.2): 32 KB = 8,192 words as four 2,048 x 32-bit banks interleaved by word, word w in
 * bank w mod 4 at row w / 4. Each bank is its own {@link SRAMMacro} with two ports (A: the core, B: the traffic director),
 * so the banking is checked, not assumed: port A touches one bank per access, and port B's 1, 2 or 4 consecutive words
 * (a naturally aligned mode-0 packet of 32, 64 or 128 bits) always fall in different banks, one write per bank port per
 * cycle (a second access to one bank port in a cycle throws, as for every macro). The spec leaves port B's build to the
 * implementer (4.11, D42: one 128-bit write per cycle); four word-interleaved banks is meshsim's.
 */
final class BankedMemory {
    static final int BANKS = 4, WORDS = 8192, BANK_WORDS = WORDS / BANKS, BYTES = 4 * WORDS;
    final String name;
    final SRAMMacro[] bank = new SRAMMacro[BANKS];

    BankedMemory(String name) {
        this.name = name;
        for (int b = 0; b < BANKS; b++) bank[b] = new SRAMMacro(name + ".bank" + b, BANK_WORDS, 2, false);
    }
    private SRAMMacro b(int w) { return bank[w & 3]; }

    int read32(int port, long t, int w) { return b(w).read32(port, t, w >>> 2); }
    void write32(int port, long t, int w, int v, int mask) { b(w).write32(port, t, w >>> 2, v, mask); }
    void write32(int port, long t, int w, int v) { write32(port, t, w, v, -1); }

    // ---- host access (no timing; between cycles) ----
    int peek(int w) { return b(w).peek(w >>> 2); }
    long peek64(int w) { return (peek(w) & 0xFFFFFFFFL) | ((long) peek(w + 1) << 32); }
    void poke(int w, int v) { b(w).poke(w >>> 2, v); }
    int size() { return WORDS; }
    long writes() { long s = 0; for (SRAMMacro m : bank) s += m.writes; return s; }
}
