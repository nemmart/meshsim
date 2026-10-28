import java.util.ArrayList;
import java.util.List;

/**
 * The conditions (spec 4.13, 8.3a), the done gate (4.12), the error line, C&C's error bit, its receive FIFO and
 * REPORT (6.6), and the broadcast bound behind REPORT's settle. Program sw/tests/cond.c. Usage: {@code java CondTest [shuffleSeed]}.
 */
public final class CondTest {
    static long shuffle = -1;
    static void out(String s) { System.out.println(s); }
    static final int EXIT = 0x7F;

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) out("shuffled child order, seed " + shuffle);
        register();
        conditions();
        doneGate();
        errorBit();
        receiveFifo();
        report();
        broadcast();
        System.exit(T.done("CondTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    static Mach mach() { return new Mach(Mach.cfg(shuffle)); }
    static Mach mach(Config k) { k.shuffleSeed = shuffle; return new Mach(k); }
    static long mask(int... tiles) { long m = 0; for (int t : tiles) m |= 1L << t; return m; }
    static CnC cmd(CnC cc, int type, long mask, int... more) {
        int[] a = new int[2 + more.length]; a[0] = (int) mask; a[1] = (int) (mask >>> 32);
        System.arraycopy(more, 0, a, 2, more.length);
        return cc.command(LinkWord.BROADCAST, type << 24, a);
    }

    // ================================================================ the register
    static void register() {
        T.group("register");
        T.eq(0xFFFFF804L, Registers.TILE_CONDITIONS.addr() & 0xFFFFFFFFL, "TILE_CONDITIONS at 0xFFFFF804 (index 1)");
        T.check(Registers.TILE_CONDITIONS.readable() && !Registers.TILE_CONDITIONS.writable(), "read-only: a store is a bus error");
        String[] want = { "router overflow", "bad mode or size", "unexpected arrival", "drift", "write-engine input overflow",
                "table request overflow", "GATHER queue overflow", "read-port error", "done early", "bus error", "illegal instruction",
                "bad peripheral write", "peripheral condition 2", "peripheral condition 3", "receive underflow" };
        T.eq(15, Conditions.N, "15 conditions in [14:0] (8.3, 26n)");
        for (int i = 0; i < 15; i++) T.eqo(want[i], Conditions.NAME[i], "bit " + i + " is " + want[i] + " (spec 4.13's table)");
    }

    // ================================================================ every condition, provoked from C
    /** Case c on the tiles in `acting`; `who` the tiles that must have the condition. Checks each tile's conditions
     *  (exactly bit c, plus `also` where named), the C-visible value, C&C's error bit and its wake. */
    static void condition(int c, long acting, long who, int also, String how, java.util.function.Consumer<Mach> inject) {
        Mach m = mach();
        Mach.Image im = m.boot("cond");
        CnC cc = m.c.cc;
        long[] woke = { -1 }, goAt = { -1 };
        cmd(cc, c, acting).go().call(() -> goAt[0] = cc.lastGoAt);
        if (inject != null) cc.then((x, t) -> { if (t < goAt[0] + 30) return false; inject.accept(m); return true; });
        cc.then((x, t) -> { if (!x.error()) return false; woke[0] = t; return true; });    // C&C's WFI with the error bit as a wake source
        cc.waitCycles(4000);                                       // the provoking traffic is over: an overflowing eject drops commands too
        boolean halts = c == Conditions.BUS_ERROR || c == Conditions.ILLEGAL;
        // the receivers of dropped traffic can never count it: they park, and HALT recovers them (4.12, 6.1)
        boolean recover = c == Conditions.ROUTER_OVF || c == Conditions.WE_IN_OVF;
        int[] snap = new int[64], stuck = new int[64];
        if (recover) cc.waitUntil(() -> { for (Tile p : m.c.tiles) if ((who >>> p.idx & 1) != 0 && !(p.core.inWfi && p.ctl.wakeMask == 0)) return false; return true; })   // parked
                       .call(() -> { for (Tile p : m.c.tiles) { snap[p.idx] = p.ctl.conditions; stuck[p.idx] = p.dir.remaining[TrafficDirector.SRAM]; } }).halt();
        else if (!halts) cmd(cc, 30, 0).go().command(LinkWord.BROADCAST, EXIT << 24).go();
        else cmd(cc, 30, 0);                                       // a command lands: every live tile reports READY
        boolean ok = m.run(() -> halts ? woke[0] >= 0 && m.c.cycle > woke[0] + 4600 : recover ? !cc.busy() : m.allStopped(), 200000);
        T.check(ok, "case " + c + " (" + Conditions.NAME[c] + "): ran");
        int right = 0, wrong = 0; long first = Long.MAX_VALUE;
        for (Tile p : m.c.tiles) {
            boolean w = (who >>> p.idx & 1) != 0;
            int want = w ? 1 << c | also : 0;
            if ((recover ? snap[p.idx] : p.ctl.conditions) == want) right++; else wrong++;
            if (!halts && w) T.eq(want, m.word(p.idx, im, "cnd", 0), "case " + c + " tile " + p.idx + ": TILE_CONDITIONS as the program reads it");
            if (w && p.ctl.errorSince >= 0) first = Math.min(first, p.ctl.errorSince);
        }
        T.eq(64, right, "case " + c + " (" + Conditions.NAME[c] + ", " + how + "): set on exactly the tiles provoking it, and nothing else" + (also != 0 ? " (with " + Conditions.names(also) + ")" : "") + "; wrong " + wrong);
        if (!recover) T.eq(first + m.c.cfg.errorLatency(), cc.errorSetAt, "case " + c + ": C&C's error bit set error_latency after the first tile's condition");
        T.eq(cc.errorSetAt, woke[0], "case " + c + ": C&C's waiting step woke on the error bit, that cycle");
        if (halts) T.check(Long.bitCount(cc.readyBits(m.c.cycle)) == 64 - Long.bitCount(who), "case " + c + ": the halted tiles never report READY");
        if (recover) {                                             // HALT cleared the stuck counter, its events, the queues and the conditions
            int clean = 0;
            for (Tile p : m.c.tiles) {
                long t = m.c.cycle;
                boolean ok1 = p.ctl.conditions == 0 && p.ctl.countersZero() && (p.ctl.events & 0x20F) == 0 && p.weIn.isEmpty(t) && p.memIdle(t)
                        && p.readPort.idle(t) && p.srq.idle() && p.dir.cmIdle(t);
                for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) if (!f.isEmpty(t)) ok1 = false;
                if (ok1) clean++;
            }
            T.eq(64, clean, "case " + c + ": after HALT every tile is clean: conditions 0, counters 0 with their done events, write path, read port, return queues and router FIFOs empty");
            for (Tile p : m.c.tiles) if ((who >>> p.idx & 1) != 0) T.check(stuck[p.idx] > 0, "case " + c + " tile " + p.idx + ": its DMA_SRAM_REMAINING was stuck at " + stuck[p.idx] + " before HALT");
        }
        results.add(String.format("  %2d %-28s %-62s tiles %s%s", c, Conditions.NAME[c], how, Long.bitCount(who), recover ? " (recovered by HALT)" : ""));
    }
    static final List<String> results = new ArrayList<>();
    static void conditions() {
        T.group("conditions");
        long all = -1L, n9 = mask(8, 9, 10, 1, 17);
        condition(Conditions.DRIFT, mask(9), mask(9), 0, "a group run reading and writing one area", null);
        condition(Conditions.ROUTER_OVF, n9, mask(9), 0, "4 neighbours stream 200 records each to tile 9", null);
        condition(Conditions.WE_IN_OVF, mask(8, 9), mask(9), 0, "40 records landing at a chunk = 3 mod 4", null);
        condition(Conditions.READ_PORT, all, all, 0, "65 reads queued at once", null);
        condition(Conditions.UNEXPECTED, all, all, 0, "a reply to itself with its counter at 0", null);
        condition(Conditions.TABLE_OVF, 0, mask(12), 0, "injected: the HIST request FIFO's overflow (a fault)",
                m -> { WordFIFO q = m.c.tiles[12].hist.q; q.overflowCount++; q.overflow = true; q.overflowCycle = m.c.cycle; });
        condition(Conditions.BUS_ERROR, mask(3, 40), mask(3, 40), 0, "a store to TILE_STATUS (the core halts)", null);
        condition(Conditions.ILLEGAL, mask(5), mask(5), 0, "div (no divider; the core halts)", null);
        condition(Conditions.GATHER_OVF, 0, mask(20), 0, "injected: the GATHER request queue's overflow (a fault)",
                m -> { WordFIFO q = m.c.tiles[20].gatherIn; q.overflowCount++; q.overflow = true; q.overflowCycle = m.c.cycle; });
        condition(Conditions.BAD_MODE_OR_SIZE, all, all, 0, "a packet with mode 0", null);
        condition(Conditions.DONE_EARLY, all, all, 0, "done with REPLY_REMAINING = 1", null);
        gatherProbe();
        out("conditions [meas: CondTest conditions]: each provoked, set on exactly the tiles that provoked it, read by the program, and C&C's error bit set error_latency later:");
        for (String s : results) out(s);
    }

    /** The GATHER finding (spec 4.4's open in-flight bound): the tile's own send port, the only source besides its
     *  address generator (which yields the SRAM read queue to the engine), can't outrun it even at k = 8. */
    static void gatherProbe() {
        Mach m = mach();
        m.boot("cond");
        cmd(m.c.cc, 12, mask(20)).go().command(LinkWord.BROADCAST, EXIT << 24).go();
        boolean ok = m.run(m::allStopped, 100000);
        Tile p = m.c.tiles[20];
        T.check(ok, "GATHER probe: ran");
        T.eq(512L, p.gather.reads, "GATHER probe: 64 requests at k = 8, 512 value reads");
        T.check(p.gatherIn.highWater <= 2 && p.ctl.conditions == 0, "GATHER probe: the request queue peaked at " + p.gatherIn.highWater + " of 16, no condition");
        gatherPeak = p.gatherIn.highWater;
        results.add(String.format("  GATHER probe: 64 requests from the send port at its full rate, k = 8: the queue peaked at %d of 16 (spec 4.4's open bound; condition 8 is a fault detector)", gatherPeak));
    }
    static int gatherPeak;

    // ================================================================ the done gate (4.12)
    static void doneGate() {
        T.group("done gate");
        for (int gate = 1; gate >= 0; gate--) {
            Config k = Mach.cfg(shuffle); k.doneGate = gate == 1;
            Mach m = mach(k);
            m.boot("cond");
            CnC cc = m.c.cc;
            // 10: done with DMA_REPLY_REMAINING = 1 (done early); then 20 twice: tiles 0, 2, 4, ... send a word to me + 1 after
            // it is back in wait_for_command(): the first takes its counter 1 -> 0 (an edge while done is set), the second
            // finds it at 0 (unexpected arrival)
            long even = 0; for (int i = 0; i < 64; i += 2) even |= 1L << i;
            cmd(cc, Conditions.DONE_EARLY, -1L).go().waitCycles(400);
            cmd(cc, 20, even).go().waitCycles(600);
            cmd(cc, 20, even).go().waitCycles(600);
            cc.command(LinkWord.BROADCAST, EXIT << 24).go();
            boolean ok = m.run(m::allStopped, 100000);
            T.check(ok, (gate == 1 ? "gate on" : "gate off") + ": ran");
            int byGo = 0, early = 0, unexp = 0; long gated = 0, inDone = 0;
            for (Tile p : m.c.tiles) {
                boolean allGo = true;
                List<long[]> wl = p.core.wfiLog;
                for (int i = 1; i < wl.size(); i++) if (wl.get(i)[2] != TileControl.WAKE_GO) allGo = false;   // [0] is the boot's
                if (allGo) byGo++;
                if ((p.ctl.conditions >> Conditions.DONE_EARLY & 1) != 0) early++;
                if ((p.ctl.conditions >> Conditions.UNEXPECTED & 1) != 0) unexp++;
                gated += p.ctl.gatedEdges; inDone += p.ctl.wakeInDone;
            }
            T.eq(64, early, (gate == 1 ? "gate on" : "gate off") + ": done with the reply outstanding: done early on every tile");
            T.eq(32, unexp, (gate == 1 ? "gate on" : "gate off") + ": an arrival after done with its counter at 0: unexpected arrival on the 32 receivers");
            if (gate == 1) {
                T.eq(64, byGo, "gate on: every wait_for_command() on every tile woken by GO, whatever arrived after done");
                T.eq(32, gated, "gate on: the late word's edge came while done was set and was ignored, on the 32 receivers");
                T.eq(0, inDone, "gate on: no wake with done set");
            } else {
                T.eq(32, inDone, "gate off (TEST ONLY): the late word's edge wakes wait_for_command() on the 32 receivers, flagged");
                T.check(byGo < 64, "gate off: so those wait_for_command()s were not woken by GO");
            }
        }
        out("done gate [meas: CondTest done gate]: with the gate, a late arrival after done (done early, or unexpected arrival at a counter of 0) never wakes wait_for_command(); without it (Config.doneGate off, test only) the reply's edge wakes it on every tile and meshsim flags each");
    }

    // ================================================================ the error bit: latency, a clear while high, HALT
    static void errorBit() {
        T.group("error bit");
        for (int lat : new int[] { 1, 8 }) {
            Config k = Mach.cfg(shuffle); k.errorLatency = lat;
            Mach m = mach(k);
            m.boot("cond");
            CnC cc = m.c.cc;
            long[] setAt = { -1 }, cleared = { -1 }, again = { -1 }, haltAt = { -1 };
            int[] condAtHalt = { -1 };
            cmd(cc, Conditions.UNEXPECTED, mask(33)).go()
              .then((x, t) -> { if (!x.error()) return false; setAt[0] = x.errorSetAt; return true; })
              .then((x, t) -> { x.clearError(t); cleared[0] = t; return true; })
              .then((x, t) -> { if (!x.error()) return false; again[0] = t; return true; })
              .waitUntil(() -> m.allInWfi() && m.c.tiles[33].ctl.done)
              .call(() -> condAtHalt[0] = m.c.tiles[33].ctl.conditions)
              .halt().call(() -> haltAt[0] = cc.lastRunAt)
              .waitCycles(lat + 10)
              .then((x, t) -> { x.clearError(t); return true; }).waitCycles(lat + 20);
            boolean ok = m.run(() -> !cc.busy(), 100000);
            T.check(ok, "error_latency " + lat + ": ran");
            Tile p = m.c.tiles[33];
            T.eq(1 << Conditions.UNEXPECTED, condAtHalt[0], "error_latency " + lat + ": tile 33 has the condition");
            T.check(p.ctl.errorSince == -1 && p.ctl.conditions == 0, "error_latency " + lat + ": entering HALT cleared it");
            long first = -1; for (Tile q : m.c.tiles) if (q.idx != 33 && q.ctl.conditions != 0) first = q.idx;
            T.eq(-1L, first, "error_latency " + lat + ": no other tile has a condition");
            T.check(setAt[0] > 0, "error_latency " + lat + ": C&C's error bit set");
            T.eq(cleared[0] + 1, again[0], "error_latency " + lat + ": a clear while the line is high: the bit sets again the next cycle");
            T.check(!cc.error(), "error_latency " + lat + ": after HALT the line falls and a clear holds");
            noise(m, "error bit " + lat, "invariant 9");
        }
        out("error bit [meas: CondTest error bit]: C&C's bit sets error_latency after the tile's condition (1 and 8 tested, see conditions); a clear while any tile's line is high is undone the next cycle; after HALT a clear holds");
    }
    /** Every violation is one of the expected kinds. */
    static void noise(Mach m, String what, String... ok) {
        T.check(m.c.violationMessages.stream().allMatch(v -> { for (String o : ok) if (v.contains(o)) return true; return false; }),
                what + ": only the expected violations (" + String.join(", ", ok) + "): " + m.c.violationReport());
    }

    // ================================================================ C&C's receive FIFO (6.6)
    static void receiveFifo() {
        T.group("receive fifo");
        for (int n = 1; n <= 2; n++) {
            Mach m = mach();
            m.c.cfg.maxViolationMessages = 1000;
            m.boot("cond");
            CnC cc = m.c.cc;
            cmd(cc, 21, 0, n).go().waitUntil(() -> m.c.cycle > cc.lastGoAt + 600);
            boolean ok = m.run(() -> !cc.busy(), 50000);
            T.check(ok, n + " per tile: ran");
            List<long[]> trace = m.c.cnc.received;
            T.eq(64L * n, trace.size(), n + " per tile: " + 64 * n + " packets reached C&C's port");
            T.eq(Math.min(64, 64 * n), cc.rxCount(), n + " per tile: the FIFO holds " + Math.min(64, 64 * n) + " (C&C read nothing)");
            T.eq(Math.max(0, 64L * n - 64), cc.rxLost, n + " per tile: " + Math.max(0, 64 * n - 64) + " lost");
            T.check(cc.rxOverflow == (n > 1), n + " per tile: the overflow flag " + (n > 1 ? "set" : "clear"));
            long v = m.c.violationMessages.stream().filter(s -> s.contains("receive FIFO")).count();
            T.eq(n > 1 ? 64L : 0L, v, n + " per tile: each lost packet reported");
            int bad = 0;
            for (int i = 0; i < Math.min(64, 64 * n); i++) {               // the register pair gives the first 64 arrivals, in order
                long[] e = cc.rxPop(); long[] tr = trace.get(i);
                if (e == null || e[0] != tr[1] || e[1] != LinkWord.src(tr[2]) || (int) (e[0] >>> 32) != e[1]) bad++;
            }
            T.eq(0, bad, n + " per tile: each entry read through the register pair is (payload, src) of the next arrival, in order");
            T.check(cc.rxPop() == null, n + " per tile: then the valid bit is 0");
            T.eq(0, m.c.tiles[0].conditions(), n + " per tile: tile 0's to-C&C FIFOs did not overflow (the timer paces the senders)");
        }
        out("receive fifo [meas: CondTest receive fifo]: 64 paced packets all kept (64 entries); 128 keep the first 64, lose 64, set the overflow flag and report each; read through the register pair in arrival order");
    }

    // ================================================================ REPORT on 64 tiles, mixed conditions, one tile crashed
    static void report() {
        T.group("report");
        Mach m = mach();
        m.boot("cond");
        CnC cc = m.c.cc;
        cc.stopOnError = true; cc.watchdog = 3000;
        cmd(cc, 22, -1L).go();
        cmd(cc, 30, 0).go();                                       // pending when the error stops the GOs: discarded
        int[] snap = new int[64];
        boolean ok = m.run(() -> cc.reportStartedAt >= 0, 100000);
        for (Tile p : m.c.tiles) snap[p.idx] = p.ctl.conditions;
        ok &= m.run(() -> cc.reportDoneAt >= 0 && !cc.busy(), 100000);
        T.check(ok, "report: the flow ran to the end");
        int[] map = { 0, Conditions.UNEXPECTED, Conditions.BAD_MODE_OR_SIZE, Conditions.DONE_EARLY, Conditions.READ_PORT };
        int good = 0, snapOk = 0;
        long[] r = cc.reportBySrc();
        for (int t = 0; t < 63; t++) {
            int want = t % 5 == 0 ? 0 : 1 << map[t % 5];
            if (snap[t] == want) snapOk++;
            if (r[t] != -1 && (int) (r[t] >>> 32) == t && (int) r[t] == want) good++;
        }
        T.eq(63, snapOk, "report: tiles 0-62 hold their provoked condition (none on tiles 0, 5, 10, ...)");
        T.eq(1 << Conditions.BUS_ERROR, snap[63], "report: tile 63 crashed with a bus error");
        T.eq(63, cc.reports.size(), "report: 63 replies; the crashed tile 63 never reaches wait_for_command() and does not reply");
        T.eq(63, good, "report: every reply is (its tile index, its TILE_CONDITIONS)");
        T.eq(-1L, r[63], "report: none from tile 63 (its missing READY and the watchdog identify it)");
        T.check(cc.reportStartedAt - cc.errorSetAt >= cc.watchdog, "report: C&C waited the watchdog for tile 63's READY before reporting");
        long disc = 0; for (Tile p : m.c.tiles) disc += p.cmem.discardedCommands;
        T.eq(64L, disc, "report: the pending command was discarded on every tile (63 never read it; tile 63's core is halted)");
        int left = 0; for (Tile p : m.c.tiles) if (p.ctl.conditions != 0) left++;
        T.eq(0, left, "report: C&C's HALT cleared every condition");
        T.check(!cc.error(), "report: and its error bit is clear");
        noise(m, "report", "invariant 4", "invariant 8", "invariant 9", "invariant 5", "mode", "timer");
        out("report [meas: CondTest report]: 64 tiles with mixed conditions and one crash: 63 replies, each its tile's index and conditions; C&C waited " + cc.watchdog + " cycles (its watchdog) for the crashed tile's READY, settled " + cc.reportSettle() + " cycles, collected, halted");
    }

    // ================================================================ the broadcast bound behind REPORT's settle (6.6)
    static void broadcast() {
        T.group("broadcast");
        StringBuilder sb = new StringBuilder("broadcast [meas: CondTest broadcast]: a one-packet broadcast from C&C, its send to command landed at every tile, idle mesh:\n");
        for (int[] v : new int[][] { { 3, 2, 1 }, { 2, 2, 1 }, { 4, 2, 1 }, { 3, 4, 1 }, { 3, 2, 2 } }) {
            for (int skew = 0; skew < 2; skew++) {
                Config c = Config.machine(); c.linkLatency = v[0]; c.cmLatency = v[1]; c.intraTileLatency = v[2]; c.skew = skew == 1; c.shuffleSeed = shuffle;
                Chip chip = new Chip(c);
                long[] sent = { -1 };
                chip.cc.then((cc, t) -> { cc.ep.send(t, LinkWord.BROADCAST, LinkWord.M_CMD, 0, LinkWord.SIZE_32, (long) Registers.CMD_REPORT << 24, 0); sent[0] = t; return true; });
                chip.run(300);
                long mx = 0; int far = -1;
                for (Tile p : chip.tiles) { long d = p.cmem.landedAt - sent[0]; if (d > mx) { mx = d; far = p.idx; } }
                T.eq(chip.cc.broadcastBound(), mx, String.format("link %d, cm %d, intra-tile %d, skew %d: the farthest tile (%d) lands at the bound", v[0], v[1], v[2], skew, far));
                if (skew == 0) sb.append(String.format("  link_latency %d, cm_latency %d, intra_tile_latency %d: max %d cycles (tile %d) = 2 + %d + %d + 8 x (%d + 1); REPORT's settle %d%n", v[0], v[1], v[2], mx, far, v[2], v[1], v[0], chip.cc.reportSettle()));
            }
        }
        out(sb.toString().stripTrailing());
    }
}
