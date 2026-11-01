/**
 * The error conditions of spec 4.13 (v1.1, 2026-10-26n), in TILE_CONDITIONS: bit n is condition n, grouped by unit: the
 * network (0 router overflow, 1 bad mode or size), the arrivals (2 unexpected arrival), the address generator (3 drift),
 * the write engine (4), the tables (5), the GATHER engine (6), the read port (7 read-port error: an overflow or a read
 * not aligned to its size), control (8 done early), the controller (9 bus error, 10 illegal instruction), the peripheral
 * slot (11 bad peripheral write, 12 and 13 the peripheral's own: the replayer's queue overrun and invalid expansion) and
 * the request port (14 receive underflow). [31:15] read 0. A condition means the software did something wrong (or the
 * hardware is faulty): a correct program on correct hardware never sets one. Read-only; nothing in RUN clears them;
 * entering HALT (the RUN -> HALT edge, and chip reset) clears them all. Any non-zero condition raises the tile's error
 * line to C&C (6.6).
 */
final class Conditions {
    static final int ROUTER_OVF = 0, BAD_MODE_OR_SIZE = 1, UNEXPECTED = 2, DRIFT = 3, WE_IN_OVF = 4, TABLE_OVF = 5,
            GATHER_OVF = 6, READ_PORT = 7, DONE_EARLY = 8, BUS_ERROR = 9, ILLEGAL = 10, BAD_PERIPH_WRITE = 11, PERIPH_2 = 12,
            PERIPH_3 = 13, RECV_UNDERFLOW = 14, N = 15;
    static final int MASK = (1 << N) - 1;
    static final String[] NAME = { "router overflow", "bad packet or misaligned access", "unexpected arrival", "drift", "write-engine input overflow",
            "table request overflow", "GATHER queue overflow", "read-port error", "done early", "bus error", "illegal instruction",
            "peripheral 1", "peripheral 2", "peripheral 3", "receive underflow" };
    static final String[] CNAME = { "ROUTER_OVF", "BAD_MODE_OR_SIZE", "UNEXPECTED", "DRIFT", "WE_IN_OVF", "TABLE_OVF",
            "GATHER_OVF", "READ_PORT", "DONE_EARLY", "BUS_ERROR", "ILLEGAL", "BAD_PERIPH_WRITE", "PERIPH_2", "PERIPH_3", "RECV_UNDERFLOW" };
    /** Kinds (4.13): P a program error; H a hardware-fault detector; P (or H) a program error a corrupted image can also
     *  cause; 12 and 13 per peripheral (the replayer's: P, and 13 also H). */
    static final String[] KIND = { "P", "P", "P", "P", "P", "H", "H", "P", "P", "P (or H)", "P (or H)", "P", "per peripheral", "per peripheral", "P" };
    private Conditions() {}
    static String names(int bits) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < N; i++) if ((bits & (1 << i)) != 0) sb.append(sb.length() > 0 ? ", " : "").append(NAME[i]);
        return sb.length() == 0 ? "none" : sb.toString();
    }
}
