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
        {   // invariant 1: FIFO overflows, scanned every 16 cycles and at the end of a run, once per FIFO, at their own cycle
            Config k = Net.cfg(-1); k.queueDepth = 2; k.weInDepth = 1; k.measureS1 = false;   // S1: its own case, later
            Net n = new Net(k);
            for (int s = 20; s < 40; s++) for (int i = 0; i < 4; i++)
                n.at(s, 10 + i, 9, LinkWord.M_WRITE, 1, (LinkWord.R_SRAM << 24) | ((s * 16 + 2 * i) << 2), LinkWord.SIZE_64, s * 100 + i, 0);
            n.runAll(2000);
            System.out.print(n.c.violationReport());
        }
        {   // the director's invariants 9 (strays), 11, 19, 22, 25
            Config k = Net.cfg(-1); k.checkUnexpected = true;
            Net n = new Net(k);
            int SR = LinkWord.R_SRAM << 24;
            n.at(3, 10, 9, LinkWord.M_WRITE, 1, SR | 0x100, LinkWord.SIZE_64, 1, 0);                 // a stray (nothing armed)
            n.c.tiles[12].dir.remaining[1] = 1;
            n.at(4, 10, 12, LinkWord.M_WRITE, 1, SR | 0x200, LinkWord.SIZE_64, 2, 0);                // armed for one
            n.at(4, 11, 12, LinkWord.M_WRITE, 1, SR | 0x208, LinkWord.SIZE_64, 3, 0);                // the second: a stray
            n.at(5, 12, 20, LinkWord.M_SRC, 0, 0, LinkWord.SIZE_64, 4, 0);                           // uncounted mode 1
            n.at(5, 13, 21, LinkWord.M_WRITE, 0, SR | 0x300, LinkWord.SIZE_64, 5, 0);                // uncounted big-SRAM write
            n.at(5, 14, 22, LinkWord.M_WRITE, 0, Net.cm(0x900), LinkWord.SIZE_32, 6, 0);             // uncounted, not a controller
            n.at(6, 15, 23, LinkWord.M_WRITE, 1, 8, LinkWord.SIZE_64, 7, 0);                         // the reserved first 16 bytes
            n.at(6, 16, 24, LinkWord.M_WRITE, 1, 0x7BC0 + 8 * 7, LinkWord.SIZE_32, 8, 0);            // another's reply pair
            n.at(7, 17, 25, LinkWord.M_WRITE, 0, 0x7FC0, LinkWord.SIZE_32, 9, 0);                    // the command block, from a tile
            n.at(7, 18, 26, LinkWord.M_WRITE, 1, 0x7FF0 + 0x8000, LinkWord.SIZE_128, 10, 11);         // past data memory's end
            n.at(8, 19, 27, LinkWord.M_WRITE, 1, SR | (0x100000 << 2), LinkWord.SIZE_64, 12, 0);     // past big SRAM's end
            n.cncSends.add(new long[] { 30, LinkWord.BROADCAST, LinkWord.M_WRITE, 0x200, 13, LinkWord.SIZE_32, 0, 0 });   // a broadcast overlay
            n.cncSends.add(new long[] { 31, 30, LinkWord.M_WRITE, 0x7FC0, 14, LinkWord.SIZE_32, 0, 0 });                 // then a unicast command
            n.runAll(2000);
            System.out.print(n.c.violationReport());
        }
        {   // the core's checks through the register block (TileControl.violate; W26, 1a): the terminal traps that set no
            // ERRORS bit (ecall, ebreak, a misaligned jump, a fetch outside the memories, a trap inside the handler), mret,
            // an access past data memory (it aliases), a store into data memory's reserved first 16 bytes
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 200;
            Mach m = new Mach(k);
            int P = Registers.PMEM_BASE, LOOP = 0x0000006F;
            int[][] prog = {
                { LOOP },                                              // 0: nothing
                { 0x00000073 },                                        // 1: ecall
                { 0x00100073 },                                        // 2: ebreak
                { 0x30200073 },                                        // 3: mret (to mepc 0: then a fetch outside)
                { 0x0020006F },                                        // 4: jal x0, +2: a misaligned jump
                { 0x010000B7, 0x01008093, 0x30509073, 0, 0 },          // 5: mtvec = P + 16; an illegal one; another in the handler
                { 0x00002223, LOOP },                                  // 6: sw x0, 4(x0): the reserved first 16 bytes
                { 0x000080B7, 0x0000A103, 0x0020A023, LOOP },          // 7: lw x2, 0x8000; sw x2, 0x8000: past data memory
            };
            for (Tile p : m.c.tiles) { int[] w = prog[p.idx % prog.length]; for (int i = 0; i < w.length; i++) p.core.pokeLocal(P + 4 * i, w[i]); }
            m.c.run(40);
            m.c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, P).runWhenReady();
            m.c.run(400);
            System.out.print(m.c.violationReport());
        }
        {   // ERRORS from the test (ref's ctl.conditions |= bits) and the switches changed mid-run (W23's requests 9, 10)
            Config k = Net.cfg(-1); k.checkErrors = true;
            Net n = new Net(k);
            n.c.run(20);
            n.c.tiles[5].ctl.conditions |= 1 << 9;                  // bus error
            n.c.run(10);
            System.out.println("after tile 5's bits: " + n.c.violations + " at cycle " + n.c.cycle);
            n.c.cfg.checkErrors = false;
            n.c.tiles[6].ctl.conditions |= 1 << 10;                 // illegal instruction, unreported
            n.c.run(10);
            n.c.cfg.checkErrors = true;
            n.c.tiles[7].ctl.conditions |= 1 << 1;                  // reported
            n.c.run(1);
            System.out.print(n.c.violationReport());
            n.c.violations = 0; n.c.violationMessages.clear(); n.c.cfg.maxViolationMessages = 1;
            n.c.tiles[8].ctl.conditions |= 1 << 2;
            n.c.tiles[9].ctl.conditions |= 1 << 3;
            n.c.step();
            System.out.print(n.c.violationReport());
        }
        {   // CtrlTest's inv program (W26, 1a): invariants 4, 9, the reserved register-block word, the table window while the
            // address generator runs (tiles 2, 6, ...); invariant 8 counted, not reported (strictReplies off)
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 1000;
            Mach m = new Mach(k);
            m.boot("inv");
            m.run(m::allStopped, 10000);
            System.out.print(m.c.violationReport());
            long ow = 0, uc = 0; for (Tile p : m.c.tiles) { ow += p.cmem.overwrittenUnread; uc += p.cmem.unreadCommands; }
            System.out.println("overwritten unread " + ow + ", unread commands " + uc);
        }
        {   // inv with invariant 8 strict (CtrlTest's invariants: strictReplies; W26, 1a)
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 1000; k.strictReplies = true;
            Mach m = new Mach(k);
            m.boot("inv");
            m.run(m::allStopped, 10000);
            System.out.print(m.c.violationReport());
        }
        {   // the table window while count-on-arrival uses HIST and the write engine uses PTR (W26, 1a): a test's stores
            // (ref's harness store runs between cycles: the flags as the cycle before left them)
            Config k = Net.cfg(-1);
            Net n = new Net(k);
            n.c.tiles[9].setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 1, 1));
            n.c.tiles[9].dir.remaining[1] = 8;
            for (int i = 0; i < 6; i++) n.at(12 + 2 * i, 10 + i, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_64, 0x0101L * (i + 1), 0);
            while (n.c.cycle < 19) n.step();
            for (int t = 20; t < 52; t++) {
                int ea = 0xFFFF2000 + ((t & 1) != 0 ? 0x400 : 0) + 4 * (t % 5);
                n.c.tiles[9].core.store(t, ea, t, -1);
                n.step();
            }
            n.runAll(1000);
            System.out.print(n.c.violationReport());
        }
        {   // the peripheral slot's own checks through the block (PeripheralSlot.violate; W26, 1a): a store to window 1's
            // word 8 (no DMA on a mesh tile), a load of its reserved word 10, a store to its reserved word 22
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 200;
            Mach m = new Mach(k);
            int P = Registers.PMEM_BASE, LOOP = 0x0000006F, X1 = 0x80000093;
            int[][] prog = { { LOOP }, { X1, 0x2200A023, LOOP }, { X1, 0x2280A103, LOOP }, { X1, 0x2400AC23, LOOP } };
            for (Tile p : m.c.tiles) { int[] w = prog[p.idx % prog.length]; for (int i = 0; i < w.length; i++) p.core.pokeLocal(P + 4 * i, w[i]); }
            m.c.run(40);
            m.c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, P).runWhenReady();
            m.c.run(300);
            System.out.print(m.c.violationReport());
        }
        {   // a WFI woken by an event other than GO with done set, the done gate off (TileControl.takeWake; W26, 1a):
            // EVENTS_MASK = run done, done pulsed, a run of count 0 started, WFI
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 200; k.doneGate = false;
            Mach m = new Mach(k);
            int P = Registers.PMEM_BASE;
            int[] w = { 0x80000093, 0x40000113, 0x0220A023, 0x00100193, 0x0030A623, 0x0030A823, 0x10500073, 0x0000006F };
            for (Tile p : m.c.tiles) if (p.idx % 5 == 2) for (int i = 0; i < w.length; i++) p.core.pokeLocal(P + 4 * i, w[i]); else p.core.pokeLocal(P, 0x0000006F);
            m.c.run(40);
            m.c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, P).runWhenReady();
            m.c.run(300);
            System.out.print(m.c.violationReport());
        }
        {   // the request bus's root drops a request for a reserved target or one without a receiver (W26, 1b; W22-13):
            // REQUEST_SET_W0 = target << 7, REQUEST_SEND, from tiles 1, 2, 3 mod 4 to targets 3, 5, 7
            Config k = Mach.cfg(-1); k.checkErrors = false; k.maxViolationMessages = 200;
            Mach m = new Mach(k);
            int P = Registers.PMEM_BASE, LOOP = 0x0000006F, X1 = 0x80000093;
            int[][] prog = { { LOOP }, { X1, 0x18000113, 0x0420A423, 0x0400AA23, LOOP }, { X1, 0x28000113, 0x0420A423, 0x0400AA23, LOOP },
                             { X1, 0x38000113, 0x0420A423, 0x0400AA23, LOOP } };
            for (Tile p : m.c.tiles) { int[] w = prog[p.idx % prog.length]; for (int i = 0; i < w.length; i++) p.core.pokeLocal(P + 4 * i, w[i]); }
            m.c.run(40);
            m.c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, P).runWhenReady();
            m.c.run(800);
            System.out.print(m.c.violationReport());
        }
        {   // C&C's receive FIFO full (W26, 1b; the endpoints' send checks wait for the harness's Net.step to call them)
            Config k = Net.cfg(-1); k.cncRxDepth = 4;
            Net n = new Net(k);
            for (int i = 0; i < 8; i++) n.at(10 + i, 40 + i, LinkWord.CC1, LinkWord.M_WRITE, 0, 0x100, LinkWord.SIZE_32, i, 0);
            n.runAll(2000);
            System.out.print(n.c.violationReport());
        }
        {   // the command slot after the boot (W26, W22's F4): the boot ROM takes the boot command at GO, so the first command
            // after it may land before the program reads slot 0 (TileControl.endCycle), queued right behind the boot
            Config k = Mach.cfg(-1); k.maxViolationMessages = 200;
            Mach m = new Mach(k);
            m.boot("search");
            m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
            m.run(m::allStopped, 20000);
            System.out.print(m.c.violationReport());
        }
        {   // the read port (ReadPort.store; W26): stores into a full queue (invariant 8; a misaligned destination is UB at the director)
            Config k = Net.cfg(-1); k.readPortDepth = 2;
            Net n = new Net(k);
            while (n.c.cycle < 19) n.step();
            for (int t = 20; t < 32; t++) {
                if (t == 20) n.c.tiles[9].core.store(t, 0xFFFFF880, (1 << 17) | (2 << 19) | 0x100, -1);
                else n.c.tiles[9].core.store(t, 0xFFFFF87C, 0x40 + 2 * t, -1);
                n.step();
            }
            n.runAll(1000);
            System.out.print(n.c.violationReport());
        }
        {   // the address generator's stream-entry checks (AddressGenerator.checkEntry; W26): eight streams of count 0 (each
            // visited and checked, nothing issued) on tile 9: size 11, mode 3 at 128, a misaligned destination, a misaligned
            // source, d = 70, the broadcast on a mesh tile, a gather to another unit, count-only on counter 0
            Net n = new Net(Net.cfg(-1));
            n.c.tiles[9].setStream(0, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 3, 1, 0, 9));
            n.c.tiles[9].setStream(1, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 2, 1, 3, 9));
            n.c.tiles[9].setStream(2, Fields.streamSrc(0x100, 0), 0x104, Fields.streamCount(0, 1, 1, 0, 9));
            n.c.tiles[9].setStream(3, Fields.streamSrc(0x101, 0), 0x0, Fields.streamCount(0, 1, 1, 0, 9));
            n.c.tiles[9].setStream(4, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 0, 1, 0, 70));
            n.c.tiles[9].setStream(5, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 0, 1, 0, 126));
            n.c.tiles[9].setStream(6, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 0, 1, 4, 5));
            n.c.tiles[9].setStream(7, Fields.streamSrc(0x100, 0), 0x0, Fields.streamCount(0, 0, 0, 5, 9));
            n.c.tiles[9].setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 8, 0, 0, 1));
            n.c.tiles[9].pulse(Fields.PULSE_START);
            n.c.run(200);
            System.out.print(n.c.violationReport());
        }
        {   // invariant 6 (W26): HIST counts past 17 bits (every HIST word at 0x1FFFF, count-on-arrival): reported once a tile
            Net n = new Net(Net.cfg(-1));
            n.c.tiles[9].setMasterConfig(Fields.masterConfig(0, 0, 0, 0, 1, 1));
            for (int i = 0; i < 256; i++) n.c.tiles[9].hist.poke(i, 0x1FFFF);
            n.c.tiles[9].dir.remaining[1] = 8;
            for (int i = 0; i < 4; i++) n.at(12 + i, 10 + 3 * i, 9, LinkWord.M_SRC, 1, 0, LinkWord.SIZE_64, 0x0101L * (i + 1), 0);
            n.runAll(1000);
            System.out.print(n.c.violationReport());
        }
        {   // invariant 2, the disjoint buffers (BigSRAM.mark; W26, 1d): phase 0, chunk 0x40 written by a packet then read by the
            // read port; Chip.newPhase; phase 1, chunk 0x80 read then written; reported once a tile and phase
            Net n = new Net(Net.cfg(-1));
            int SR = LinkWord.R_SRAM << 24;
            n.c.tiles[9].dir.remaining[1] = 2; n.c.tiles[9].dir.remaining[2] = 4;
            n.at(12, 10, 9, LinkWord.M_WRITE, 1, SR | (0x40 << 2), LinkWord.SIZE_64, 0x11, 0);
            n.at(12, 11, 9, LinkWord.M_WRITE, 1, SR | (0x41 << 2), LinkWord.SIZE_32, 0x12, 0);
            while (n.c.cycle < 39) n.step();
            n.c.tiles[9].core.store(40, 0xFFFFF880, (1 << 17) | (2 << 19) | 0x100, -1); n.step();
            n.c.tiles[9].core.store(41, 0xFFFFF87C, 0x40, -1); n.step();
            n.c.tiles[9].core.store(42, 0xFFFFF87C, 0x40, -1); n.step();
            n.runAll(500);
            n.c.newPhase();
            long T = n.c.cycle + 5;
            while (n.c.cycle < T - 1) n.step();
            n.c.tiles[9].core.store(T, 0xFFFFF87C, 0x80, -1); n.step();
            n.at(13, T + 20, 9, LinkWord.M_WRITE, 1, SR | (0x80 << 2), LinkWord.SIZE_64, 0x13, 0);
            n.runAll(500);
            System.out.print(n.c.violationReport());
        }
        {   // invariant 2 across GOs (W26, 1d): the a2a, whose phases (each GO a new one) read and write the same buffers
            Mach m = new Mach(Mach.cfg(-1));
            A2A data = new A2A(7, 2000);
            for (Tile p : m.c.tiles) {
                int s = p.idx;
                for (int k = 0; k < data.rec[s].length; k++) p.sram.poke64(A2A.SRC + 2 * k, data.rec[s][k]);
                for (int d = 0; d < 64; d++) p.sram.poke(0xF0000 + d, data.cnt[s][d]);
                for (int src = 0; src < 64; src++) p.sram.poke(0xF0000 + 64 + src, data.cnt[src][s]);
                for (int g = 0; g < 16; g++) p.sram.poke(0xF0000 + 128 + g, data.R[g]);
                p.sram.poke(0xF0000 + 144, 0);
            }
            m.boot("a2a");
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
            m.run(m::allStopped, 2_000_000);
            System.out.print(m.c.violationReport());
        }
        {   // invariant 2's phases at GO (TileControl.endCycle: sram.newPhase at every GO; W26, 1d): tile 5 reads chunk 0x40 (a
            // test's read-port stores), GO, then C&C writes the chunk: another phase, no violation
            Config k = Mach.cfg(-1); k.checkErrors = false;
            Mach m = new Mach(k);
            int P = Registers.PMEM_BASE;
            int[] w = { 0x80000093, 0x00100193, 0x0030A623, 0x10500073, 0xFF9FF06F };   // done, WFI, again
            for (Tile p : m.c.tiles) for (int i = 0; i < w.length; i++) p.core.pokeLocal(P + 4 * i, w[i]);
            m.c.run(40);
            m.c.cc.command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, P).runWhenReady();
            m.c.run(300);
            m.c.tiles[5].dir.remaining[2] = 1;
            long T = m.c.cycle + 1;
            m.c.tiles[5].core.store(T, 0xFFFFF880, (1 << 17) | (2 << 19) | 0x100, -1); m.c.run(1);
            m.c.tiles[5].core.store(T + 1, 0xFFFFF87C, 0x40, -1); m.c.run(100);
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();   // READY is done and a command landed
            m.c.run(150);
            m.c.cc.send(5, LinkWord.M_WRITE, (LinkWord.R_SRAM << 24) | (0x40 << 2), LinkWord.SIZE_64, 0x77, 0);
            m.c.run(200);
            System.out.print(m.c.violationReport());
        }
        {   // the slot's DMA_HDR and DMA_ADDR changed mid-transfer (PeripheralSlot.setHdr, setAddr; 4.17.9; W26): ref's test
            // peripheral on tile 7 (Config.testPeripheral), a 7-word transfer started, both words stored again while it runs
            Config k = Config.machine(); k.periphType = 0;
            k.testPeripheral = tile -> tile == 7 ? new TestPeripheral() : null;
            Net n = new Net(k);
            n.c.run(200);
            int W1 = 0xFFFFF800 + 0x200, hdr = 7 | 1 << 12 | 65 << 25 | 5 << 19;
            int[][] st = { { W1 + 32, hdr }, { W1 + 36, 0x400 }, { W1 + 16, 1 }, { W1 + 32, hdr }, { W1 + 36, 0x500 } };
            for (int[] s : st) { n.c.tiles[7].core.store(n.c.cycle + 1, s[0], s[1], -1); n.step(); }
            for (int i = 0; i < 40; i++) n.step();
            System.out.print(n.c.violationReport());
        }
        {   // invariant 2's phases at the counters-done latch (TileControl.java:280, sram.newPhase; W22's F6; W26): on tile 12,
            // a run copies A -> B, its counters done (a new phase), a run copies B -> A: no violation; a third run reads and
            // writes one buffer: flagged, with its phase number
            Net n = new Net(Net.cfg(-1));
            Tile p = n.c.tiles[12];
            final int A = 0x20000, B = 0x40000, N = 64;
            for (int i = 0; i < N; i++) p.sram.poke64(A + 2 * i, 0xA000L + i);
            int[][] runs = { { A, B }, { B, A }, { A, A + 2 * N - 16 } };
            for (int r = 0; r < 3; r++) {
                p.setStream(r, Fields.streamSrc(runs[r][0]), 0, Fields.streamCount(N, Fields.SIZE_64, 1, LinkWord.M_SRC, 0));
                p.setPtr(12, runs[r][1]); p.dir.remaining[1] = N;
                p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, r, 1, 0, 0, 1));
                p.pulse(Fields.PULSE_START);
                n.c.run(600);
            }
            System.out.print(n.c.violationReport());
            System.out.println("disjoint-buffer accesses counted: " + p.sram.disjointViolations);
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
