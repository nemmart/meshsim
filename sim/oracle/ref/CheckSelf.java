/**
 * CheckSelf (WORKER20, P2): v1.3's checker (Chip.checkInvariants, violation, violationReport) provoked kind by kind, its
 * report printed, for tests/checker.cpp to reproduce line for line (tests/test_mach.cpp; fixture tests/mach/checkself.out,
 * written by oracle/ref/checkself.sh). Grows with the kinds the C++ units record.
 */
final class CheckSelf {
    public static void main(String[] a) {
        {   // the injection path and the director: bad packets, size 11, count-only on counter 0, a gather elsewhere
            Net n = new Net(Net.cfg(-1));
            n.at(3, 10, 9, LinkWord.M_WRITE, 0, 0xFF000000, LinkWord.SIZE_32, 0x11, 0);        // region 0xFF: bad
            n.at(4, 11, 9, LinkWord.M_WRITE, 0, Net.cm(0x900), LinkWord.SIZE_UNDEF, 0x22, 0);  // size 11
            n.at(5, 12, 9, LinkWord.M_COUNT, 0, 0, LinkWord.SIZE_32, 0x33, 0);                 // count-only, counter 0
            n.at(6, 13, 9, LinkWord.M_GATHER, 0, 0, LinkWord.SIZE_64, 0x44, 0);                // mode 4 to another unit
            n.at(7, 14, 9, LinkWord.M_COUNT, 0, 0, LinkWord.SIZE_UNDEF, 0x55, 0);              // two at once: the last's text
            n.at(8, 14, 9, LinkWord.M_AUG, 1, 0, LinkWord.SIZE_128, 0x66, 0x77);               // mode 3 at 128: bad
            n.at(3, 40, 9, LinkWord.M_WRITE, 0, 0xFF000000, LinkWord.SIZE_64, 0x88, 0);        // again, later
            boolean ok = n.runAll(1000);
            System.out.println("packets: " + (ok ? "drained" : "NOT drained") + " at cycle " + n.c.cycle);
            System.out.print(n.c.violationReport());
        }
        {   // ERRORS (invariant 15): reported once per bit while any error line is high
            Config k = Net.cfg(-1); k.checkErrors = true;
            Net n = new Net(k);
            n.at(3, 10, 9, LinkWord.M_WRITE, 0, 0xFF000000, LinkWord.SIZE_32, 0x11, 0);        // ERRORS [1] at tile 9
            n.at(4, 12, 12, LinkWord.M_WRITE, 0, 0xFE000000, LinkWord.SIZE_32, 0x22, 0);       // ERRORS [1] at tile 12
            n.at(5, 30, 9, LinkWord.M_COUNT, 1, 0, LinkWord.SIZE_32, 0x33, 0);                 // unexpected: ERRORS [2] at 9
            n.at(6, 31, 9, LinkWord.M_WRITE, 0, 0xFF000000, LinkWord.SIZE_32, 0x44, 0);        // bit 1 again: not reported
            n.runAll(1000);
            System.out.print(n.c.violationReport());
        }
        {   // the message cap (maxViolationMessages): the count goes on
            Config k = Net.cfg(-1); k.maxViolationMessages = 3;
            Net n = new Net(k);
            for (int i = 0; i < 6; i++) n.at(10 + i, 10 + i, 20, LinkWord.M_COUNT, 0, 0, LinkWord.SIZE_32, i, 0);
            n.runAll(1000);
            System.out.print(n.c.violationReport());
        }
    }
}
