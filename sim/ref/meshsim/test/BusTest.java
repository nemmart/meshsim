/** Test 1: bus primitives, generic and primitive variants against the same rules (brief 2.2). */
public final class BusTest {
    public static void main(String[] a) {
        latch(); delayLine(); overflowFifo(); backpressureFifo();
        wordLatch(); wordDelayLine(); wordFifo(); wordHold(); wordBackpressureFifo(); wordBackpressureDelayLine(); registerBlock(); sram();
        System.exit(T.done("BusTest"));
    }

    static void latch() {
        T.group("Latch");
        Latch<Integer> l = new Latch<>("l");
        T.check(!l.ready(1), "empty before first put");
        l.put(1, 7);
        T.check(!l.ready(1), "put at t not visible at t");
        T.throwsISE(() -> l.peek(1), "peek before visible throws");
        T.throwsISE(() -> l.put(1, 8), "double put throws");
        T.eq(7, l.peek(2), "visible at t+1");
        T.eq(7, l.take(2), "take non-destructive");
        T.eq(7, l.peek(5), "stays visible");
        l.put(5, 9);
        T.eq(7, l.peek(5), "old value during put cycle");
        T.eq(9, l.peek(6), "new value next cycle");
        T.throwsISE(() -> l.peek(4), "time backwards throws");
    }

    static void delayLine() {
        T.group("DelayLine");
        DelayLine<String> d = new DelayLine<>("d", 3);
        d.put(10, "x");
        T.check(!d.ready(10), "not visible at 10");
        d.put(11, "y");
        for (long t = 11; t < 13; t++) T.check(!d.ready(t), "not visible at " + t);
        T.check(d.ready(13), "visible at t+L");
        T.eqo("x", d.peek(13), "peek");
        T.eqo("x", d.take(13), "take");
        T.check(!d.ready(13), "y not yet");
        T.eqo("y", d.take(14), "in order");
        T.check(!d.ready(15), "empty");
        DelayLine<Integer> d1 = new DelayLine<>("d1", 1, 2);
        d1.put(1, 1); d1.put(2, 2); d1.put(3, 3);
        T.throwsISE(() -> d1.put(4, 4), "capacity exceeded (consumer did not take) throws");
    }

    static void overflowFifo() {
        T.group("OverflowFIFO (generic, not used by the design: M15's rule, a pop makes room in its cycle)");
        OverflowFIFO<Integer> f = new OverflowFIFO<>("f", 2);
        T.check(f.canPut(1), "canPut always");
        f.put(1, 1);
        T.check(!f.ready(1), "put at t not visible at t");
        T.throwsISE(() -> f.take(1), "take when nothing visible throws");
        f.put(2, 2);
        T.eq(1, f.peek(2), "visible at t+1");
        f.put(3, 3);                       // occ(3)=2, put -> settle 2-0+1 = 3 > 2: dropped
        T.eq(2, f.size(3), "size");
        T.check(!f.overflow, "no overflow yet at 3");
        T.eq(2, f.size(4), "size after drop");
        T.check(f.overflow, "overflow flagged");
        T.eq(3, f.overflowCycle, "overflow cycle recorded");
        T.eq(1, f.take(4), "take");
        f.put(4, 4);                       // occ 2-1+1 = 2 fits
        T.eq(2, f.size(5), "put and take same cycle both count");
        T.eq(2, f.take(5), "fifo order");
        T.eq(4, f.take(6), "fifo order 2");
        T.eq(1, f.overflowCount, "one drop");
        T.eq(2, f.highWater, "high water");
        // depth 1 sustains one put and one take per cycle whatever the order
        OverflowFIFO<Integer> g = new OverflowFIFO<>("g", 1);
        g.put(1, 10);
        for (int t = 2; t < 20; t++) {
            if ((t & 1) == 0) { T.eq(t + 8, g.take(t), "d1 take-then-put " + t); g.put(t, t + 9); }
            else { g.put(t, t + 9); T.eq(t + 8, g.take(t), "d1 put-then-take " + t); }
        }
        T.check(!g.overflow, "depth-1 no overflow with 1r/1w per cycle");
        g.put(20, 0); g.put(21, 0);        // no take at 21: occ 1-0+1 = 2 > 1
        T.check(g.ready(22) && g.overflow && g.overflowCycle == 20, "depth-1 drop flagged at the settle of 20");
        T.throwsISE(() -> { g.take(23); g.take(23); }, "two takes in one cycle throw");
    }

