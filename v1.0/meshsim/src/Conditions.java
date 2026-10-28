/**
 * The error conditions of spec 4.13, in TILE_CONDITIONS [10:0]: bit n is condition n. A condition means the software
 * did something wrong (or the hardware is faulty): a correct program on correct hardware never sets one. Read-only;
 * nothing in RUN clears them; entering HALT (the RUN -> HALT edge, and chip reset) clears them all. Any non-zero
 * condition raises the tile's error line to C&C (6.6). Drift: in a group run, the SRAM read queue refused an
 * address-generator request. Done early: done pulsed while a down-counter is non-zero.
 */
final class Conditions {
    static final int DRIFT = 0, ROUTER_OVF = 1, WE_IN_OVF = 2, RDPORT_OVF = 3, UNEXPECTED = 4, TABLE_OVF = 5,
            BUS_ERROR = 6, ILLEGAL = 7, GATHER_OVF = 8, BAD_MODE = 9, DONE_EARLY = 10, N = 11;
    static final int MASK = (1 << N) - 1;
    static final String[] NAME = { "drift", "router overflow", "write-engine input overflow", "read-port overflow",
            "unexpected arrival", "table request overflow", "bus error", "illegal instruction", "GATHER queue overflow",
            "bad mode", "done early" };
    static final String[] CNAME = { "DRIFT", "ROUTER_OVF", "WE_IN_OVF", "RDPORT_OVF", "UNEXPECTED", "TABLE_OVF",
            "BUS_ERROR", "ILLEGAL", "GATHER_OVF", "BAD_MODE", "DONE_EARLY" };
    private Conditions() {}
    static String names(int bits) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < N; i++) if ((bits & (1 << i)) != 0) sb.append(sb.length() > 0 ? ", " : "").append(NAME[i]);
        return sb.length() == 0 ? "none" : sb.toString();
    }
}
