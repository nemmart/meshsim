/**
 * Item S1 (DECISIONS-v1.2 S1; spec 4.8, 4.10, invariant 24), measured: for every counted write into big SRAM, PTR and
 * HIST, the cycles from the traffic director's acceptance to the write being complete (stored in the area, or written
 * back by the table's read-modify-write), against K, the cycles from acceptance to its counter's decrement. A write
 * complete later than K would let a read issued at done overtake it: meshsim flags it (invariant 24). The margin a
 * workload leaves is K minus the largest latency, before the controller's reaction to done (at least 4 cycles more):
 * meshsim flags a write complete later than K + 4 (invariant 24).
 */
final class S1 {
    /** The controller's least reaction to a counter's done before a read can follow (S1: waking from WFI, loading
     *  EVENTS, at least 4 cycles): a write complete within K + REACTION can't be overtaken; a later one is flagged. */
    static final int REACTION = 4;
    long sramMax, ptrMax, histMax, sramN, ptrN, histN, late; String lastLate;
    void sram(long lat) { sramN++; if (lat > sramMax) sramMax = lat; }
    /** Big SRAM's ordering point: acceptance -> the write in its area's queue (a later read of the area queues behind it). */
    long sramOrderMax;
    void sramOrder(long lat) { if (lat > sramOrderMax) sramOrderMax = lat; }
    void ptr(long lat) { ptrN++; if (lat > ptrMax) ptrMax = lat; }
    void hist(long lat) { histN++; if (lat > histMax) histMax = lat; }
    /** The table-window case (17 A): a controller load of HIST, the cycles since the tile's last HIST write-back (the
     *  smallest within 100 cycles of an update is the case's margin; an access during an update is flagged by 4.10's rule). */
    long histLoadMin = Long.MAX_VALUE, histLoads;
    void histLoad(long since) { if (since >= 0 && since < 100) { histLoads++; if (since < histLoadMin) histLoadMin = since; } }
    String report(int k) {
        return String.format("S1: accept -> complete, the largest: big SRAM %d (%d writes; in its area's queue by " + sramOrderMax + "), PTR %d (%d updates), HIST %d (%d updates); K = %d: margin %d before the controller's reaction (+%d)%s",
                sramMax, sramN, ptrMax, ptrN, histMax, histN, k, k - Math.max(sramMax, Math.max(ptrMax, histMax)), REACTION, late > 0 ? "; " + late + " LATE" : "")
                + (histLoads > 0 ? String.format("; the table-window case: %d HIST loads soon after an update, the nearest %d cycles after its write-back", histLoads, histLoadMin) : "");
    }
}