    static void backpressureFifo() {
        T.group("BackpressureFIFO");
        BackpressureFIFO<Integer> b = new BackpressureFIFO<>("b", 1);
        T.check(b.canPut(1), "empty: can put");
        b.put(1, 1);
        T.check(!b.canPut(2), "full at start of 2");
        b.take(2);
        T.check(!b.canPut(2), "canPut from start-of-cycle occupancy even after a take");
        T.throwsISE(() -> b.put(2, 2), "put when canPut false throws");
        T.check(b.canPut(3), "empty at 3");
        b.put(3, 3);
        T.eq(3, b.take(4), "value");
        BackpressureFIFO<Integer> b2 = new BackpressureFIFO<>("b2", 2);
        b2.put(1, 1); T.check(b2.canPut(2), "depth 2: one in, can put"); b2.put(2, 2);
        T.check(!b2.canPut(3), "full");
        b2.take(3); T.check(!b2.canPut(3), "still start-of-cycle"); T.check(b2.canPut(4), "next cycle");
    }

    static void wordLatch() {
        T.group("WordLatch");
        WordLatch l = new WordLatch("wl");
        T.check(!l.ready(1), "empty");
        l.put(1, 1, 2, 3);
        T.check(!l.ready(1), "not at t");
        T.throwsISE(() -> l.put(1, 0, 0, 0), "double put");
        T.eq(1, l.peekA(2), "a"); T.eq(2, l.peekB(2), "b"); T.eq(3, l.peekC(2), "c");
        l.take(2); T.eq(1, l.peekA(9), "persistent");
    }

    static void wordDelayLine() {
        T.group("WordDelayLine");
        WordDelayLine d = new WordDelayLine("wd", 2);
        d.put(5, 7, 8, 9);
        T.check(!d.ready(6), "not at t+1");
        T.check(d.ready(7), "at t+L");
        d.put(7, 1, 1, 1);
        T.eq(7, d.peekA(7), "peek"); d.take(7);
        T.check(!d.ready(8), "second not yet");
        T.eq(1, d.peekB(9), "second at 9"); d.take(9);
        T.throwsISE(() -> d.take(9), "empty take throws");
    }

    static void wordFifo() {
        T.group("WordFIFO");
        // v1.0 (V1.0-CHANGES item 5, 13a): a FIFO is full by its count before the cycle's pop
        WordFIFO f = new WordFIFO("wf", 2);
        f.put(1, 1, 0, 0);
        f.put(2, 2, 0, 0);                               // 1 at the start of 2: fits
        T.eq(1, f.peekA(3), "fifo order");
        T.eq(2, f.size(3), "two entries");
        f.take(3); f.put(3, 3, 0, 0);                    // full at the start of 3: the push overflows despite the pop
        f.ready(4);
        T.check(f.overflow && f.overflowCycle == 3 && f.overflowCount == 1, "a push at full overflows even with a same-cycle pop");
        T.eq(1, f.size(4), "the pop happened, the push was dropped");
        T.eq(2, f.peekA(4), "the old entry remains");
        f.take(4); f.put(4, 4, 0, 0);                    // 1 at the start of 4: take and put both count
        T.eq(1, f.size(5), "below full: a push with a same-cycle pop fits");
        T.eq(4, f.peekA(5), "fifo order 2");
        T.eq(1, WordFIFO.countBeforePop ? 1 : 0, "the v1.0 rule is the default");
        // depth 1 sustains a put every other cycle with a take each cycle it holds one
        WordFIFO g = new WordFIFO("g", 1);
        g.put(10, 7, 0, 0);
        T.eq(7, g.peekA(11), "depth 1: visible at t+1");
        g.take(11); g.put(11, 8, 0, 0);                  // full at the start of 11: dropped
        g.ready(12);
        T.check(g.overflow && g.overflowCycle == 11, "depth 1: a put in the cycle of a take at full overflows");
        T.eq(1, g.highWater, "high water");
    }

    static void wordHold() {
        T.group("WordHold");
        WordHold h = new WordHold("wh");
        T.check(h.canPut(1), "empty");
        h.put(1, 11, 0, 0);
        T.check(!h.canPut(2), "busy at 2");
        T.check(!h.refused(2), "first put not refused");
        h.put(2, 22, 0, 0);                  // consumer does not take at 2 -> refused
        T.check(h.refused(3), "refused seen at 3");
        T.eq(11, h.peekA(3), "old word still there");
        h.put(3, 22, 0, 0);                  // re-present; consumer takes at 3
        h.take(3);
        T.check(!h.refused(4), "accepted");
        T.eq(22, h.peekA(4), "new word visible at 4");
        T.check(!h.canPut(4), "busy");
        h.take(4);
        T.check(!h.canPut(4), "canPut is start-of-cycle");
        T.check(h.canPut(5), "empty at 5");
        T.eq(1, h.refusals, "one refusal");
        h.put(5, 33, 0, 0);
        h.put(6, 44, 0, 0);                  // nobody takes at 6: refused
        T.check(h.refused(7), "refused"); h.retry(7); h.take(7);
        T.eq(44, h.peekA(8), "retry re-presented the refused word");
        T.throwsISE(() -> h.retry(9), "retry without a refusal throws");
    }

    static void wordBackpressureFifo() {
        T.group("WordBackpressureFIFO");
        WordBackpressureFIFO b = new WordBackpressureFIFO("wb", 1);
        T.check(b.canPut(1), "empty: can put");
        b.put(1, 5, 0, 0);
        T.check(!b.canPut(2), "full at start of 2");
        b.take(2);
        T.check(!b.canPut(2), "canPut from start-of-cycle occupancy even after a take");
        T.throwsISE(() -> b.put(2, 6, 0, 0), "put when canPut false throws");
        T.check(b.canPut(3), "empty at 3");
        b.put(3, 7, 0, 0); T.eq(7, b.peekA(4), "value");
        WordBackpressureFIFO b2 = new WordBackpressureFIFO("wb2", 2);
        b2.put(1, 1, 0, 0); T.check(b2.canPut(2), "depth 2: one in, can put"); b2.put(2, 2, 0, 0);
        T.check(!b2.canPut(3), "full"); b2.take(3); T.check(!b2.canPut(3), "still start-of-cycle"); T.check(b2.canPut(4), "next cycle");
        T.check(!b2.isEmpty(4), "one visible"); b2.take(4); T.check(b2.isEmpty(5), "empty");
    }

    static void wordBackpressureDelayLine() {
        T.group("WordBackpressureDelayLine");
        WordBackpressureDelayLine d = new WordBackpressureDelayLine("wbd", 3, 2);
        T.check(d.canPut(1), "empty"); d.put(1, 1, 0, 0);
        T.check(d.canPut(2), "one in flight of 2"); d.put(2, 2, 0, 0);
        T.check(!d.canPut(3), "two in flight: reservation full");
        T.throwsISE(() -> d.put(3, 3, 0, 0), "put without reservation throws");
        T.check(!d.ready(3), "not visible before t+L");
        T.eq(1, d.peekA(4), "first visible at t+L");
        T.check(!d.canPut(4), "visible but not taken still holds its slot");
        d.take(4);
        T.check(!d.canPut(4), "start-of-cycle: slot frees next cycle");
        T.check(d.canPut(5), "slot free at 5");
        T.eq(2, d.peekA(5), "second"); d.take(5);
        T.eq(0, d.inFlight(6), "empty");
    }

    static void registerBlock() {
        T.group("RegisterBlock");
        RegisterBlock b = new RegisterBlock("rb", 6);
        final int CMD = 0, CNT = 1, STATUS = 2, EVENTS = 3, RW = 4, POP = 5;
        b.setW1C(EVENTS); b.setOrMerge(EVENTS);
        b.addRule(CMD, CNT, RegisterBlock.RULE_INC, 0);
        b.addRule(POP, CNT, RegisterBlock.RULE_SUB_VALUE, 0);
        b.addRule(CMD, STATUS, RegisterBlock.RULE_SET, 0x100);
        // core write at t: unit and core see it at t+1; the store cycle still reads the old value
        b.write(1, RW, 0x55);
        T.eq(0, b.read(1, RW), "store cycle reads the old value");
        T.check(!b.written(1, RW), "not written yet");
        T.eq(0x55, b.read(2, RW), "load at t+1 sees the new value");
        T.check(b.written(2, RW) && b.writtenValue(2, RW) == 0x55, "unit sees the write at t+1");
        T.check(!b.written(3, RW), "written is a one-cycle event");
        T.throwsISE(() -> { b.write(3, RW, 1); b.write(3, CMD, 1); }, "two core writes in one cycle throw");
        // unit status written at t readable at t+1; masked writes from several units in one cycle
        b.unitWrite(4, STATUS, 0x1, 0x1); b.unitWrite(4, STATUS, 0x2, 0x2);
        T.eq(0, b.read(4, STATUS) & 3, "old value during the cycle");
        T.eq(3, b.read(5, STATUS) & 3, "both fields at t+1");
        T.throwsISE(() -> { b.unitWrite(5, STATUS, 0, 0x1); b.unitWrite(5, STATUS, 0, 0x1); }, "overlapping unit masks throw");
        // command register with a counter rule: MEM_OUTSTANDING-style, right from t+1
        b.unitWrite(6, CNT, 0); b.write(6, CMD, 0x1234);
        T.eq(1, b.read(7, CNT), "counter counts the command from t+1 (unit value 0 + 1)");
        T.check(b.written(7, CMD) && b.writtenValue(7, CMD) == 0x1234, "command seen by the unit");
        T.eq(0x100, b.read(7, STATUS) & 0x100, "SET rule: busy bit set by the store");
        b.unitWrite(7, CNT, 1); b.write(7, CMD, 0x5678);         // unit now counts it; a second command
        T.eq(2, b.read(8, CNT), "unit value 1 + the new command");
        b.unitWrite(8, CNT, 2); b.write(8, POP, 2);               // pop 2 -> counter 0 at t+1
        T.eq(0, b.read(9, CNT), "SUB_VALUE rule");
        // EVENTS: OR-merged unit sets from several units in one cycle, core W1C clear; set wins over clear in the same cycle
        b.unitWrite(10, EVENTS, 1 << 3); b.unitWrite(10, EVENTS, 1 << 3 | 1 << 5); b.unitWrite(10, EVENTS, 1 << 7);
        T.eq(0xA8, b.read(11, EVENTS), "three writers OR-merged");
        b.write(11, EVENTS, 1 << 3 | 1 << 5); b.unitWrite(11, EVENTS, 1 << 5);
        T.eq(0xA0, b.read(12, EVENTS), "W1C cleared bit 3; bit 5 set again in the same cycle stays");
        // core write wins over a unit write to the same register in the same cycle
        b.unitWrite(12, RW, 0x11); b.write(12, RW, 0x22);
        T.eq(0x22, b.read(13, RW), "core wins");
        b.unitWrite(13, RW, 0x33);
        T.eq(0x33, b.read(14, RW), "unit write alone");
    }

    static void sram() {
        T.group("SRAMMacro");
        SRAMMacro m = new SRAMMacro("m", 16, 2, false);
        m.write32(0, 1, 4, 0x12345678);
        T.eq(0, m.read32(1, 1, 4), "read returns start-of-cycle contents");
        T.throwsISE(() -> m.read32(0, 1, 0), "two accesses on one port in a cycle throw");
        T.eq(0x12345678, m.read32(0, 2, 4), "write committed at end of cycle");
        m.write32(1, 2, 4, 0xFF, 0xFF);
        T.eq(0x123456FF, m.read32(0, 3, 4), "masked write");
        m.write128(1, 3, 6, 0x1111111122222222L, 0x3333333344444444L);
        m.read128(0, 4, 6);
        T.eq(0x1111111122222222L, m.rdLo, "128 lo"); T.eq(0x3333333344444444L, m.rdHi, "128 hi");
        T.eq(0x4444444411111111L, m.read64(0, 5, 7), "64-bit read at an odd word address");
        T.throwsISE(() -> m.read64(0, 6, 15), "out of range throws");
        SRAMMacro rom = new SRAMMacro("rom", 4, 1, true);
        T.throwsISE(() -> rom.write32(0, 1, 0, 1), "rom write throws");
    }
}
